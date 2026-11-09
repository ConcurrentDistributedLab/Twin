#include "priorityq.h"

#include "../../Library/Pinning/pinning.h"
#include "../../Library/Random/fastrand.h"

#include <limits.h>
#include <time.h>

/* Thread setup: algorithm-specific state, then pin + seed RNG. */
void init_thread_state(int tid, DataStructure *priorityq, void *state) {
  tid = tid - TIMETHREAD;
  
#if defined(TWIN_LIST)
  CCPriorityQThreadState_s *cc_state = (CCPriorityQThreadState_s *)state;
  DEPQ_ThreadStateInit((MinMaxQueue_s *)priorityq, cc_state, tid);
#elif defined(BST)
  ds_adapter<long long, void *> *pq =
      static_cast<ds_adapter<long long, void *> *>(priorityq);
  pq->initThread(tid);
#elif defined(CCBST)
  CCPriorityQThreadState_s *cc_state = (CCPriorityQThreadState_s *)state;
  ds_adapter<long long, void *> *pq =
      static_cast<ds_adapter<long long, void *> *>(priorityq);
  pq->initThread(cc_state, tid);

#elif defined(TSL)
  init_local();
#elif defined(TWIN_TSL)
  CCPriorityQThreadState_s *cc_state = (CCPriorityQThreadState_s *)state;
  init_local((tsl_set_t*) priorityq, cc_state, tid);

#elif defined(CHROMATIC)
  DataStructure *pq = static_cast<DataStructure *>(priorityq);
  pq->initThread(tid);
#elif defined(CCCHROMATIC)
  CCPriorityQThreadState_s *cc_state = (CCPriorityQThreadState_s *)state;
  DataStructure *pq = static_cast<DataStructure *>(priorityq);
  pq->initThread(cc_state, tid);

#elif defined(LJ)
  initThread(tid);
#elif defined(TWIN_LJ)
  CCPriorityQThreadState_s *cc_state = (CCPriorityQThreadState_s *)state;
  initThread((pq_t *)priorityq, cc_state, tid);
#else
  int *simple_state = (int *)state;
  *simple_state = 0;
#endif

  threadPin(tid, availableCores);

  synchFastRandomSetSeed(tid + 1);
}

unsigned int getTreeHeightMin(DataStructure *priorityq) {
#if defined(BST) || defined(CCBST)
  return static_cast<ds_adapter<long long, void *> *>(priorityq)->getTreeHeight();

#elif defined(CHROMATIC) || defined(CCCHROMATIC)
  return priorityq->get_tree_height();

#elif defined(TSL)
  return calculate_tree_height(priorityq);
#elif defined(TWIN_TSL)
  return calculate_tree_height(priorityq, 0);
#else
  return 0;
#endif

}

unsigned int getTreeHeightMax(DataStructure *priorityq) {
#if defined(TWIN_TSL)
  return calculate_tree_height(priorityq, 1);
#else
  return 0;
#endif

}

void scanPQ(DataStructure *priorityq) {
#if defined(BST) || defined(CCBST)
  static_cast<ds_adapter<long long, void *> *>(priorityq)->scan();

#elif defined(CHROMATIC) || defined(CCCHROMATIC)
  priorityq->scan();

#elif defined(TSL)    || defined(TWIN_TSL)  || \
      defined(LJ) || defined(TWIN_LJ) || \
      defined(TWIN_LIST)
  scan(priorityq);
#endif

}

/*
 * Mixed insert/delete: roll in [1, 60]; insert if roll <= 60*percent_op/100,
 * else delete. Denominator 60 must stay in sync with random_delete.
 */
void random_insert_or_delete(DataStructure *priorityq, int tid, void *state,
                             int key, unsigned int percent_op, unsigned int percent_ex) {
  long rnum = synchFastRandomRange(1, 60);

  if (rnum <= (60 * percent_op) / 100) {
    pq_insert(priorityq, key, tid);
  } else {
    random_delete(priorityq, tid, state, percent_ex);
  }
}

/* Delete-min vs delete-max using the same [1, 60] scale as above. */
void random_delete(DataStructure *priorityq, int tid, void *state,
                   unsigned int percent) {
  long rnum = synchFastRandomRange(1, 60);

  if (rnum <= (60 * percent) / 100 || ONLYMIN) {
    pq_delete_min(priorityq, state, tid);
  } else {
    pq_delete_max(priorityq, state, tid);
  }
}
