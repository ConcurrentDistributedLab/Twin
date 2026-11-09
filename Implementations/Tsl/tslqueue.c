/*   
 *   File: priorityqueue-tslqueue.c
 *   Author: Adones Rukundo 
 *	 <adones@chalmers.se, adones@must.ac.ug, adon.rkd@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program (see LICENSE.md in this directory).
 * If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "tslqueue.h"
#include "../../Library/Allocator/BumpAllocator.hpp"
#ifdef COMPUTE_STATS
#include "../../src/core/globals.h"
#endif
#include <x86intrin.h>

#ifndef DELETE_RATE
#define DELETE_RATE 125
#endif

__thread node_t *previous_dummy, *previous_head;
BumpAllocator<node_t> *alloc;
__thread uint32_t physical_deletes = DELETE_RATE / 2;

/*********** optimisation variables ***********************/
uint32_t physical_delete_threshold = DELETE_RATE;

void init_local() 
{
	
}

static node_t* create_node(int tid) 
{
    volatile node_t* new_node;
	new_node = alloc->allocate(tid);
	
    if (new_node == NULL) 
	{
        perror("alloc in tsl create_node");
        exit(1);
    }
	new_node->key = 0;
	new_node->left = NULL;
	new_node->right = NULL;
	new_node->parent = NULL;
	new_node->value = 0;
	new_node->next = NULL;
	
    return (node_t*) new_node;
}

tsl_set_t* create_set(size_t num_threads)
{
	volatile node_t *root, *head, *dummy_node;
	tsl_set_t *new_set;

	alloc = new BumpAllocator<node_t>(num_threads);
	
	new_set = (tsl_set_t*) malloc(sizeof(tsl_set_t));
	head = (volatile node_t*) alloc->allocate(0);
    root = (volatile node_t*) alloc->allocate(0);
	dummy_node = (volatile node_t*) alloc->allocate(0);
    if (root == NULL || head == NULL) 
	{
        perror("alloc in tsl create_set");
        exit(1);
    }
	
	dummy_node->key = 0;
	dummy_node->left = (void*) head;
	dummy_node->right = (void*) MARK_LEAF_NODE(dummy_node);
	dummy_node->parent = (void*) root;
	dummy_node->value = 0;
	dummy_node->next = NULL;
	
	head->right = NULL;
	head->left = NULL;
	head->next = (void*) dummy_node;
	head->key = 0;
		
	root->right = NULL;
	root->left = (void*) dummy_node;
	root->parent = NULL;
	root->key = 1;

	new_set->head = (node_t*) head;
	new_set->root = (node_t*) root;
	new_set->num_threads = num_threads;
	new_set->del_scale = num_threads * 100;
	
	return new_set;
}

uint8_t insert(tsl_set_t *set, skey_t key, sval_t value, int tid) 
{	
	volatile node_t *cas1_node, *cas2_node, *leaf_node;
	node_t *next_leaf;
	uint8_t parent_direction;
	insert_seek_t ins_seek;
	
	volatile node_t *new_node = create_node(tid);
#ifdef COMPUTE_STATS
  	totalIns += 1;
#endif

	new_node->right = (void*) MARK_LEAF_NODE(new_node);
	new_node->key = key;
	new_node->value = value;
	while (1)
	{															
		cas1_node = NULL;
		cas2_node = NULL;
		 
		ins_seek = insert_search(set, key);
		if(ins_seek.duplicate == DUPLICATE_DIRECTION)
		{
			return 0;//reject duplicates
		}
		else if(ins_seek.child == NULL) continue;
		
		parent_direction = ins_seek.parent_direction;
		cas1_node = ins_seek.cas1_node;
		cas2_node = ins_seek.cas2_node;

		leaf_node = ins_seek.child; //insertion point		
		next_leaf = ins_seek.next_node;
		
		/***************** Prepare new internal node and its children ************************************/		
		
		new_node->left = (void*) MARK_LEAF_NODE(leaf_node);
		new_node->parent_direction = parent_direction;	
		new_node->parent = (void*) cas1_node;
		new_node->next = (void*) next_leaf;//create node link
		new_node->inserting = 1;
		/***************** Insert ************************************/			
		#ifdef COMPUTE_STATS
			numInsRetries += 1;
		#endif
		if(leaf_node->next == next_leaf)
		{
			if(parent_direction == RIGHT_DIRECTION)
			{
				#if ATOMIC_INSTRUCTION==1
					next_t current_nextRight, new_nextRight;
					new_node->inserting = 0;
					current_nextRight.next = next_leaf;
					current_nextRight.right = cas2_node;
					new_nextRight.next = new_node;
					new_nextRight.right = new_node;
					if(leaf_node->next == next_leaf && cas1_node->right == cas2_node)
					{
						if(CAE((next_t*)&leaf_node->next,&current_nextRight,&new_nextRight))
						{
							return 1;
						}
					}
				#else
					if(leaf_node->next == next_leaf)	
					{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif
						if(CAS(&leaf_node->next,next_leaf,new_node))
						{
							if(new_node->inserting)
							{
								if(cas1_node->right == cas2_node){
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif
									CAS(&cas1_node->right,cas2_node,new_node);
								}
								//unmark inserted new item node
								if(new_node->inserting)new_node->inserting = 0;
							}
							return 1;
						}
					}
				#endif
			}
			else if(parent_direction == LEFT_DIRECTION)//&& !GETMARK(cas1_node->next)
			{
				if(leaf_node->next == next_leaf)	
				{	
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif
					if(CAS(&leaf_node->next,next_leaf,new_node))
					{
						if(new_node->inserting)
						{
							if(cas1_node->left == cas2_node){
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif
								CAS(&cas1_node->left,cas2_node,new_node);
							}
							//unmark inserted new item node
							if(new_node->inserting)new_node->inserting = 0;
						}
						return 1;
					}
				}
			}
		}		
	}
}

