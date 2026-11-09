#include "globals.h"
#include <stdio.h>
#include <stdint.h>

#ifdef COMPUTE_STATS
#pragma message("\x1B[33mStatistics are open.\x1B[0m")
__thread unsigned long long numAlloc = 0;
__thread unsigned long long sizeAlloc = 0;
__thread unsigned long long numDelRetries = 0;
__thread unsigned long long numInsRetries = 0;
__thread unsigned long long numIns = 0;
__thread unsigned long long totalIns = 0;
__thread unsigned long long totalReb = 0;
__thread unsigned long long totalDel = 0;

__thread unsigned long long locate_calls = 0;

static unsigned long long global_numAlloc = 0;
static unsigned long long global_sizeAlloc = 0;
static unsigned long long global_numDelRetries = 0;
static unsigned long long global_numInsRetries = 0;
static unsigned long long global_numIns = 0;
static unsigned long long global_totalIns = 0;
static unsigned long long global_totalReb = 0;
static unsigned long long global_totalDel = 0;
static unsigned long long global_locate_calls = 0;

#ifdef COMPUTE_AVG
#pragma message("\x1B[33mAverage number of pointers traversed per type operation.\x1B[0m")
__thread unsigned long long  pointers_traversed = 0;
__thread unsigned long long  pointers_ins_traversed = 0;
__thread unsigned long long  pointers_reb_traversed = 0;
__thread unsigned long long  external_pointers_traversed = 0;
static unsigned long long global_pointers_del_traversed = 0;
static unsigned long long  global_pointers_ins_traversed = 0;
static unsigned long long  global_pointers_reb_traversed = 0;
static unsigned long long  global_external_pointers_traversed = 0;
#endif

#ifdef COMPUTE_COMBINER
#pragma message("\x1B[33mCombiner changes calculation is open.\x1B[0m")
__thread unsigned long long combiner_changes = 0;
static unsigned long long global_combiner_changes = 0;
#endif

#endif // COMPUTE_STATS

#ifdef COMPUTE_HEIGHT
#pragma message("\x1B[33mTree height calculation is open.\x1B[0m")
unsigned int global_tree_height_min = 0;
unsigned int global_tree_height_max = 0;
#endif

#ifdef COMPUTE_ATOMIC
#pragma message("\x1B[33mAtomic operations calculation is open.\x1B[0m")
__thread unsigned long long atomic_count = 0;
__thread unsigned long long external_atomic_count = 0;
static unsigned long long global_atomic_count = 0;
static unsigned long long global_external_atomic_count = 0;
#endif

#ifdef COMPUTE_HW_EVENTS
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <unistd.h>
#include <string.h>

#pragma message("\x1B[33mHardware and Performance Events calculation is open.\x1B[0m")

// Global variables to store results
unsigned long long global_task_clock = 0;
unsigned long long global_context_switches = 0;
unsigned long long global_cpu_migrations = 0;
unsigned long long global_page_faults = 0;
unsigned long long global_major_faults = 0;
unsigned long long global_minor_faults = 0;
unsigned long long global_cycles = 0;
unsigned long long global_instructions = 0;
unsigned long long global_branches = 0;
unsigned long long global_branch_misses = 0;
unsigned long long global_l1i_misses = 0;
unsigned long long global_l1d_misses = 0;
unsigned long long global_l2_misses = 0;
unsigned long long global_llc_loads = 0;
unsigned long long global_llc_misses = 0;
unsigned long long global_itlb_misses = 0;
unsigned long long global_dtlb_load_misses = 0;
unsigned long long global_dtlb_store_misses = 0;
unsigned long long global_machine_clears_memory_ordering = 0;
unsigned long long global_stalls_total = 0;
unsigned long long global_stalls_mem_any = 0;

// File descriptors
static int fd_task_clock = -1;
static int fd_context_switches = -1;
static int fd_cpu_migrations = -1;
static int fd_page_faults = -1;
static int fd_major_faults = -1;
static int fd_minor_faults = -1;
static int fd_cycles = -1;
static int fd_instructions = -1;
static int fd_branches = -1;
static int fd_branch_misses = -1;
static int fd_l1i = -1;
static int fd_l1d = -1;
static int fd_l2 = -1;
static int fd_llc_loads = -1;
static int fd_llc_misses = -1;
static int fd_itlb = -1;
static int fd_dtlb_load = -1;
static int fd_dtlb_store = -1;
static int fd_machine_clears_memory_ordering = -1;
static int fd_stalls_total = -1;
static int fd_stalls_mem_any = -1;

