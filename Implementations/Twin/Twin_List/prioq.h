#ifndef PRIOQ_H
#define PRIOQ_H

/* Delete-min/max path uses CC-Synch combining (see DEPQ_DeleteMin/Max). */
#include "CCSynchLibrary/HeaderFiles/ccsynch.h"

typedef unsigned long Key_t;
typedef void         *Value_t;

#define KEY_NULL 0

/* Internal key values with special meanings. */
#define SENTINEL_KEYMIN ( 1UL) /* Key value of first dummy node. */
#define SENTINEL_KEYMAX (~1UL) /* Key value of last dummy node.  */
#define EMPTY_PRIORITYQ (Value_t)0

#define MIN 0
#define MAX 1

typedef struct Node {
    Key_t      k;
    Value_t    v;

    volatile int       reserved;

    volatile struct Node *next[2];
} Node_s;


typedef struct MinMaxQueue {
    volatile Node_s *Head[2]    CACHE_ALIGN;  
    volatile Node_s *LastDeleted[2]    CACHE_ALIGN;  
    CCSynchStruct Combiner[2] CACHE_ALIGN;
    
    char   pad[128] CACHE_ALIGN;
    int64_t checksum_delete CACHE_ALIGN;
} MinMaxQueue_s;


typedef struct CCPriorityQThreadState {
	CCSynchThreadState delete_thread_state[2] CACHE_ALIGN;
} CCPriorityQThreadState_s;


#define get_marked_ref(_p)      ((void *)(((uintptr_t)(_p)) | 1))
#define get_unmarked_ref(_p)    ((void *)(((uintptr_t)(_p)) & ~1))
#define is_marked_ref(_p)       (((uintptr_t)(_p)) & 1)

/* Interface */
void DEPQ_ThreadStateInit(MinMaxQueue_s* object_struct, CCPriorityQThreadState_s *lobject_struct, int pid);

MinMaxQueue_s *DEPQ_Init(int nthreads);

int DEPQ_Insert(MinMaxQueue_s*  pq, Key_t k, Value_t v, int pid);

Value_t DEPQ_DeleteMin(MinMaxQueue_s*  pq, CCPriorityQThreadState_s*  lobject_struct, int pid);

Value_t DEPQ_DeleteMax(MinMaxQueue_s*  pq, CCPriorityQThreadState_s*  lobject_struct, int pid);

void scan(MinMaxQueue_s *pq);

#endif // PRIOQ_H
