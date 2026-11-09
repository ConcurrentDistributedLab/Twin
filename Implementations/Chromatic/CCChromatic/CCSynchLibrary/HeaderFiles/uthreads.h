/// @file uthreads.h
/// @brief This file exposes the API by the Synch framework for creating fibers inside a posix thread.
#undef _FORTIFY_SOURCE

#ifndef _UTHREADS_H_
#define _UTHREADS_H_

#include <stdlib.h>
#include <ucontext.h>
#include <setjmp.h>
#include <stdbool.h>

#include "config.h"

#ifdef SYNCH_NUMA_SUPPORT
#    include <numa.h>
#endif

void synchFiberYield(void);
#endif
