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
 * LRU and object timer handling.
 *
 * We have two data structures, a LRU-list and a binary heap for the timers
 * and two ways to kill objects: TTL-timeouts and LRU cleanups.
 *
 * To avoid holding the mutex while we ponder the fate of objects, we
 * have the following prototol:
 *
 * Any object on the LRU is also on the binheap (normal case)
 *
 * An object is taken off only the binheap but left on the LRU during timer
 * processing because we have no easy way to put it back the right place
 * in the LRU list.
 *
 * An object is taken off both LRU and binheap for LRU processing, (which
 * implies that it must be on both, from where it follows that the timer
 * is not chewing on it) because we expect the majority of objects to be
 * discarded by LRU and save a lock cycle that way, and because we can
 * properly replace it's position in the binheap.
 */

#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

static const char *tmr_prefetch	= "prefetch";
static const char *tmr_ttl	= "ttl";

struct objexp {
	unsigned		magic;
#define OBJEXP_MAGIC		0x4d301302
	struct object		*obj;
	double			timer_when;
	const char		*timer_what;
	unsigned		timer_idx;
	VTAILQ_ENTRY(objexp)	list;
	int			on_lru;
	double			lru_stamp;
};

static pthread_t exp_thread;
static struct binheap *exp_heap;
static MTX exp_mtx;
static VTAILQ_HEAD(,objexp) lru = VTAILQ_HEAD_INITIALIZER(lru);

/*
 * This is a magic marker for the objects currently on the SIOP [look it up]
 * so that other users of the object will not stumble trying to change the
 * ttl or lru position.
 */
#define BINHEAP_NOIDX 0

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
	o->objexp->obj = o;
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
		oe->timer_what = tmr_prefetch;
	} else if (o->prefetch > 0.0) {
		assert(o->prefetch <= o->ttl);
		oe->timer_when = o->prefetch;
		oe->timer_what = tmr_prefetch;
	} else {
		oe->timer_when = o->ttl + HSH_Grace(o->grace);
		oe->timer_what = tmr_ttl;
	}
	assert(!isnan(oe->timer_when));
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
	struct objexp *oe;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	assert(o->busy);
	assert(o->cacheable);
	HSH_Ref(o);
	add_objexp(o);
	oe = o->objexp;

	oe->lru_stamp = now;
	update_object_when(o);
	LOCK(&exp_mtx);
	binheap_insert(exp_heap, oe);
	assert(oe->timer_idx != BINHEAP_NOIDX);
	VTAILQ_INSERT_TAIL(&lru, oe, list);
	oe->on_lru = 1;
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
	if (oe == NULL)
		return;
	CHECK_OBJ_NOTNULL(oe, OBJEXP_MAGIC);
	if (oe->lru_stamp + params->lru_timeout > now)
		return;
	TRYLOCK(&exp_mtx, i);
	if (i)
		return;
	if (oe->on_lru) {
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
	assert(oe->timer_idx != BINHEAP_NOIDX);
	binheap_delete(exp_heap, oe->timer_idx); /* XXX: binheap_shuffle() ? */
	assert(oe->timer_idx == BINHEAP_NOIDX);
	binheap_insert(exp_heap, oe);
	assert(oe->timer_idx != BINHEAP_NOIDX);
	UNLOCK(&exp_mtx);
}


/*--------------------------------------------------------------------
 * This thread monitors the root of the binary heap and whenever an
 * object gets close enough, VCL is asked to decide if it should be
 * discarded or prefetched.
 */

