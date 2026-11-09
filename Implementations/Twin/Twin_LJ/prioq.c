/*************************************************************************
 * prioq.c
 *
 * Lock-free concurrent priority queue.
 *
 * Copyright (c) 2012-2014, Jonatan Linden
 *
 * Adapted from Keir Fraser's skiplist,
 * Copyright (c) 2001-2003, Keir Fraser
 *
 * Keir Fraser's skiplist is available at
 * http://www.cl.cam.ac.uk/research/srg/netos/lock-free/.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <x86intrin.h>

#include "../../../Library/Allocator/BumpAllocator.hpp"

/* interface, constant defines, and typedefs */
#include "prioq.h"

#include "../../../src/core/globals.h"

typedef long int (*CombinerFunctionPointer)(void*, long int, int);
typedef void (*RestructureFunctionPointer)(void*, long int, int);

__thread unsigned int _prand = 0;
BumpAllocator<node_s> *alloc;

/* initialize new node */
static node_t *alloc_node(int tid) {
  node_t *n;
  /* XORSHIFT32 */
  unsigned int r = _prand;
  r ^= r << 13;
  r ^= r >> 17;
  r ^= r << 5;
  _prand = r;

  /* Stop bit at NUM_LEVELS-1: r never 0 (__builtin_ctz UB), level never exceeds NUM_LEVELS. */
  r |= (1u << (NUM_LEVELS - 1));

  /* uniformly distributed bits => geom. dist. level, p = 0.5 */
  int level = __builtin_ctz(r) + 1;
  assert(1 <= level && level <= 32);

  n = (node_t *)alloc->allocate(tid);
  n->level = level;
  n->reserved = 0;
  n->inserting[0] = 1;
  n->inserting[1] = 1;
  return n;
}

/* No-op: node lifetime owned by bump allocator. */
static void free_node(volatile node_t *n) {
  (void)n;
}

static int before(pkey_t k1, pkey_t k2, int type) {
    if (type == MIN) return k1 < k2;
    else return k2 < k1;
}

/***** locate_preds *****
 * Record predecessors and non-deleted successors of key k.  If k is
 * encountered during traversal of list, the node will be in succs[0].
 *
 * To detect skew in insert operation, return a pointer to the only
 * deleted node not having it's delete flag set.
 *
 * Skew example illustration, when locating 3. Level 1 is shifted in
 * relation to level 0, due to not noticing that s[1] is deleted until
 * level 0 is reached. (pointers in illustration are implicit, e.g.,
 * 0 --> 7 at level 2.)
 *
 *                   del
 *                   p[0]
 * p[2]  p[1]        s[1]  s[0]  s[2]
 *  |     |           |     |     |
 *  v     |           |     |     v
 *  _     v           v     |     _
 * | |    _           _	    v    | |
 * | |   | |    _    | |    _    | |
 * | |   | |   | |   | |   | |   | |
 *  0     1     2     4     6     7
 *  d     d     d
 *
 */

static volatile node_t *locate_preds(pq_t *pq, pkey_t k, volatile node_t **preds,
                            volatile node_t **succs, int type) {
  volatile node_t *x, *x_next, *del = NULL;
  int d = 0, i;
#ifdef COMPUTE_STATS
  locate_calls += 1;
#endif

  x = pq->head[type];
  i = NUM_LEVELS - 1;
  while (i >= 0) {
    x_next = x->next[type][i];
    d = is_marked_ref(x_next);
    x_next = (volatile node_t *)get_unmarked_ref(x_next);
    assert(x_next != NULL);

    while (before(x_next->k, k, type) || is_marked_ref(x_next->next[type][0]) || ((i == 0) && d)) {
      /* Record bottom level deleted node not having delete flag
       * set, if traversed. */
      if (i == 0 && d)
        del = x_next;
      x = x_next;
      x_next = x->next[type][i];
      d = is_marked_ref(x_next);
      x_next = (volatile node_t *)get_unmarked_ref(x_next);
      assert(x_next != NULL);
      #ifdef COMPUTE_AVG
        pointers_ins_traversed += 1;
      #endif	
    }
    preds[i] = x;
    succs[i] = x_next;
    i--;
  }
  return del;
}