static long perf_event_open_wrapper(struct perf_event_attr *hw_event, pid_t pid,
                                    int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

// Helper to open a performance event and handle formatting for multiplexing
static int open_perf_event(uint32_t type, uint64_t config) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.size = sizeof(struct perf_event_attr);
    pe.type = type;
    pe.config = config;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_guest = 1;
    pe.exclude_hv = 1;
    pe.inherit = 1;
    // VERY IMPORTANT for Multiplexing: Measure active time vs enabled time
    pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    return perf_event_open_wrapper(&pe, 0, -1, -1, 0);
}

void init_hw_events_measurement(void) {
    // Software Events
    fd_task_clock = open_perf_event(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK);
    fd_context_switches = open_perf_event(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES);
    fd_cpu_migrations = open_perf_event(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS);
    fd_page_faults = open_perf_event(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS);
    fd_major_faults = open_perf_event(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ);
    fd_minor_faults = open_perf_event(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN);

    // Hardware Events
    fd_cycles = open_perf_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
    fd_instructions = open_perf_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
    fd_branches = open_perf_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS);
    fd_branch_misses = open_perf_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES);

    // Hardware Cache Events
    fd_l1i = open_perf_event(PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)));
    fd_l1d = open_perf_event(PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)));
    fd_l2 = open_perf_event(PERF_TYPE_RAW, 0x1ff1); // Raw L2
    fd_llc_loads = open_perf_event(PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16)));
    fd_llc_misses = open_perf_event(PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)));
    fd_itlb = open_perf_event(PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_ITLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)));
    fd_dtlb_load = open_perf_event(PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)));
    fd_dtlb_store = open_perf_event(PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)));
    fd_machine_clears_memory_ordering = open_perf_event(PERF_TYPE_RAW, 0x2c3);

    fd_stalls_total = open_perf_event(PERF_TYPE_RAW, 0x40004a3); // CYCLE_ACTIVITY.STALLS_TOTAL (Event=0xA3, UMask=0x04, CMask=4) -> 0x040004A3
    fd_stalls_mem_any = open_perf_event(PERF_TYPE_RAW, 0x140014a3); // CYCLE_ACTIVITY.STALLS_MEM_ANY (Skylake/KabyLake+: Event=0xA3, UMask=0x14, CMask=20) -> 0x140014A3
}

// Start Helper Macro
#define START_EVENT(fd) do { if ((fd) != -1) { ioctl((fd), PERF_EVENT_IOC_RESET, 0); ioctl((fd), PERF_EVENT_IOC_ENABLE, 0); } } while(0)

void start_hw_events_measurement(void) {
    START_EVENT(fd_task_clock); START_EVENT(fd_context_switches); START_EVENT(fd_cpu_migrations);
    START_EVENT(fd_page_faults); START_EVENT(fd_major_faults); START_EVENT(fd_minor_faults);
    START_EVENT(fd_cycles); START_EVENT(fd_instructions); START_EVENT(fd_branches); START_EVENT(fd_branch_misses);
    START_EVENT(fd_l1i); START_EVENT(fd_l1d); START_EVENT(fd_l2); START_EVENT(fd_llc_loads); START_EVENT(fd_llc_misses);
    START_EVENT(fd_itlb); START_EVENT(fd_dtlb_load); START_EVENT(fd_dtlb_store);
    START_EVENT(fd_machine_clears_memory_ordering);
    START_EVENT(fd_stalls_total); START_EVENT(fd_stalls_mem_any);
}

// Stop and Scale Helper Function
static void stop_and_read_event(int *fd, unsigned long long *val) {
    if (*fd != -1) {
        ioctl(*fd, PERF_EVENT_IOC_DISABLE, 0);
        struct {
            uint64_t value;
            uint64_t time_enabled;
            uint64_t time_running;
        } res;
        
        if (read(*fd, &res, sizeof(res)) == sizeof(res)) {
            if (res.time_running > 0 && res.time_enabled > 0) {
                // Scaling fixes Multiplexing drops
                *val = (unsigned long long)((double)res.value * ((double)res.time_enabled / (double)res.time_running));
            } else {
                *val = res.value;
            }
        }
        close(*fd);
        *fd = -1;
    }
}

