/**
 * C++ implementation of lock-free chromatic tree using LLX/SCX and DEBRA(+).
 * 
 * Copyright (C) 2016 Trevor Brown
 * Contact (tabrown [at] cs [dot] toronto [dot edu]) with any questions or comments.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CCCHROMATIC_IMPL_H
#define	CCCHROMATIC_IMPL_H

#include <cassert>
#include <cstdlib>
#include "CCSynchLibrary/HeaderFiles/ccsynch.h"
#include "chromatic.h"
#include "../../../src/core/globals.h"
#include <x86intrin.h>
using namespace std;

#define IF_FAIL_TO_PROTECT_SCX(info, tid, _obj, arg2, arg3) \
    info.obj = _obj; \
    info.ptrToObj = arg2; \
    info.nodeContainingPtrToObjIsMarked = arg3; \
    if (_obj != dummy && !recordmgr->protect(tid, _obj, callbackCheckNotRetired, (void*) &info))
#define IF_FAIL_TO_PROTECT_NODE(info, tid, _obj, arg2, arg3) \
    info.obj = _obj; \
    info.ptrToObj = arg2; \
    info.nodeContainingPtrToObjIsMarked = arg3; \
    if (_obj != root && !recordmgr->protect(tid, _obj, callbackCheckNotRetired, (void*) &info))

inline CallbackReturn callbackCheckNotRetired(CallbackArg arg) {
    Chromatic_retired_info *info = (Chromatic_retired_info*) arg;
    if ((void*) info->ptrToObj->load(memory_order_relaxed) == info->obj) {
        // we insert a compiler barrier (not a memory barrier!)
        // to prevent these if statements from being merged or reordered.
        // we care because we need to see that ptrToObj == obj
        // and THEN see that ptrToObject is a field of an object
        // that is not marked. seeing both of these things,
        // in this order, implies that obj is in the data structure.
        SOFTWARE_BARRIER;
        if (!info->nodeContainingPtrToObjIsMarked->load(memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

#define allocateSCXRecord(tid) recordmgr->template allocate<SCXRecord<K,V> >((tid))
#define allocateNode(tid) recordmgr->template allocate<Node<K,V> >((tid))

#define initializeSCXRecord(_tid, _newop, _type, _nodes, _llxResults, _field, _newNode) { \
    (_newop)->type = (_type); \
    (_newop)->newNode = (_newNode); \
    for (int i=0;i<NUM_OF_NODES[(_type)];++i) { \
        (_newop)->nodes[i] = (_nodes)[i]; \
    } \
    for (int i=0;i<NUM_TO_FREEZE[(_type)];++i) { \
        (_newop)->scxRecordsSeen[i] = (SCXRecord<K,V>*) (_llxResults)[i]; \
    } \
    /* note: synchronization is not necessary for the following accesses, \
       since a memory barrier will occur before this object becomes reachable \
       from an entry point to the data structure. */ \
    (_newop)->state.store(SCXRecord<K,V>::STATE_INPROGRESS, memory_order_relaxed); \
    (_newop)->allFrozen.store(false, memory_order_relaxed); \
    (_newop)->field = (_field); \
}
#define initializeNode(_tid, _newnode, _key, _value, _weight, _left, _right) \
(_newnode); \
{ \
    (_newnode)->key = (_key); \
    (_newnode)->value = (_value); \
    (_newnode)->weight = (_weight); \
    /* note: synchronization is not necessary for the following accesses, \
       since a memory barrier will occur before this object becomes reachable \
       from an entry point to the data structure. */ \
    (_newnode)->left.store((uintptr_t) (_left), memory_order_relaxed); \
    (_newnode)->right.store((uintptr_t) (_right), memory_order_relaxed); \
    (_newnode)->scxRecord.store((uintptr_t) dummy, memory_order_relaxed); \
    (_newnode)->marked.store(false, memory_order_relaxed); \
}

template<class K, class V, class Compare, class MasterRecordMgr>
Chromatic<K,V,Compare,MasterRecordMgr>::Chromatic(const K& _NO_KEY,
            const V& _NO_VALUE,
            const V& _RETRY,
            const int numProcesses,
            int neutralizeSignal,
            int allowedViolationsPerPath)
    : N(allowedViolationsPerPath),
            NO_KEY(_NO_KEY),
            NO_VALUE(_NO_VALUE),
            RETRY(_RETRY),
            recordmgr(new MasterRecordMgr(numProcesses, neutralizeSignal)) {
    const int tid = 0;
    recordmgr->initThread(tid);
    
    dummy = allocateSCXRecord(tid);
    dummy->type = SCXRecord<K,V>::TYPE_NOOP;
    dummy->state.store(SCXRecord<K,V>::STATE_ABORTED, memory_order_relaxed); // this is a NO-OP, so it shouldn't start as InProgress; aborted is just more efficient than committed, since we won't try to help marked leaves, which always have the dummy scx record...
    Node<K,V> *rootleft = allocateNode(tid);
    initializeNode(tid, rootleft, NO_KEY, NO_VALUE, 1, NULL, NULL);
    root = allocateNode(tid);
    initializeNode(tid, root, NO_KEY, NO_VALUE, 1, rootleft, NULL);
    cmp = Compare();
    allocatedSCXRecord = new SCXRecord<K,V>*[numProcesses*PREFETCH_SIZE_WORDS];
    allocatedNodes = new Node<K,V>*[numProcesses*(PREFETCH_SIZE_WORDS+MAX_NODES-1)];
    for (int tid=0;tid<numProcesses;++tid) {
        GET_ALLOCATED_SCXRECORD_PTR(tid) = NULL;
    }
    /* Twin: delete-min/max serialized through CC-Synch combiners. */
    CCSynchStructInit(&Combiner[MIN], numProcesses);
    CCSynchStructInit(&Combiner[MAX], numProcesses);
}

/**
 * This function must be called once by each thread that will
 * invoke any functions on this class.
 * 
 * It must be okay that we do this with the main thread and later with another thread!!!
 */
template<class K, class V, class Compare, class MasterRecordMgr>
void Chromatic<K,V,Compare,MasterRecordMgr>::initThread(CCPriorityQThreadState_s *lobject_struct, const int tid) {
    recordmgr->initThread(tid);
    if (GET_ALLOCATED_SCXRECORD_PTR(tid) == NULL) {
        REPLACE_ALLOCATED_SCXRECORD(tid);
        for (int i=0;i<MAX_NODES-1;++i) {
            REPLACE_ALLOCATED_NODE(tid, i);
        }
    }
    CCSynchThreadStateInit(&Combiner[MIN], &lobject_struct->delete_thread_state[MIN], tid);
    CCSynchThreadStateInit(&Combiner[MAX], &lobject_struct->delete_thread_state[MAX], tid);
}

template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::contains(const int tid, const K& key) {
    pair<V,bool> result = find(tid, key);
    return result.second;
}

template<class K, class V, class Compare, class MasterRecordMgr>
const pair<V,bool> Chromatic<K,V,Compare,MasterRecordMgr>::find(const int tid, const K& key) {
    pair<V,bool> result;
    Chromatic_retired_info info;
    Node<K,V> *p;
    Node<K,V> *l;
    for (;;) {
        // root is never retired, so we don't need to call
        // protectPointer before accessing its child pointers
        p = (Node<K,V>*) root->left.load(memory_order_relaxed);
        IF_FAIL_TO_PROTECT_NODE(info, tid, p, &root->left, &root->marked) {
            continue; /* retry */ 
        }
        DEBUG assert(p != root);
        DEBUG assert(recordmgr->isProtected(tid, p));
        l = (Node<K,V>*) p->left.load(memory_order_relaxed);
        if (l == NULL) {
            result = pair<V,bool>(NO_VALUE, false); // no keys in data structure
            return result; // success
        }
        IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
            continue; /* retry */ 
        }

        DEBUG assert(recordmgr->isProtected(tid, l));
        while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) {
            DEBUG assert(recordmgr->isProtected(tid, p));
            recordmgr->unprotect(tid, p);
            p = l; // note: the new p is currently protected
            DEBUG assert(recordmgr->isProtected(tid, p));
            DEBUG assert(p->key != NO_KEY);
            if (cmp(key, p->key)) {
                DEBUG assert(recordmgr->isProtected(tid, p));
                l = (Node<K,V>*) p->left.load(memory_order_relaxed);
                IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
                    continue; /* retry */ 
                }
            } else {
                DEBUG assert(recordmgr->isProtected(tid, p));
                l = (Node<K,V>*) p->right.load(memory_order_relaxed);
                IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->right, &p->marked) {
                    continue; /* retry */ 
                }
            }
            DEBUG assert(recordmgr->isProtected(tid, l));
        }
        DEBUG assert(recordmgr->isProtected(tid, l));
        if (key == l->key) {
            DEBUG assert(recordmgr->isProtected(tid, l));
            result = pair<V,bool>(l->value, true);
        } else {
            result = pair<V,bool>(NO_VALUE, false);
        }
        return result; // success
    }
    return pair<V,bool>(NO_VALUE, false);
}

template<class K, class V, class Compare, class MasterRecordMgr>
const V Chromatic<K,V,Compare,MasterRecordMgr>::insert(const int tid, const K& key, const V& val) {
    bool onlyIfAbsent = false;
    V result = NO_VALUE;
    bool shouldRebalance = false;
    bool finished = false;
    #ifdef COMPUTE_STATS
        totalIns += 1;
    #endif
    while (!finished) {
        #ifdef COMPUTE_STATS
            numInsRetries += 1;
        #endif
        finished = updateInsert(tid, key, val, onlyIfAbsent, &result, &shouldRebalance);
    }
    // call another routine to handle rebalancing.
    // this is considered to be a whole new operation (possibly many, in fact).
    if (shouldRebalance) {
        fixAllToKey(tid, key);
    }
    return result;
}

template<class K, class V, class Compare, class MasterRecordMgr>
const bool Chromatic<K,V,Compare,MasterRecordMgr>::insertIfAbsent(const int tid, const K& key, const V& val) {
    bool onlyIfAbsent = true;
    V result = NO_VALUE;
    bool shouldRebalance = false;
    bool finished = false;
    #ifdef COMPUTE_STATS
        totalIns += 1;
    #endif
    while (!finished) {
        #ifdef COMPUTE_STATS
            numInsRetries += 1;
        #endif
        finished = updateInsert(tid, key, val, onlyIfAbsent, &result, &shouldRebalance);
    }
    // call another routine to handle rebalancing.
    // this is considered to be a whole new operation (possibly many, in fact).
    if (shouldRebalance) {
        fixAllToKey(tid, key);
    }
    return (result == NO_VALUE);
}

