/*   
 *   File: priorityqueue-tslqueue.h
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "CCSynchLibrary/HeaderFiles/ccsynch.h"

#define SENTINEL_KEYMIN (0UL) /* Key value of first dummy node. */
#define SENTINEL_KEYMAX (~1UL) /* Key value of last dummy node.  */

#define ALIGNED(N) __attribute__ ((aligned (N)))
#define CACHE_LINE_SIZE   64

/******************x86 code atomics*****************************/
#define CAE(a,b,c) 			__atomic_compare_exchange (a,b,c,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)
#define CAS(a,b,c) 			__sync_bool_compare_and_swap(a,b,c)
#define FAXOR(a,b) 			__sync_fetch_and_or(a,b)
/***************************************************************/

typedef unsigned int skey_t;
typedef void* sval_t;

#define DS_ADD(s,k,t)       	insert(s,k,k)
#define DS_REMOVE(s)    		delete_min(s)
#define DS_SIZE(s)          	pq_size(s)
#define DS_NEW(n)            	create_set(n)
#define DS_LOCAL()          	init_local()

#define DS_TYPE             tsl_set_t
#define DS_NODE             node_t
#define DS_KEY              skey_t

#define MIN 0
#define MAX 1

#define LEFT_DIRECTION 1
#define RIGHT_DIRECTION 2
#define DUPLICATE_DIRECTION 3
#define NOT_MARKED 0
#define DELETE_MARK 1
#define INSERT_MARK 2
#define LEAF_MARK 3

/*********MACROS**********/
#define READ_LEFT()									\
	operation_mark = GETMARK((volatile node_t*)parent_node->next[type]);	\
	parent_node_left= (node_t*) parent_node->left[type];			\
	child_node = ADDRESS(parent_node_left);			\
	child_mark = GETMARK(parent_node_left);			\
	parent_direction = LEFT_DIRECTION;				
	
#define READ_RIGHT()								\
	operation_mark = GETMARK((volatile node_t*)parent_node->next[type]);	\
	parent_node_right = (node_t*) parent_node->right[type];			\
	child_node = ADDRESS(parent_node_right);		\
	child_mark = GETMARK(parent_node_right);		\
	parent_direction = RIGHT_DIRECTION;				

#define TRAVERSE(type)									\
	if (type == MIN) {									\
		if(key <= parent_node->key) {READ_LEFT();}		\
		else { READ_RIGHT();}							\
	}													\
	else {												\
		if(key > parent_node->key) {READ_LEFT();}		\
		else { READ_RIGHT();}							\
	}													\

typedef struct CCPriorityQThreadState {
	CCSynchThreadState delete_thread_state[2] CACHE_ALIGN;
} CCPriorityQThreadState_s;

typedef ALIGNED(CACHE_LINE_SIZE) struct generic_node
{
	void *next[2];
	void *parent[2];
	void *left[2];
	void *right[2];
	sval_t value;
	skey_t key;
	
	uint8_t inserting[2];
	uint8_t reserved;
	uint8_t parent_direction[2];
	// uint8_t padding[CACHE_LINE_SIZE - (sizeof(skey_t) + sizeof(sval_t) + (sizeof(void*)*4) + (sizeof(uint8_t)*4) + (sizeof(int)))];
}node_t;

typedef ALIGNED(CACHE_LINE_SIZE) struct tslqueue_set
{
  node_t *head;
  node_t *root;
  node_t *LastDeleted[2];
  size_t num_threads;
  uint32_t del_scale;
  CCSynchStruct Combiner[2];
  uint8_t padding[CACHE_LINE_SIZE - ((sizeof(node_t*)*2) + sizeof(size_t) + sizeof(uint32_t))];
} tsl_set_t;

typedef struct next_cast
{
	void *next;
	void *right;
} next_t;

typedef ALIGNED(CACHE_LINE_SIZE) struct insert_seek_record_info
{
    node_t* child;
	node_t* next_node;
	node_t* cas1_node;
	node_t* cas2_node;
	uint8_t duplicate;
	uint8_t parent_direction;
	uint8_t padding[CACHE_LINE_SIZE - ((sizeof(node_t*)*4) + (sizeof(uint8_t)*2))];
} insert_seek_t;

/* Interfaces */

static inline node_t* ADDRESS(volatile node_t* ptr) {
    return (node_t*) (((uint64_t)ptr) & 0xfffffffffffffffc);
}
static inline uint64_t GETMARK(volatile node_t* ptr) {
    return ((uint64_t)ptr) & 3;
}

static inline uint64_t MARK_DELETE_NODE(volatile node_t* ptr) {
    return (((uint64_t)ptr)) | DELETE_MARK;
}

static inline uint64_t MARK_INSERT_NODE(volatile node_t* ptr) {
    return (((uint64_t)ptr)) | INSERT_MARK;
}

static inline uint64_t MARK_LEAF_NODE(volatile node_t* ptr) {
    return (((uint64_t)ptr)) | LEAF_MARK;
}

tsl_set_t* create_set(size_t num_threads);
void init_local(tsl_set_t* object_struct, CCPriorityQThreadState_s *lobject_struct, int tid);

uint8_t depq_insert(tsl_set_t *set, skey_t key, sval_t value, int tid);
sval_t delete_min(tsl_set_t *set, CCPriorityQThreadState_s*  lobject_struct, int tid);
sval_t delete_max(tsl_set_t *set, CCPriorityQThreadState_s*  lobject_struct, int tid);
void scan(tsl_set_t *set);
	
unsigned int calculate_tree_height(tsl_set_t *set, int type);