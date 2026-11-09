#include "delete_tests.h"

#include "../../core/priorityq.h"
#include "../../../Library/Timer/timer.h"
#include "../../../Library/Random/fastrand.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

void *test_delete(void *argument) {
  ThreadsArgument *arg = (ThreadsArgument *)argument;
  DataStructure *priorityq = arg->priorityq;
  unsigned int percent_ex = arg->percent_ex;
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
    random_delete(priorityq, arg->tid, &state, percent_ex);
  }

  pthread_barrier_wait(&(arg->sync[1]));

  if (arg->tid == TIMETHREAD) {
    stopTimer(&(arg->timers[0]));
  }

  arg->countDeletes = count / threads;

  aggregate_thread_stats();

  pthread_exit(NULL);
}