void stop_hw_events_measurement(void) {
    stop_and_read_event(&fd_task_clock, &global_task_clock);
    stop_and_read_event(&fd_context_switches, &global_context_switches);
    stop_and_read_event(&fd_cpu_migrations, &global_cpu_migrations);
    stop_and_read_event(&fd_page_faults, &global_page_faults);
    stop_and_read_event(&fd_major_faults, &global_major_faults);
    stop_and_read_event(&fd_minor_faults, &global_minor_faults);
    stop_and_read_event(&fd_cycles, &global_cycles);
    stop_and_read_event(&fd_instructions, &global_instructions);
    stop_and_read_event(&fd_branches, &global_branches);
    stop_and_read_event(&fd_branch_misses, &global_branch_misses);
    stop_and_read_event(&fd_l1i, &global_l1i_misses);
    stop_and_read_event(&fd_l1d, &global_l1d_misses);
    stop_and_read_event(&fd_l2, &global_l2_misses);
    stop_and_read_event(&fd_llc_loads, &global_llc_loads);
    stop_and_read_event(&fd_llc_misses, &global_llc_misses);
    stop_and_read_event(&fd_itlb, &global_itlb_misses);
    stop_and_read_event(&fd_dtlb_load, &global_dtlb_load_misses);
    stop_and_read_event(&fd_dtlb_store, &global_dtlb_store_misses);
    stop_and_read_event(&fd_machine_clears_memory_ordering, &global_machine_clears_memory_ordering);
    stop_and_read_event(&fd_stalls_total, &global_stalls_total);
    stop_and_read_event(&fd_stalls_mem_any, &global_stalls_mem_any);
}
#endif

unsigned int availableCores = 4;
unsigned int numberOfThreads = 10;
unsigned int maxThreads = 10;

unsigned int op_count = 32768;
unsigned int less_op = 32;
unsigned int op_between_check = 500;

unsigned int ms_main = 1000;
unsigned int ms_secondary = 1000;

void aggregate_thread_stats(void) {
#ifdef COMPUTE_STATS
    FAA(&global_numAlloc, numAlloc);
    FAA(&global_sizeAlloc, sizeAlloc);
    FAA(&global_numDelRetries, numDelRetries);
    FAA(&global_numInsRetries, numInsRetries);
    FAA(&global_numIns, numIns);
    FAA(&global_totalIns, totalIns);
    FAA(&global_totalReb, totalReb);
    FAA(&global_totalDel, totalDel);
    
    FAA(&global_locate_calls, locate_calls);

#ifdef COMPUTE_AVG
    FAA(&global_pointers_del_traversed, pointers_traversed);
    FAA(&global_pointers_ins_traversed, pointers_ins_traversed);
    FAA(&global_pointers_reb_traversed, pointers_reb_traversed);
    FAA(&global_external_pointers_traversed, external_pointers_traversed);
#endif

#ifdef COMPUTE_COMBINER
    FAA(&global_combiner_changes, combiner_changes);
#endif

#endif // COMPUTE_STATS

#ifdef COMPUTE_ATOMIC
    FAA(&global_atomic_count, atomic_count);
    FAA(&global_external_atomic_count, external_atomic_count);
#endif
}

void reset_all_global_stats(void) {
#ifdef COMPUTE_STATS
    global_numAlloc = 0;
    global_sizeAlloc = 0;
    global_numDelRetries = 0;
    global_numInsRetries = 0;
    global_numIns = 0;
    global_totalIns = 0;
    global_totalReb = 0;
    global_totalDel = 0;
    
    global_locate_calls = 0;

#ifdef COMPUTE_AVG
    global_pointers_del_traversed = 0;
    global_pointers_ins_traversed = 0;
    global_pointers_reb_traversed = 0;
    global_external_pointers_traversed = 0;
#endif

#ifdef COMPUTE_COMBINER
    global_combiner_changes = 0;
#endif

#endif // COMPUTE_STATS

#ifdef COMPUTE_ATOMIC
    global_atomic_count = 0;
    global_external_atomic_count = 0;
#endif

#ifdef COMPUTE_HW_EVENTS
    global_task_clock = 0;
    global_context_switches = 0;
    global_cpu_migrations = 0;
    global_page_faults = 0;
    global_major_faults = 0;
    global_minor_faults = 0;
    global_cycles = 0;
    global_instructions = 0;
    global_branches = 0;
    global_branch_misses = 0;
    global_l1i_misses = 0;
    global_l1d_misses = 0;
    global_l2_misses = 0;
    global_llc_loads = 0;
    global_llc_misses = 0;
    global_itlb_misses = 0;
    global_dtlb_load_misses = 0;
    global_dtlb_store_misses = 0;
    global_machine_clears_memory_ordering = 0;
    global_stalls_total = 0;
    global_stalls_mem_any = 0;
#endif
}

