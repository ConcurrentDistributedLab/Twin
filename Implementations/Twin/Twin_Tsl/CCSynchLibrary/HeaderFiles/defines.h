#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <malloc.h>
#include <sched.h>

#ifdef SYNCH_NUMA_SUPPORT
#    include <numa.h>
#endif

#define MAX_VENDOR_STR_SIZE 64

/// @brief The vendor of the processor is unknown.
#define UNKNOWN_MACHINE       0x0
/// @brief The vendor is an AMD x86 processor.
#define AMD_X86_MACHINE       0x1
/// @brief The vendor is an Intel X86 processor.
#define INTEL_X86_MACHINE     0x2
/// @brief This is a generic X86 processor.
#define X86_GENERIC_MACHINE   0x3
/// @brief This is a generic ARM processor.
#define ARM_GENERIC_MACHINE   0x4
/// @brief This is a generic RISC-V processor.
#define RISCV_GENERIC_MACHINE 0x5

#if defined(__GNUC__) && (__GNUC__ * 10000 + __GNUC_MINOR__ * 100) >= 40100
#    define __CASPTR(A, B, C)           __sync_bool_compare_and_swap((long *)A, (long)B, (long)C)
#    define __SWAP(A, B)                __sync_lock_test_and_set((long *)A, (long)B)
#    define synchStorePrefetch(A)       __builtin_prefetch((const void *)A, 1, 3);
#    if defined(__amd64__) || defined(__x86_64__)
#        define synchStoreFence() asm volatile("sfence" ::: "memory")
#        define synchNonTSOFence()
#    else
#        define synchStoreFence()  __sync_synchronize()
#        define synchNonTSOFence() __sync_synchronize()
#    endif
#elif defined(__GNUC__) && (defined(__amd64__) || defined(__x86_64__))
#    define synchStoreFence()     asm volatile("sfence" ::: "memory")
#    define synchStorePrefetch(A) asm volatile("prefetchnta %0" ::"m"(*((const int *)A)))
#    define synchNonTSOFence()
#else
#    error Current machine architecture and compiler are not supported yet!
#endif

#if defined(__GNUC__) && (defined(__amd64__) || defined(__x86_64__))
/// @brief This macro emits a X86 pause instruction.
#    define synchPause()                                                                                                                                                                               \
        {                                                                                                                                                                                              \
            int __i;                                                                                                                                                                                   \
            for (__i = 0; __i < 16; __i++) {                                                                                                                                                           \
                asm volatile("pause");                                                                                                                                                                 \
                asm volatile("pause");                                                                                                                                                                 \
                asm volatile("pause");                                                                                                                                                                 \
                asm volatile("pause");                                                                                                                                                                 \
            }                                                                                                                                                                                          \
        }
#else
/// @brief This macro emits a NO-OP instruction.
#    define synchPause()
#endif
