/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * LRU and object timer handling.
 *
 * We have two data structures, a LRU-list and a binary heap for the timers
 * and two ways to kill objects: TTL-timeouts and LRU cleanups.
 *
 * Any object on the LRU is also on the binheap and vice versa.
 *
 * We hold one object reference for both data structures.
 *
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "shmlog.h"
#include "binary_heap.h"
#include "cache.h"
#include "vcl.h"
#include "hash_slinger.h"
#include "stevedore.h"

static pthread_t exp_thread;
static struct binheap *exp_heap;
static struct lock exp_mtx;

/*
 * This is a magic marker for the objects currently on the SIOP [look it up]
 * so that other users of the object will not stumble trying to change the
 * ttl or lru position.
 */
#define BINHEAP_NOIDX 0

/*--------------------------------------------------------------------
 * When & why does the timer fire for this object ?
 */

static int
update_object_when(const struct object *o)
{
	struct objcore *oc;
	double when;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oc = o->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	Lck_AssertHeld(&exp_mtx);

	when = o->ttl + HSH_Grace(o->grace);
	assert(!isnan(when));
	if (when == oc->timer_when)
		return (0);
	oc->timer_when = when;
	return (1);
}

/*--------------------------------------------------------------------
 * Object has been added to cache, record in lru & binheap.
 *
 * We grab a reference to the object, which will keep it around until
 * we decide its time to let it go.
 */

void
EXP_Inject(struct objcore *oc, struct objcore_head *lru, double ttl)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	Lck_Lock(&exp_mtx);
	assert(oc->timer_idx == BINHEAP_NOIDX);
	oc->timer_when = ttl;
	binheap_insert(exp_heap, oc);
	assert(oc->timer_idx != BINHEAP_NOIDX);
	if (lru != NULL) {
		VTAILQ_INSERT_TAIL(lru, oc, lru_list);
		oc->flags |= OC_F_ONLRU;
	}
	Lck_Unlock(&exp_mtx);
}

/*--------------------------------------------------------------------
 * Object has been added to cache, record in lru & binheap.
 *
 * We grab a reference to the object, which will keep it around until
 * we decide its time to let it go.
 */

void
EXP_Insert(struct object *o)
{
	struct objcore *oc;
	struct objcore_head *lru;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	AN(o->objhead);
	AN(ObjIsBusy(o));
	assert(o->cacheable);
	HSH_Ref(o);
	CHECK_OBJ_NOTNULL(o->objcore, OBJCORE_MAGIC);
	oc = o->objcore;

	assert(o->entered != 0 && !isnan(o->entered));
	o->last_lru = o->entered;
	Lck_Lock(&exp_mtx);
	assert(oc->timer_idx == BINHEAP_NOIDX);
	(void)update_object_when(o);
	binheap_insert(exp_heap, oc);
	assert(oc->timer_idx != BINHEAP_NOIDX);
	lru = STV_lru(o->objstore);
	if (lru != NULL) {
		VTAILQ_INSERT_TAIL(lru, oc, lru_list);
		oc->flags |= OC_F_ONLRU;
	}
	Lck_Unlock(&exp_mtx);
	if (o->smp_object != NULL)
		SMP_TTLchanged(o);
}

/*--------------------------------------------------------------------
 * Object was used, move to tail of LRU list.
 *
 * To avoid the exp_mtx becoming a hotspot, we only attempt to move
 * objects if they have not been moved recently and if the lock is available.
 * This optimization obviously leaves the LRU list imperfectly sorted, but
 * that can be worked around by examining obj.last_use in vcl_discard{}
 */

int
EXP_Touch(const struct object *o)
{
	struct objcore *oc;
	int retval = 0;
	struct objcore_head *lru;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oc = o->objcore;
	if (oc == NULL)
		return (retval);
	lru = STV_lru(o->objstore);
	if (lru == NULL)
		return (retval);
	AN(o->objhead);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	if (Lck_Trylock(&exp_mtx))
		return (retval);
	if (oc->flags & OC_F_ONLRU) {
		VTAILQ_REMOVE(lru, oc, lru_list);
		VTAILQ_INSERT_TAIL(lru, oc, lru_list);
		VSL_stats->n_lru_moved++;
		retval = 1;
	}
	Lck_Unlock(&exp_mtx);
	return (retval);
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
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oc = o->objcore;
	if (oc == NULL)
		return;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	Lck_Lock(&exp_mtx);
	/*
	 * The hang-man might have this object of the binheap while
	 * tending to a timer.  If so, we do not muck with it here.
	 */
	if (oc->timer_idx != BINHEAP_NOIDX && update_object_when(o)) {
		/*
		 * XXX: this could possibly be optimized by shuffling
		 * XXX: up or down, but that leaves some very nasty
		 * XXX: corner cases, such as shuffling all the way
		 * XXX: down the left half, then back up the right half.
		 */
		assert(oc->timer_idx != BINHEAP_NOIDX);
		binheap_delete(exp_heap, oc->timer_idx);
		assert(oc->timer_idx == BINHEAP_NOIDX);
		binheap_insert(exp_heap, oc);
		assert(oc->timer_idx != BINHEAP_NOIDX);
	}
	Lck_Unlock(&exp_mtx);
	if (o->smp_object != NULL)
		SMP_TTLchanged(o);
}


