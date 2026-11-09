#include "random_tests.h"

#include "../../core/priorityq.h"
#include "../../../Library/Timer/timer.h"
#include "../../../Library/Random/fastrand.h"

#include <stdlib.h>
#include <assert.h>

void *test_random_with_elements_rk_time(void *argument) {
  ThreadsArgument *arg = (ThreadsArgument *)argument;
  DataStructure *priorityq = arg->priorityq;
  unsigned int percent_op = arg->percent_op;
  unsigned int percent_ex = arg->percent_ex;
  unsigned int key;
  unsigned int i = 0;
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

  int cnt = 0;
  int op = 0;
  unsigned long long countInserts = 0;
  while (*(arg->control)) {
    if (((++cnt) % OPS_BETWEEN_TIME_CHECKS) == 0) {
      if (getCurrentElapsedMilliseconds(&(arg->timers[0])) > ms) {
        *(arg->control) = 0;
        break;
      }
    }
    key = synchFastRandomHugeRange32(2, (~2UL));
    random_insert_or_delete(priorityq, arg->tid, &state, key, percent_op, percent_ex);
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