insert_seek_t insert_search(tsl_set_t *set, skey_t key)
{ 
	volatile node_t *child_node, *grand_parent_node, *parent_node, *root = set->root;
	node_t *child_next, *current_next, *parent_node_right, *parent_node_left, *marked_node;
	uint8_t parent_direction;
	volatile uint64_t operation_mark = 0, child_mark=0;
	
	insert_seek_t ins_seek;
#ifdef COMPUTE_STATS
  locate_calls += 1;
#endif

	ins_seek.child = ins_seek.next_node = ins_seek.cas1_node = ins_seek.cas2_node = NULL;
	ins_seek.parent_direction = ins_seek.duplicate = 0;
	
	marked_node = NULL;
	grand_parent_node = NULL;
	parent_node = root;
	child_node = (volatile node_t*) root->left;
	while (1) 
	{										
		/***************** Traverse deleted nodes (right traverse)************************************/
		if(operation_mark == DELETE_MARK)
		{						
		#ifdef COMPUTE_AVG
			pointers_ins_traversed += 1;
		#endif		
			READ_RIGHT();
			marked_node = (node_t*)parent_node;
			
			while(1)
			{				
				if(operation_mark == DELETE_MARK)
				{					
					if(child_mark != LEAF_MARK)
					{
						parent_node = child_node;
						#ifdef COMPUTE_AVG
							pointers_ins_traversed += 1;
						#endif	
						READ_RIGHT();
						continue;
					}
					else
					{
						parent_node = ADDRESS((volatile node_t*)child_node->next);
						READ_RIGHT();
						#ifdef COMPUTE_AVG
							pointers_ins_traversed += 1;
						#endif	
						break;
					}
				}
				else  
				{
					if(!GETMARK((volatile node_t*)grand_parent_node->next) && grand_parent_node->left==marked_node)
					{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif
						CAS(&grand_parent_node->left,marked_node,parent_node);
					}
					TRAVERSE();
					#ifdef COMPUTE_AVG
						pointers_ins_traversed += 1;
					#endif	
					break;
				}				
			}
			continue;
		}
		/***************** Traverse internal nodes ************************************/				
		if(child_mark != LEAF_MARK)
		{													
			grand_parent_node = parent_node;
			parent_node = child_node;
			TRAVERSE();
			#ifdef COMPUTE_AVG
				pointers_ins_traversed += 1;
			#endif	
		}		
		else
		{							
			current_next = (node_t*)child_node->next;
			child_next = ADDRESS(current_next);
			if(GETMARK((volatile node_t*)current_next))
			{
				GO_NEXT:
				parent_node = child_next;								
				READ_RIGHT();
				#ifdef COMPUTE_AVG
					pointers_ins_traversed += 1;
				#endif	
			}
			#if ATOMIC_INSTRUCTION==1
				else if(parent_direction == LEFT_DIRECTION && child_next!=NULL && child_next->inserting)
			#else
				else if(child_next!=NULL && child_next->inserting)
			#endif
			{			
				try_helping_insert(child_next);
				parent_node = child_next;
				TRAVERSE();
				#ifdef COMPUTE_AVG
					pointers_ins_traversed += 1;
				#endif	
			}
			else if(child_next!=NULL && child_next->key == key)
			{				
				ins_seek.duplicate = DUPLICATE_DIRECTION;
				return ins_seek;
			}
			else if((parent_direction == LEFT_DIRECTION && parent_node->left == ((node_t*)(MARK_LEAF_NODE(child_node)))) || (parent_direction == RIGHT_DIRECTION && parent_node->right == ((node_t*)MARK_LEAF_NODE(child_node))))
			{				
				ins_seek.child = (node_t*) child_node;
				ins_seek.cas1_node = (node_t*) parent_node;
				ins_seek.cas2_node = (node_t*) MARK_LEAF_NODE(child_node);
				ins_seek.next_node = child_next;
				ins_seek.parent_direction = parent_direction;
				return ins_seek;
			}
			else
			{
				TRAVERSE();
				#ifdef COMPUTE_AVG
					pointers_ins_traversed += 1;
				#endif	
			}
		}		
	}
}
static inline void try_helping_insert(volatile node_t *new_node)
{
	uint8_t parent_direction;
	node_t *cas1_node, *cas2_node;
	
	parent_direction = new_node->parent_direction;
	cas1_node = (node_t*) new_node->parent;
	cas2_node = (node_t*) new_node->left;
	
	if(parent_direction == LEFT_DIRECTION && new_node->inserting)
	{	
		if(new_node->inserting)
		{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif																				
			CAS(&cas1_node->left,cas2_node,new_node);
			if(new_node->inserting)new_node->inserting = 0;
		}
	}
	else if(parent_direction == RIGHT_DIRECTION && new_node->inserting)
	{
		if(new_node->inserting)
		{		
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif																			
			CAS(&cas1_node->right,cas2_node,new_node);
			if(new_node->inserting)new_node->inserting = 0;
		}	
	}
}
void physical_delete(tsl_set_t *set, volatile node_t *dummy_node)
{
	volatile node_t *child_node, *child_next, *grand_parent_node, *parent_node, *root = set->root;
	uint8_t parent_direction, clear=0;
	node_t *parent_node_left, *parent_node_right, *cas_val, *current_next, *marked_node;
	uint64_t operation_mark, child_mark;
#ifdef COMPUTE_STATS
    totalReb += 1;
#endif
	
	grand_parent_node = NULL;
	parent_node = root;
	child_node = (volatile node_t*) root->left;
	child_mark = 0;
	operation_mark = 0;
	marked_node = NULL;
	
	while (1) 
	{											
		/***************** Traverse deleted nodes ************************************/
		if(operation_mark == DELETE_MARK)
		{			
			READ_RIGHT();			
			#ifdef COMPUTE_AVG
				pointers_reb_traversed += 1;
			#endif
			marked_node = (node_t*) parent_node;

			while(1)
			{								
				if(operation_mark == DELETE_MARK)
				{					
					if(child_mark != LEAF_MARK)
					{					
						parent_node = child_node;						
						READ_RIGHT();
						#ifdef COMPUTE_AVG
							pointers_reb_traversed += 1;
						#endif
						continue;
					}
					else
					{
						child_next = ADDRESS((volatile node_t*)child_node->next);						
						if(child_next->inserting && child_next->parent == parent_node)
						{
							try_helping_insert(child_next);
						}
						//Confirm edge is complete
						else if(parent_node->right == (node_t*)(MARK_LEAF_NODE(child_node)))
						{
							if(grand_parent_node->key!=0)
							{
								grand_parent_node->key=0;
							}
							goto FINISH;
						}
						READ_RIGHT();
						#ifdef COMPUTE_AVG
							pointers_reb_traversed += 1;
						#endif
						continue;						
					}
				}
				else  
				{									
					if(!GETMARK((volatile node_t*)(grand_parent_node->next)))
					{
						if(grand_parent_node->left == marked_node)
						{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
							if(CAS(&grand_parent_node->left,marked_node,parent_node))
							{
								READ_LEFT();
								#ifdef COMPUTE_AVG
									pointers_reb_traversed += 1;
								#endif
								break;
							}
						}
						parent_node = grand_parent_node;
						READ_LEFT();
						#ifdef COMPUTE_AVG
							pointers_reb_traversed += 1;
						#endif
						break;
					}
					goto FINISH;
				}				
			}
		}
		else
		{			
			/***************** Traverse active nodes ************************************/				
			if(child_mark != LEAF_MARK)
			{													
				if(parent_node->key == 0 || parent_node==dummy_node)
				{
					if(parent_node->key!=0)
						parent_node->key=0;
					goto FINISH;
				}
				grand_parent_node = parent_node;
				parent_node = child_node;			
				READ_LEFT();
				#ifdef COMPUTE_AVG
					pointers_reb_traversed += 1;
				#endif
				continue;
			}	
			else
			{				
				current_next = (node_t*) child_node->next;
				child_next = ADDRESS(current_next);		
				if(GETMARK(current_next))
				{
					if(child_next->inserting && child_next->parent == parent_node)
					{
						try_helping_insert(child_next);
					}
					else if(parent_node->left == (node_t*)(MARK_LEAF_NODE(child_node)))
					{
						if(child_next->key!=0)child_next->key=0;
						goto FINISH;
					}					
					READ_LEFT();
					#ifdef COMPUTE_AVG
						pointers_reb_traversed += 1;
					#endif
					continue;			
				}
			}
			FINISH:
			break;
		}
	}
}

