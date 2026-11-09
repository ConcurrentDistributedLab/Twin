#include "../HeaderFiles/uthreads.h"

#define FIBER_STACK 65536

typedef struct Fiber {
    ucontext_t context; /* Stores the current context */
    jmp_buf jmp;
    bool active;
} Fiber;

typedef struct FiberData {
    void *(*func)(void *);
    jmp_buf *cur;
    ucontext_t *prev;
    long arg;
} FiberData;

static __thread volatile int N_FIBERS = 1;
static __thread Fiber *FIBER_LIST = NULL;
static __thread Fiber *FIBER_RECYCLE = NULL;
static __thread int MAX_FIBERS = 1;
static __thread int currentFiber = 0;

inline static void switch_to_fiber(Fiber *prev, Fiber *cur) {
    if (_setjmp(prev->jmp) == 0) {
        _longjmp(cur->jmp, 1);
    }
}

void synchFiberYield(void) {
    int prev_fiber;

    if (N_FIBERS == 1)
        return;
    // Saved the state so call the next fiber
    prev_fiber = currentFiber;
    do {
        currentFiber = (currentFiber + 1) % MAX_FIBERS;
    } while (FIBER_LIST[currentFiber].active == false);
    switch_to_fiber(&FIBER_LIST[prev_fiber], &FIBER_LIST[currentFiber]);
    if (FIBER_RECYCLE != NULL) {
        //synchFreeMemory(FIBER_RECYCLE, sizeof(Fiber));
#ifdef SYNCH_NUMA_SUPPORT
        numa_free(FIBER_RECYCLE, sizeof(Fiber));
#else
        free(FIBER_RECYCLE);
#endif
        //

        FIBER_RECYCLE = NULL;
    }
}

