/**
 * Implementation of the lock-free external BST of Ellen, Fatourou, Ruppert and van Breugel.
 * This is a heavily modified version of the ASCYLIB implementation (see copyright in ellen.h).
 * The modifications are copyrighted (consistent with the original license)
 *   by Maya Arbel-Raviv and Trevor Brown, 2018.
 */

#ifndef BST_ADAPTER_H
#define BST_ADAPTER_H

#include <iostream>
#include <csignal>
#include "config/errors.h"
#include "CCSynchLibrary/HeaderFiles/ccsynch.h"
#include "ellen_impl.h"
#include "../../../Library/Allocator/BumpAllocator.hpp"

#define MIN 0
#define MAX 1

typedef struct CCPriorityQThreadState {
	CCSynchThreadState delete_thread_state[2] CACHE_ALIGN;
} CCPriorityQThreadState_s;

#define RECORD_MANAGER_T record_manager<node_t<K,V>, info_t<K,V>>
#define DATA_STRUCTURE_T ellen<K, V, RECORD_MANAGER_T>

#ifndef FAO
#define FAO(ptr, value) (__sync_fetch_and_or(ptr, value))
#endif

template <typename K, typename V>
class ds_adapter {
private:
    const V NO_VALUE;
    DATA_STRUCTURE_T * ds;
    CCSynchStruct Combiner[2];
    PAD;

    static RetVal deleteMinWrapper(void *state, ArgVal arg, int pid) {
        V result;
        ds_adapter<K,V> *adapter = static_cast<ds_adapter<K, V>*>(state);
        result = adapter->ds->bst_deleteMin(pid);
        return reinterpret_cast<RetVal>(result);
    }

    static RetVal deleteMaxWrapper(void *state, ArgVal arg, int pid) {
        V result;
        ds_adapter<K,V> *adapter = static_cast<ds_adapter<K, V>*>(state);
        result = adapter->ds->bst_deleteMax(pid);
        return reinterpret_cast<RetVal>(result);
    }

public:
    ds_adapter(const int NUM_THREADS,
            const K& KEY_MIN,
            const K& KEY_MAX,
            const V& VALUE_RESERVED)
    : NO_VALUE(VALUE_RESERVED)
    {
        ds = new DATA_STRUCTURE_T(NUM_THREADS, KEY_MIN, KEY_MAX, NO_VALUE, 0 /* unused */);
        CCSynchStructInit(&Combiner[MIN], NUM_THREADS);
        CCSynchStructInit(&Combiner[MAX], NUM_THREADS);
    }
    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void initThread(CCPriorityQThreadState_s *lobject_struct, int tid) {
        ds->initThread(tid);
        CCSynchThreadStateInit(&Combiner[MIN], &lobject_struct->delete_thread_state[MIN], tid);
        CCSynchThreadStateInit(&Combiner[MAX], &lobject_struct->delete_thread_state[MAX], tid);
    }
    void deinitThread(const int tid) {
        ds->deinitThread(tid);
    }

    V insert(const int tid, const K& key, const V& val) {
        setbench_error("insert-replace functionality not implemented for this data structure");
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        V v = ds->bst_insert(tid, key, val);
        return v;
    }
    V erase(const int tid, const K& key, int type) {
        return ds->bst_delete(tid, key)->value;
    }
    /* Twin: extract-min/max go through CC-Synch combiner (not direct bst_delete*). */
    V eraseMin(CCPriorityQThreadState_s *lobject_struct, const int tid) {
        V vwi;
        vwi = reinterpret_cast<V>(CCSynchApplyOp(
            &Combiner[MIN],
            &(static_cast<CCPriorityQThreadState_s*>(lobject_struct)->delete_thread_state[MIN]),
            &ds_adapter<K, V>::deleteMinWrapper,
            this,
            static_cast<ArgVal>(MIN),
            tid
        ));
        return vwi;
    }
    V eraseMax(CCPriorityQThreadState_s *lobject_struct, const int tid) {
        V vwi;
        vwi = reinterpret_cast<V>(CCSynchApplyOp(
            &Combiner[MAX],
            &(static_cast<CCPriorityQThreadState_s*>(lobject_struct)->delete_thread_state[MAX]),
            &ds_adapter<K, V>::deleteMaxWrapper,
            this,
            static_cast<ArgVal>(MAX),
            tid
        ));
        return vwi;
    }
    V find(const int tid, const K& key, int type) {
        return ds->bst_find(tid, key)->value;
    }
    bool contains(const int tid, const K& key, int type) {
        return find(tid, key, type) != getNoValue();
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        setbench_error("not implemented");
    }
    void printSummary() { }
    bool validateStructure() {
        return true;
    }
    void printObjectSizes() {
        std::cout<<"sizes: node="
                <<(sizeof(node_t<K,V>))
                <<" descriptor="<<(sizeof(info_t<K,V>))
                <<std::endl;
    }
    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() { }

    unsigned int getTreeHeight() {
        return ds->get_tree_height();
    }

    void scan() {
        ds->scan();
    }
};

#endif