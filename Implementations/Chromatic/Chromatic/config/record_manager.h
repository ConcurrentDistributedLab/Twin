/**
 * C++ record manager implementation (PODC 2015) by Trevor Brown.
 *
 * Copyright (C) 2015 Trevor Brown
 *
 */

#ifndef RECORD_MANAGER_H
#define	RECORD_MANAGER_H

#include <atomic>
#include "globals.h"
#include "errors.h"
#include "record_manager_single_type.h"

#include <iostream>
#include <exception>
#include <stdexcept>
#include <typeinfo>

inline CallbackReturn callbackReturnTrue(CallbackArg arg) {
    return true;
}

// compile time check for duplicate template parameters
// compare first with rest to find any duplicates
template <typename T> void check_duplicates(void) {}
template <typename T, typename First, typename... Rest>
void check_duplicates(void) {
    if (typeid(T) == typeid(First)) {
        throw std::logic_error("duplicate template arguments provided to RecordManagerSet");
    }
    check_duplicates<T, Rest...>();
}
// base case: empty template
// this is a compile time check for invalid arguments
template <typename... Rest>
class RecordManagerSet {
    PAD;
public:
    RecordManagerSet(const int numProcesses) {}
    template <typename T>
    record_manager_single_type<T> * get(T * const recordType) {
        throw std::logic_error("invalid type passed to RecordManagerSet::get()");
        return NULL;
    }
};

// "recursive" case
template <typename First, typename... Rest>
class RecordManagerSet<First, Rest...> : RecordManagerSet<Rest...>{
    PAD;
    record_manager_single_type<First> * const mgr;
	PAD;
public:
    RecordManagerSet(const int numProcesses)
        : RecordManagerSet<Rest...>(numProcesses)
        , mgr(new record_manager_single_type<First>(numProcesses))
        {
        //cout<<"RecordManagerSet with First="<<typeid(First).name()<<" and sizeof...(Rest)="<<sizeof...(Rest)<<std::endl;
        check_duplicates<First, Rest...>(); // check if first is in {rest...}
    }
    ~RecordManagerSet() {
        std::cout<<"recordmanager set destructor started for object type "<<typeid(First).name()<<std::endl;
        delete mgr;
        std::cout<<"recordmanager set destructor finished for object type "<<typeid(First).name()<<std::endl;
        // note: should automatically call the parent class' destructor afterwards
    }
    // note: the compiled code for get() should be a single read and return statement
    template<typename T>
    inline record_manager_single_type<T> * get(T * const recordType) {
        if (typeid(First) == typeid(T)) {
            //cout<<"MATCH: typeid(First)="<<typeid(First).name()<<" typeid(T)="<<typeid(T).name()<<std::endl;
            return (record_manager_single_type<T> *) mgr;
        } else {
            //cout<<"NO MATCH: typeid(First)="<<typeid(First).name()<<" typeid(T)="<<typeid(T).name()<<std::endl;
            return ((RecordManagerSet<Rest...> *) this)->get(recordType);
        }
    }
    // note: recursion should be compiled out
    void clearCounters(void) {
        mgr->clearCounters();
    }
    void registerThread(const int tid) {
        mgr->initThread(tid);
    }
    void unregisterThread(const int tid) {
        mgr->deinitThread(tid);
    }
    void printStatus() {
        mgr->printStatus();
    }
    inline void qUnprotectAll(const int tid) {
        mgr->qUnprotectAll(tid);
    }
};

template <typename First, typename... Rest>
class RecordManagerSetPostPadded : public RecordManagerSet<First, Rest...> {
    PAD;
public:
    RecordManagerSetPostPadded(const int numProcesses)
        : RecordManagerSet<First, Rest...>(numProcesses) {}
};

class padded_bool {
public:
    union {
        bool v;
        char padding[128];
    };
    padded_bool() {
        v = 0;
    }
};

