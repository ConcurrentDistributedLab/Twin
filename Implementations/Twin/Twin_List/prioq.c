#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <x86intrin.h>

#include "../../../Library/Allocator/BumpAllocator.hpp"

/* interface, constant defines, and typedefs */
#include "prioq.h"

#include "../../../src/core/globals.h"

#define CAS __sync_bool_compare_and_swap
#define FAO __sync_fetch_and_or
#define FAA __sync_fetch_and_add

#define CHECK_AND_ABORT(cond, msg) \
    if (cond) { \
        printf("\033[1;31m%s\033[0m\n", msg); \
        exit(EXIT_FAILURE); \
    }

BumpAllocator<Node_s> *alloc;

typedef long int (*CombinerFunctionPointer)(void*, long int, int);
typedef void (*RestructureFunctionPointer)(void*, long int, int);

static int halfInsert(MinMaxQueue_s* pq, Node_s *ptr, Key_t k, int type, int pid);
static void locate(MinMaxQueue_s* pq, Key_t k, Node_s **pred, Node_s **succ, int type);
static int before(Key_t k1, Key_t k2, int type);

static Value_t combinerDelete(MinMaxQueue_s*  pq, ArgVal type, int pid);
static void restructure(MinMaxQueue_s*  pq, ArgVal type, int pid);

static Node_s* allocNode(Key_t k, Value_t v, int pid);
static void freeNode(Node_s* n);


/*====================== Interface ======================*/

void DEPQ_ThreadStateInit(MinMaxQueue_s *object_struct, \
                                 CCPriorityQThreadState_s *lobject_struct, int pid) {
    CCSynchThreadStateInit(&object_struct->Combiner[MIN], \
                                &lobject_struct->delete_thread_state[MIN], pid);
    CCSynchThreadStateInit(&object_struct->Combiner[MAX], \
                                &lobject_struct->delete_thread_state[MAX], pid);
}

/* Allocate Twin-List: sentinel heads, LastDeleted, and per-end CC-Synch combiners. */
MinMaxQueue_s* DEPQ_Init(int nthreads) {
    MinMaxQueue_s *pq;
    Node_s *MinHead, *MaxHead;

    alloc = new BumpAllocator<Node_s>(nthreads);

    /* head and tail nodes */
    MinHead = allocNode(SENTINEL_KEYMIN, (Value_t) 0, 0);
    MaxHead = allocNode(SENTINEL_KEYMAX, (Value_t) 0, 0);

    MinHead->next[MIN] = MaxHead;
    MaxHead->next[MAX] = MinHead;

    pq = (MinMaxQueue_s*) malloc(sizeof(MinMaxQueue_s));

    pq->Head[MIN] = MinHead;
    pq->Head[MAX] = MaxHead;
    pq->LastDeleted[MIN] = MinHead;
    pq->LastDeleted[MAX] = MaxHead;

    CCSynchStructInit(&pq->Combiner[MIN], nthreads);
    CCSynchStructInit(&pq->Combiner[MAX], nthreads);

    pq->checksum_delete = 0;

    return pq;
}


/* Insert on both list ends (MIN and MAX). Returns 1 on success, 0 if key exists/deleted. */
int DEPQ_Insert(MinMaxQueue_s*  pq, Key_t k, Value_t v, int pid)
{
    int result = 0;
    Node_s *ptr;

    /* Initialise a new node for insertion. */
    ptr = allocNode(k, v, pid);
#ifdef COMPUTE_STATS
  totalIns += 1;
#endif
    
    return halfInsert(pq, ptr, k, MIN, pid) && halfInsert(pq, ptr, k, MAX, pid);
}


static int halfInsert(MinMaxQueue_s* pq, Node_s *ptr, Key_t k, int type, int pid)
{
    int first = 1;
    Node_s *pred = (Node_s*) pq->LastDeleted[type], *succ;

    while (first == 1 || !CAS(&(pred->next[type]), succ, ptr)) {
#ifdef COMPUTE_STATS
        numInsRetries += 1;
#endif
        locate(pq, k, &pred, &succ, type);

        /* return if node is deleted, or key already exists, i.e., 
        * is present in a non-deleted node */
        if ((ptr->reserved == 1) || (succ->k == k && !is_marked_ref(pred->next[type]) &&
                pred->next[type] == succ && succ->reserved != 1)) {
            return 0;
        }

        /* The node is logically inserted once it is present at the bottom
        * level. */
        ptr->next[type] = succ;

        first = 0;   
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
    }

    return 1;
}

