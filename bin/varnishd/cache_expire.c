/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 *
 * Expiry of cached objects and execution of prefetcher
 *
 * XXX: Objects can linger on deathrow as long as a slow client
 * XXX: tickles data away from it.  With many slow clients this could
 * XXX: possibly make deathrow very long and make the hangman waste
 * XXX: time.  The solution is to have another queue for such "pending
 * XXX: cases" and have HSH_Deref() move them to deathrow when they
 * XXX: are ready.
 */

#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shmlog.h"
#include "binary_heap.h"
#include "cache.h"

/*
 * Objects have sideways references in the binary heap and the LRU list
 * and we want to avoid paging in a lot of objects just to move them up
 * or down the binheap or to move a unrelated object on the LRU list.
 * To avoid this we use a proxy object, objexp, to hold the relevant 
 * housekeeping fields parts of an object.
 */

enum e_objtimer {
	TIMER_TTL,
	TIMER_PREFETCH
};

struct objexp {
	unsigned		magic;
#define OBJEXP_MAGIC		0x4d301302
	struct object		*obj;
	double			timer_when;
	enum e_objtimer		timer_what;
	unsigned		timer_idx;
	VTAILQ_ENTRY(objexp)	list;
	double			lru_stamp;
};

static pthread_t exp_thread;
static struct binheap *exp_heap;
static MTX exp_mtx;
static VTAILQ_HEAD(,objexp) deathrow = VTAILQ_HEAD_INITIALIZER(deathrow);
static VTAILQ_HEAD(,objexp) lru = VTAILQ_HEAD_INITIALIZER(lru);

/*
 * This is a magic marker for the objects currently on the SIOP [look it up]
 * so that other users of the object will not stumble trying to change the
 * ttl or lru position.
 */
#define BINHEAP_NOIDX 0
static const unsigned lru_target = (unsigned)(-3);

/*--------------------------------------------------------------------
 * Add and Remove objexp's from objects.
 */

static void
add_objexp(struct object *o)
{

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	AZ(o->objexp);
	assert(o->busy);
	assert(o->cacheable);
	o->objexp = calloc(sizeof *o->objexp, 1);
	AN(o->objexp);
	o->objexp->magic = OBJEXP_MAGIC;
	o->objexp->timer_idx = BINHEAP_NOIDX;
}

static void
del_objexp(struct object *o)
{
	struct objexp *oe;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oe = o->objexp;
	o->objexp = NULL;
	CHECK_OBJ_NOTNULL(oe, OBJEXP_MAGIC);
	assert(oe->timer_idx == BINHEAP_NOIDX);
	free(oe);
}

/*--------------------------------------------------------------------
 * When & why does the timer fire for this object ?
 */

static void
update_object_when(const struct object *o)
{
	struct objexp *oe;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oe = o->objexp;
	CHECK_OBJ_NOTNULL(oe, OBJEXP_MAGIC);

	if (o->prefetch < 0.0) {
		oe->timer_when = o->ttl + o->prefetch;
		oe->timer_what = TIMER_PREFETCH;
	} else if (o->prefetch > 0.0) {
		assert(o->prefetch <= o->ttl);
		oe->timer_when = o->prefetch;
		oe->timer_what = TIMER_PREFETCH;
	} else {
		oe->timer_when = o->ttl + o->grace;
		oe->timer_what = TIMER_TTL;
	}
}

/*--------------------------------------------------------------------
 * Object has been added to cache, record in lru & binheap.
 *
 * We grab a reference to the object, which will keep it around until
 * we decide its time to let it go.
 */

void
EXP_Insert(struct object *o, double now)
{

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	assert(o->busy);
	assert(o->cacheable);
	HSH_Ref(o);
	add_objexp(o);
	o->objexp->lru_stamp = now;
	update_object_when(o);
	LOCK(&exp_mtx);
	binheap_insert(exp_heap, o->objexp);
	VTAILQ_INSERT_TAIL(&lru, o->objexp, list);
	UNLOCK(&exp_mtx);
}

/*--------------------------------------------------------------------
 * Object was used, move to tail of LRU list.
 *
 * To avoid the exp_mtx becoming a hotspot, we only attempt to move
 * objects if they have not been moved recently and if the lock is available.
 * This optimization obviously leaves the LRU list imperfectly sorted, but
 * that can be worked around by examining obj.last_use in vcl_discard{}
 */