template<class K, class V, class Compare, class MasterRecordMgr>
const pair<V,bool> Chromatic<K,V,Compare,MasterRecordMgr>::erase(const int tid, const K& key) {
    V result = NO_VALUE;
    bool shouldRebalance = false;
    bool finished = false;
    while (!finished) {
        finished = updateErase(tid, key, &result, &shouldRebalance);
    }
    // call another routine to handle rebalancing.
    // this is considered to be a whole new operation (possibly many, in fact).
    if (shouldRebalance) {
        fixAllToKey(tid, key);
    }
    return pair<V,bool>(result, (result != NO_VALUE));
}

template<class K, class V, class Compare, class MasterRecordMgr>
V Chromatic<K, V, Compare, MasterRecordMgr>::extractMin(ArgVal arg, int tid) {
    V result = NO_VALUE;
    K key;
    bool shouldRebalance = false;
    bool finished = false;
#ifdef COMPUTE_STATS
    totalDel += 1;
#endif
    while (!finished) {
        #ifdef COMPUTE_STATS
            numDelRetries += 1;
        #endif
        finished = updateEraseMin(tid, key, &result, &shouldRebalance);
    }
    // call another routine to handle rebalancing.
    // this is considered to be a whole new operation (possibly many, in fact).
    if (shouldRebalance) {
        fixAllToKey(tid, key);
    }
    return result;
}

template<class K, class V, class Compare, class MasterRecordMgr>
V Chromatic<K, V, Compare, MasterRecordMgr>::extractMax(ArgVal arg, int tid) {
    V result = NO_VALUE;
    K key;
    bool shouldRebalance = false;
    bool finished = false;
#ifdef COMPUTE_STATS
    totalDel += 1;
#endif
    while (!finished) {
        #ifdef COMPUTE_STATS
            numDelRetries += 1;
        #endif
        finished = updateEraseMax(tid, key, &result, &shouldRebalance);
    }
    // call another routine to handle rebalancing.
    // this is considered to be a whole new operation (possibly many, in fact).
    if (shouldRebalance) {
        fixAllToKey(tid, key);
    }
    return result;
}

template<class K, class V, class Compare, class MasterRecordMgr>
const pair<V,bool> Chromatic<K,V,Compare,MasterRecordMgr>::deleteMin(CCPriorityQThreadState_s *lobject_struct, const int tid) {
    V result = CCSynchApplyOp(
            &Combiner[MIN],
            &(static_cast<CCPriorityQThreadState_s*>(lobject_struct)->delete_thread_state[MIN]),
            &Chromatic<K,V,Compare,MasterRecordMgr>::staticExtractMinWrapper,
            this,
            static_cast<ArgVal>(MIN),
            tid
        );
    return pair<V,bool>(result, (result != NO_VALUE));
}

template<class K, class V, class Compare, class MasterRecordMgr>
const pair<V,bool> Chromatic<K,V,Compare,MasterRecordMgr>::deleteMax(CCPriorityQThreadState_s *lobject_struct, const int tid) {
    V result = CCSynchApplyOp(
        &Combiner[MAX],
        &(static_cast<CCPriorityQThreadState_s*>(lobject_struct)->delete_thread_state[MAX]),
        static_cast<RetVal (*)(void*, ArgVal, int)>(
            reinterpret_cast<RetVal (*)(void*, ArgVal, int)>(
                &Chromatic<K,V,Compare,MasterRecordMgr>::staticExtractMaxWrapper
            )
        ),
        this,
        static_cast<ArgVal>(MAX),
        tid
    );
    return pair<V,bool>(result, (result != NO_VALUE));
}

template<class K, class V, class Compare, class MasterRecordMgr>
void Chromatic<K,V,Compare,MasterRecordMgr>::fixAllToKey(const int tid, const K& key) {
    bool finished = false;
    while (!finished) {
        #ifdef COMPUTE_STATS
            totalReb += 1;
        #endif
        finished = updateRebalancingStep(tid, key);
    }
}

// RULE: ANY OUTPUT OF updateXXXXX MUST BE FULLY WRITTEN BEFORE SCX IS INVOKED!
template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::updateInsert(
            const int tid,
            const K& key,
            const V& val,
            const bool onlyIfAbsent,
            V * const result,
            bool * const shouldRebalance) {    
    int debugLoopCount = 0;
    Node<K,V> *p = root, *l;
    int count = 0;
    
    // root is never retired, so we don't need to call
    // protect before accessing its child pointers
    l = (Node<K,V>*) root->left.load(memory_order_relaxed);

    Chromatic_retired_info info;
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &root->left, &root->marked) {
        return false;
    } // return and retry
#ifdef COMPUTE_STATS
  locate_calls += 1;
#endif
    DEBUG assert(recordmgr->isProtected(tid, l));
    if ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) { // the tree contains some node besides sentinels...
        p = l;          // note: p is protected by the above call to protect(..., l, ...)
        DEBUG assert(recordmgr->isProtected(tid, l));
        l = (Node<K,V>*) l->left.load(memory_order_relaxed);    // note: l must have key infinity, and l->left must not.

        // loop invariant: p and l are protected by calls to protect(tid, ...)
        IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
            return false;
        } // return and retry
        DEBUG assert(recordmgr->isProtected(tid, l));
        while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) {
            DEBUG assert(recordmgr->isProtected(tid, l));
            DEBUG assert(recordmgr->isProtected(tid, p));
            if (l->weight > 1 || l->weight == 0 && p->weight == 0) ++count; // count violations on this path
            recordmgr->unprotect(tid, p);
//            DEBUG assert(!recordmgr->isProtected(tid, p));
            p = l;  // note: p is protected by the call to protect made in the last iteration of this loop (or above the loop)

            DEBUG assert(p->key != NO_KEY);
            if (cmp(key, p->key)) {
                DEBUG assert(recordmgr->isProtected(tid, p));
                l = (Node<K,V>*) p->left.load(memory_order_relaxed);
                IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
                    return false;
                } // return and retry
            } else {
                DEBUG assert(recordmgr->isProtected(tid, p));
                l = (Node<K,V>*) p->right.load(memory_order_relaxed);
                IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->right, &p->marked) {
                    return false;
                } // return and retry
            }
            #ifdef COMPUTE_AVG
                pointers_ins_traversed += 1;
            #endif	
            DEBUG assert(recordmgr->isProtected(tid, l));
        }
    }

    // if we find the key in the tree already
    DEBUG assert(recordmgr->isProtected(tid, l));
    if (key == l->key) {
        if (onlyIfAbsent) {
            DEBUG assert(recordmgr->isProtected(tid, l));
            *result = l->value;
            return true; // success
        }

        void *llxResults[NUM_TO_FREEZE[SCXRecord<K,V>::TYPE_REPLACE]];
        Node<K,V> *nodes[] = {p, l};
        Node<K,V> *pleft, *pright;
        // note: p is already protected by a call to protect in the search phase, above
        DEBUG assert(recordmgr->isProtected(tid, p) || p == root);
        if ((llxResults[0] = LLX(tid, p, &pleft, &pright)) == NULL) {
            return false;
        } //RETRY;
        if (l != pleft && l != pright) {
            return false;
        } //RETRY;
        DEBUG assert(recordmgr->isProtected(tid, l));
        *result = l->value;

        DEBUG assert(recordmgr->isProtected(tid, l));
        DEBUG assert((Node<K,V>*) l->left.load(memory_order_relaxed) == NULL);
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, l->weight, NULL, NULL);

        DEBUG assert(recordmgr->isProtected(tid, p) || p == root);
        DEBUG assert(recordmgr->isProtected(tid, l));
        if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_REPLACE, nodes, llxResults, (l == pleft ? &p->left : &p->right), GET_ALLOCATED_NODE_PTR(tid, 0))) {
            return true;
        } else {
            return false; //RETRY;
        }
    } else {
        void *llxResults[NUM_TO_FREEZE[SCXRecord<K,V>::TYPE_INS]];
        Node<K,V> *nodes[] = {p, l};
        Node<K,V> *pleft, *pright;
        // note: p is already protected by a call to protect in the search phase, above
        DEBUG assert(recordmgr->isProtected(tid, p) || p == root);
        if ((llxResults[0] = LLX(tid, p, &pleft, &pright)) == NULL) {
            return false;
        } //RETRY;
        if (l != pleft && l != pright) {
            return false;
        } //RETRY;

        // Compute the weight for the new parent node.
        // If l is a sentinel then we must set its weight to one.
        DEBUG assert(recordmgr->isProtected(tid, l));
        DEBUG assert(recordmgr->isProtected(tid, p) || p == root);
        int newWeight = (IS_SENTINEL(l, p) ? 1 : l->weight - 1);

        /*Node<K,V> *newLeaf =*/
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, 1, NULL, NULL);
        DEBUG assert(recordmgr->isProtected(tid, l));
        /*Node<K,V> *newCopyOfL =*/
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, 1, NULL, NULL);
        /*Node<K,V> *newInternal;*/
        DEBUG assert(recordmgr->isProtected(tid, l));
        if (l->key == NO_KEY || cmp(key, l->key)) {
            DEBUG assert(recordmgr->isProtected(tid, l));
            /*newInternal = */
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), l->key, l->value, newWeight, GET_ALLOCATED_NODE_PTR(tid, 0), GET_ALLOCATED_NODE_PTR(tid, 1));
        } else {
            /*newInternal = */
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), key, val, newWeight, GET_ALLOCATED_NODE_PTR(tid, 1), GET_ALLOCATED_NODE_PTR(tid, 0));
        }
        *result = NO_VALUE;
        *shouldRebalance = (count > N);

        if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_INS, nodes, llxResults, (l == pleft ? &p->left : &p->right), GET_ALLOCATED_NODE_PTR(tid, 2))) {
            return true; // success
        } else {
            return false; //RETRY;
        }
    }
}

