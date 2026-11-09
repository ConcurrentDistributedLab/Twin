#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include "../../Library/Timer/timer.h"
#include "globals.h"
#include "algorithms.h"
#include <pthread.h>

/* Worker tids are offset by TIMETHREAD; that thread owns the wall-clock timer. */
#define TIMETHREAD 1

#ifndef ONLYMIN
#define ONLYMIN 0
#endif

#define OPERATIONS_COUNT op_count
#define LESS_OPERATIONS less_op
#define OPS_BETWEEN_TIME_CHECKS op_between_check

/* Per-worker args passed into test threads (barriers: see test_config.c). */
typedef struct ThreadsArgument_s {
  int tid;
  DataStructure *priorityq;
  pthread_barrier_t *sync;
  Timer *timers;
  unsigned int percent_op;
  unsigned int percent_ex;
  int *control;
  unsigned long long countInserts;
  unsigned long long countDeletes;
  unsigned int target_ops;
  unsigned int target_ms;
  unsigned int num_threads;
} ThreadsArgument;

inline DataStructure *pq_initialize(int maxThreads) {
#if defined(TWIN_LIST)
  return DEPQ_Init(maxThreads);
#elif defined(TSL) || defined(TWIN_TSL)
  return create_set(maxThreads);
#elif defined(BST) || defined(CCBST)
  void *const unused2 = NULL;
  return new DataStructure(maxThreads, -1, LLONG_MAX, unused2);
#elif defined(CHROMATIC) || defined(CCCHROMATIC)
  return new DataStructure(-1, -1, -2, maxThreads, SIGQUIT);
#elif defined(LJ) || defined(TWIN_LJ)
  return pq_init(maxThreads, maxThreads);
#else
#error You have not defined any Library
#endif
}

inline int pq_insert(DataStructure *priorityq, unsigned int key, int tid) {
  tid = tid - TIMETHREAD;
#if defined(TWIN_LIST)
  return DEPQ_Insert(priorityq, key, NULL, tid);
#elif defined(TSL)
  return insert(priorityq, key, NULL, tid);
#elif defined(TWIN_TSL)
  return depq_insert(priorityq, key, NULL, tid);
#elif defined(BST) || defined(CCBST)
  priorityq->insertIfAbsent(tid, key, NULL);
  return 1;
#elif defined(CHROMATIC) || defined(CCCHROMATIC)
  priorityq->insert(tid, key, key);
  return 1;
#elif defined(LJ) || defined(TWIN_LJ)
  return insert(priorityq, key, NULL, tid);
#else
#error "You have not defined any Library"
#endif
}

inline void pq_delete_min(DataStructure *priorityq, void *state, int tid) {
  tid = tid - TIMETHREAD;
#if defined(TWIN_LIST)
  DEPQ_DeleteMin(priorityq, (CCPriorityQThreadState_s *)state, tid);
#elif defined(TSL)
  delete_min(priorityq);
#elif defined(TWIN_TSL)
  delete_min(priorityq, (CCPriorityQThreadState_s *)state, tid);
#elif defined(BST)
  priorityq->eraseMin(tid);
#elif defined(CCBST)
  priorityq->eraseMin((CCPriorityQThreadState_s *)state, tid);
#elif defined(CHROMATIC)
  priorityq->deleteMin(tid);
#elif defined(CCCHROMATIC)
  priorityq->deleteMin((CCPriorityQThreadState_s *)state, tid);
#elif defined(LJ)
  deleteMin(priorityq, tid);
#elif defined(TWIN_LJ)
  deleteMin(priorityq, (CCPriorityQThreadState_s *)state, tid);
#else
#error "You have not defined any Library"
#endif
}

inline void pq_delete_max(DataStructure *priorityq, void *state, int tid) {
  tid = tid - TIMETHREAD;
#if ONLYMIN == 0
#if defined(TWIN_LIST)
  DEPQ_DeleteMax(priorityq, (CCPriorityQThreadState_s *)state, tid);
#elif defined(BST)
  priorityq->eraseMax(tid);
#elif defined(CCBST)
  priorityq->eraseMax((CCPriorityQThreadState_s *)state, tid);
#elif defined(TWIN_TSL)
  delete_max(priorityq, (CCPriorityQThreadState_s *)state, tid);
#elif defined(CHROMATIC)
  priorityq->deleteMax(tid);
#elif defined(CCCHROMATIC)
  priorityq->deleteMax((CCPriorityQThreadState_s *)state, tid);
#elif defined(TWIN_LJ)
  deleteMax(priorityq, (CCPriorityQThreadState_s *)state, tid);
#else
#error "You have not defined any Library"
#endif
#else
#pragma message("\x1B[33mDelete-max operations are closed.\x1B[0m")
#endif
}

void init_thread_state(int tid, DataStructure *priorityq, void *state);
unsigned int getTreeHeightMin(DataStructure *priorityq);
unsigned int getTreeHeightMax(DataStructure *priorityq);
void scanPQ(DataStructure *priorityq);
void random_insert_or_delete(DataStructure *priorityq, int tid, void *state,
                             int key, unsigned int percent_op, unsigned int percent_ex);
void random_delete(DataStructure *priorityq, int tid, void *state,
                   unsigned int percent);

#define SCAN_PQ(priorityq) scanPQ(priorityq)

#endif // PRIORITY_QUEUE_H