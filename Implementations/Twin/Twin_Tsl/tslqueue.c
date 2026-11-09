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
 * along with this program (see Implementations/Tsl/LICENSE.md).
 * If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "tslqueue.h"
#include "../../../Library/Allocator/BumpAllocator.hpp"
#ifdef COMPUTE_STATS
#include "../../../src/core/globals.h"
#endif
#include <x86intrin.h>

typedef long int (*CombinerFunctionPointer)(void*, long int, int);
typedef void (*RestructureFunctionPointer)(void*, long int, int);

static insert_seek_t insert_search(tsl_set_t *set, skey_t key, int type);
static uint8_t insert(tsl_set_t *set, skey_t key, sval_t value, volatile node_t *new_node, int type, int tid);
static sval_t combinerDelete(tsl_set_t *set, int type, int tid);
static inline void try_helping_insert(volatile node_t* item_node, int type);
static void physical_delete(tsl_set_t *set, int type, int tid);

BumpAllocator<node_t> *alloc;

void init_local(tsl_set_t* object_struct, CCPriorityQThreadState_s *lobject_struct, int tid)
{
    CCSynchThreadStateInit(&object_struct->Combiner[MIN], \
                                &lobject_struct->delete_thread_state[MIN], tid);
    CCSynchThreadStateInit(&object_struct->Combiner[MAX], \
                                &lobject_struct->delete_thread_state[MAX], tid);
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
	new_node->left[MIN] = NULL;
	new_node->right[MIN] = NULL;
	new_node->parent[MIN] = NULL;
	new_node->next[MIN] = NULL;

	new_node->left[MAX] = NULL;
	new_node->right[MAX] = NULL;
	new_node->parent[MAX] = NULL;
	new_node->next[MAX] = NULL;
	new_node->value = 0;
	new_node->reserved = 0;

    return (node_t*) new_node;
}

tsl_set_t* create_set(size_t num_threads)
{
	volatile node_t *root, *head, *dummyMin, *dummyMax;
	tsl_set_t *new_set;

	alloc = new BumpAllocator<node_t>(num_threads);
	
	new_set = (tsl_set_t*) malloc(sizeof(tsl_set_t));

	head = (volatile node_t*) alloc->allocate(0);
    root = (volatile node_t*) alloc->allocate(0);
	dummyMin = (volatile node_t*) alloc->allocate(0);
	dummyMax = (volatile node_t*) alloc->allocate(0);
	
	dummyMin->key = SENTINEL_KEYMIN;
	dummyMin->left[MIN] = (void*) head;
	dummyMin->left[MAX] = (void*) NULL;

	dummyMin->right[MIN] = (void*) MARK_LEAF_NODE(dummyMin);
	dummyMin->right[MAX] = NULL;

	dummyMin->parent[MIN] = (void*) root;
	dummyMin->parent[MAX] = (void*) NULL;

	dummyMin->value = 0;
	dummyMin->next[MIN] = NULL;
	dummyMin->next[MAX] = NULL;
	

	dummyMax->key = SENTINEL_KEYMAX;
	dummyMax->left[MAX] = (void*) head;
	dummyMax->left[MIN] = (void*) NULL;

	dummyMax->right[MAX] = (void*) MARK_LEAF_NODE(dummyMax);
	dummyMax->right[MIN] = NULL;

	dummyMax->parent[MAX] = (void*) root;
	dummyMax->parent[MIN] = (void*) NULL;

	dummyMax->value = 0;
	dummyMax->next[MAX] = NULL;
	dummyMax->next[MIN] = NULL;


	head->right[MIN] = NULL;
	head->right[MAX] = NULL;
	head->left[MIN] = NULL;
	head->left[MAX] = NULL;

	head->next[MIN] = (void*) dummyMin;
	head->next[MAX] = (void*) dummyMax;

	head->key = SENTINEL_KEYMIN;

	root->right[MIN] = NULL;
	root->right[MAX] = NULL;
	root->left[MIN] = (void*) dummyMin;
	root->left[MAX] = (void*) dummyMax;

	root->parent[MIN] = NULL;
	root->parent[MAX] = NULL;

	root->key = 1;

	new_set->head = (node_t*) head;
	new_set->root = (node_t*) root;
	new_set->LastDeleted[MIN] = (node_t*) dummyMin;
	new_set->LastDeleted[MAX] = (node_t*) dummyMax;
	new_set->num_threads = num_threads;
	new_set->del_scale = num_threads * 100;

    CCSynchStructInit(&new_set->Combiner[MIN], num_threads);
    CCSynchStructInit(&new_set->Combiner[MAX], num_threads);

	return new_set;
}