// RULE: ANY OUTPUT OF updateXXXXX MUST BE FULLY WRITTEN BEFORE SCX IS INVOKED!
template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::updateErase(
            const int tid,
            const K& key,
            V * const result,
            bool * const shouldRebalance) {

    int debugLoopCount = 0;
    Node<K,V> *gp, *p, *l;


    // root is never retired, so we don't need to call
    // protect before accessing its child pointers
    l = (Node<K,V>*) root->left.load(memory_order_relaxed);

    Chromatic_retired_info info;
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &root->left, &root->marked) {
        return false;
    } // return and retry
    DEBUG assert(recordmgr->isProtected(tid, l));
    if ((Node<K,V>*) l->left.load(memory_order_relaxed) == NULL) {
        return true;
    } // only sentinels in tree...
    
    int count = 0;
    gp = root;      // note: gp is protected because it is the root
    p = l;          // note: p is protected by the above call to protect(..., l, ...)
    DEBUG assert(recordmgr->isProtected(tid, p));
    l = (Node<K,V>*) p->left.load(memory_order_relaxed);    // note: l must have key infinity, and l->left must not.
    
    // loop invariant: gp, p, l are all protected by calls to protect(tid, ...) (and no other nodes are)
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
        return false;
    } // return and retry
    DEBUG assert(recordmgr->isProtected(tid, l));
    while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) {
        DEBUG assert(recordmgr->isProtected(tid, l));
        DEBUG assert(recordmgr->isProtected(tid, p));
        if (l->weight > 1 || l->weight == 0 && p->weight == 0) ++count; // count violations on this path
        if (gp != root) recordmgr->unprotect(tid, gp);
        gp = p; // note: gp is protected by the call to protect made in the second last iteration of this loop (or above the loop)
        p = l;  // note: p is protected by the call to protect made in the last iteration of this loop (or above the loop)
        DEBUG assert(recordmgr->isProtected(tid, gp));
        DEBUG assert(recordmgr->isProtected(tid, p));
        
        DEBUG assert(p->key != NO_KEY);
        if (cmp(key, p->key)) {
            DEBUG assert(recordmgr->isProtected(tid, p));
            l = (Node<K,V>*) p->left.load(memory_order_relaxed);
            IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
                return false;
            } // return and retry
        } else {
            DEBUG assert(recordmgr->isProtected(tid, p));
            l = (Node<K,V>*) p->right.load(memory_order_relaxed);
            IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->right, &p->marked) {
                return false;
            } // return and retry
        }
        DEBUG assert(recordmgr->isProtected(tid, l));
    }

    // if we fail to find the key in the tree
    DEBUG assert(recordmgr->isProtected(tid, l));
    if (key != l->key) {
        *result = NO_VALUE;
        return true; // success
    } else {
        DEBUG assert(key != NO_KEY);
        void *llxResults[NUM_TO_FREEZE[SCXRecord<K,V>::TYPE_DEL]];
        Node<K,V> *nodes[] = {gp, p, NULL, l};
        Node<K,V> *gpleft, *gpright;
        Node<K,V> *pleft, *pright;
        Node<K,V> *sleft, *sright;
        // note: gp is already protected
        DEBUG assert(recordmgr->isProtected(tid, gp) || gp == root);
        if ((llxResults[0] = LLX(tid, gp, &gpleft, &gpright)) == NULL) {
            return false;
        }
        if (p != gpleft && p != gpright) {
            return false;
        }
        // note: p is already protected
        DEBUG assert(recordmgr->isProtected(tid, p));
        if ((llxResults[1] = LLX(tid, p, &pleft, &pright)) == NULL) {
            return false;
        }
        if (l != pleft && l != pright) {
            return false;
        }
        DEBUG assert(recordmgr->isProtected(tid, l));
        *result = l->value;

        // Read fields for the sibling s of l
        // note: we must call protect(..., s, ...) because LLX will read its fields.
        Node<K,V> *s = (l == pleft ? pright : pleft);
        IF_FAIL_TO_PROTECT_NODE(info, tid, s, (l == pleft ? &p->right : &p->left), &p->marked) {
            return false;
        } // return and retry
        DEBUG assert(recordmgr->isProtected(tid, s));
        if ((llxResults[2] = LLX(tid, s, &sleft, &sright)) == NULL) {
            return false;
        }
        nodes[2] = s;

        // Now, if the op. succeeds, all structure is guaranteed to be just as we verified

        // Compute weight for the new node that replaces p (and l)
        // If p is a sentinel then we must set the new node's weight to one.
        DEBUG assert(recordmgr->isProtected(tid, p));
        DEBUG assert(recordmgr->isProtected(tid, s));
        int newWeight = (IS_SENTINEL(p, gp) ? 1 : p->weight + s->weight);

        DEBUG assert(recordmgr->isProtected(tid, s));
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), s->key, s->value, newWeight, sleft, sright);
        DEBUG assert(recordmgr->isProtected(tid, gp) || gp == root);
        DEBUG assert(recordmgr->isProtected(tid, p));
        DEBUG assert(recordmgr->isProtected(tid, l));
        DEBUG assert(recordmgr->isProtected(tid, s));
        *shouldRebalance = (count > N);
        
        if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_DEL, nodes, llxResults, (p == gpleft ? &gp->left : &gp->right), GET_ALLOCATED_NODE_PTR(tid, 0))) {
            return true; // success
        } else {
            return false; // retry
        }
    }
}

// RULE: ANY OUTPUT OF updateXXXXX MUST BE FULLY WRITTEN BEFORE SCX IS INVOKED!
template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::updateEraseMin(
            const int tid,
            K& key,
            V * const result,
            bool * const shouldRebalance) {
    int debugLoopCount = 0;
    Node<K,V> *gp, *p, *l;


    // root is never retired, so we don't need to call
    // protect before accessing its child pointers
    l = (Node<K,V>*) root->left.load(memory_order_relaxed);

    Chromatic_retired_info info;
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &root->left, &root->marked) {
        return false;
    } // return and retry
    DEBUG assert(recordmgr->isProtected(tid, l));
    if ((Node<K,V>*) l->left.load(memory_order_relaxed) == NULL) {
        return true;
    } // only sentinels in tree...
    
    int count = 0;
    gp = root;      // note: gp is protected because it is the root
    p = l;          // note: p is protected by the above call to protect(..., l, ...)
    DEBUG assert(recordmgr->isProtected(tid, p));
    l = (Node<K,V>*) p->left.load(memory_order_relaxed);    // note: l must have key infinity, and l->left must not.
    
    // loop invariant: gp, p, l are all protected by calls to protect(tid, ...) (and no other nodes are)
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
        return false;
    } // return and retry
    DEBUG assert(recordmgr->isProtected(tid, l));
    while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) {
    #ifdef COMPUTE_AVG
        pointers_traversed += 1;
    #endif
        DEBUG assert(recordmgr->isProtected(tid, l));
        DEBUG assert(recordmgr->isProtected(tid, p));
        if (l->weight > 1 || l->weight == 0 && p->weight == 0) ++count; // count violations on this path
        if (gp != root) recordmgr->unprotect(tid, gp);
        gp = p; // note: gp is protected by the call to protect made in the second last iteration of this loop (or above the loop)
        p = l;  // note: p is protected by the call to protect made in the last iteration of this loop (or above the loop)
        DEBUG assert(recordmgr->isProtected(tid, gp));
        DEBUG assert(recordmgr->isProtected(tid, p));
        
        DEBUG assert(p->key != NO_KEY);
        DEBUG assert(recordmgr->isProtected(tid, p));
        l = (Node<K,V>*) p->left.load(memory_order_relaxed);
        IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
            return false;
        } // return and retry
        DEBUG assert(recordmgr->isProtected(tid, l));
    }

    // if we fail to find the key in the tree
    DEBUG assert(recordmgr->isProtected(tid, l));
    if (l->key == NO_KEY) {
        *result = NO_VALUE;
        return true; // success
    } else {
        key = l->key;
        DEBUG assert(key != NO_KEY);
        void *llxResults[NUM_TO_FREEZE[SCXRecord<K,V>::TYPE_DEL]];
        Node<K,V> *nodes[] = {gp, p, NULL, l};
        Node<K,V> *gpleft, *gpright;
        Node<K,V> *pleft, *pright;
        Node<K,V> *sleft, *sright;
        // note: gp is already protected
        DEBUG assert(recordmgr->isProtected(tid, gp) || gp == root);
        if ((llxResults[0] = LLX(tid, gp, &gpleft, &gpright)) == NULL) {
            return false;
        }
        if (p != gpleft && p != gpright) {
            return false;
        }
        // note: p is already protected
        DEBUG assert(recordmgr->isProtected(tid, p));
        if ((llxResults[1] = LLX(tid, p, &pleft, &pright)) == NULL) {
            return false;
        }
        if (l != pleft && l != pright) {
            return false;
        }
        DEBUG assert(recordmgr->isProtected(tid, l));
        *result = l->value;

        // Read fields for the sibling s of l
        // note: we must call protect(..., s, ...) because LLX will read its fields.
        Node<K,V> *s = (l == pleft ? pright : pleft);
        IF_FAIL_TO_PROTECT_NODE(info, tid, s, (l == pleft ? &p->right : &p->left), &p->marked) {
            return false;
        } // return and retry
        DEBUG assert(recordmgr->isProtected(tid, s));
        if ((llxResults[2] = LLX(tid, s, &sleft, &sright)) == NULL) {
            return false;
        }
        nodes[2] = s;

        // Now, if the op. succeeds, all structure is guaranteed to be just as we verified

        // Compute weight for the new node that replaces p (and l)
        // If p is a sentinel then we must set the new node's weight to one.
        DEBUG assert(recordmgr->isProtected(tid, p));
        DEBUG assert(recordmgr->isProtected(tid, s));
        int newWeight = (IS_SENTINEL(p, gp) ? 1 : p->weight + s->weight);

        DEBUG assert(recordmgr->isProtected(tid, s));
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), s->key, s->value, newWeight, sleft, sright);
        DEBUG assert(recordmgr->isProtected(tid, gp) || gp == root);
        DEBUG assert(recordmgr->isProtected(tid, p));
        DEBUG assert(recordmgr->isProtected(tid, l));
        DEBUG assert(recordmgr->isProtected(tid, s));
        *shouldRebalance = (count > N);
        
        if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_DEL, nodes, llxResults, (p == gpleft ? &gp->left : &gp->right), GET_ALLOCATED_NODE_PTR(tid, 0))) {
            return true; // success
        } else {
            return false; // retry
        }
    }
}