void printStatistics(void) {
#ifdef COMPUTE_STATS
  printf("\n--- Statistics ---\n");
  printf("Total allocations: %llu\n", global_numAlloc);
  printf("Total allocated size: %llu bytes\n", global_sizeAlloc);
  printf("Total delete retries: %llu\n", global_numDelRetries);
  printf("Total insert retries: %llu\n", global_numInsRetries);
  printf("Total Rebalances: %llu\n", global_totalReb);
  printf("Total Inserts: %llu\n", global_totalIns);
  printf("Total Deletes: %llu\n", global_totalDel);
  printf("Total Locates: %llu\n", global_locate_calls);

#ifdef COMPUTE_AVG
    printf("Pointers Delete traversed: %llu\n", global_pointers_del_traversed);
    printf("Pointers Insert traversed: %llu\n", global_pointers_ins_traversed);
    printf("Pointers Rebalance traversed: %llu\n", global_pointers_reb_traversed);
    printf("External Pointers traversed: %llu\n", global_external_pointers_traversed);
#endif

#ifdef COMPUTE_COMBINER
    printf("Number of Combiner Changes: %llu\n", global_combiner_changes);
#endif

#endif // COMPUTE_STATS

#ifdef COMPUTE_HEIGHT
  printf("Tree height Min: %u\n", global_tree_height_min);
  printf("Tree height Max: %u\n", global_tree_height_max);
#endif

#ifdef COMPUTE_ATOMIC
    printf("Number of Atomic Operations: %llu\n", global_atomic_count);
    printf("Number of External Atomic Operations: %llu\n", global_external_atomic_count);
#endif

#ifdef COMPUTE_HW_EVENTS
    printf("\n--- Hardware Events ---\n");
    printf("Task Clock (ns): %llu\n", global_task_clock);
    printf("Context Switches: %llu\n", global_context_switches);
    printf("CPU Migrations: %llu\n", global_cpu_migrations);
    printf("Page Faults: %llu\n", global_page_faults);
    printf("Major Faults: %llu\n", global_major_faults);
    printf("Minor Faults: %llu\n", global_minor_faults);
    printf("Cycles: %llu\n", global_cycles);
    printf("Instructions: %llu\n", global_instructions);
    printf("Branches: %llu\n", global_branches);
    printf("Branch Misses: %llu\n", global_branch_misses);
    printf("L1I Cache Misses: %llu\n", global_l1i_misses);
    printf("L1D Cache Misses: %llu\n", global_l1d_misses);
    printf("L2 Cache Misses (Raw): %llu\n", global_l2_misses);
    printf("LLC Loads: %llu\n", global_llc_loads);
    printf("LLC Load Misses: %llu\n", global_llc_misses);
    printf("iTLB Load Misses: %llu\n", global_itlb_misses);
    printf("dTLB Load Misses: %llu\n", global_dtlb_load_misses);
    printf("dTLB Store Misses: %llu\n", global_dtlb_store_misses);
    printf("Machine Clears (Memory Ordering): %llu\n", global_machine_clears_memory_ordering);
    printf("Cycle Activity Stalls Total: %llu\n", global_stalls_total);
    printf("Cycle Activity Stalls Mem Any: %llu\n", global_stalls_mem_any);
#endif

  printf("\n--- Thread/System Info ---\n");
  printf("Available CPU cores: %u\n", availableCores);
  printf("Number of threads: %u\n", numberOfThreads);
  printf("Number of Operations: %u\n", op_count);
  printf("Number of Less Operations: %u\n", less_op);
  printf("Number of Operations Between Check in Time: %u\n", op_between_check);
}