#ifndef TEST_GLOBALS_H
#define TEST_GLOBALS_H

#define FAO __sync_fetch_and_or
#define FAA __sync_fetch_and_add

#ifdef COMPUTE_STATS
extern __thread unsigned long long numAlloc;
extern __thread unsigned long long sizeAlloc;
extern __thread unsigned long long numDelRetries;
extern __thread unsigned long long numInsRetries;
extern __thread unsigned long long numIns;
extern __thread unsigned long long totalIns;
extern __thread unsigned long long totalReb;
extern __thread unsigned long long totalDel;

extern __thread unsigned long long locate_calls;

#ifdef COMPUTE_AVG
extern __thread unsigned long long pointers_traversed;
extern __thread unsigned long long pointers_ins_traversed;
extern __thread unsigned long long pointers_reb_traversed;
extern __thread unsigned long long external_pointers_traversed;
#endif

#ifdef COMPUTE_COMBINER
extern __thread unsigned long long combiner_changes;
#endif

#endif // COMPUTE_STATS

#ifdef COMPUTE_HEIGHT
extern unsigned int global_tree_height_min;
extern unsigned int global_tree_height_max;
#endif

#ifdef COMPUTE_ATOMIC
extern __thread unsigned long long atomic_count;
extern __thread unsigned long long external_atomic_count;
#endif

#ifdef COMPUTE_HW_EVENTS
void init_hw_events_measurement(void);
void start_hw_events_measurement(void);
void stop_hw_events_measurement(void);
#endif

extern unsigned int op_count;
extern unsigned int less_op;
extern unsigned int op_between_check;

// Global configuration
extern unsigned int availableCores;
extern unsigned int numberOfThreads;
extern unsigned int maxThreads;

extern unsigned int ms_main;
extern unsigned int ms_secondary;

void aggregate_thread_stats(void);
void printStatistics(void);
void reset_all_global_stats(void);

#endif