// RULE: ANY OUTPUT OF updateXXXXX MUST BE FULLY WRITTEN BEFORE SCX IS INVOKED!
template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::updateEraseMax(
            const int tid,
            K& key,
            V * const result,
            bool * const shouldRebalance) {
    int debugLoopCount = 0;
    Node<K,V> *gp, *p, *l;


    // root is never retired, so we don't need to call
    // protect before accessing its child pointers
    l = (Node<K,V>*) root->left.load(memory_order_relaxed);

    Chromatic_retired_info info;
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &root->left, &root->marked) {
        return false;
    } // return and retry
    DEBUG assert(recordmgr->isProtected(tid, l));
    if ((Node<K,V>*) l->left.load(memory_order_relaxed) == NULL) {
        return true;
    } // only sentinels in tree...
    
    int count = 0;
    gp = root;      // note: gp is protected because it is the root
    p = l;          // note: p is protected by the above call to protect(..., l, ...)
    DEBUG assert(recordmgr->isProtected(tid, p));
    l = (Node<K,V>*) p->left.load(memory_order_relaxed);    // note: l must have key infinity, and l->left must not.
    
    // loop invariant: gp, p, l are all protected by calls to protect(tid, ...) (and no other nodes are)
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
        return false;
    } // return and retry
    DEBUG assert(recordmgr->isProtected(tid, l));
    while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) {
    #ifdef COMPUTE_AVG
        pointers_traversed += 1;
    #endif
        DEBUG assert(recordmgr->isProtected(tid, l));
        DEBUG assert(recordmgr->isProtected(tid, p));
        if (l->weight > 1 || l->weight == 0 && p->weight == 0) ++count; // count violations on this path
        if (gp != root) recordmgr->unprotect(tid, gp);
        gp = p; // note: gp is protected by the call to protect made in the second last iteration of this loop (or above the loop)
        p = l;  // note: p is protected by the call to protect made in the last iteration of this loop (or above the loop)
        DEBUG assert(recordmgr->isProtected(tid, gp));
        DEBUG assert(recordmgr->isProtected(tid, p));
        
        DEBUG assert(p->key != NO_KEY);
        DEBUG assert(recordmgr->isProtected(tid, p));
        l = (Node<K,V>*) p->right.load(memory_order_relaxed);
        IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->right, &p->marked) {
            return false;
        } // return and retry
        DEBUG assert(recordmgr->isProtected(tid, l));
    }

    // if we fail to find the key in the tree
    DEBUG assert(recordmgr->isProtected(tid, l));
    if (l->key == NO_KEY) {
        *result = NO_VALUE;
        return true; // success
    } else {
        key = l->key;
        DEBUG assert(key != NO_KEY);
        void *llxResults[NUM_TO_FREEZE[SCXRecord<K,V>::TYPE_DEL]];
        Node<K,V> *nodes[] = {gp, p, NULL, l};
        Node<K,V> *gpleft, *gpright;
        Node<K,V> *pleft, *pright;
        Node<K,V> *sleft, *sright;
        // note: gp is already protected
        DEBUG assert(recordmgr->isProtected(tid, gp) || gp == root);
        if ((llxResults[0] = LLX(tid, gp, &gpleft, &gpright)) == NULL) {
            return false;
        }
        if (p != gpleft && p != gpright) {
            return false;
        }
        // note: p is already protected
        DEBUG assert(recordmgr->isProtected(tid, p));
        if ((llxResults[1] = LLX(tid, p, &pleft, &pright)) == NULL) {
            return false;
        }
        if (l != pleft && l != pright) {
            return false;
        }
        DEBUG assert(recordmgr->isProtected(tid, l));
        *result = l->value;

        // Read fields for the sibling s of l
        // note: we must call protect(..., s, ...) because LLX will read its fields.
        Node<K,V> *s = (l == pleft ? pright : pleft);
        IF_FAIL_TO_PROTECT_NODE(info, tid, s, (l == pleft ? &p->right : &p->left), &p->marked) {
            return false;
        } // return and retry
        DEBUG assert(recordmgr->isProtected(tid, s));
        if ((llxResults[2] = LLX(tid, s, &sleft, &sright)) == NULL) {
            return false;
        }
        nodes[2] = s;

        // Now, if the op. succeeds, all structure is guaranteed to be just as we verified

        // Compute weight for the new node that replaces p (and l)
        // If p is a sentinel then we must set the new node's weight to one.
        DEBUG assert(recordmgr->isProtected(tid, p));
        DEBUG assert(recordmgr->isProtected(tid, s));
        int newWeight = (IS_SENTINEL(p, gp) ? 1 : p->weight + s->weight);

        DEBUG assert(recordmgr->isProtected(tid, s));
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), s->key, s->value, newWeight, sleft, sright);
        DEBUG assert(recordmgr->isProtected(tid, gp) || gp == root);
        DEBUG assert(recordmgr->isProtected(tid, p));
        DEBUG assert(recordmgr->isProtected(tid, l));
        DEBUG assert(recordmgr->isProtected(tid, s));
        *shouldRebalance = (count > N);
        
        if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_DEL, nodes, llxResults, (p == gpleft ? &gp->left : &gp->right), GET_ALLOCATED_NODE_PTR(tid, 0))) {
            return true; // success
        } else {
            return false; // retry
        }
    }
}