/* Find pred/succ for key on one end (type = MIN or MAX); skip marked nodes. */
static void locate(MinMaxQueue_s* pq, Key_t k, Node_s **pred, Node_s **succ, int type) {
    Node_s *x, *x_next;
    Node_s *head;
#ifdef COMPUTE_STATS
  locate_calls += 1;
#endif

    head = (Node_s*) pq->LastDeleted[type];
    if (is_marked_ref((*pred)->next[type])) 
        x = (Node_s*) ((is_marked_ref(head->next[type])) ? get_unmarked_ref(head->next[type]) : head);
    else x = *pred;
    x_next = (Node_s*) get_unmarked_ref(x->next[type]);

    while (before(x_next->k, k, type) || is_marked_ref(x_next->next[type]) ) {
        x = x_next;
        x_next = (Node_s*) get_unmarked_ref(x->next[type]);
        #ifdef COMPUTE_AVG
            pointers_ins_traversed += 1;
        #endif	
    }

    (*pred) = x;
    (*succ) = x_next;
}

static int before(Key_t k1, Key_t k2, int type) {
    if (type == MIN) return k1 < k2;
    else return k2 < k1;
}


Value_t DEPQ_DeleteMin(MinMaxQueue_s*  priorq, CCPriorityQThreadState_s*  lobject_struct, int pid) {
    return (Value_t) CCSynchApply2Op(&priorq->Combiner[MIN], &lobject_struct->delete_thread_state[MIN], (CombinerFunctionPointer) combinerDelete, priorq, (RestructureFunctionPointer) restructure, priorq, (ArgVal)MIN, pid);
}


Value_t DEPQ_DeleteMax(MinMaxQueue_s*  priorq, CCPriorityQThreadState_s*  lobject_struct, int pid) {
    return (Value_t) CCSynchApply2Op(&priorq->Combiner[MAX], &lobject_struct->delete_thread_state[MAX], (CombinerFunctionPointer) combinerDelete, priorq, (RestructureFunctionPointer) restructure, priorq, (ArgVal)MAX, pid);
}


/* Combiner callback: logically delete next live node on end `type` (MIN or MAX). */
static Value_t combinerDelete(MinMaxQueue_s*  pq, ArgVal type, int pid) {
    Value_t value = (Value_t)0;
    Node_s* lastDeleted = (Node_s*)0;
    Node_s* tail = (Node_s*)0;
    int wasReserved;

    lastDeleted = (Node_s*) pq->LastDeleted[type];
    tail = (Node_s*) pq->Head[1 - type];

    wasReserved = 1;
#ifdef COMPUTE_STATS
    totalDel += 1;
#endif
    while (wasReserved == 1) {
        if (lastDeleted->next[type] == tail) {
            value = 0;
            break;
        }
        else {
            /* Mark next pointer, then claim the node via reserved. */
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
            lastDeleted = (Node_s*) FAO(&lastDeleted->next[type], 1);   
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
            wasReserved = FAO(&lastDeleted->reserved, 1);
#ifdef COMPUTE_STATS
    numDelRetries += 1;
#endif
#ifdef COMPUTE_AVG
    pointers_traversed += 1;
#endif
            value = lastDeleted->v;
        }
    }

    pq->LastDeleted[type] = lastDeleted;   

    return value;
}


static void restructure(MinMaxQueue_s*  pq, ArgVal type, int pid) {
    volatile Node_s *head = pq->Head[type];
    volatile Node_s *lastDeleted = pq->LastDeleted[type];
#ifdef COMPUTE_STATS
    totalReb += 1;
#endif

  if (lastDeleted == head || \
        lastDeleted == get_unmarked_ref(head->next[type])) return;
  else {
    head->next[type] = (volatile Node_s*) get_marked_ref(lastDeleted);
  }
}


static Node_s* allocNode(Key_t k, Value_t v, int pid) {
    Node_s* ptr;

    ptr = alloc->allocate(pid);

    ptr->k = k;
    ptr->v = v;

    ptr->reserved = 0;

    ptr->next[MIN] = (Node_s*) 0;
    ptr->next[MAX] = (Node_s*) 0;

    return ptr;
}


/* No-op: node lifetime is owned by the bump allocator (no per-node free). */
static void freeNode(Node_s* n) { (void)n; }

/* Benchmark helper: walk the MIN chain and clflush each node (not part of queue API). */
extern void scan(MinMaxQueue_s *pq) {
    if (pq == NULL || pq->Head[MIN] == NULL) {
        return;
    }
    
    volatile Node_s *curr = pq->Head[MIN]->next[MIN];
    
    unsigned int i = 0;
    while (curr != NULL && curr != pq->Head[MAX]) {
      volatile Node_s *next_node = curr->next[MIN];
      
      for (size_t offset = 0; offset < sizeof(Node_s); offset += 64) {
          _mm_clflush((char*)curr + offset);
      }
          
      curr = next_node;
      i += 1;
    }
    printf("Number of nodes in the priority queue: %u\n", i);
}