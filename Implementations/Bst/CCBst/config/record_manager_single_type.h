/**
 * C++ record manager implementation (PODC 2015) by Trevor Brown.
 *
 * Copyright (C) 2015 Trevor Brown
 *
 */

#ifndef RECORD_MANAGER_SINGLE_TYPE_H
#define	RECORD_MANAGER_SINGLE_TYPE_H

#include <pthread.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <typeinfo>

#include "plaf.h"
#include "globals.h"

#include "../../../../Library/Allocator/BumpAllocator.hpp"

// maybe Record should be a size
template <typename Record>
class record_manager_single_type {
protected:
    typedef Record* record_pointer;

public:
    PAD;
    BumpAllocator<Record>  *alloc;

    const int NUM_PROCESSES;
    PAD;

    record_manager_single_type(const int numProcesses)
            : NUM_PROCESSES(numProcesses) {
        alloc = new BumpAllocator<Record>(numProcesses);
    }
    ~record_manager_single_type() {
        delete alloc;
    }

    void initThread(const int tid) {}

    void deinitThread(const int tid) { }

    inline void clearCounters() { }

    inline static bool shouldHelp() { // FOR DEBUGGING PURPOSES
        return true;
    }
    inline bool isProtected(const int tid, record_pointer obj) {
        return true;
    }
    // for hazard pointers (and reference counting)
    inline bool protect(const int tid, record_pointer obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return true;
    }
    inline void unprotect(const int tid, record_pointer obj) { }
    // warning: qProtect must be reentrant and lock-free (=== async-signal-safe)
    inline bool qProtect(const int tid, record_pointer obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return true;
    }
    inline void qUnprotectAll(const int tid) { }
    inline bool isQProtected(const int tid, record_pointer obj) {
        return false;
    }

    inline static bool supportsCrashRecovery() {
        return false;
    }
    inline static bool quiescenceIsPerRecordType() {
        return false;
    }
    inline bool isQuiescent(const int tid) {
        return true;
    }

    // for epoch based reclamation
    template <typename First, typename... Rest>
    inline void startOp(const int tid, void * const * const reclaimers, const int numReclaimers, const bool readOnly = false) { }

    template <typename First, typename... Rest>
    inline void debugGCSingleThreaded(void * const * const reclaimers, const int numReclaimers) { }

    // for all schemes except reference counting
    inline void retire(const int tid, record_pointer p) { }

    // for all schemes
    inline record_pointer allocate(const int tid) {
        return alloc->allocate(tid);
    }
    inline void deallocate(const int tid, record_pointer p) { }
};

#endif