static void *
exp_timer(void *arg)
{
	struct worker ww;
	struct objexp *oe;
	struct object *o;
	double t;
	struct sess *sp;
	unsigned char logbuf[1024];		/* XXX size ? */

	THR_SetName("cache-timeout");
	(void)arg;

	sp = SES_New(NULL, 0);
	XXXAN(sp);
	sp->wrk = &ww;
	ww.magic = WORKER_MAGIC;
	ww.wlp = ww.wlb = logbuf;
	ww.wle = logbuf + sizeof logbuf;

	AZ(sleep(10));		/* XXX: Takes time for VCL to arrive */
	VCL_Get(&sp->vcl);
	t = TIM_real();
	while (1) {
		LOCK(&exp_mtx);
		oe = binheap_root(exp_heap);
		CHECK_OBJ_ORNULL(oe, OBJEXP_MAGIC);
		if (oe == NULL || oe->timer_when > t) { /* XXX: > or >= ? */
			UNLOCK(&exp_mtx);
			WSL_Flush(&ww, 0);
			AZ(sleep(1));
			VCL_Refresh(&sp->vcl);
			t = TIM_real();
			continue;
		}
	
		o = oe->obj;
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		assert(oe->timer_idx != BINHEAP_NOIDX);
		binheap_delete(exp_heap, oe->timer_idx);
		assert(oe->timer_idx == BINHEAP_NOIDX);

		{	/* Sanity checking */
			struct objexp *oe2 = binheap_root(exp_heap);
			if (oe2 != NULL) {
				assert(oe2->timer_idx != BINHEAP_NOIDX);
				assert(oe2->timer_when >= oe->timer_when);
			}
		}

		assert(oe->on_lru);
		UNLOCK(&exp_mtx);

		WSL(&ww, SLT_ExpPick, 0, "%u %s", o->xid, oe->timer_what);

		if (oe->timer_what == tmr_prefetch) {
			o->prefetch = 0.0;
			sp->obj = o;
			VCL_prefetch_method(sp);
			sp->obj = NULL;
			if (sp->handling == VCL_RET_FETCH) {
				WSL(&ww, SLT_Debug, 0, "Attempt Prefetch %u",
				    o->xid);
			}
			update_object_when(o);
			LOCK(&exp_mtx);
			binheap_insert(exp_heap, oe);
			assert(oe->timer_idx != BINHEAP_NOIDX);
			UNLOCK(&exp_mtx);
		} else {
			assert(oe->timer_what == tmr_ttl);
			sp->obj = o;
			VCL_timeout_method(sp);
			sp->obj = NULL;

			assert(sp->handling == VCL_RET_DISCARD);
			WSL(&ww, SLT_ExpKill, 0,
			    "%u %d", o->xid, (int)(o->ttl - t));
			LOCK(&exp_mtx);
			VTAILQ_REMOVE(&lru, o->objexp, list);
			oe->on_lru = 0;
			VSL_stats->n_expired++;
			UNLOCK(&exp_mtx);
			del_objexp(o);
			HSH_Deref(o);
		}
	}
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
	struct object *o;

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
	VTAILQ_FOREACH(oe, &lru, list) {
		CHECK_OBJ_NOTNULL(oe, OBJEXP_MAGIC);
		if (oe->timer_idx == BINHEAP_NOIDX)	/* exp_timer has it */
			continue;
		if (oe->obj->refcnt == 1)
			break;
	}
	if (oe != NULL) {
		/*
		 * We hazzard the guess that the object is more likely to
		 * be tossed than kept, and forge ahead on the work to save
		 * a lock cycle.  If the object is kept, we reverse these
		 * actions below.
		 */
		VTAILQ_REMOVE(&lru, oe, list);
		oe->on_lru = 0;
		binheap_delete(exp_heap, oe->timer_idx);
		assert(oe->timer_idx == BINHEAP_NOIDX);
		VSL_stats->n_lru_nuked++;
	}
	UNLOCK(&exp_mtx);

	if (oe == NULL)
		return (-1);

	/*
	 * Ask VCL in the context of the clients session, in order to allow
	 * client QoS considerations to inform the decision. Temporarily
	 * substitute the object we want to nuke for the sessions own object.
	 */
	o = sp->obj;
	sp->obj = oe->obj;
	VCL_discard_method(sp);
	sp->obj = o;
	o = oe->obj;

	if (sp->handling == VCL_RET_DISCARD) {
		WSL(sp->wrk, SLT_ExpKill, 0, "%u LRU", o->xid);
		del_objexp(o);
		HSH_Deref(o);
		return (1);
	}

	assert(sp->handling == VCL_RET_KEEP);

	/* Insert in binheap and lru again */
	LOCK(&exp_mtx);
	VSL_stats->n_lru_nuked--; 		/* It was premature */
	VSL_stats->n_lru_saved++;
	binheap_insert(exp_heap, oe);
	assert(oe->timer_idx != BINHEAP_NOIDX);
	VTAILQ_INSERT_TAIL(&lru, oe, list);
	oe->on_lru = 1;
	UNLOCK(&exp_mtx);
	return (0);
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
	struct objexp *oe;

	(void)priv;
	CAST_OBJ_NOTNULL(oe, p, OBJEXP_MAGIC);
	oe->timer_idx = u;
}

/*--------------------------------------------------------------------*/

void
EXP_Init(void)
{

	MTX_INIT(&exp_mtx);
	exp_heap = binheap_new(NULL, object_cmp, object_update);
	XXXAN(exp_heap);
	AZ(pthread_create(&exp_thread, NULL, exp_timer, NULL));
}