sval_t delete_min(tsl_set_t *set) 
{		
	volatile node_t *leaf_node, *next_leaf, *head = set->head;
	node_t *xor_node, *current_next, *head_item_node, *new_head=NULL;
	sval_t value;
#ifdef COMPUTE_STATS
  	totalDel += 1;
#endif
	
	leaf_node = (node_t*) head->next;
	head_item_node = (node_t*) head->next;
	/*optimisation batch deleting*/
	if(previous_head == leaf_node) { leaf_node = previous_dummy; }
	else { previous_head = head_item_node; }

	while(1)
	{	
		current_next = (node_t*) leaf_node->next;
		next_leaf = ADDRESS((volatile node_t*)(current_next));
		#ifdef COMPUTE_AVG
			pointers_traversed += 1;
		#endif
		if(next_leaf == NULL)
		{
			previous_dummy = (node_t*) leaf_node;
			return 0;
		}
		else
		{
			physical_deletes += 1;																
			if(GETMARK((volatile node_t*)(current_next)))
			{
				leaf_node = next_leaf;
				continue;
			}
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
#ifdef COMPUTE_AVG
	pointers_traversed += 1;
#endif

			xor_node = (node_t*) FAXOR(&leaf_node->next,1);

#ifdef COMPUTE_STATS
	numDelRetries += 1;
#endif
			if(!GETMARK((volatile node_t*)(xor_node)))
			{
				value = xor_node->value;
				previous_dummy = xor_node;
				
				if (physical_deletes > physical_delete_threshold)
				{
					physical_deletes = 0;
					if(head->next == head_item_node)
					{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif			
						if(CAS(&head->next,head_item_node,xor_node))
						{
							previous_head = xor_node;
							if(xor_node->key!=0)xor_node->key=0;
							physical_delete(set, xor_node);
						}	
					}
				}
				return value;
			}

			leaf_node = ADDRESS((volatile node_t*)(xor_node));
		}
	} 
	
}