template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::updateRebalancingStep(
            const int tid,
            const K& key) {
    Node<K,V> *ggp, *gp, *p, *l;

    // root is never retired, so we don't need to call
    // protect before accessing its child pointers
    l = (Node<K,V>*) root->left.load(memory_order_relaxed);

    Chromatic_retired_info info;
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &root->left, &root->marked) return false; // return and retry
    if ((Node<K,V>*) l->left.load(memory_order_relaxed) == NULL) return true; // only sentinels in tree...
    
    ggp = gp = root;
    p = l; // note: p is protected by the above call to protect(..., l, ...)
    DEBUG assert(recordmgr->isProtected(tid, p));
    l = (Node<K,V>*) l->left.load(memory_order_relaxed); // note: l must have key infinity, and l->left must not.
    
    // loop invariant: ggp, gp, p, l are all protected by calls to protect(tid, ...) (and no other nodes are)
    DEBUG assert(recordmgr->isProtected(tid, p));
    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) return false; // return and retry
    DEBUG assert(recordmgr->isProtected(tid, l));
    while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL && l->weight <= 1 && (l->weight != 0 || p->weight != 0)) {
        #ifdef COMPUTE_AVG
            pointers_reb_traversed += 1;
        #endif
        if (ggp != root) recordmgr->unprotect(tid, ggp);
        ggp = gp;
        gp = p;
        p = l; // note: p is still protected by the call to protect made in the last iteration (or above the loop, where p = l)
        DEBUG assert(recordmgr->isProtected(tid, ggp) || ggp == root);
        DEBUG assert(recordmgr->isProtected(tid, gp));
        DEBUG assert(recordmgr->isProtected(tid, p));
        DEBUG assert(recordmgr->isProtected(tid, l));
        
        DEBUG assert(p->key != NO_KEY);
        if (cmp(key, p->key)) {
            l = (Node<K,V>*) p->left.load(memory_order_relaxed);
            IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) return false; // return and retry
        } else {
            l = (Node<K,V>*) p->right.load(memory_order_relaxed);
            IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->right, &p->marked) return false; // return and retry
        }
    }
    DEBUG assert(recordmgr->isProtected(tid, l));
    if (l->weight == 1) {
        return true; // (if no violation, then we hit a leaf, so we can stop)
    }

    // a few aliases to make the code more uniform
    // note: these nodes have already been passed to protect() in the
    //       search phase, above. they are currently protected.
    //       thus, we don't need to call protect() before LLXing them.
    // the following variable names follow a specific convention that encodes
    // their ancestry relative to one another.
    //       u is the topmost node.
    //       uX is a child of u, but we don't know/care which child it is.
    //       uXL is the left child of uX. uXR is the right child of uX.
    // more complicated example:
    //       uXXRLR is the right child of uXXRL,
    //       which is the left child of uXXR,
    //       which is the right child of uXX,
    //       which is a child of uX,
    //       which is a child of u.
    Node<K,V> * const u = ggp;
    Node<K,V> * const uX = gp;
    Node<K,V> * const uXX = p;
    Node<K,V> * const uXXX = l;

    void *iu;       Node<K,V> *uL, *uR;
    void *iuX;      Node<K,V> *uXL, *uXR;
    void *iuXX;     Node<K,V> *uXXL, *uXXR;
    void *iuXL;     Node<K,V> *uXLL, *uXLR;
    void *iuXR;     Node<K,V> *uXRL, *uXRR;
    void *iuXXL;    Node<K,V> *uXXLL, *uXXLR;
    void *iuXXR;    Node<K,V> *uXXRL, *uXXRR;
    void *iuXXLL;   Node<K,V> *uXXLLL, *uXXLLR;
    void *iuXXLR;   Node<K,V> *uXXLRL, *uXXLRR;
    void *iuXXRL;   Node<K,V> *uXXRLL, *uXXRLR;
    void *iuXXRR;   Node<K,V> *uXXRRL, *uXXRRR;
    void *iuXXLRL;  Node<K,V> *uXXLRLL, *uXXLRLR;
    void *iuXXLRR;  Node<K,V> *uXXLRRL, *uXXLRRR;
    void *iuXXRLL;  Node<K,V> *uXXRLLL, *uXXRLLR;
    void *iuXXRLR;  Node<K,V> *uXXRLRL, *uXXRLRR;

    DEBUG assert(recordmgr->isProtected(tid, u) || u == root);
    if ((iu = LLX(tid, u, &uL, &uR)) == NULL) return false; // return and retry
    bool uXleft = (uX == uL);
    if (!uXleft && uX != uR) return false; // return and retry

    DEBUG assert(recordmgr->isProtected(tid, uX) || uX == root);
    if ((iuX = LLX(tid, uX, &uXL, &uXR)) == NULL) return false; // return and retry
    bool uXXleft = (uXX == uXL);
    if (!uXXleft && uXX != uXR) return false; // return and retry

    DEBUG assert(recordmgr->isProtected(tid, uXX));
    if ((iuXX = LLX(tid, uXX, &uXXL, &uXXR)) == NULL) return false; // return and retry
    bool uXXXleft = (uXXX == uXXL);
    if (!uXXXleft && uXXX != uXXR) return false; // return and retry

    // any further nodes we LLX will have to have protect calls first
    
    /**
     * Overweight violation
     */

    IF_FAIL_TO_PROTECT_NODE(info, tid, uXXL, &uXX->left, &uXX->marked) return false; // return and retry
    if ((iuXXL = LLX(tid, uXXL, &uXXLL, &uXXLR)) == NULL) return false; // return and retry

    IF_FAIL_TO_PROTECT_NODE(info, tid, uXXR, &uXX->right, &uXX->marked) return false; // return and retry
    if ((iuXXR = LLX(tid, uXXR, &uXXRL, &uXXRR)) == NULL) return false; // return and retry

    if (uXXX->weight > 1) {
        if (uXXXleft) {
            /**
             * Rebalance left overweight violation
             */
            if (uXXR->weight == 0) {
                if (uXX->weight == 0) {
                    if (uXXleft) {
                        IF_FAIL_TO_PROTECT_NODE(info, tid, uXR, &uX->right, &uX->marked) return false; // return and retry
                        if ((iuXR = LLX(tid, uXR, &uXRL, &uXRR)) == NULL) return false; // return and retry
                        
                        if (uXR->weight == 0) {
                            void *llxResults[] = {iu, iuX, iuXX, iuXR}; Node<K,V> *nodes[] = {u, uX, uXX, uXR};
                            doBlk(tid, nodes, llxResults, uXleft);
                        } else { // assert: uXR->weight > 0
                            void *llxResults[] = {iu, iuX, iuXX, iuXXR}; Node<K,V> *nodes[] = {u, uX, uXX, uXXR};
                            doRb2(tid, nodes, llxResults, uXleft);
                        }
                    } else { // assert: uXX == uXR
                        IF_FAIL_TO_PROTECT_NODE(info, tid, uXL, &uX->left, &uX->marked) return false; // return and retry
                        if ((iuXL = LLX(tid, uXL, &uXLL, &uXLR)) == NULL) return false; // return and retry
                        
                        if (uXL->weight == 0) {
                            void *llxResults[] = {iu, iuX, iuXL, iuXX}; Node<K,V> *nodes[] = {u, uX, uXL, uXX};
                            doBlk(tid, nodes, llxResults, uXleft);
                        } else {
                            void *llxResults[] = {iu, iuX, iuXX}; Node<K,V> *nodes[] = {u, uX, uXX};
                            doRb1Sym(tid, nodes, llxResults, uXleft);
                        }
                    }
                } else { // assert: uXX->weight > 0
                    IF_FAIL_TO_PROTECT_NODE(info, tid, uXXRL, &uXXR->left, &uXXR->marked) return false; // return and retry
                    if ((iuXXRL = LLX(tid, uXXRL, &uXXRLL, &uXXRLR)) == NULL) return false; // return and retry

                    if (uXXRL->weight > 1) {
                        void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXRL}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXRL};
                        doW1(tid, nodes, llxResults, uXXleft);
                    } else if (uXXRL->weight == 0) {
                        void *llxResults[] = {iuX, iuXX, iuXXR, iuXXRL}; Node<K,V> *nodes[] = {uX, uXX, uXXR, uXXRL};
                        doRb2Sym(tid, nodes, llxResults, uXXleft);
                    } else { // assert: uXXRL->weight == 1
                        if (uXXRLR == NULL) {
                            return false; // return and retry
                        }
                        
                        IF_FAIL_TO_PROTECT_NODE(info, tid, uXXRLR, &uXXRL->right, &uXXRL->marked) return false; // return and retry
                        if ((iuXXRLR = LLX(tid, uXXRLR, &uXXRLRL, &uXXRLRR)) == NULL) return false; // return and retry
                        
                        if (uXXRLR->weight == 0) {
                            void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXRL, iuXXRLR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXRL, uXXRLR};
                            doW4(tid, nodes, llxResults, uXXleft);
                        } else { // assert: uXXRLR->weight > 0
                            if (uXXRLL == NULL) {
                                return false; // return and retry
                            }
                            
                            IF_FAIL_TO_PROTECT_NODE(info, tid, uXXRLL, &uXXRL->left, &uXXRL->marked) return false; // return and retry
                            if ((iuXXRLL = LLX(tid, uXXRLL, &uXXRLLL, &uXXRLLR)) == NULL) return false; // return and retry
                            
                            if (uXXRLL->weight == 0) {
                                void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXRL, iuXXRLL}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXRL, uXXRLL};
                                doW3(tid, nodes, llxResults, uXXleft);
                            } else { // assert: uXXRLL->weight > 0
                                void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXRL}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXRL};
                                doW2(tid, nodes, llxResults, uXXleft);
                            }
                        }
                    }
                }
            } else if (uXXR->weight == 1) {
                if (uXXRL == NULL) {
                    return false; // return and retry
                }
                
                IF_FAIL_TO_PROTECT_NODE(info, tid, uXXRR, &uXXR->right, &uXXR->marked) return false; // return and retry
                if ((iuXXRR = LLX(tid, uXXRR, &uXXRRL, &uXXRRR)) == NULL) return false; // return and retry

                IF_FAIL_TO_PROTECT_NODE(info, tid, uXXRL, &uXXR->left, &uXXR->marked) return false; // return and retry
                if ((iuXXRL = LLX(tid, uXXRL, &uXXRLL, &uXXRLR)) == NULL) return false; // return and retry
                
                if (uXXRR->weight == 0) {
                    void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXRR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXRR};
                    doW5(tid, nodes, llxResults, uXXleft);
                } else if (uXXRL->weight == 0) {
                    void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXRL}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXRL};
                    doW6(tid, nodes, llxResults, uXXleft);
                } else {
                    void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR};
                    doPush(tid, nodes, llxResults, uXXleft);
                }
            } else {
                void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR};
                doW7(tid, nodes, llxResults, uXXleft);
            }
        } else {
            /**
             * Rebalance right overweight violation
             */
            if (uXXL->weight == 0) {
                if (uXX->weight == 0) {
                    if (!uXXleft) {
                        IF_FAIL_TO_PROTECT_NODE(info, tid, uXL, &uX->left, &uX->marked) return false; // return and retry
                        if ((iuXL = LLX(tid, uXL, &uXLL, &uXLR)) == NULL) return false; // return and retry
                        
                        if (uXL->weight == 0) {
                            void *llxResults[] = {iu, iuX, iuXL, iuXX}; Node<K,V> *nodes[] = {u, uX, uXL, uXX};
                            doBlk(tid, nodes, llxResults, uXleft);
                        } else { // assert: uXL->weight > 0
                            void *llxResults[] = {iu, iuX, iuXX, iuXXL}; Node<K,V> *nodes[] = {u, uX, uXX, uXXL};
                            doRb2Sym(tid, nodes, llxResults, uXleft);
                        }
                    } else { // assert: uXX == uXL
                        IF_FAIL_TO_PROTECT_NODE(info, tid, uXR, &uX->right, &uX->marked) return false; // return and retry
                        if ((iuXR = LLX(tid, uXR, &uXRL, &uXRR)) == NULL) return false; // return and retry
                        
                        if (uXR->weight == 0) {
                            void *llxResults[] = {iu, iuX, iuXX, iuXR}; Node<K,V> *nodes[] = {u, uX, uXX, uXR};
                            doBlk(tid, nodes, llxResults, uXleft);
                        } else {
                            void *llxResults[] = {iu, iuX, iuXX}; Node<K,V> *nodes[] = {u, uX, uXX};
                            doRb1(tid, nodes, llxResults, uXleft);
                        }
                    }
                } else { // assert: uXX->weight > 0
                    IF_FAIL_TO_PROTECT_NODE(info, tid, uXXL, &uXX->left, &uXX->marked) return false; // return and retry
                    if ((iuXXL = LLX(tid, uXXL, &uXXLL, &uXXLR)) == NULL) return false; // return and retry

                    IF_FAIL_TO_PROTECT_NODE(info, tid, uXXLR, &uXXL->right, &uXXL->marked) return false; // return and retry
                    if ((iuXXLR = LLX(tid, uXXLR, &uXXLRL, &uXXLRR)) == NULL) return false; // return and retry

                    if (uXXLR->weight > 1) {
                        void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXLR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXLR};
                        doW1Sym(tid, nodes, llxResults, uXXleft);
                    } else if (uXXLR->weight == 0) {
                        void *llxResults[] = {iuX, iuXX, iuXXL, iuXXLR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXLR};
                        doRb2(tid, nodes, llxResults, uXXleft);
                    } else { // assert: uXXLR->weight == 1
                        if (uXXLRL == NULL) {
                            return false; // return and retry
                        }

                        IF_FAIL_TO_PROTECT_NODE(info, tid, uXXLRL, &uXXLR->left, &uXXLR->marked) return false; // return and retry
                        if ((iuXXLRL = LLX(tid, uXXLRL, &uXXLRLL, &uXXLRLR)) == NULL) return false; // return and retry
                        
                        if (uXXLRL->weight == 0) {
                            void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXLR, iuXXLRL}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXLR, uXXLRL};
                            doW4Sym(tid, nodes, llxResults, uXXleft);
                        } else { // assert: uXXLRL->weight > 0
                            if (uXXLRR == NULL) {
                                return false; // return and retry
                            }

                            IF_FAIL_TO_PROTECT_NODE(info, tid, uXXLRR, &uXXLR->right, &uXXLR->marked) return false; // return and retry
                            if ((iuXXLRR = LLX(tid, uXXLRR, &uXXLRRL, &uXXLRRR)) == NULL) return false; // return and retry
                            
                            if (uXXLRR->weight == 0) {
                                void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXLR, iuXXLRR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXLR, uXXLRR};
                                doW3Sym(tid, nodes, llxResults, uXXleft);
                            } else { // assert: uXXLRR->weight > 0
                                void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXLR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXLR};
                                doW2Sym(tid, nodes, llxResults, uXXleft);
                            }
                        }
                    }
                }
            } else if (uXXL->weight == 1) {
                if (uXXLR == NULL) {
                    return false; // return and retry // note: if uXXLL is NULL, then uXXLR is NULL, since tree is always a full binary tree, and children of leaves don't change
                }

                IF_FAIL_TO_PROTECT_NODE(info, tid, uXXLL, &uXXL->left, &uXXL->marked) return false; // return and retry
                if ((iuXXLL = LLX(tid, uXXLL, &uXXLLL, &uXXLLR)) == NULL) return false; // return and retry

                IF_FAIL_TO_PROTECT_NODE(info, tid, uXXLR, &uXXL->right, &uXXL->marked) return false; // return and retry
                if ((iuXXLR = LLX(tid, uXXLR, &uXXLRL, &uXXLRR)) == NULL) return false; // return and retry
                
                if (uXXLL->weight == 0) {
                    void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXLL}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXLL};
                    doW5Sym(tid, nodes, llxResults, uXXleft);
                } else if (uXXLR->weight == 0) {
                    void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR, iuXXLR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR, uXXLR};
                    doW6Sym(tid, nodes, llxResults, uXXleft);
                } else {
                    void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR};
                    doPushSym(tid, nodes, llxResults, uXXleft);
                }
            } else {
                void *llxResults[] = {iuX, iuXX, iuXXL, iuXXR}; Node<K,V> *nodes[] = {uX, uXX, uXXL, uXXR};
                doW7Sym(tid, nodes, llxResults, uXXleft);
            }
        }
    /**
     * Red-red violation
     */
    } else {
        if (uXXleft) {
            /**
             * Rebalance left red-red violation
             */
            IF_FAIL_TO_PROTECT_NODE(info, tid, uXR, &uX->right, &uX->marked) return false; // return and retry
            if ((iuXR = LLX(tid, uXR, &uXRL, &uXRR)) == NULL) return false; // return and retry

            if (uXR->weight == 0) {
                void *llxResults[] = {iu, iuX, iuXX, iuXR}; Node<K,V> *nodes[] = {u, uX, uXX, uXR};
                doBlk(tid, nodes, llxResults, uXleft);
            } else if (uXXXleft) {
                void *llxResults[] = {iu, iuX, iuXX}; Node<K,V> *nodes[] = {u, uX, uXX};
                doRb1(tid, nodes, llxResults, uXleft);
            } else {
                IF_FAIL_TO_PROTECT_NODE(info, tid, uXXR, &uXX->right, &uXX->marked) return false; // return and retry
                if ((iuXXR = LLX(tid, uXXR, &uXXRL, &uXXRR)) == NULL) return false; // return and retry
                
                void *llxResults[] = {iu, iuX, iuXX, iuXXR}; Node<K,V> *nodes[] = {u, uX, uXX, uXXR};
                doRb2(tid, nodes, llxResults, uXleft);
            }
        } else {
            /**
             * Rebalance right red-red violation
             */
            IF_FAIL_TO_PROTECT_NODE(info, tid, uXL, &uX->left, &uX->marked) return false; // return and retry
            if ((iuXL = LLX(tid, uXL, &uXLL, &uXLR)) == NULL) return false; // return and retry

            if (uXL->weight == 0) {
                void *llxResults[] = {iu, iuX, iuXL, iuXX}; Node<K,V> *nodes[] = {u, uX, uXL, uXX};
                doBlk(tid, nodes, llxResults, uXleft);
            } else if (!uXXXleft) {
                void *llxResults[] = {iu, iuX, iuXX}; Node<K,V> *nodes[] = {u, uX, uXX};
                doRb1Sym(tid, nodes, llxResults, uXleft);
            } else {
                IF_FAIL_TO_PROTECT_NODE(info, tid, uXXL, &uXX->left, &uXX->marked) return false; // return and retry
                if ((iuXXL = LLX(tid, uXXL, &uXXLL, &uXXLR)) == NULL) return false; // return and retry
                
                void *llxResults[] = {iu, iuX, iuXX, iuXXL}; Node<K,V> *nodes[] = {u, uX, uXX, uXXL};
                doRb2Sym(tid, nodes, llxResults, uXleft);
            }
        }
    }
    
    return false;
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doBlk(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[2]->key, nodes[2]->value, 1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[3]->key, nodes[3]->value, 1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    int weight = (IS_SENTINEL(nodes[1], nodes[0]) ? 1 : nodes[1]->weight-1); // root of old subtree is a sentinel
    Node<K,V> *nodeX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[1]->key, nodes[1]->value, weight, nodeXL, nodeXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_BLK, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doRb1(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 0, (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed), (Node<K,V>*) nodes[1]->right.load(memory_order_relaxed));
    int weight = nodes[1]->weight;
    Node<K,V> *nodeX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[2]->key, nodes[2]->value, weight, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), nodeXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_RB1, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doRb2(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[2]->key, nodes[2]->value, 0, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed));
    Node<K,V> *nodeXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[1]->key, nodes[1]->value, 0, (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed), (Node<K,V>*) nodes[1]->right.load(memory_order_relaxed));
    int weight = nodes[1]->weight;
    Node<K,V> *nodeX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[3]->key, nodes[3]->value, weight, nodeXL, nodeXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_RB2, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doPush(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[3]->key, nodes[3]->value, 0, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    int weight = (IS_SENTINEL(nodes[1], nodes[0]) ? 1 : nodes[1]->weight+1); // root of old subtree is a sentinel
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[1]->key, nodes[1]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_PUSH, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW1(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXLL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXLR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[4]->key, nodes[4]->value, nodes[4]->weight-1, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 1, nodeXXLL, nodeXXLR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[3]->key, nodes[3]->value, weight, nodeXXL, (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W1, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW2(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXLL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXLR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[4]->key, nodes[4]->value, 0, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 1, nodeXXLL, nodeXXLR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[3]->key, nodes[3]->value, weight, nodeXXL, (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W2, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW3(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXLLL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 4), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXLL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[1]->key, nodes[1]->value, 1, nodeXXLLL, (Node<K,V>*) nodes[5]->left.load(memory_order_relaxed));
    Node<K,V> *nodeXXLR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[4]->key, nodes[4]->value, 1, (Node<K,V>*) nodes[5]->right.load(memory_order_relaxed), (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[5]->key, nodes[5]->value, 0, nodeXXLL, nodeXXLR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[3]->key, nodes[3]->value, weight, nodeXXL, (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W3, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW4(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXLL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 1, nodeXXLL, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed));
    Node<K,V> *nodeXXRL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 4), nodes[5]->key, nodes[5]->value, 1, (Node<K,V>*) nodes[5]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[5]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[3]->key, nodes[3]->value, 0, nodeXXRL, (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[4]->key, nodes[4]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W4, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW5(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXLL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 1, nodeXXLL, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[4]->key, nodes[4]->value, 1, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed));
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[3]->key, nodes[3]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W5, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW6(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXLL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 1, nodeXXLL, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[3]->key, nodes[3]->value, 1, (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[4]->key, nodes[4]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W6, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW7(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    int weight = (IS_SENTINEL(nodes[1], nodes[0]) ? 1 : nodes[1]->weight+1); // root of old subtree is a sentinel
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[1]->key, nodes[1]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W7, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doRb1Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 0, (Node<K,V>*) nodes[1]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed));
    int weight = nodes[1]->weight;
    Node<K,V> *nodeX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[2]->key, nodes[2]->value, weight, nodeXL, (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_RB1SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doRb2Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 0, (Node<K,V>*) nodes[1]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed));
    Node<K,V> *nodeXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[2]->key, nodes[2]->value, 0, (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    int weight = nodes[1]->weight;
    Node<K,V> *nodeX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[3]->key, nodes[3]->value, weight, nodeXL, nodeXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_RB2SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doPushSym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[2]->key, nodes[2]->value, 0, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    int weight = (IS_SENTINEL(nodes[1], nodes[0]) ? 1 : nodes[1]->weight+1); // root of old subtree is a sentinel
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[1]->key, nodes[1]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_PUSHSYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW1Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXRL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[4]->key, nodes[4]->value, nodes[4]->weight-1, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXRR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 1, nodeXXRL, nodeXXRR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[2]->key, nodes[2]->value, weight, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W1SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW2Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXRL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[4]->key, nodes[4]->value, 0, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXRR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[1]->key, nodes[1]->value, 1, nodeXXRL, nodeXXRR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[2]->key, nodes[2]->value, weight, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W2SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW3Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXRL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[4]->key, nodes[4]->value, 1, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[5]->left.load(memory_order_relaxed));
    Node<K,V> *nodeXXRRR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 4), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXRR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[1]->key, nodes[1]->value, 1, (Node<K,V>*) nodes[5]->right.load(memory_order_relaxed), nodeXXRRR);
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[5]->key, nodes[5]->value, 0, nodeXXRL, nodeXXRR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[2]->key, nodes[2]->value, weight, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W3SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW4Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXLR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[5]->key, nodes[5]->value, 1, (Node<K,V>*) nodes[5]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[5]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[2]->key, nodes[2]->value, 0, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), nodeXXLR);
    Node<K,V> *nodeXXRR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 4), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[1]->key, nodes[1]->value, 1, (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed), nodeXXRR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[4]->key, nodes[4]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W4SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW5Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[4]->key, nodes[4]->value, 1, (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXRR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[1]->key, nodes[1]->value, 1, (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed), nodeXXRR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[2]->key, nodes[2]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W5SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW6Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[2]->key, nodes[2]->value, 1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[4]->left.load(memory_order_relaxed));
    Node<K,V> *nodeXXRR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 3), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[1]->key, nodes[1]->value, 1, (Node<K,V>*) nodes[4]->right.load(memory_order_relaxed), nodeXXRR);
    int weight = nodes[1]->weight;
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[4]->key, nodes[4]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W6SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