uint8_t depq_insert(tsl_set_t *set, skey_t key, sval_t value, int tid) {
	volatile node_t *new_node = create_node(tid);
	new_node->key = key;
	new_node->value = value;
#ifdef COMPUTE_STATS
  totalIns += 1;
#endif

	return insert(set, key, value, new_node, MIN, tid) && insert(set, key, value, new_node, MAX, tid);
}

uint8_t insert(tsl_set_t *set, skey_t key, sval_t value, volatile node_t *new_node, int type, int tid) 
{	
	volatile node_t *cas1_node, *cas2_node, *leaf_node;
	node_t *next_leaf;
	uint8_t parent_direction;
	insert_seek_t ins_seek;
	
	new_node->right[type] = (void*) MARK_LEAF_NODE(new_node);
	while (1)
	{															
		cas1_node = NULL;
		cas2_node = NULL;
		 
		ins_seek = insert_search(set, key, type);
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
		
		new_node->left[type] = (void*) MARK_LEAF_NODE(leaf_node);
		new_node->parent_direction[type] = parent_direction;	
		new_node->parent[type] = (void*) cas1_node;
		new_node->next[type] = (void*) next_leaf;//create node link
		new_node->inserting[type] = 1;
		/***************** Insert ************************************/
		#ifdef COMPUTE_STATS
			numInsRetries += 1;
		#endif
		if(leaf_node->next[type] == next_leaf)
		{
			if(parent_direction == RIGHT_DIRECTION)
			{
				if(leaf_node->next[type] == next_leaf)	
				{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
					if(CAS(&leaf_node->next[type],next_leaf,new_node))
					{
						if(new_node->inserting[type])
						{
							if(cas1_node->right[type] == cas2_node) {
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
								CAS(&cas1_node->right[type],cas2_node,new_node);
							}
							//unmark inserted new item node
							if(new_node->inserting[type])new_node->inserting[type] = 0;
						}
						return 1;
					}
				}
			}
			else if(parent_direction == LEFT_DIRECTION)//&& !GETMARK(cas1_node->next[type])
			{
				if(leaf_node->next[type] == next_leaf)	
				{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
					if(CAS(&leaf_node->next[type],next_leaf,new_node))
					{
						if(new_node->inserting[type])
						{
							if(cas1_node->left[type] == cas2_node) {
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
								CAS(&cas1_node->left[type],cas2_node,new_node);
							}
							//unmark inserted new item node
							if(new_node->inserting[type])new_node->inserting[type] = 0;
						}
						return 1;
					}
				}
			}
		}					
	}
}