template <typename RecordTypesFirst, typename... RecordTypesRest>
class record_manager {
protected:
    typedef record_manager<RecordTypesFirst,RecordTypesRest...> SelfType;
    PAD;
    RecordManagerSetPostPadded<RecordTypesFirst,RecordTypesRest...> * rmset;

   PAD;
   padded_bool init[MAX_THREADS_POW2];

public:
//    PAD;
    const int NUM_PROCESSES;
    PAD;

    record_manager(const int numProcesses, const int _neutralizeSignal = -1 /* unused except in conjunction with special DEBRA+ memory reclamation */)
            : NUM_PROCESSES(numProcesses)
    {
        rmset = new RecordManagerSetPostPadded<RecordTypesFirst, RecordTypesRest...>(numProcesses);
    }
    ~record_manager() {
    }
    void initThread(const int tid) {
        if (init[tid].v) return; else init[tid].v = !init[tid].v;
    }
    void deinitThread(const int tid) {
        if (!init[tid].v) return; else init[tid].v = !init[tid].v;
    }
    void clearCounters() { }
    void printStatus(void) { }
    template <typename T>
    inline record_manager_single_type<T> * get(T * const recordType) {
        return rmset->get((T *) NULL);
    }

    // for hazard pointers

    template <typename T>
    inline bool isProtected(const int tid, T * const obj) {
        return rmset->get((T *) NULL)->isProtected(tid, obj);
    }

    template <typename T>
    inline bool protect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return rmset->get((T *) NULL)->protect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }

    template <typename T>
    inline void unprotect(const int tid, T * const obj) {
        rmset->get((T *) NULL)->unprotect(tid, obj);
    }

    // for DEBRA+

    // warning: qProtect must be reentrant and lock-free (i.e., async-signal-safe)
    template <typename T>
    inline bool qProtect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return rmset->get((T *) NULL)->qProtect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }

    template <typename T>
    inline bool isQProtected(const int tid, T * const obj) {
        return rmset->get((T *) NULL)->isQProtected(tid, obj);
    }

    inline void qUnprotectAll(const int tid) { }

    // for epoch based reclamation
    inline bool isQuiescent(const int tid) {
        return rmset->get((RecordTypesFirst *) NULL)->isQuiescent(tid); // warning: if quiescence information is logically shared between all types, with the actual data being associated only with the first type (as it is here), then isQuiescent will return inconsistent results if called in functions that recurse on the template argument list in this class.
    }
    inline void endOp(const int tid) { }

    // for all schemes
    template <typename T>
    inline void retire(const int tid, T * const p) {
        assert(init[tid].v && "must call record_manager initThread before retire");
        rmset->get((T *) NULL)->retire(tid, p);
    }

    template <typename T>
    inline T * allocate(const int tid) {
        assert(init[tid].v && "must call record_manager initThread before allocate");
//        GSTATS_ADD_IX(tid, num_prop_epoch_allocations, 1, GSTATS_GET(tid, thread_announced_epoch));
        return rmset->get((T *) NULL)->allocate(tid);
    }

    // optional function which can be used if it is safe to call free()
    template <typename T>
    inline void deallocate(const int tid, T * const p) {
#ifndef NGC
        assert(init[tid].v && "must call record_manager initThread before deallocate");
        rmset->get((T *) NULL)->deallocate(tid, p);
#endif
    }

    class MemoryReclamationGuard {
        const int tid;
        record_manager<RecordTypesFirst, RecordTypesRest...> * recmgr;
    public:
        MemoryReclamationGuard(const int _tid, record_manager<RecordTypesFirst, RecordTypesRest...> * _recmgr, const bool readOnly = false)
        : tid(_tid), recmgr(_recmgr) { }
    };

    inline MemoryReclamationGuard getGuard(const int tid, const bool readOnly = false) {
        SOFTWARE_BARRIER;
        return MemoryReclamationGuard(tid, this, readOnly);
    }
};

#endif