template<class K, class V, class Compare, class MasterRecordMgr> bool Chromatic<K,V,Compare,MasterRecordMgr>::doW7Sym(const int tid, Node<K,V> **nodes, void **llxResults, bool fieldIsLeft) {
    Node<K,V> *nodeXXL = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), nodes[2]->key, nodes[2]->value, nodes[2]->weight-1, (Node<K,V>*) nodes[2]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[2]->right.load(memory_order_relaxed));
    Node<K,V> *nodeXXR = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 2), nodes[3]->key, nodes[3]->value, nodes[3]->weight-1, (Node<K,V>*) nodes[3]->left.load(memory_order_relaxed), (Node<K,V>*) nodes[3]->right.load(memory_order_relaxed));
    int weight = (IS_SENTINEL(nodes[1], nodes[0]) ? 1 : nodes[1]->weight+1); // root of old subtree is a sentinel
    Node<K,V> *nodeXX = initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), nodes[1]->key, nodes[1]->value, weight, nodeXXL, nodeXXR);
    if (SCXAndEnterQuiescentState(tid, SCXRecord<K,V>::TYPE_W7SYM, nodes, llxResults, (fieldIsLeft ? &nodes[0]->left : &nodes[0]->right), nodeXX)) {
        return true;
    } else {
        return false;
    }
}

/* 2-bit state | 5-bit highest index reached | 24-bit frozen flags for each element of nodes[] on which a freezing CAS was performed = total 31 bits (highest bit unused) */
#define ABORT_STATE_INIT(i, flags) (SCXRecord<K,V>::STATE_ABORTED | ((i)<<2) | ((flags)<<7))
#define STATE_GET_FLAGS(state) ((state) & 0x7FFFFF80)
#define STATE_GET_HIGHEST_INDEX_REACHED(state) (((state) & 0x7C)>>2)
#define STATE_GET_WITH_FLAG_OFF(state, i) ((state) & ~(1<<(i+7)))
//#define STATE_GET_FLAG(state, i) ((state>>(i+7)) & 1)

// this internal function is called only by scx(), and only when otherSCX is protected by a call to recordmgr->protect
template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::tryRetireSCXRecord(const int tid, SCXRecord<K,V> * const otherSCX, Node<K,V> * const node) {
    if (otherSCX == dummy) return false; // never retire the dummy scx record!
    if (otherSCX->state.load(memory_order_relaxed) == SCXRecord<K,V>::STATE_COMMITTED) {
        // in this tree, committed scx records are only pointed to by one node.
        // so, when this function is called, the scx record is already retired.
        recordmgr->retire(tid, otherSCX);
        return true;
    } else { // assert: scx->state >= STATE_ABORTED
        DEBUG assert(otherSCX->state.load(memory_order_relaxed) >= 2); /* state >= aborted */
        // node->scxRecord no longer points to scx, so we set
        // the corresponding bit in scx->state to 0.
        // when state == ABORT_STATE_NO_FLAGS(state), scx is retired.
        const int n = NUM_TO_FREEZE[otherSCX->type];
        Node<K,V> ** const otherNodes = otherSCX->nodes;
        bool casSucceeded = false;
        int stateNew = -1;
        for (int i=0;i<n;++i) {
            if (otherNodes[i] == node) {
                while (!casSucceeded) {
                    int stateOld = otherSCX->state.load(memory_order_relaxed);
                    stateNew = STATE_GET_WITH_FLAG_OFF(stateOld, i);
                    DEBUG DEBUG assert(stateOld >= 2);
                    DEBUG DEBUG assert(stateNew >= 2);
                    DEBUG assert(stateNew < stateOld);
                    casSucceeded = otherSCX->state.compare_exchange_weak(stateOld, stateNew);       // MEMBAR ON X86/64
                }
                break;
            }
        }
        // many scxs can all be CASing state and trying to retire this node.
        // the one who gets to invoke retire() is the one whose CAS sets
        // the flag subfield of scx->state to 0.
        if (casSucceeded && STATE_GET_FLAGS(stateNew) == 0) {
            recordmgr->retire(tid, otherSCX);
            return true;
        }
    }
    return false;
}