uint32_t pq_size(tsl_set_t *set) 
{
	uint32_t count=0;
	node_t* prev_node;
	node_t* leaf_node, *next_leaf, *head = set->head;

	leaf_node = ADDRESS((volatile node_t*)(head->next));
	next_leaf = ADDRESS((volatile node_t*)(leaf_node->next));
	
	while(leaf_node != NULL)
	{		
		next_leaf = ADDRESS((volatile node_t*)(leaf_node->next));
		if(!GETMARK((volatile node_t*)(leaf_node->next)) && next_leaf!=NULL)
		{
			/** Count the number of leaf nodes excluding the dummy nodes and deleted nodes **/
			count+=1;
		}
		leaf_node = ADDRESS((volatile node_t*)(leaf_node->next));
	}
    return count;
}

static unsigned int calculate_node_height(node_t *node, int current_height) {
    if (node == NULL || GETMARK((volatile node_t*)node) != NOT_MARKED) {
        return current_height;
    }
    
    int left_height = calculate_node_height((node_t*)node->left, current_height + 1);
    int right_height = calculate_node_height((node_t*)node->right, current_height + 1);
    
    return (left_height > right_height) ? left_height : right_height;
}

unsigned int calculate_tree_height(tsl_set_t *set) {
    return calculate_node_height((node_t*)set->root->left, 0);
}

/* Benchmark helper: walk chain and clflush each node (not part of queue API). */
void scan(tsl_set_t *set) {
    if (set == NULL || set->head == NULL) {
        return;
    }

    node_t *curr = (node_t*) set->head->next;
    unsigned int i = 0;
    
    while (curr != NULL && curr != set->head) {
		node_t *next_node = (node_t*) curr->next;
		
		for (size_t offset = 0; offset < sizeof(node_t); offset += 64) {
            _mm_clflush((char*)curr + offset);
        }
        
        curr = next_node;
        i += 1;
    }
    printf("Number of nodes scanned: %u\n", i);
}