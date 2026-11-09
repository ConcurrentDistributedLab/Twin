#ifndef PRIOQ_H
#define PRIOQ_H

#include "CCSynchLibrary/HeaderFiles/ccsynch.h"

typedef unsigned long pkey_t;
typedef void         *pval_t;

#define KEY_NULL 0
#define NUM_LEVELS 32
/* Internal key values with special meanings. */
#define SENTINEL_KEYMIN ( 0UL) /* Key value of first dummy node. */
#define SENTINEL_KEYMAX (~1UL) /* Key value of last dummy node.  */

#define MIN 0
#define MAX 1

typedef struct CCPriorityQThreadState {
	CCSynchThreadState delete_thread_state[2] CACHE_ALIGN;
} CCPriorityQThreadState_s;

typedef struct node_s
{
    pkey_t    k;
    int       level;
    volatile int       reserved;
    volatile int       inserting[2]; //char pad2[4];
    pval_t    v;
    volatile struct node_s *next[2][NUM_LEVELS];
} node_t;

typedef struct
{
    int    max_offset;
    int    max_level;
    int    nthreads;
    node_t *head[2];
    volatile node_t *LastDeleted[2]    CACHE_ALIGN;
    CCSynchStruct Combiner[2] CACHE_ALIGN;
    char   pad[128];
} pq_t;

#define get_marked_ref(_p)      ((void *)(((uintptr_t)(_p)) | 1))
#define get_unmarked_ref(_p)    ((void *)(((uintptr_t)(_p)) & ~1))
#define is_marked_ref(_p)       (((uintptr_t)(_p)) & 1)

/* compiler memory barrier */
#define CMB() __asm__ __volatile__ ("" : : : "memory")

/* Interface */
extern void initThread(pq_t *pq, CCPriorityQThreadState_s *lobject_struct, int tid);
extern pq_t *pq_init(int max_offset, int maxThreads);
extern int insert(pq_t *pq, pkey_t k, pval_t v, int tid);
extern pval_t deleteMin(pq_t*  priorq, CCPriorityQThreadState_s*  lobject_struct, int pid);
extern pval_t deleteMax(pq_t*  priorq, CCPriorityQThreadState_s*  lobject_struct, int pid);
extern void print_skiplist_exact_distances(pq_t *pq);
extern void scan(pq_t *pq);

#endif // PRIOQ_H