// you may call this only in a quiescent state.
// the scx records in scxRecordsSeen must be protected (or we must know no one can have freed them--this is the case in this implementation).
// if this is being called from crash recovery, all nodes in nodes[] and the scx record must be Qprotected.
template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::reclaimMemoryAfterSCX(
            const int tid,
            const int operationType,
            Node<K,V> **nodes,
            SCXRecord<K,V> * const * const scxRecordsSeen,
            const int state) {
    
    // NOW, WE ATTEMPT TO RECLAIM ANY RETIRED NODES AND SCX RECORDS
    // first, we determine how far we got in the loop in help()
    int highestIndexReached = (state == SCXRecord<K,V>::STATE_COMMITTED 
            ? NUM_TO_FREEZE[operationType]
            : STATE_GET_HIGHEST_INDEX_REACHED(state));
    DEBUG assert(highestIndexReached>=0);
    DEBUG assert(highestIndexReached<=MAX_NODES);
    
    SCXRecord<K,V> *debugSCXRecord = allocatedSCXRecord[tid*PREFETCH_SIZE_WORDS];
    
    if (highestIndexReached == 0) {
        DEBUG assert(state == 2); /* aborted but only got to help() loop iteration 0 */
        // scx was never inserted into the data structure,
        // so we can reuse it for our next operation.
        return false; // aborted = failed, so return false
    } else {
        DEBUG assert(highestIndexReached > 0);
        // now that we're in a quiescent state,
        // it's safe to perform non-restartable operations on bookkeeping data structures
        // (since no other thread will force us to restart in a quiescent state).

        // we wrote a pointer to newscxrecord into the data structure,
        // so we cannot reuse it immediately for our next operation.
        // instead, we allocate a new scx record for our next operation.
        DEBUG assert(recordmgr->isQuiescent(tid));
        allocatedSCXRecord[tid*PREFETCH_SIZE_WORDS] = allocateSCXRecord(tid);

        // if the state was COMMITTED, then we cannot reuse the nodes the we
        // took from allocatedNodes[], either, so we must replace these nodes.
        // for the chromatic tree, the number of nodes can be found in
        // NUM_INSERTS[opreationType].
        // in general, we have to add a parameter, specified when you call SCX,
        // that says how large the replacement subtree of new nodes is.
        // alternatively, we could just move this out into the data structure code,
        // to be performed AFTER an scx completes.
        if (state == SCXRecord<K,V>::STATE_COMMITTED) {
            for (int i=0;i<NUM_INSERTED[operationType];++i) {
                REPLACE_ALLOCATED_NODE(tid, i);
            }
        }
        
        // consider the set of scx records for which we will invoke tryRetireSCXRecords,
        // in the following code block.
        // we don't need to call protect object for any of these scx records,
        // because none of them can be retired until we've invoked tryRetireSCXRecord!
        // this is because we changed pointers that pointed to each of these
        // scx records when we performed help(), above.
        // thus, we know they are not retired.
    
        // the scx records in scxRecordsSeen[] may now be retired
        // (since this scx changed each nodes[i]->scxRecord so that it does not
        //  point to any scx record in scxRecordsSeen[].)
        // we start at j=1 because nodes[0] may have been retired and freed
        // since we entered a quiescent state.
        // furthermore, we don't need to check if nodes[0]->left == NULL, since
        // we know nodes[0] is never a leaf.
        for (int j=0;j<highestIndexReached;++j) {
            // if nodes[j] is not a leaf, then we froze it, changing the scx record
            // that nodes[j] points to. so, we try to retire the scx record is
            // no longer pointed to by nodes[j].
            // note: we know scxRecordsSeen[j] is not retired, since we have not
            //       zeroed out its flag representing an incoming pointer
            //       from nodes[j] until we execute tryRetireSCXRecord() below.
            //       (it follows that we don't need to invoke protect().)
            if (scxRecordsSeen[j] != LLX_RETURN_IS_LEAF) {
                bool success = tryRetireSCXRecord(tid, scxRecordsSeen[j], nodes[j]);
                DEBUG2 {
                    // note: tryRetireSCXRecord returns whether it retired an scx record.
                    //       this code checks that we don't retire the same scx record twice!
                    if (success && scxRecordsSeen[j] != dummy) {
                        for (int k=j+1;k<highestIndexReached;++k) {
                            DEBUG assert(scxRecordsSeen[j] != scxRecordsSeen[k]);
                        }
                    }
                }
            }
        }
        SOFTWARE_BARRIER; // prevent compiler from moving retire() calls before tryRetireSCXRecord() calls above
        if (state == SCXRecord<K,V>::STATE_COMMITTED) {
            const int nNodes = NUM_OF_NODES[operationType];
            // nodes[1], nodes[2], ..., nodes[nNodes-1] are now retired
            for (int j=1;j<nNodes;++j) {
                recordmgr->retire(tid, nodes[j]);
            }
            return true; // committed = successful, so return true
        } else {
            DEBUG assert(state >= 2); /* is >= SCXRecord<K,V>::STATE_ABORTED */
            return false;
        }
    }
}

// you may call this only if each node in nodes is protected by a call to recordmgr->protect
template<class K, class V, class Compare, class MasterRecordMgr>
bool Chromatic<K,V,Compare,MasterRecordMgr>::scx(
            const int tid,
            const int operationType,
            Node<K,V> **nodes,
            void **llxResults,
            atomic_uintptr_t *field,        // pointer to a "field pointer" that will be changed
            Node<K,V> *newNode) {
    SCXRecord<K,V> *newscxrecord = allocatedSCXRecord[tid*PREFETCH_SIZE_WORDS];
    initializeSCXRecord(tid, newscxrecord, operationType, nodes, llxResults, field, newNode);
    
    // if this memory reclamation scheme supports crash recovery, it's important
    // that we protect the scx record and its nodes so we can help the scx complete
    // once we've recovered from the crash.
    SOFTWARE_BARRIER;
    if (!recordmgr->protect(tid, newscxrecord, callbackReturnTrue, NULL, false)) {
        DEBUG assert(false); exit(-1);
    }
    // no membar is needed for this protect call,
    // because newscxrecord is not inserted into the data structure
    // (and, hence, cannot be retired),
    // until the first freezing CAS in help().
    // since this freezing CAS implies a membar on x86/64, we don't need
    // one here to make sure newscxrecord is protected before it is retired.
    SOFTWARE_BARRIER;
    int state = help(tid, newscxrecord, false);
    bool result = reclaimMemoryAfterSCX(tid, operationType, nodes, (SCXRecord<K,V> * const * const) llxResults, state);
    recordmgr->qUnprotectAll(tid);
    return result;
}