void
EXP_Touch(const struct object *o, double now)
{
	int i;
	struct objexp *oe;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oe = o->objexp;
	CHECK_OBJ_NOTNULL(oe, OBJEXP_MAGIC);
	if (oe->lru_stamp + params->lru_timeout > now)
		return;
	TRYLOCK(&exp_mtx, i);
	if (i)
		return;
	assert(oe->timer_idx != BINHEAP_NOIDX);
	if (oe->timer_idx != lru_target) {
		VTAILQ_REMOVE(&lru, oe, list);
		VTAILQ_INSERT_TAIL(&lru, oe, list);
		oe->lru_stamp = now;
		VSL_stats->n_lru_moved++;
	}
	UNLOCK(&exp_mtx);
}

/*--------------------------------------------------------------------
 * We have changed one or more of the object timers, shuffle it 
 * accordingly in the binheap
 *
 * The VCL code can send us here on a non-cached object, just return.
 *
 * XXX: special case check for ttl = 0 ?
 */

void
EXP_Rearm(const struct object *o)
{
	struct objexp *oe;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oe = o->objexp;
	if (oe == NULL)
		return;
	CHECK_OBJ_NOTNULL(oe, OBJEXP_MAGIC);
	update_object_when(o);
	LOCK(&exp_mtx);
	if (oe->timer_idx != lru_target) {
		assert(oe->timer_idx != BINHEAP_NOIDX);
		binheap_delete(exp_heap, oe->timer_idx);
		binheap_insert(exp_heap, oe);
	}
	UNLOCK(&exp_mtx);
}

/*--------------------------------------------------------------------
 * This thread monitors deathrow and kills objects when they time out.
 */

static void *
exp_hangman(void *arg)
{
	struct objexp *oe;
	struct object *o;
	double t;

	THR_Name("cache-hangman");
	(void)arg;

	t = TIM_real();
	while (1) {
		LOCK(&exp_mtx);
		VTAILQ_FOREACH(oe, &deathrow, list) {
			CHECK_OBJ(oe, OBJEXP_MAGIC);
			o = oe->obj;
			CHECK_OBJ(o, OBJECT_MAGIC);
			if (o->ttl >= t) {
				oe = NULL;
				break;
			}
			if (o->busy) {
				VSL(SLT_Debug, 0,
				    "Grim Reaper: Busy object xid %u", o->xid);
				continue;
			}
			if (o->refcnt == 1)
				break;
		}
		if (oe == NULL) {
			UNLOCK(&exp_mtx);
			AZ(sleep(1));
			t = TIM_real();
			continue;
		}
		VTAILQ_REMOVE(&deathrow, oe, list);
		VSL_stats->n_deathrow--;
		VSL_stats->n_expired++;
		UNLOCK(&exp_mtx);
		o = oe->obj;
		VSL(SLT_ExpKill, 0, "%u %d", o->xid, (int)(o->ttl - t));
		del_objexp(o);
		HSH_Deref(o);
	}
}

/*--------------------------------------------------------------------
 * This thread monitors the root of the binary heap and whenever an
 * object gets close enough, VCL is asked to decide if it should be
 * discarded or prefetched.
 * If discarded, the object is put on deathrow where exp_hangman() will
 * do what needs to be done.
 * XXX: If prefetched pass to the pool for pickup.
 */

static void *
exp_prefetch(void *arg)
{
	struct worker ww;
	struct objexp *oe, *oe2;
	struct object *o;
	double t;
	struct sess *sp;
	unsigned char log[1024];		/* XXX size ? */

	THR_Name("cache-timeout");
	(void)arg;

	sp = SES_New(NULL, 0);
	XXXAN(sp);
	sp->wrk = &ww;
	ww.magic = WORKER_MAGIC;
	ww.wlp = ww.wlb = log;
	ww.wle = log + sizeof log;

	AZ(sleep(10));		/* XXX: Takes time for VCL to arrive */
	VCL_Get(&sp->vcl);
	t = TIM_real();
	while (1) {
		LOCK(&exp_mtx);
		oe = binheap_root(exp_heap);
		CHECK_OBJ_ORNULL(oe, OBJEXP_MAGIC);
		if (oe == NULL || oe->timer_when > t) {	/* XXX: >= ? */
			UNLOCK(&exp_mtx);
			WSL_Flush(&ww);
			AZ(sleep(1));
			VCL_Refresh(&sp->vcl);
			t = TIM_real();
			continue;
		}
		binheap_delete(exp_heap, oe->timer_idx);
		assert(oe->timer_idx == BINHEAP_NOIDX);

		/* Sanity check */
		oe2 = binheap_root(exp_heap);
		if (oe2 != NULL)
			assert(oe2->timer_when >= oe->timer_when);

		UNLOCK(&exp_mtx);

		WSL(&ww, SLT_ExpPick, 0, "%u %s", oe->obj->xid,
		    oe->timer_what == TIMER_PREFETCH ? "prefetch" : "ttl");

		o = oe->obj;
		if (oe->timer_what == TIMER_PREFETCH) {
			o->prefetch = 0.0;
			update_object_when(o);
			LOCK(&exp_mtx);
			binheap_insert(exp_heap, o);
			UNLOCK(&exp_mtx);
			sp->obj = o;
			VCL_prefetch_method(sp);
			if (sp->handling == VCL_RET_FETCH) {
				WSL(&ww, SLT_Debug, 0, "Attempt Prefetch %u",
				    o->xid);
			}
		} else { /* TIMER_TTL */
			sp->obj = o;
			VCL_timeout_method(sp);

			assert(sp->handling == VCL_RET_DISCARD);
			LOCK(&exp_mtx);
			VTAILQ_REMOVE(&lru, o->objexp, list);
			VTAILQ_INSERT_TAIL(&deathrow, o->objexp, list);
			VSL_stats->n_deathrow++;
			UNLOCK(&exp_mtx);
		}
	}
}