/*--------------------------------------------------------------------
 * This thread monitors the root of the binary heap and whenever an
 * object gets close enough, VCL is asked to decide if it should be
 * discarded.
 */

static void *
exp_timer(struct sess *sp, void *priv)
{
	struct objcore *oc;
	struct object *o;
	double t;
	struct objcore_head *lru;

	(void)priv;
	AZ(sleep(10));		/* XXX: Takes time for VCL to arrive */
	VCL_Get(&sp->vcl);
	t = TIM_real();
	while (1) {
		Lck_Lock(&exp_mtx);
		oc = binheap_root(exp_heap);
		CHECK_OBJ_ORNULL(oc, OBJCORE_MAGIC);
		if (oc == NULL || oc->timer_when > t) { /* XXX: > or >= ? */
			Lck_Unlock(&exp_mtx);
			WSL_Flush(sp->wrk, 0);
			WRK_SumStat(sp->wrk);
			AZ(sleep(1));
			VCL_Refresh(&sp->vcl);
			t = TIM_real();
			continue;
		}

		/* It's time... */

		/* Remove from binheap */
		assert(oc->timer_idx != BINHEAP_NOIDX);
		binheap_delete(exp_heap, oc->timer_idx);
		assert(oc->timer_idx == BINHEAP_NOIDX);

		/* And from LRU */
		if (oc->flags & OC_F_ONLRU) {
			assert(!(oc->flags & OC_F_PERSISTENT));
			lru = STV_lru(o->objstore);
			AN(lru);
			VTAILQ_REMOVE(lru, o->objcore, lru_list);
			oc->flags &= ~OC_F_ONLRU;
		} else {
			assert(oc->flags & OC_F_PERSISTENT);
		}

		VSL_stats->n_expired++;

		Lck_Unlock(&exp_mtx);

		if (!(oc->flags & OC_F_PERSISTENT)) {
			o = oc->obj;
			CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
			HSH_Deref(sp->wrk, &o);
			CHECK_OBJ_NOTNULL(o->objhead, OBJHEAD_MAGIC);
		}
		WSL(sp->wrk, SLT_ExpKill, 0, "%u %d",
		    o->xid, (int)(o->ttl - t));
	}
}

/*--------------------------------------------------------------------
 * Attempt to make space by nuking, the oldest object on the LRU list
 * which isn't in use.
 * Returns: 1: did, 0: didn't, -1: can't
 */

int
EXP_NukeOne(struct sess *sp, struct objcore_head *lru)
{
	struct objcore *oc;

	/*
	 * Find the first currently unused object on the LRU.
	 *
	 * Ideally we would have the refcnt in the objcore so we object does
	 * not need to get paged in for this check, but it does not pay off
	 * the complexity:  The chances of an object being in front of the LRU,
	 * with active references, likely means that it is already in core. An
	 * object with no active references will be prodded further anyway.
	 *
	 */
	Lck_Lock(&exp_mtx);
	VTAILQ_FOREACH(oc, lru, lru_list) {
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		if (oc->timer_idx == BINHEAP_NOIDX)	/* exp_timer has it */
			continue;
		if (oc->obj->refcnt == 1)
			break;
	}
	if (oc != NULL) {
		VTAILQ_REMOVE(lru, oc, lru_list);
		oc->flags &= ~OC_F_ONLRU;
		binheap_delete(exp_heap, oc->timer_idx);
		assert(oc->timer_idx == BINHEAP_NOIDX);
		VSL_stats->n_lru_nuked++;
	}
	Lck_Unlock(&exp_mtx);

	if (oc == NULL)
		return (-1);

	WSL(sp->wrk, SLT_ExpKill, 0, "%u LRU", oc->obj->xid);
	HSH_Deref(sp->wrk, &(oc->obj));
	return (1);
}

/*--------------------------------------------------------------------
 * BinHeap helper functions for objcore.
 */

static int
object_cmp(void *priv, void *a, void *b)
{
	struct objcore *aa, *bb;

	(void)priv;
	CAST_OBJ_NOTNULL(aa, a, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(bb, b, OBJCORE_MAGIC);
	return (aa->timer_when < bb->timer_when);
}

static void
object_update(void *priv, void *p, unsigned u)
{
	struct objcore *oc;

	(void)priv;
	CAST_OBJ_NOTNULL(oc, p, OBJCORE_MAGIC);
	oc->timer_idx = u;
}

/*--------------------------------------------------------------------*/

void
EXP_Init(void)
{

	Lck_New(&exp_mtx);
	exp_heap = binheap_new(NULL, object_cmp, object_update);
	XXXAN(exp_heap);
	WRK_BgThread(&exp_thread, "cache-timeout", exp_timer, NULL);
}
