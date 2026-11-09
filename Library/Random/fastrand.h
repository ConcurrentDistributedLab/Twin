/* Fast integer PRNGs for the benchmark harness (low overhead, per-thread state). */
#ifndef _FASTRAND_H_
#define _FASTRAND_H_

#include <stdint.h>

/* Max value returned by synchFastRandom / synchFastRandomRange. */
#define SYNCH_RAND_MAX 32768

/* POSIX.1-2001-style generator; returns value in {0, ..., SYNCH_RAND_MAX}. */
long synchFastRandom(void);

/* Marsaglia multiply-with-carry 32-bit generator. */
uint32_t synchFastRandom32(void);

void synchFastRandomSetSeed(uint32_t seed);

/* Range helpers. For synchFastRandomRange, high must be <= SYNCH_RAND_MAX and low < high. */
long synchFastRandomRange(long low, long high);
uint32_t synchFastRandomRange32(uint32_t low, uint32_t high);
uint32_t synchFastRandomHugeRange32(uint32_t low, uint32_t high);

#endif