insert_seek_t insert_search(tsl_set_t *set, skey_t key, int type)
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
	child_node = (volatile node_t*) root->left[type];
	assert(child_node != NULL);
	while (1) 
	{							
		/***************** Tranverse deleted nodes (right tranverse)************************************/
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
						READ_RIGHT();
						#ifdef COMPUTE_AVG
							pointers_ins_traversed += 1;
						#endif
						continue;
					}
					else
					{
						parent_node = ADDRESS((volatile node_t*)child_node->next[type]);
						READ_RIGHT();
						#ifdef COMPUTE_AVG
							pointers_ins_traversed += 1;
						#endif
						break;
					}
				}
				else  
				{
					if(!GETMARK((volatile node_t*)grand_parent_node->next[type]) && grand_parent_node->left[type]==marked_node)
					{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
						CAS(&grand_parent_node->left[type],marked_node,parent_node);
					}
					TRAVERSE(type);
					#ifdef COMPUTE_AVG
						pointers_ins_traversed += 1;
					#endif
					break;
				}				
			}
			continue;
		}
		/***************** Tranverse internal nodes ************************************/			
		if(child_mark != LEAF_MARK)
		{
			grand_parent_node = parent_node;
			parent_node = child_node;
			TRAVERSE(type);
			#ifdef COMPUTE_AVG
				pointers_ins_traversed += 1;
			#endif
		}
		else
		{
			current_next = (node_t*)child_node->next[type];
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
			else if(child_next!=NULL && child_next->inserting[type])
			{
				try_helping_insert(child_next, type);
				parent_node = child_next;
				assert(child_next != NULL);
				TRAVERSE(type);
				#ifdef COMPUTE_AVG
					pointers_ins_traversed += 1;
				#endif
			}
			else if(child_next!=NULL && child_next->key == key)
			{				
				ins_seek.duplicate = DUPLICATE_DIRECTION;
				return ins_seek;
			}
			else if((parent_direction == LEFT_DIRECTION && parent_node->left[type] == ((node_t*)(MARK_LEAF_NODE(child_node)))) || (parent_direction == RIGHT_DIRECTION && parent_node->right[type] == ((node_t*)MARK_LEAF_NODE(child_node))))
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
				TRAVERSE(type);
				#ifdef COMPUTE_AVG
					pointers_ins_traversed += 1;
				#endif
			}
		}		
	}
}
static inline void try_helping_insert(volatile node_t *new_node, int type)
{
	uint8_t parent_direction;
	node_t *cas1_node, *cas2_node;
	
	parent_direction = new_node->parent_direction[type];
	cas1_node = (node_t*) new_node->parent[type];
	cas2_node = (node_t*) new_node->left[type];
	
	if(parent_direction == LEFT_DIRECTION && new_node->inserting[type])
	{	
		if(new_node->inserting[type])
		{				
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif																	
			CAS(&cas1_node->left[type],cas2_node,new_node);
			if(new_node->inserting[type])new_node->inserting[type] = 0;
		}
	}
	else if(parent_direction == RIGHT_DIRECTION && new_node->inserting[type])
	{
		if(new_node->inserting[type])
		{				
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif																	
			CAS(&cas1_node->right[type],cas2_node,new_node);
			if(new_node->inserting[type])new_node->inserting[type] = 0;
		}	
	}
}