/*--------------------------------------------------------------------
 * BinHeap helper functions for objexp.
 */

static int
object_cmp(void *priv, void *a, void *b)
{
	struct objexp *aa, *bb;

	(void)priv;
	CAST_OBJ_NOTNULL(aa, a, OBJEXP_MAGIC);
	CAST_OBJ_NOTNULL(bb, b, OBJEXP_MAGIC);
	return (aa->timer_when < bb->timer_when);
}

static void
object_update(void *priv, void *p, unsigned u)
{
	struct objexp *o;

	(void)priv;
	CAST_OBJ_NOTNULL(o, p, OBJEXP_MAGIC);
	o->timer_idx = u;
}

/*--------------------------------------------------------------------
 * Attempt to make space by nuking, with VCLs permission, the oldest
 * object on the LRU list which isn't in use.
 * Returns: 1: did, 0: didn't, -1: can't
 */

int
EXP_NukeOne(struct sess *sp)
{
	struct objexp *oe;
	struct object *o2;

	/*
	 * Find the first currently unused object on the LRU.
	 *
	 * Ideally we would have the refcnt in the objexp so we the object does
	 * not need to get paged in for this check, but it does not pay off
	 * the complexity:  The chances of an object being in front of the LRU,
	 * with active references, likely means that it is already in core. An
	 * object with no active references will be prodded further anyway.
	 *
	 * NB: Checking refcount here is no guarantee that it does not gain 
	 * another ref while we ponder its destiny without the lock held.
	 */
	LOCK(&exp_mtx);
	VTAILQ_FOREACH(oe, &lru, list)
		if (oe->obj->refcnt == 1)
			break;
	if (oe != NULL) {
		/*
		 * Take it off the binheap and LRU while we chew, so we can
		 * release the lock while we ask VCL.
		 */
		VTAILQ_REMOVE(&lru, oe, list);
		binheap_delete(exp_heap, oe->timer_idx);
		assert(oe->timer_idx == BINHEAP_NOIDX);
		oe->timer_idx = lru_target;	/* Magic marker */
		VSL_stats->n_lru_nuked++; 	/* Fixed up below if false */
	}
	UNLOCK(&exp_mtx);

	if (oe == NULL)
		return (-1);

	/*
	 * Ask VCL in the context of the clients session, in order to allow
	 * client QoS considerations to inform the decision. Temporarily
	 * substitute the object we want to nuke for the sessions own object.
	 */
	o2 = sp->obj;
	sp->obj = oe->obj;
	VCL_discard_method(sp);
	sp->obj = o2;
	o2 = oe->obj;

	if (sp->handling == VCL_RET_DISCARD) {
		VSL(SLT_ExpKill, 0, "%u LRU", o2->xid);
		del_objexp(o2);
		HSH_Deref(o2);
		return (1);
	}

	assert(sp->handling == VCL_RET_KEEP);

	/* Insert in binheap and lru again */
	oe->timer_idx = BINHEAP_NOIDX;
	oe->lru_stamp = sp->wrk->used;
	LOCK(&exp_mtx);
	VSL_stats->n_lru_nuked--; 		/* It was premature */
	VSL_stats->n_lru_saved++;
	binheap_insert(exp_heap, oe);
	VTAILQ_INSERT_TAIL(&lru, oe, list);
	UNLOCK(&exp_mtx);
	return (0);
}

/*--------------------------------------------------------------------*/

void
EXP_Init(void)
{

	MTX_INIT(&exp_mtx);
	exp_heap = binheap_new(NULL, object_cmp, object_update);
	XXXAN(exp_heap);
	AZ(pthread_create(&exp_thread, NULL, exp_prefetch, NULL));
	AZ(pthread_create(&exp_thread, NULL, exp_hangman, NULL));
}
