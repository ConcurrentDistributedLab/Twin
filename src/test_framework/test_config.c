#include "test_config.h"
#include "../../Library/Timer/timer.h"
#include "../core/priorityq.h"
#include "../../Library/Pinning/pinning.h"

#include "tests/insert_tests.h"
#include "tests/delete_tests.h"
#include "tests/random_tests.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

typedef struct {
  void *(*func)(void *);
  void *(*pre)(void *);
} TestConfig;

#define TLB_PAGES 2048
#define PAGE_SIZE 4096
char tlb_dummy_buffer[TLB_PAGES * PAGE_SIZE];

static FILE *safe_fopen(const char *filename, const char *mode);
static void test_barrier_init(pthread_barrier_t *barriers, int barrier_count, int num_threads);
static void test_barrier_cleanup(pthread_barrier_t *barriers,
                                 int barrier_count);
static void* thrash_tlb_worker(void* arg);
static void reset_tlb_all_cores(unsigned int num_threads);
static double* execute_test_case(DataStructure *priorityq, void *(*test_func)(void *),
                              unsigned int percent_op, unsigned int percent_ex, unsigned int target_ops,
                              unsigned int target_ms, unsigned int num_threads);
static void create_results(const TestConfig* config, unsigned int percent_op, unsigned int percent_ex, int numberOfThreads);

static FILE *safe_fopen(const char *filename, const char *mode) {
  FILE *fp = fopen(filename, mode);
  if (!fp) {
    perror("Failed to open file");
    exit(EXIT_FAILURE);
  }
  return fp;
}

static void test_barrier_init(pthread_barrier_t *barriers, int barrier_count, int num_threads) {
  for (int i = 0; i < barrier_count; i++) {
    if (pthread_barrier_init(&barriers[i], NULL, num_threads) != 0) {
      perror("Failed to initialize barrier");
      exit(EXIT_FAILURE);
    }
  }
}

static void test_barrier_cleanup(pthread_barrier_t *barriers,
                                 int barrier_count) {
  for (int i = 0; i < barrier_count; i++) {
    pthread_barrier_destroy(&barriers[i]);
  }
}

void* thrash_tlb_worker(void* arg) {
    int tid = (int)(intptr_t)arg;

    threadPin(tid, availableCores);

    volatile char temp = 0; 
    for (int i = 0; i < TLB_PAGES; i++) {
        temp += tlb_dummy_buffer[i * PAGE_SIZE];
    }

    return NULL;
}

void reset_tlb_all_cores(unsigned int num_threads) {
    if (num_threads == 0) return;

    pthread_t *threads = (pthread_t*) malloc(num_threads * sizeof(pthread_t));

    for (unsigned int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, thrash_tlb_worker, (void*)(intptr_t)i);
    }

    for (unsigned int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
}

static double* execute_test_case(DataStructure *priorityq, void *(*test_func)(void *),
                              unsigned int percent_op, unsigned int percent_ex, unsigned int target_ops,
                              unsigned int target_ms, unsigned int num_threads) {
  /* Barriers: [0] start, [1] post-warmup / end of fixed-op run, [2] end of timed run. */
  int barriers_needed = 3;
  int timers_needed = 1;
  int control = 1;
  
  pthread_barrier_t barriers[barriers_needed];
  pthread_t threads[num_threads];
  Timer timers[timers_needed];

  test_barrier_init(barriers, barriers_needed, num_threads);
  ThreadsArgument contexts[num_threads];

  for (int i = 0; i < num_threads; i++) {
    contexts[i].tid = i + TIMETHREAD;
    contexts[i].priorityq = priorityq;
    contexts[i].timers = timers;
    contexts[i].sync = barriers;
    contexts[i].percent_op = percent_op;
    contexts[i].percent_ex = percent_ex;
    contexts[i].control = &control;
    contexts[i].countInserts = 0;
    contexts[i].countDeletes = 0;
    contexts[i].target_ops = target_ops;
    contexts[i].target_ms = target_ms;
    contexts[i].num_threads = num_threads;
  }

  for (int i = 0; i < num_threads; i++) {
    pthread_create(&threads[i], NULL, test_func, &contexts[i]);
  }

  for (int i = 0; i < num_threads; i++) {
    pthread_join(threads[i], NULL);
  }

  double *results = (double*)malloc(2 * sizeof(double));
  results[0] = 0;
  results[1] = 0;

  for (int i = 0; i < num_threads; i++) {
      results[0] += (contexts[i].countInserts + contexts[i].countDeletes);
  }
  results[1] = elapsedTimeMilliSeconds(&timers[0]);

  test_barrier_cleanup(barriers, barriers_needed);

  return results;
}

static void create_results(const TestConfig* config, unsigned int percent_op, unsigned int percent_ex, int numberOfThreads) {
  double *results;
  double fres_ops = 0;
  double fres_time = 0;
  DataStructure *priorityq;
  
  priorityq = pq_initialize(maxThreads); 

  if (config->pre != NULL) {
      execute_test_case(priorityq, config->pre, percent_op, percent_ex, less_op, ms_secondary, maxThreads);
      reset_all_global_stats();

      SCAN_PQ(priorityq);
      reset_tlb_all_cores(maxThreads);
  }

#ifdef COMPUTE_HW_EVENTS
  init_hw_events_measurement();
  start_hw_events_measurement();
#endif
    
  results = execute_test_case(priorityq, config->func, 
                              percent_op, percent_ex, op_count, ms_main, numberOfThreads);
  fres_ops = results[0];
  fres_time = results[1];
                              
#ifdef COMPUTE_HW_EVENTS
  stop_hw_events_measurement();
#endif

  #ifdef COMPUTE_HEIGHT
    global_tree_height_min = getTreeHeightMin(priorityq);
    global_tree_height_max = getTreeHeightMax(priorityq);
  #endif
  
  printf("[RESULTS_OPS] %lf\n", fres_ops);
  printf("[RESULTS_TIME] %lf\n\n", fres_time);
}

void run_test(Test type, unsigned int percent_op, unsigned int percent_ex, int numberOfThreads) {
  assert(type < NUM_TEST_TYPE && percent_op <= 100 && percent_ex <= 100);
  const TestConfig test_configs[NUM_TEST_TYPE] = {
    /* TEST_INSERT_INCREASE_TIME */
    {test_insert_asc_time, NULL},
    /* TEST_INSERT_RANDOM_TIME */
    {test_insert_random_time, NULL},
    /* TEST_DELETE_RANDOM */
    {test_delete, test_insert_rk},
    /* TEST_RANDOM_WITH_ELEMENTS_RK_TIME */
    {test_random_with_elements_rk_time, test_insert_rk}
  };

  const TestConfig *config = &test_configs[type];
  create_results(config, percent_op, percent_ex, numberOfThreads);
}