// you may call this only if scx is protected by a call to recordmgr->protect.
// each node in scx->nodes must be protected by a call to recordmgr->protect.
// returns the state field of the scx record "scx."
template<class K, class V, class Compare, class MasterRecordMgr>
int Chromatic<K,V,Compare,MasterRecordMgr>::help(const int tid, SCXRecord<K,V> *scx, bool helpingOther) {
    DEBUG assert(recordmgr->isProtected(tid, scx));
    DEBUG assert(scx != dummy);
//    bool updateCAS = false;
    const int type                          = scx->type;
    const int nFreeze                       = NUM_TO_FREEZE[type];
    Node<K,V> ** const nodes                = scx->nodes;
    SCXRecord<K,V> ** const scxRecordsSeen  = scx->scxRecordsSeen;
    Node<K,V> * const newNode               = scx->newNode;
    //SOFTWARE_BARRIER; // prevent compiler from reordering read(state) before read(nodes), read(scxRecordsSeen), read(newNode). an x86/64 cpu will not reorder these reads.
    int __state = scx->state.load(memory_order_relaxed);
    if (__state != SCXRecord<K,V>::STATE_INPROGRESS) {
        return __state;
    }
    // note: the above cannot cause us to leak the memory allocated for scx,
    // since, if !helpingOther, then we created the SCX record,
    // and did not write it into the data structure.
    // so, no one could have helped us, and state must be INPROGRESS.
        
    // a note about reclaiming SCX records:
    // IN THEORY, there are exactly three cases in which an SCX record passed
    // to help() is not in the data structure and can be retired.
    //    1. help was invoked directly by SCX, and it failed its first
    //       CAS. in this case the SCX record can be immediately freed.
    //    2. a pointer to an SCX record U with state == COMMITTED is
    //       changed by a CAS to point to a different SCX record.
    //       in this case, the SCX record is retired, but cannot
    //       immediately be freed.
    //     - intuitively, we can retire it because,
    //       after the SCX that created U commits, only the node whose
    //       pointer was changed still points to U. so, when a pointer
    //       that points to U is changed, U is no longer pointed to by
    //       any node in the tree.
    //     - however, a helper or searching process might still have
    //       a local pointer to U, or a local pointer to a
    //       retired node that still points to U.
    //     - so, U can only be freed safely after no process has a
    //       pointer to a retired node that points to U.
    //     - in other words, U can be freed only when all retired nodes
    //       that point to it can be freed.
    //     - if U is retired when case 2 occurs, then it will be retired
    //       AFTER all nodes that point to it are retired. thus, it will
    //       be freed at the same time as, or after, those nodes.
    //    3. a pointer to an SCX record U with state == ABORTED is
    //       changed by a CAS to point to a different SCX record.
    //       this is the hard case, because several nodes in the tree may
    //       point to U.
    //     - in this case, we store the number of pointers from nodes in the
    //       tree to this SCX record in the state field of this SCX record.
    // [NOTE: THE FOLLOWING THREE BULLET POINTS ARE FOR AN OLD IDEA;
    //  THE CURRENT IDEA IS SLIGHTLY DIFFERENT.]    
    //     - when the state of an SCX record becomes STATE_ABORTED, we store
    //       STATE_ABORTED + i in the state field, where i is the number of
    //       incoming pointers from nodes in the tree. (STATE_INPROGRESS and
    //       STATE_COMMITTED are both less than STATE_ABORTED.)
    //     - every time we change a pointer from an SCX record U to another
    //       SCX record U', and U.state > STATE_ABORTED, we decrement U.state.
    //     - if U.state == STATE_ABORTED, then we know there are no incoming
    //       pointers to U from nodes in the tree, so we can retire U.
    //
    // HOWEVER, in practice, we don't freeze leaves for insert and delete,
    // so we have to be careful to deal with a possible memory leak.
    // if some operations (e.g., rebalancing steps) DO freeze leaves, then
    // we can wind up in a situation where a rebalancing step freezes a leaf
    // and is aborted, then a successful insertion or deletion retires
    // that leaf without freezing it. in this scenario, the scx record
    // for the rebalancing step will never be retired, since no further
    // freezing CAS will modify it's scx record pointer (which means it will
    // never trigger case 3, above).
    // there are three (easy) possible fixes for this problem.
    //   1. make sure all operations freeze leaves
    //   2. make sure no operation freezes leaves
    //   3. when retiring a node, if it points to an scx record with
    //      state aborted, then respond as if we were in case 3, above.
    //      (note: since the dummy scx record has state ABORTED,
    //       we have to be a little bit careful; we ignore the dummy.)
    // in this implementation, we choose option 2. this is viable because
    // leaves are immutable, and, hence, do not need to be frozen.
    
    // freeze sub-tree
    int flags = 1; // bit i is 1 if nodes[i] is not a leaf, and 0 otherwise.
    // note that flags bit 0 is always set, since nodes[0] is never a leaf.
    // (technically, if we abort in the first iteration,
    //  flags=1 makes no sense (since it suggests there is one pointer to scx
    //  from a node in the tree), but in this case we ignore the flags variable.)
    for (int i=helpingOther; i<nFreeze; ++i) {
        if (scxRecordsSeen[i] == LLX_RETURN_IS_LEAF) {
            DEBUG assert(i > 0); // nodes[0] cannot be a leaf...
            continue; // do not freeze leaves
        }
        
        uintptr_t exp = (uintptr_t) scxRecordsSeen[i];
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
        bool successfulCAS = nodes[i]->scxRecord.compare_exchange_strong(exp, (uintptr_t) scx);     // MEMBAR ON X86/64
        
        if (!successfulCAS && (SCXRecord<K,V>*) exp != scx) { // if work was not done
            if (scx->allFrozen.load(memory_order_relaxed)) {
                DEBUG assert(scx->state.load(memory_order_relaxed) == 1); /*STATE_COMMITTED*/
                return SCXRecord<K,V>::STATE_COMMITTED; // success
            } else {
                if (i == 0) {
                    // if i == 0, then our scx record was never in the tree, and,
                    // consequently, no one else can have a pointer to it.
                    // so, there is no need to change scx->state.
                    // (recall that helpers start with helpingOther == true,
                    //  so i>0 for every helper. thus, if and only if i==0,
                    //  we created this scx record and failed our first CAS.)
                    DEBUG assert(!helpingOther);
                    return ABORT_STATE_INIT(0, 0); // scx is aborted (but no one else will ever know)
                } else {
                    // if this is the first failed freezing CAS to occur for this SCX,
                    // then flags encodes the pointers to this scx record from nodes IN the tree.
                    // (the following CAS will succeed only the first time it is performed
                    //  by any thread running help() for this scx.)
                    int expectedState = SCXRecord<K,V>::STATE_INPROGRESS;
                    int newState = ABORT_STATE_INIT(i, flags);
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
                    bool success = scx->state.compare_exchange_strong(expectedState, newState);     // MEMBAR ON X86/64
                    DEBUG assert(expectedState != 1); /* not committed */
                    // note2: a regular write will not do, here, since two people can start helping, one can abort at i>0, then after a long time, the other can fail to CAS i=0, so they can get different i values.
                    DEBUG assert(scx->state >= 2); /* SCXRecord<K,V>::STATE_ABORTED */
                    // ABORTED THE SCX AFTER PERFORMING ONE OR MORE SUCCESSFUL FREEZING CASs
                    if (success) {
                        return newState;
                    } else {
                        return expectedState; // this has been overwritten by compare_exchange_strong with the value that caused the CAS to fail.
                    }
                }
            }
        } else {
            flags |= (1<<i); // nodes[i] was frozen for scx
            DEBUG assert((SCXRecord<K,V>*) exp == scx || ((SCXRecord<K,V> *) exp)->state != 0); // not in progress
        }
    }
    scx->allFrozen.store(true, memory_order_relaxed);
    // note: i think the sequential consistency memory model is not actually needed here...
    // why? in an execution where no reads are moved before allFrozen by the
    // compiler/cpu (because we added a barrier here), any process that sees
    // allFrozen = true has also just seen that nodes[i]->op != &op,
    // which means that the operation it is helping has already completed!
    // in particular, the child CAS will already have been done, which implies
    // that allFrozen will have been set to true, since the compiler/cpu cannot
    // move the (first) child CAS before the (first) write to allFrozen.
    SOFTWARE_BARRIER;
    for (int i=1; i<nFreeze; ++i) {
        if (scxRecordsSeen[i] == LLX_RETURN_IS_LEAF) continue; // do not mark leaves
        DEBUG assert(recordmgr->isProtected(tid, scx));
        DEBUG assert(nodes[i] == root || recordmgr->isProtected(tid, nodes[i]));
        nodes[i]->marked.store(true, memory_order_relaxed); // finalize all but first node
    }

    // CAS in the new sub-tree (update CAS)
    uintptr_t expected = (uintptr_t) nodes[1];
    DEBUG assert(nodes[1] == root || recordmgr->isProtected(tid, nodes[1]));
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
    scx->field->compare_exchange_strong(expected, (uintptr_t) newNode);                             // MEMBAR ON X86/64
    DEBUG assert(scx->state.load(memory_order_relaxed) < 2); // not aborted
    scx->state.store(SCXRecord<K,V>::STATE_COMMITTED, memory_order_relaxed);
    
    return SCXRecord<K,V>::STATE_COMMITTED; // success
}

// you may call this only if node is protected by a call to recordmgr->protect
template<class K, class V, class Compare, class MasterRecordMgr>
void *Chromatic<K,V,Compare,MasterRecordMgr>::llx(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight) {
    DEBUG assert(node == root || recordmgr->isProtected(tid, node));
    Chromatic_retired_info info;
    SCXRecord<K,V> *scx1 = (SCXRecord<K,V>*) node->scxRecord.load(memory_order_relaxed);
    IF_FAIL_TO_PROTECT_SCX(info, tid, scx1, &node->scxRecord, &node->marked) {
        return NULL;
    } // return and retry
    DEBUG assert(scx1 == dummy || recordmgr->isProtected(tid, scx1));
    int state = scx1->state.load(memory_order_relaxed);
    SOFTWARE_BARRIER;       // prevent compiler from moving the read of marked before the read of state (no hw barrier needed on x86/64, since there is no read-read reordering)
    bool marked = node->marked.load(memory_order_relaxed);
    SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
    if ((state == SCXRecord<K,V>::STATE_COMMITTED && !marked) || state >= SCXRecord<K,V>::STATE_ABORTED) {
        SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
        *retLeft = (Node<K,V>*) node->left.load(memory_order_relaxed);
        *retRight = (Node<K,V>*) node->right.load(memory_order_relaxed);
        if (*retLeft == NULL) {
            return (void*) LLX_RETURN_IS_LEAF;
        }
        SOFTWARE_BARRIER; // prevent compiler from moving the read of node->scxRecord before the read of left or right
        SCXRecord<K,V> *scx2 = (SCXRecord<K,V>*) node->scxRecord.load(memory_order_relaxed);
        if (scx1 == scx2) {
            // on x86/64, we do not need any memory barrier here to prevent mutable fields of node from being moved before our read of scx1, because the hardware does not perform read-read reordering. on another platform, we would need to ensure no read from after this point is reordered before this point (technically, before the read that becomes scx1)...
            return scx1;    // success
        }
    } else if (state == SCXRecord<K,V>::STATE_INPROGRESS) {
        DEBUG assert(scx1 != dummy);
        DEBUG assert(recordmgr->isProtected(tid, scx1));
        help(tid, scx1, true);
    } else {
        // state committed and marked
        DEBUG assert(state == 1); /* SCXRecord<K,V>::STATE_COMMITTED */
        DEBUG assert(marked);
        SCXRecord<K,V> *scx3 = (SCXRecord<K,V>*) node->scxRecord.load(memory_order_relaxed);
        IF_FAIL_TO_PROTECT_SCX(info, tid, scx3, &node->scxRecord, &node->marked) {
            return NULL;
        } // return and retry
        DEBUG assert(scx3 != dummy);
        DEBUG assert(recordmgr->isProtected(tid, scx3));
        help(tid, scx3, true);
    }
    return NULL;            // fail
}

template <class K, class V, class Compare, class MasterRecordMgr>
unsigned int Chromatic<K,V,Compare,MasterRecordMgr>::calculate_tree_height(Node<K,V>* node) {
    if (node == nullptr) {
        return 0;
    }
    
    Node<K,V>* left_child = (Node<K,V>*) node->left.load(memory_order_relaxed);
    int left_height = calculate_tree_height(left_child);
    Node<K,V>* right_child = (Node<K,V>*) node->right.load(memory_order_relaxed);
    int right_height = calculate_tree_height(right_child);
    
    return 1 + std::max(left_height, right_height);
}

template <class K, class V, class Compare, class MasterRecordMgr>
unsigned int Chromatic<K,V,Compare,MasterRecordMgr>::get_tree_height() {
    if (root == nullptr) return 0;
    Node<K,V>* left_child = (Node<K,V>*) root->left.load(memory_order_relaxed);    
    
    return calculate_tree_height(left_child);
}

template <class K, class V, class Compare, class MasterRecordMgr>
unsigned int Chromatic<K,V,Compare,MasterRecordMgr>::scan_postorder_helper(Node<K,V>* node) {
    if (node == nullptr) {
        return 0;
    }
    
    Node<K,V>* left_child = (Node<K,V>*) node->left.load(memory_order_relaxed);
    unsigned int left_count = scan_postorder_helper(left_child);
    
    Node<K,V>* right_child = (Node<K,V>*) node->right.load(memory_order_relaxed);
    unsigned int right_count = scan_postorder_helper(right_child);
    
    for (size_t offset = 0; offset < sizeof(Node<K,V>); offset += 64) {
        _mm_clflush((char*)node + offset);
    }
    
    return left_count + right_count + 1;
}

template <class K, class V, class Compare, class MasterRecordMgr>
void Chromatic<K,V,Compare,MasterRecordMgr>::scan() {
    unsigned int total_nodes = 0;
    
    if (root != nullptr) {
        Node<K,V>* left_child = (Node<K,V>*) root->left.load(memory_order_relaxed);
        total_nodes = scan_postorder_helper(left_child);
    }
    
    std::cout << "Number of nodes in the priority queue: " << total_nodes << std::endl;
}

#endif	/* CCCHROMATIC_IMPL_H */

