#define _GNU_SOURCE

#include <pthread.h>
#include <sched.h>
#include <stdio.h>

#include "pinning.h"

/*
 * Pin the calling thread to a CPU.
 * With PIN_TITAN: map tid onto the 96-core Titan NUMA layout (4 nodes, HT).
 * Otherwise: round-robin over [0, max_cores).
 */
void threadPin(int tid, int max_cores) {
#ifdef PIN_TITAN
    int add = 0;
    if (tid % 2 == 1) add = 24;
    if (tid > 47) {
        tid -= 48;
        add += 48;
    }
    tid /= 2;

    int cpu_id = (tid % 2) * 12 + (tid / 2) + add;

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_id, &mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        perror("sched_setaffinity");
    }
#else
    int cpu_id = tid % max_cores;

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_id, &mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        perror("sched_setaffinity");
    }
#endif
}