static void physical_delete(tsl_set_t *set, int type, int tid)
{
	volatile node_t *child_node, *child_next, *grand_parent_node, *parent_node, *root = set->root;
	uint8_t parent_direction, clear=0;
	node_t *parent_node_left, *parent_node_right, *cas_val, *current_next, *marked_node;
	uint64_t operation_mark, child_mark;
	volatile node_t *dummy_node = set->LastDeleted[type];
	volatile node_t *head = set->head;
#ifdef COMPUTE_STATS
    totalReb += 1;
#endif

	head->next[type] = (node_t*) dummy_node;

	grand_parent_node = NULL;
	parent_node = root;
	child_node = (volatile node_t*) root->left[type];
	child_mark = 0;
	operation_mark = 0;
	marked_node = NULL;
	
	while (1) 
	{											
		/***************** Tranverse deleted nodes ************************************/
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
						child_next = ADDRESS((volatile node_t*)child_node->next[type]);						
						if(child_next->inserting[type] && child_next->parent[type] == parent_node)
						{
							try_helping_insert(child_next, type);
						}
						//Confirm edge is complete
						else if(parent_node->right[type] == (node_t*)(MARK_LEAF_NODE(child_node)))
						{
							if(type == MIN){
								if(grand_parent_node->key!=SENTINEL_KEYMIN)grand_parent_node->key=SENTINEL_KEYMIN;
							}
							else {
								if(grand_parent_node->key!=SENTINEL_KEYMAX)grand_parent_node->key=SENTINEL_KEYMAX;
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
					if(!GETMARK((volatile node_t*)(grand_parent_node->next[type])))
					{
						if(grand_parent_node->left[type] == marked_node)
						{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
							if(CAS(&grand_parent_node->left[type],marked_node,parent_node))
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
			/***************** Tranverse active nodes ************************************/				
			if(child_mark != LEAF_MARK)
			{													
				if(parent_node->key == SENTINEL_KEYMIN || parent_node->key == SENTINEL_KEYMAX || parent_node==dummy_node)
				{					
					if(type == MIN){
						if(parent_node->key!=SENTINEL_KEYMIN)parent_node->key=SENTINEL_KEYMIN;
					}
					else {
						if(parent_node->key!=SENTINEL_KEYMAX)parent_node->key=SENTINEL_KEYMAX;
					}
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
				current_next = (node_t*) child_node->next[type];
				child_next = ADDRESS(current_next);		
				if(GETMARK(current_next))
				{
					if(child_next->inserting[type] && child_next->parent[type] == parent_node)
					{
						try_helping_insert(child_next, type);
					}
					else if(parent_node->left[type] == (node_t*)(MARK_LEAF_NODE(child_node)))
					{
						if(type == MIN){
							if(child_next->key!=SENTINEL_KEYMIN)child_next->key=SENTINEL_KEYMIN;
						}
						else {
							if(child_next->key!=SENTINEL_KEYMAX)child_next->key=SENTINEL_KEYMAX;
						}
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

static sval_t combinerDelete(tsl_set_t *set, int type, int tid) 
{		
	volatile node_t *next_leaf;
	volatile node_t *lastDeleted;
    int wasReserved;
	node_t *xor_node, *current_next, *head_item_node, *new_head=NULL;
	sval_t value;

	lastDeleted = (node_t*) set->LastDeleted[type];
    wasReserved = 1;

  #ifdef COMPUTE_STATS
    int i = 0;
    totalDel += 1;
  #endif
  
	while(wasReserved == 1)
	{			
		if(lastDeleted->next[type] == NULL) { 
			value = 0;
			wasReserved = 0;
		}
		else
		{
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
			lastDeleted = (node_t*) FAXOR(&lastDeleted->next[type],1);
			value = lastDeleted->value;
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
			wasReserved = FAXOR(&lastDeleted->reserved,1);
#ifdef COMPUTE_STATS
	numDelRetries += 1;
#endif
#ifdef COMPUTE_AVG
	pointers_traversed += 1;
#endif
		}
	}

    set->LastDeleted[type] = (node_t*) lastDeleted;

	return value;
}

/* Twin: extract via CC-Synch; restructure callback is physical_delete. */
sval_t delete_max(tsl_set_t *set, CCPriorityQThreadState_s*  lobject_struct, int tid) {
	return (sval_t) CCSynchApply2Op(&set->Combiner[MAX], &lobject_struct->delete_thread_state[MAX], (CombinerFunctionPointer) combinerDelete, set, (RestructureFunctionPointer) physical_delete, set, (ArgVal)MAX, tid);
}

sval_t delete_min(tsl_set_t *set, CCPriorityQThreadState_s*  lobject_struct, int tid) {		
	return (sval_t) CCSynchApply2Op(&set->Combiner[MIN], &lobject_struct->delete_thread_state[MIN], (CombinerFunctionPointer) combinerDelete, set, (RestructureFunctionPointer) physical_delete, set, (ArgVal)MIN, tid);
}

static unsigned int calculate_node_height(node_t *node, int current_height, int type) {
    if (node == NULL || GETMARK((volatile node_t*)node) != NOT_MARKED) {
        return current_height;
    }
    
    int left_height = calculate_node_height((node_t*)node->left[type], current_height + 1, type);
    int right_height = calculate_node_height((node_t*)node->right[type], current_height + 1, type);
    
    return (left_height > right_height) ? left_height : right_height;
}

unsigned int calculate_tree_height(tsl_set_t *set, int type) {
    return calculate_node_height((node_t*)set->root->left[type], 0, type);
}

void scan(tsl_set_t *set) {
    if (set == NULL || set->head == NULL) {
        return;
    }

    node_t *curr = (node_t*) set->head->next[MIN];
    unsigned int i = 0;
    
    while (curr != NULL && curr != set->head) {
		node_t *next_node = (node_t*) curr->next[MIN];
		
		for (size_t offset = 0; offset < sizeof(node_t); offset += 64) {
            _mm_clflush((char*)curr + offset);
        }
        
        curr = next_node;
        i += 1;
    }
    printf("Number of nodes in the priority queue: %u\n", i);
}