#include "../HeaderFiles/ccsynch.h"
#include <stdbool.h>
#include "../HeaderFiles/inline.h"
#include "../../../../../src/core/globals.h"

/* Twin: includes src/core/globals.h for optional COMPUTE_* counters. */
static const int CCSYNCH_HELP_FACTOR = 10;

RetVal CCSynchApplyOp(CCSynchStruct *l, CCSynchThreadState *st_thread, RetVal (*sfunc)(void *, ArgVal, int), void *state, ArgVal arg, int pid) {
    volatile CCSynchNode *p;
    volatile CCSynchNode *cur;
    CCSynchNode *next_node, *tmp_next;
    int help_bound = CCSYNCH_HELP_FACTOR * l->nthreads;
    int counter = 0;

    next_node = st_thread->next;
    next_node->next = NULL;
    next_node->locked = true;
    next_node->completed = false;

    //cur = (CCSynchNode *)synchSWAP(&l->Tail, next_node);
#if defined(_EMULATE_SWAP_)
#    warning synchSWAP instructions are simulated!
    void *old_val;
    void *new_val;

    while (true) {
        old_val = (void *)*((volatile long *)&l->Tail);
        new_val = next_node;
        if (((void *)*((volatile long *)&l->Tail)) == old_val && synchCASPTR(&l->Tail, old_val, new_val) == true) break;
    }
    cur = (CCSynchNode *) old_val;
#else
#ifdef COMPUTE_ATOMIC
  external_atomic_count += 1;
#endif
    cur = (CCSynchNode *)__SWAP(&l->Tail, next_node);
#endif
    
    cur->arg_ret = arg;
    cur->pid = pid;
    cur->next = (CCSynchNode *)next_node;
    st_thread->next = (CCSynchNode *)cur;

    while (cur->locked) { // spinning
        synchResched();
    }
    if (cur->completed) // I have been helped
        return cur->arg_ret;
#ifdef DEBUG
    l->rounds++;
#endif
    p = cur; // I have not been helped

#ifdef COMPUTE_LATENCY
    uint64_t start_tsc = 0;
    int ops_served = 0;
    start_tsc = read_tsc();
#endif

    while (p->next != NULL && counter < help_bound) {
        synchStorePrefetch(p->next);
        counter++;
#ifdef DEBUG
        l->counter++;
#endif
        tmp_next = p->next;
        p->arg_ret = sfunc(state, p->arg_ret, pid);
        synchNonTSOFence();
        p->completed = true;
        synchNonTSOFence();
        p->locked = false;
        p = tmp_next;

#ifdef COMPUTE_LATENCY
        ops_served+=1;
#endif
#ifdef COMPUTE_AVG
        external_pointers_traversed += 1;
#endif	
    }

#ifdef COMPUTE_LATENCY
    if (ops_served > 0) {
        uint64_t end_tsc = read_tsc();
        t_cycles += (end_tsc - start_tsc);
        t_ops += ops_served;
    }
#endif

    synchNonTSOFence();
    p->locked = false; // Unlock the next one
    synchStoreFence();

#ifdef COMPUTE_COMBINER
  combiner_changes += 1;
#endif

    return cur->arg_ret;
}

RetVal CCSynchApply2Op(CCSynchStruct *l, CCSynchThreadState *st_thread, \
        RetVal (*sfunc1)(void *, ArgVal, int), void *state1, \
        void (*sfunc2)(void *, ArgVal, int), void *state2, ArgVal arg, int pid) {

    volatile CCSynchNode *p;
    volatile CCSynchNode *cur;
    CCSynchNode *next_node, *tmp_next;
    int help_bound = CCSYNCH_HELP_FACTOR * l->nthreads;
    int counter = 0;

    next_node = st_thread->next;
    next_node->next = NULL;
    next_node->locked = true;
    next_node->completed = false;

    //cur = (CCSynchNode *)synchSWAP(&l->Tail, next_node);
#if defined(_EMULATE_SWAP_)
#    warning synchSWAP instructions are simulated!
    void *old_val;
    void *new_val;

    while (true) {
        old_val = (void *)*((volatile long *)&l->Tail);
        new_val = next_node;
        if (((void *)*((volatile long *)&l->Tail)) == old_val && synchCASPTR(&l->Tail, old_val, new_val) == true) break;
    }
    cur = (CCSynchNode *) old_val;
#else
#ifdef COMPUTE_ATOMIC
  external_atomic_count += 1;
#endif
    cur = (CCSynchNode *)__SWAP(&l->Tail, next_node);
#endif
    
    cur->arg_ret = arg;
    cur->pid = pid;
    cur->next = (CCSynchNode *)next_node;
    st_thread->next = (CCSynchNode *)cur;

    while (cur->locked) { // spinning
        synchResched();
    }
    if (cur->completed) // I have been helped
        return cur->arg_ret;
#ifdef DEBUG
    l->rounds++;
#endif
    p = cur; // I have not been helped

#ifdef COMPUTE_LATENCY
    uint64_t start_tsc = 0;
    int ops_served = 0;
    start_tsc = read_tsc();
#endif

    while (p->next != NULL && counter < help_bound) {
        synchStorePrefetch(p->next);
        counter++;
#ifdef DEBUG
        l->counter++;
#endif
        tmp_next = p->next;
        p->arg_ret = sfunc1(state1, p->arg_ret, pid);
        synchNonTSOFence();
        p->completed = true;
        synchNonTSOFence();
        p->locked = false;
        p = tmp_next;

#ifdef COMPUTE_LATENCY
        ops_served += 1;
#endif
#ifdef COMPUTE_AVG
        external_pointers_traversed += 1;
#endif	
    }
    sfunc2(state2, arg, pid);

#ifdef COMPUTE_LATENCY
    if (ops_served > 0) {
        uint64_t end_tsc = read_tsc();
        t_cycles += (end_tsc - start_tsc);
        t_ops += ops_served;
    }
#endif
    synchNonTSOFence();
    p->locked = false; // Unlock the next one
    synchStoreFence();

#ifdef COMPUTE_COMBINER
  combiner_changes += 1;
#endif

    return cur->arg_ret;
}

void CCSynchStructInit(CCSynchStruct *l, uint32_t nthreads) {
    l->nthreads = nthreads;

    if (synchGetMachineModel() == INTEL_X86_MACHINE) {
        l->nodes = NULL;
        l->Tail = (CCSynchNode*) synchGetAlignedMemory(CACHE_LINE_SIZE, sizeof(CCSynchNode));
    } else {
        l->nodes = (CCSynchNode*) synchGetAlignedMemory(CACHE_LINE_SIZE, (nthreads + 1) * sizeof(CCSynchNode));
        l->Tail = &l->nodes[nthreads];
    }

#ifdef DEBUG
    l->rounds = l->counter = 0;
#endif

    l->Tail->next = NULL;
    l->Tail->locked = false;
    l->Tail->completed = false;

    synchStoreFence();
}

void CCSynchThreadStateInit(CCSynchStruct *l, CCSynchThreadState *st_thread, int pid) {
    if (synchGetMachineModel() == INTEL_X86_MACHINE) {
        st_thread->next = (CCSynchNode*) synchGetAlignedMemory(CACHE_LINE_SIZE, sizeof(CCSynchStruct));
    } else {
        st_thread->next = &l->nodes[pid];
    }
}
