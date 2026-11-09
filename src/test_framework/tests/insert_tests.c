#include "insert_tests.h"

#include "../../core/priorityq.h"
#include "../../../Library/Timer/timer.h"
#include "../../../Library/Random/fastrand.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

void *test_insert_rk(void *argument) {
  ThreadsArgument *arg = (ThreadsArgument *)argument;
  DataStructure *priorityq = arg->priorityq;
  unsigned int percent_op = arg->percent_op;
  unsigned int percent_ex = arg->percent_ex;
  unsigned int key;
  unsigned int i = 0;
  unsigned int count = arg->target_ops;
  unsigned int threads = arg->num_threads;

#if defined(CCSYNCH)
  CCPriorityQThreadState_s state;
#else
  int state;
#endif

  init_thread_state(arg->tid, priorityq, &state);

  pthread_barrier_wait(&(arg->sync[0]));

  if (arg->tid == TIMETHREAD) {
    startTimer(&(arg->timers[0]));
  }

  for (i = 0; i < count / threads; i++) {
    key = synchFastRandomHugeRange32(2, (~2UL));
    if (pq_insert(priorityq, key, arg->tid) != 1) {
      i -= 1;
    }
  }

  pthread_barrier_wait(&(arg->sync[1]));

  if (arg->tid == TIMETHREAD) {
    stopTimer(&(arg->timers[0]));
  }

  arg->countInserts = count / threads;

  aggregate_thread_stats();
  
  pthread_exit(NULL);
}

void *test_insert_asc_time(void *argument) {
  ThreadsArgument *arg = (ThreadsArgument *)argument;
  DataStructure *priorityq = arg->priorityq;
  unsigned int percent_op = arg->percent_op;
  unsigned int percent_ex = arg->percent_ex;
  unsigned int key;
  unsigned int ms = arg->target_ms;
  unsigned int threads = arg->num_threads;

#if defined(CCSYNCH)
  CCPriorityQThreadState_s state;
#else
  int state;
#endif

  init_thread_state(arg->tid, priorityq, &state);
  assert(*(arg->control) == 1);

  pthread_barrier_wait(&(arg->sync[0]));

  if (arg->tid == TIMETHREAD) {
    startTimer(&(arg->timers[0]));
  }

  pthread_barrier_wait(&(arg->sync[1]));

  int cnt = 0;
  unsigned long long countInserts = 0;
  while (*(arg->control)) {
    if (((++cnt) % OPS_BETWEEN_TIME_CHECKS) == 0) {
      if (getCurrentElapsedMilliseconds(&(arg->timers[0])) > ms) {
        *(arg->control) = 0;
        break;
      }
    }
    key = arg->tid + countInserts * threads;
    pq_insert(priorityq, key, arg->tid);
    countInserts += 1;
  }

  pthread_barrier_wait(&(arg->sync[2]));

  if (arg->tid == TIMETHREAD) {
    stopTimer(&(arg->timers[0]));
  }

  arg->countInserts = countInserts;

  aggregate_thread_stats();

  pthread_exit(NULL);
}

void *test_insert_random_time(void *argument) {
  ThreadsArgument *arg = (ThreadsArgument *)argument;
  DataStructure *priorityq = arg->priorityq;
  unsigned int percent_op = arg->percent_op;
  unsigned int percent_ex = arg->percent_ex;
  unsigned int key;
  unsigned int i = 0;
  int cnt = 0;
  unsigned long long countInserts = 0, countDeletes = 0;
  unsigned int ms = arg->target_ms;

#if defined(CCSYNCH)
  CCPriorityQThreadState_s state;
#else
  int state;
#endif

  init_thread_state(arg->tid, priorityq, &state);
  assert(*(arg->control) == 1);

  pthread_barrier_wait(&(arg->sync[0]));

  if (arg->tid == TIMETHREAD) {
    startTimer(&(arg->timers[0]));
  }

  pthread_barrier_wait(&(arg->sync[1]));

  cnt = 0;
  countInserts = 0;
  while (*(arg->control)) {
    if (((++cnt) % OPS_BETWEEN_TIME_CHECKS) == 0) {
      if (getCurrentElapsedMilliseconds(&(arg->timers[0])) > ms) {
        *(arg->control) = 0;
        break;
      }
    }
    key = synchFastRandomHugeRange32(2, (~2UL));
    pq_insert(priorityq, key, arg->tid);
    countInserts += 1;
  }

  pthread_barrier_wait(&(arg->sync[2]));

  if (arg->tid == TIMETHREAD) {
    stopTimer(&(arg->timers[0]));
  }

  arg->countInserts = countInserts;

  aggregate_thread_stats();

  pthread_exit(NULL);
}