void initThread(pq_t *pq, CCPriorityQThreadState_s *lobject_struct, int tid) {  
    CCSynchThreadStateInit(&pq->Combiner[MIN], \
                                &lobject_struct->delete_thread_state[MIN], tid);
    CCSynchThreadStateInit(&pq->Combiner[MAX], \
                                &lobject_struct->delete_thread_state[MAX], tid);
                                
    unsigned int seed = (unsigned int)(tid + 1) ^ (unsigned int)__rdtsc();
    seed ^= seed >> 16;
    seed *= 0x85ebca6b;
    seed ^= seed >> 13;
    seed *= 0xc2b2ae35;
    seed ^= seed >> 16;
    
    _prand = seed;
}

/***** insert *****
 * Insert a new node n with key k and value v.
 * The node will not be inserted if another node with key k is already
 * present in the list.
 *
 * The predecessors, preds, and successors, succs, at all levels are
 * recorded, after which the node n is inserted from bottom to
 * top. Conditioned on that succs[i] is still the successor of
 * preds[i], n will be spliced in on level i.
 */
static int half_insert(pq_t *pq, volatile node_t *new_node, int type, int tid) {
  volatile node_t *preds[NUM_LEVELS], *succs[NUM_LEVELS];
  volatile node_t *del = NULL;
  pkey_t k = new_node->k;
  int i = 1;
  int ins = 0;
  /* lowest level insertion retry loop */

retry:
  ins = 0;
#ifdef COMPUTE_STATS
    numInsRetries += 1;
#endif
  del = locate_preds(pq, k, preds, succs, type);

  /* return if key already exists, i.e., is present in a non-deleted
   * node */
  if (succs[0]->k == k && !is_marked_ref(preds[0]->next[type][0]) &&
      preds[0]->next[type][0] == succs[0]) {
    new_node->inserting[type] = 0;
    free_node(new_node);
    goto insert_out;
  }
  ins = 1;
  new_node->next[type][0] = succs[0];

  /* The node is logically inserted once it is present at the bottom
   * level. */
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
  if (!__sync_bool_compare_and_swap(&preds[0]->next[type][0], succs[0], new_node)) {
    /* either succ has been deleted (modifying preds[0]),
     * or another insert has succeeded or preds[0] is head,
     * and a restructure operation has updated it */
    goto retry;
  }

  /* Insert at each of the other levels in turn. */
  i = 1;
  while (i < new_node->level) {
    /* If successor of new_node is deleted, we're done. (We're done if
     * only new_node is deleted as well, but this we can't tell) If a
     * candidate successor at any level is deleted, we consider
     * the operation completed. */
    if (is_marked_ref(new_node->next[type][0]) || is_marked_ref(succs[i]->next[type][0]) ||
        del == succs[i])
      goto success;

    /* prepare next pointer of new_node node */
    new_node->next[type][i] = succs[i];
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
    if (!__sync_bool_compare_and_swap(&preds[i]->next[type][i], succs[i], new_node)) {
      #ifdef COMPUTE_STATS
          numInsRetries += 1;
      #endif
      /* failed due to competing insert or restructure */
      del = locate_preds(pq, k, preds, succs, type);

      /* if new_node has been deleted, we're done */
      if (succs[0] != new_node)
        goto success;

    } else {
      /* Succeeded at this level. */
      i++;
    }
  }

success:
  if (new_node) {
    /* this flag must be reset *after* all CAS have completed */
    new_node->inserting[type] = 0;
  }

insert_out:
  return ins;
}

int insert(pq_t *pq, pkey_t k, pval_t v, int tid) {
  volatile node_t *new_node = NULL;

  assert(SENTINEL_KEYMIN < k && k < SENTINEL_KEYMAX);

  /* Initialise a new node for insertion. */
  new_node = alloc_node(tid);
  new_node->k = k;
  new_node->v = v;
#ifdef COMPUTE_STATS
  totalIns += 1;
#endif

#ifndef CCLINDEN
  return half_insert(pq, new_node, MIN, tid) && half_insert(pq, new_node, MAX, tid);
#else
  return half_insert(pq, new_node, MIN, tid);
#endif
}

