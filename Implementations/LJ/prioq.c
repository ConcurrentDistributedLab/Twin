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
#include <stdio.h>
#include <string.h>
#include <x86intrin.h>

#include "../../Library/Allocator/BumpAllocator.hpp"

/* interface, constant defines, and typedefs */
#include "prioq.h"

#include "../../src/core/globals.h"

#ifndef DELETE_RATE
#define DELETE_RATE 8
#endif

__thread unsigned int _prand = 0;
BumpAllocator<node_s> *alloc;

/*********** optimisation variables ***********************/
uint32_t physical_delete_rate = DELETE_RATE;

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
  n->inserting = 1;
  memset(n->next, 0, level * sizeof(node_t *));
  return n;
}

/* No-op: node lifetime owned by bump allocator (Fraser GC not used here). */
static void free_node(node_t *n) {
  (void)n;
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

static node_t *locate_preds(pq_t *pq, pkey_t k, node_t **preds,
                            node_t **succs) {
  node_t *x, *x_next, *del = NULL;
  int d = 0, i;
#ifdef COMPUTE_STATS
  locate_calls += 1;
#endif

  x = pq->head;
  i = NUM_LEVELS - 1;
  while (i >= 0) {
    x_next = x->next[i];
    d = is_marked_ref(x_next);
    x_next = (node_t *)get_unmarked_ref(x_next);
    assert(x_next != NULL);

    while (x_next->k < k || is_marked_ref(x_next->next[0]) || ((i == 0) && d)) {
      /* Record bottom level deleted node not having delete flag
       * set, if traversed. */
      if (i == 0 && d)
        del = x_next;
      x = x_next;
      x_next = x->next[i];
      d = is_marked_ref(x_next);
      x_next = (node_t *)get_unmarked_ref(x_next);
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

void initThread(int tid) {
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
int insert(pq_t *pq, pkey_t k, pval_t v, int tid) {
  node_t *preds[NUM_LEVELS], *succs[NUM_LEVELS];
  node_t *new_node = NULL, *del = NULL;
  int i = 1;
  int ins = 0;

  assert(SENTINEL_KEYMIN < k && k < SENTINEL_KEYMAX);

  /* Initialise a new node for insertion. */
  new_node = alloc_node(tid);
  new_node->k = k;
  new_node->v = v;

  /* lowest level insertion retry loop */
#ifdef COMPUTE_STATS
totalIns += 1;
#endif
retry:
  ins = 0;
#ifdef COMPUTE_STATS
    numInsRetries += 1;
#endif
  del = locate_preds(pq, k, preds, succs);

  /* return if key already exists, i.e., is present in a non-deleted
   * node */
  if (succs[0]->k == k && !is_marked_ref(preds[0]->next[0]) &&
      preds[0]->next[0] == succs[0]) {
    new_node->inserting = 0;
    free_node(new_node);
    goto insert_out;
  }
  ins = 1;
  new_node->next[0] = succs[0];

  /* The node is logically inserted once it is present at the bottom
   * level. */
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
  if (!__sync_bool_compare_and_swap(&preds[0]->next[0], succs[0], new_node)) {
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
    if (is_marked_ref(new_node->next[0]) || is_marked_ref(succs[i]->next[0]) ||
        del == succs[i])
      goto success;

    /* prepare next pointer of new_node node */
    new_node->next[i] = succs[i];
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
    if (!__sync_bool_compare_and_swap(&preds[i]->next[i], succs[i], new_node)) {
      #ifdef COMPUTE_STATS
          numInsRetries += 1;
      #endif
      /* failed due to competing insert or restructure */
      del = locate_preds(pq, k, preds, succs);

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
    new_node->inserting = 0;
  }

insert_out:
  return ins;
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
static void restructure(pq_t *pq) {
  node_t *pred, *cur, *h;
  int i = NUM_LEVELS - 1;
#ifdef COMPUTE_STATS
    totalReb += 1;
#endif

  pred = pq->head;
  while (i > 0) {
    /* the order of these reads must be maintained */
    h = pq->head->next[i]; /* record observed head */
    CMB();
    cur = pred->next[i]; /* take one step forward from pred */
    if (!is_marked_ref(h->next[0])) {
      i--;
      continue;
    }
    /* traverse level until non-marked node is found
     * pred will always have its delete flag set
     */
    while (is_marked_ref(cur->next[0])) {
      pred = cur;
      cur = pred->next[i];
    #ifdef COMPUTE_AVG
      pointers_reb_traversed += 1;
    #endif
    }
    assert(is_marked_ref(pred->next[0]));

    /* swing head pointer */
#ifdef COMPUTE_ATOMIC
    atomic_count += 1;
#endif	
    if (__sync_bool_compare_and_swap(&pq->head->next[i], h, cur))
      i--;
  }
}

/* deletemin
 *
 * Delete element with smallest key in queue.
 * Try to update the head node's pointers, if offset > max_offset.
 *
 * Traverse level 0 next pointers until one is found that does
 * not have the delete bit set.
 */
pval_t deleteMin(pq_t *pq, int tid) {
  pval_t v = NULL;
  node_t *x, *nxt, *obs_head = NULL, *newhead, *cur;
  int offset, lvl;
#ifdef COMPUTE_STATS
  	totalDel += 1;
#endif

  newhead = NULL;
  offset = lvl = 0;

  x = pq->head;
  obs_head = x->next[0];
#ifdef COMPUTE_AVG
    pointers_traversed += 1;
#endif

  do {
    offset++;

    /* expensive, high probability that this cache line has
     * been modified */
    nxt = x->next[0];
    /* Tail cannot be deleted. */
    if ((node_t *)get_unmarked_ref(nxt) == pq->tail) {
      goto min_out;
    }

    /* optimization */
    if (!is_marked_ref(nxt)) {
      /* the marker is on the preceding pointer */
      /* linearisation point deletemin */
#ifdef COMPUTE_ATOMIC
      atomic_count += 1;
#endif	

      nxt = __sync_fetch_and_or(&x->next[0], 1);

#ifdef COMPUTE_STATS
          numDelRetries += 1;
#endif
    }

#ifdef COMPUTE_AVG
    pointers_traversed += 1;
#endif
  } while ((x = (node_t *)get_unmarked_ref(nxt)) && is_marked_ref(nxt));

  assert(!is_marked_ref(x));

  v = x->v;

  /* Candidate for new bottom-level head-pointed node. */
  newhead = x;

  /* if the offset is big enough, try to update the head node and
   * perform memory reclamation */
  if (offset > physical_delete_rate) {
    /* Optimization. Marginally faster */
    if (pq->head->next[0] == obs_head) {
      /* try to swing the lowest level head pointer to point to newhead,
      * which is deleted */
    #ifdef COMPUTE_ATOMIC
        atomic_count += 1;
    #endif	
      if (__sync_bool_compare_and_swap(&pq->head->next[0], obs_head,
                                      get_marked_ref(newhead))) {
        /* Update higher level pointers. */
        restructure(pq);
      }
    }
  }
min_out:
  return v;
}

/*
 * Init structure, setup sentinel head and tail nodes.
 */
pq_t *pq_init(int max_offset, int maxThreads) {
  pq_t *pq;
  node_t *t, *h;
  int i;

  /* head and tail nodes */
  t = (node_t *)calloc(1, sizeof *t + (NUM_LEVELS - 1) * sizeof(node_t *));
  h = (node_t *)calloc(1, sizeof *h + (NUM_LEVELS - 1) * sizeof(node_t *));

  t->inserting = 0;
  h->inserting = 0;

  t->k = SENTINEL_KEYMAX;
  h->k = SENTINEL_KEYMIN;
  t->v = NULL;
  h->v = NULL;
  h->level = NUM_LEVELS;
  t->level = NUM_LEVELS;

  for (i = 0; i < NUM_LEVELS; i++)
    h->next[i] = t;

  pq = (pq_t *)malloc(sizeof *pq);
  pq->head = h;
  pq->tail = t;
  pq->max_offset = maxThreads * 100;

  alloc = new BumpAllocator<node_s>(maxThreads);

  return pq;
}

/* Cleanup, mark all the nodes for recycling. */
void pq_destroy(pq_t *pq) {
  node_t *cur, *pred;
  cur = pq->head;
  while (cur != pq->tail) {
    pred = cur;
    cur = (node_t *)get_unmarked_ref(pred->next[0]);
    free_node(pred);
  }
  free(pq->tail);
  free(pq->head);
  free(pq);
}

pval_t search(pq_t *pq, pkey_t k, int tid) {
  node_t *x, *x_next, *xm;
  int d = 0, i, j;
  x = pq->head;
  i = NUM_LEVELS - 1;
  while (i >= 0) {
    x_next = x->next[i];
    d = is_marked_ref(x_next);
    x_next = (node_t *)get_unmarked_ref(x_next);
    assert(x_next != NULL);

    while (x_next->k <= k || is_marked_ref(x_next->next[0]) ||
           ((i == 0) && d)) {

      if (x_next->k == k && !is_marked_ref(x_next->next[0])) {
        j = i - 1;
        while (j >= 0) {
          while (x->next[j] != x_next) {
            xm = x->next[j];
            x = (node_t *)get_unmarked_ref(xm);
          }
          j--;
        }
        if (!is_marked_ref(x->next[0])) {
          return x_next->v;
        }
      }

      x = x_next;
      x_next = x->next[i];
      d = is_marked_ref(x_next);
      x_next = (node_t *)get_unmarked_ref(x_next);
      assert(x_next != NULL);
    }

    i--;
  }

  return 0;
}

/* Benchmark helper: level-0 physical distances between active nodes. */
void print_skiplist_exact_distances(pq_t *pq) {
    if (!pq || !pq->head) return;

    unsigned long long last_pos[NUM_LEVELS] = {0};
    unsigned long long first_dist[NUM_LEVELS] = {0};
    unsigned long long sum_dist[NUM_LEVELS] = {0};
    unsigned long long link_count[NUM_LEVELS] = {0};
    int has_first[NUM_LEVELS] = {0};

    /* Head is position 0; every node (including logically deleted) advances pos. */
    node_t *curr = (node_t *)get_unmarked_ref(pq->head->next[0]);
    unsigned long long pos = 0;

    while (curr != pq->tail && curr != NULL) {
        pos++;

        if (!is_marked_ref(curr->next[0])) {
            for (int i = 0; i < curr->level; i++) {
                if (!has_first[i]) {
                    first_dist[i] = pos;
                    has_first[i] = 1;
                    link_count[i]++;
                } else {
                    sum_dist[i] += (pos - last_pos[i]);
                    link_count[i]++;
                }
                last_pos[i] = pos;
            }
        }
        curr = (node_t *)get_unmarked_ref(curr->next[0]);
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
            printf(" %2d   | %-16llu | %-19.2f | %llu\n",
                   i, first_dist[i], avg_dist, link_count[i]);
        }
    }
    printf("--------------------------------------------------------------\n");
}

/* Benchmark helper: walk level-0 and clflush each node (not part of queue API). */
void scan(pq_t *pq) {
    if (pq == NULL || pq->head == NULL) {
        return;
    }

    volatile node_t *curr = pq->head->next[0];

    unsigned int i = 0;
    while (curr != NULL && curr != pq->tail) {
      volatile node_t *next_node = curr->next[0];

      for (size_t offset = 0; offset < sizeof(node_t); offset += 64) {
          _mm_clflush((char*)curr + offset);
      }

      curr = next_node;
      i += 1;
    }
    printf("Number of nodes scanned: %u\n", i);
}