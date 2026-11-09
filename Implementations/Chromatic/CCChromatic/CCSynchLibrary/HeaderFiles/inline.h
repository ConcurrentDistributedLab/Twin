#ifndef INLINE
#define INLINE

#include "defines.h"
#include "uthreads.h"


void synchFiberYield(void);

/*
inline void *_SWAP(void *A, void *B);
inline bool _CASPTR(void *A, void *B, void *C);
inline void synchResched(void);
inline void synchFreeMemory(void *ptr, size_t size);
inline uint64_t synchGetMachineModel(void);
inline void *synchGetAlignedMemory(size_t align, size_t size);
*/

bool __noop_resched = false;
bool __uthread_sched = false;

inline bool _CASPTR(void *A, void *B, void *C) {
    return __CASPTR(A, B, C);
}

inline void synchResched(void) {
    if (__noop_resched) {
        synchPause();
    } else if (__uthread_sched) {
        synchFiberYield();
    } else {
        sched_yield();
    }
}


inline void synchFreeMemory(void *ptr, size_t size) {
#ifdef SYNCH_NUMA_SUPPORT
    numa_free(ptr, size);
#else
    free(ptr);
#endif
}


inline uint64_t synchGetMachineModel(void) {
#if defined(__amd64__) || defined(__x86_64__)
    char cpu_model[MAX_VENDOR_STR_SIZE] = {'\0'};

    asm volatile("movl $0, %%eax\n"
                 "cpuid\n"
                 "movl %%ebx, %0\n"
                 "movl %%edx, %1\n"
                 "movl %%ecx, %2\n"
                 : "=m"(cpu_model[0]), "=m"(cpu_model[4]), "=m"(cpu_model[8])
                 :: "%eax", "%ebx", "%edx", "%ecx", "memory");
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Machine model: %s\n", cpu_model);
#endif

    if (strcmp(cpu_model, "AuthenticAMD") == 0)
        return AMD_X86_MACHINE;
    else if (strcmp(cpu_model, "GenuineIntel") == 0)
        return INTEL_X86_MACHINE;
    else
        return X86_GENERIC_MACHINE;
#elif defined(__aarch64__)
    return ARM_GENERIC_MACHINE;
#elif defined(__riscv__) || defined(__riscv)
    return RISCV_GENERIC_MACHINE;
#else
    return UNKNOWN_MACHINE;
#endif
}


inline void *synchGetAlignedMemory(size_t align, size_t size) {
    void *p;

#ifdef SYNCH_NUMA_SUPPORT
    p = (void *)  numa_alloc_local(size + align);
    long plong = (long)p;
    plong += align;
    plong &= ~(align - 1);
    p = (void *)plong;
#else
    p = (void *)memalign(align, size);
#endif

    if (p == NULL) {
        perror("memory allocation fail");
        exit(EXIT_FAILURE);
    } else
        return p;
}


#define synchSWAP(A, B) _SWAP((void *)(A), (void *)(B))
#define synchCASPTR(A, B, C) _CASPTR((void *)(A), (void *)(B), (void *)(C))

#endif