/***** restructure *****
 *
 * Update the head node's pointers from level 1 and up. Will locate
 * the last node at each level that has the delete flag set, and set
 * the head to point to the successor of that node. After completion,
 * if operating in isolation, for each level i, it holds that
 * head->next[i-1] is before or equal to head->next[i].
 *
 * Illustration valid state after completion:
 *
 *             h[0]  h[1]  h[2]
 *              |     |     |
 *              |     |     v
 *  _           |     v     _
 * | |    _     v     _	   | |
 * | |   | |    _    | |   | |
 * | |   | |   | |   | |   | |
 *  d     d
 *
 */
static void restructure(pq_t *pq, int type) {
  volatile node_t *pred, *cur, *h;
  volatile node_t *head = pq->head[type];
  volatile node_t *lastDeleted = pq->LastDeleted[type];
  int i = NUM_LEVELS - 1;
#ifdef COMPUTE_STATS
    totalReb += 1;
#endif

  if (lastDeleted == head || \
       lastDeleted == get_unmarked_ref(head->next[type][0])) 
            return;

  head->next[type][0] = (volatile node_t*) get_marked_ref(lastDeleted);

  pred = head;
  while (i > 0) {
    /* the order of these reads must be maintained */
    h = head->next[type][i]; /* record observed head */
    CMB();
    cur = pred->next[type][i]; /* take one step forward from pred */
    if (!is_marked_ref(h->next[type][0])) {
      i--;
      continue;
    }
    /* traverse level until non-marked node is found
     * pred will always have its delete flag set
     */
    while (is_marked_ref(cur->next[type][0])) {
      pred = cur;
      cur = pred->next[type][i];
    #ifdef COMPUTE_AVG
      pointers_reb_traversed += 1;
    #endif
    }
    assert(is_marked_ref(pred->next[type][0]));

    /* swing head pointer */
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
    if (__sync_bool_compare_and_swap(&head->next[type][i], h, cur))
      i--;
  }
  
}


static pval_t combinerDelete(pq_t*  pq, ArgVal type, int pid) {
    pval_t value = (pval_t)0;
    volatile node_t* lastDeleted = (volatile node_t*)0;
    volatile node_t* tail = (volatile node_t*)0;
    int wasReserved;

    lastDeleted = pq->LastDeleted[type];
    tail = pq->head[1 - type];
    wasReserved = 1;
  #ifdef COMPUTE_STATS
    totalDel += 1;
  #endif
    while (wasReserved == 1) {
        if (lastDeleted->next[type][0] == tail) {
            value = (pval_t)0;
            break;
        }
        else {
            /* Mark next pointer, then claim the node via reserved. */
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
            lastDeleted = (volatile node_t *) __sync_fetch_and_or(&lastDeleted->next[type][0], 1);
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
            wasReserved = __sync_fetch_and_or(&lastDeleted->reserved, 1);
  #ifdef COMPUTE_STATS
            numDelRetries += 1;
  #endif
  #ifdef COMPUTE_AVG
        pointers_traversed += 1;
  #endif
            value = lastDeleted->v;
        }
    }

    pq->LastDeleted[type] = (volatile node_t*) lastDeleted;

    return value;
}

pval_t deleteMin(pq_t*  priorq, CCPriorityQThreadState_s*  lobject_struct, int pid) {
    return (pval_t) CCSynchApply2Op(&priorq->Combiner[MIN], &lobject_struct->delete_thread_state[MIN], (CombinerFunctionPointer) combinerDelete, priorq, (RestructureFunctionPointer) restructure, priorq, (ArgVal)MIN, pid);
}


pval_t deleteMax(pq_t*  priorq, CCPriorityQThreadState_s*  lobject_struct, int pid) {
#ifndef CCLINDEN
    return (pval_t) CCSynchApply2Op(&priorq->Combiner[MAX], &lobject_struct->delete_thread_state[MAX], (CombinerFunctionPointer) combinerDelete, priorq, (RestructureFunctionPointer) restructure, priorq, (ArgVal)MAX, pid);
#else
#warning "Delete-max operations are closed."
    return (pval_t) 0;
#endif
}

/*
 * Init structure, setup sentinel head and tail nodes.
 */
pq_t *pq_init(int max_offset, int maxThreads) {
  pq_t *pq;
  node_t *t, *h;
  int i;

  pq = (pq_t *)malloc(sizeof *pq);
  alloc = new BumpAllocator<node_s>(maxThreads);

  /* head and tail nodes */
  t = alloc->allocate(0);
  h = alloc->allocate(0);

  assert(t != NULL && h != NULL);

  t->inserting[MIN] = 0;
  t->inserting[MAX] = 0;
  h->inserting[MIN] = 0;
  h->inserting[MAX] = 0;

  t->k = SENTINEL_KEYMAX;
  h->k = SENTINEL_KEYMIN;
  t->v = NULL;
  h->v = NULL;
  h->level = NUM_LEVELS;
  t->level = NUM_LEVELS;

  for (i = 0; i < NUM_LEVELS; i++){
    h->next[MIN][i] = t;
    t->next[MAX][i] = h;
  }

  pq->head[MIN] = h;
  pq->head[MAX] = t;
  pq->max_offset = max_offset;

  pq->LastDeleted[MIN] = h;
  pq->LastDeleted[MAX] = t;
  CCSynchStructInit(&pq->Combiner[MIN], maxThreads);
  CCSynchStructInit(&pq->Combiner[MAX], maxThreads);

  return pq;
}

/*
 * Calculates the exact physical distances at level 0.
 */
void print_type_skiplist_exact_distances(pq_t *pq, int type) {
    if (!pq || !pq->head[type]) return;

    unsigned long long last_pos[NUM_LEVELS] = {0};
    unsigned long long first_dist[NUM_LEVELS] = {0};
    unsigned long long sum_dist[NUM_LEVELS] = {0};
    unsigned long long link_count[NUM_LEVELS] = {0};
    int has_first[NUM_LEVELS] = {0};

    // Start from the 1st node after the head (head is considered pos = 0)
    node_t *curr = (node_t *)get_unmarked_ref(pq->head[type]->next[type][0]);
    unsigned long long pos = 0; 

    while (curr != pq->head[1-type] && curr != NULL) {
        pos++; // Each node (even logically deleted ones) counts as 1 physical step
        
        // We are interested in the distances between ACTIVE nodes
        if (!is_marked_ref(curr->next[type][0])) {
            for (int i = 0; i < curr->level; i++) {
                if (!has_first[i]) {
                    // a) Distance from the head to the 1st node of the level
                    first_dist[i] = pos; 
                    has_first[i] = 1;
                    link_count[i]++;
                } else {
                    // b) Distance between nodes at the same level
                    sum_dist[i] += (pos - last_pos[i]);
                    link_count[i]++;
                }
                // Store the current position as the "last known" for this level
                last_pos[i] = pos;
            }
        }
        curr = (node_t *)get_unmarked_ref(curr->next[type][0]);
    }

    printf("\n--- Exact Node Distances (Physical Level-0 Steps) ---\n");
    printf("Level | Head to 1st Node | Avg Inter-Node Dist | Total Links\n");
    printf("--------------------------------------------------------------\n");

    for (int i = 0; i < NUM_LEVELS; i++) {
        if (!has_first[i]) {
            printf(" %2d   | %-16s | %-19s | %llu\n", i, "Only Tail Exists", "-", 0ULL);
        } else {
            double avg_dist = 0.0;
            if (link_count[i] > 0) {
                avg_dist = (double)sum_dist[i] / link_count[i];
            }
            // Using %llu for unsigned long long variables
            printf(" %2d   | %-16llu | %-19.2f | %llu\n", 
                   i, first_dist[i], avg_dist, link_count[i]);
        }
    }
    printf("--------------------------------------------------------------\n");
}

void print_skiplist_exact_distances(pq_t *pq) {
    print_type_skiplist_exact_distances(pq, MIN);
    print_type_skiplist_exact_distances(pq, MAX);
}

/* Benchmark helper: walk level-0 MIN chain and clflush each node (not part of queue API). */
void scan(pq_t *pq) {
    if (pq == NULL || pq->head[MIN] == NULL) {
        return;
    }

    volatile node_t *curr = pq->head[MIN]->next[MIN][0];
    
    unsigned int i = 0;
    while (curr != NULL && curr != pq->head[MAX]) {
      volatile node_t *next_node = curr->next[MIN][0];
      
      for (size_t offset = 0; offset < sizeof(node_t); offset += 64) {
          _mm_clflush((char*)curr + offset);
      }
          
      curr = next_node;
      i += 1;
    }
    printf("Number of nodes in the priority queue: %u\n", i);
}