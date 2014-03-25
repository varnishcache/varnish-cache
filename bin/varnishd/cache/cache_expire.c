/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>

#include "cache.h"

#include "binary_heap.h"
#include "hash/hash_slinger.h"
#include "vtim.h"

struct exp_priv {
	unsigned			magic;
#define EXP_PRIV_MAGIC			0x9db22482
	struct lock			mtx;

	struct worker			*wrk;
	struct vsl_log			vsl;

	VTAILQ_HEAD(,objcore)		inbox;
	struct binheap			*heap;
	pthread_cond_t			condvar;
};

static struct exp_priv *exphdl;

/*--------------------------------------------------------------------
 * struct exp manipulations
 */

void
EXP_Clr(struct exp *e)
{

	e->ttl = -1;
	e->grace = 0;
	e->keep = 0;
	e->t_origin = 0;
}

/*--------------------------------------------------------------------
 * Calculate an objects effective ttl time, taking req.ttl into account
 * if it is available.
 */

double
EXP_Ttl(const struct req *req, const struct object *o)
{
	double r;

	r = o->exp.ttl;
	if (req != NULL && req->d_ttl > 0. && req->d_ttl < r)
		r = req->d_ttl;
	return (o->exp.t_origin + r);
}

/*--------------------------------------------------------------------
 * Calculate when we should wake up for this object
 */

static double
exp_when(const struct object *o)
{
	double when;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

	when = o->exp.t_origin + o->exp.ttl + o->exp.grace + o->exp.keep;
	assert(!isnan(when));
	return (when);
}

/*--------------------------------------------------------------------
 * Post an objcore to the exp_thread's inbox.
 */

static void
exp_mail_it(struct objcore *oc)
{
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AN(oc->exp_flags & OC_EF_OFFLRU);
	Lck_Lock(&exphdl->mtx);
	if (oc->exp_flags & OC_EF_DYING)
		VTAILQ_INSERT_HEAD(&exphdl->inbox, oc, lru_list);
	else
		VTAILQ_INSERT_TAIL(&exphdl->inbox, oc, lru_list);
	VSC_C_main->exp_mailed++;
	AZ(pthread_cond_signal(&exphdl->condvar));
	Lck_Unlock(&exphdl->mtx);
}

/*--------------------------------------------------------------------
 * Inject an object with a reference into the lru/binheap.
 *
 * This can either come from a stevedore (persistent) during startup
 * or from EXP_Insert() below.
 */

void
EXP_Inject(struct objcore *oc, struct lru *lru, double when)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AZ(oc->exp_flags & (OC_EF_OFFLRU | OC_EF_INSERT | OC_EF_MOVE));
	AZ(oc->exp_flags & OC_EF_DYING);
	// AN(oc->flags & OC_F_BUSY);

	if (lru == NULL)
		lru = oc_getlru(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	Lck_Lock(&lru->mtx);
	lru->n_objcore++;
	oc->exp_flags |= OC_EF_OFFLRU | OC_EF_INSERT | OC_EF_EXP;
	if (when < 0)
		oc->exp_flags |= OC_EF_MOVE;
	else
		oc->timer_when = when;
	Lck_Unlock(&lru->mtx);

	exp_mail_it(oc);
}

/*--------------------------------------------------------------------
 * Insert new object.
 *
 * We grab a reference to the object, which will keep it around until
 * we decide its time to let it go.
 */

void
EXP_Insert(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	HSH_Ref(oc);
	EXP_Inject(oc, NULL, -1);
}

/*--------------------------------------------------------------------
 * Object was used, move to tail of LRU list.
 *
 * To avoid the exphdl->mtx becoming a hotspot, we only attempt to move
 * objects if they have not been moved recently and if the lock is available.
 * This optimization obviously leaves the LRU list imperfectly sorted.
 */

void
EXP_Touch(struct objcore *oc, double now)
{
	struct lru *lru;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (oc->busyobj != NULL)
		return;

	if (now - oc->last_lru < cache_param->lru_interval)
		return;

	lru = oc_getlru(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	/*
	 * For -spersistent (and possibly other stevedores, we don't move
	 * objects on the lru list, since LRU doesn't really help much.
	 */
	if (lru->flags & LRU_F_DONTMOVE)
		return;

	if (Lck_Trylock(&lru->mtx))
		return;

	AN(oc->exp_flags & OC_EF_EXP);

	if (!(oc->exp_flags & OC_EF_OFFLRU)) {
		/* Can only touch it while it's actually on the LRU list */
		VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
		VTAILQ_INSERT_TAIL(&lru->lru_head, oc, lru_list);
		VSC_C_main->n_lru_moved++;
	}
	oc->last_lru = now;
	Lck_Unlock(&lru->mtx);
}

/*--------------------------------------------------------------------
 * We have changed one or more of the object timers, tell the exp_thread
 *
 */

void
EXP_Rearm(struct object *o, double now, double ttl, double grace, double keep)
{
	struct objcore *oc;
	struct lru *lru;
	double when;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oc = o->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);

	AN(oc->exp_flags & OC_EF_EXP);

	if (!isnan(ttl))
		o->exp.ttl = now + ttl - o->exp.t_origin;
	if (!isnan(grace))
		o->exp.grace = grace;
	if (!isnan(keep))
		o->exp.keep = keep;

	when = exp_when(o);

	VSL(SLT_ExpKill, 0, "EXP_Rearm p=%p E=%.9f e=%.9f f=0x%x", oc,
	    oc->timer_when, when, oc->flags);

	if (when > o->exp.t_origin && when > oc->timer_when)
		return;

	lru = oc_getlru(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	Lck_Lock(&lru->mtx);

	if (!isnan(now) && when <= now)
		oc->exp_flags |= OC_EF_DYING;
	else
		oc->exp_flags |= OC_EF_MOVE;

	if (oc->exp_flags & OC_EF_OFFLRU) {
		oc = NULL;
	} else {
		oc->exp_flags |= OC_EF_OFFLRU;
		VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
	}
	Lck_Unlock(&lru->mtx);

	if (oc != NULL)
		exp_mail_it(oc);
}

/*--------------------------------------------------------------------
 * Attempt to make space by nuking the oldest object on the LRU list
 * which isn't in use.
 * Returns: 1: did, 0: didn't, -1: can't
 */

int
EXP_NukeOne(struct busyobj *bo, struct lru *lru)
{
	struct objcore *oc, *oc2;
	struct objhead *oh;
	struct object *o;

	/* Find the first currently unused object on the LRU.  */
	Lck_Lock(&lru->mtx);
	VTAILQ_FOREACH_SAFE(oc, &lru->lru_head, lru_list, oc2) {
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

		VSLb(bo->vsl, SLT_ExpKill, "LRU_Cand p=%p f=0x%x r=%d",
		    oc, oc->flags, oc->refcnt);

		AZ(oc->exp_flags & OC_EF_OFFLRU);
		AZ(oc->exp_flags & OC_EF_DYING);

		/*
		 * It wont release any space if we cannot release the last
		 * reference, besides, if somebody else has a reference,
		 * it's a bad idea to nuke this object anyway.
		 */
		if (oc->refcnt > 1)
			continue;
		oh = oc->objhead;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		if (Lck_Trylock(&oh->mtx))
			continue;
		if (oc->refcnt == 1) {
			oc->exp_flags |= OC_EF_DYING | OC_EF_OFFLRU;
			oc->refcnt++;
			VSC_C_main->n_lru_nuked++; // XXX per lru ?
			VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
		} else {
			oc = NULL;
		}
		Lck_Unlock(&oh->mtx);
		if (oc != NULL)
			break;
	}
	Lck_Unlock(&lru->mtx);

	if (oc == NULL) {
		VSLb(bo->vsl, SLT_ExpKill, "LRU_Fail");
		return (-1);
	}

	/* XXX: We could grab and return one storage segment to our caller */
	o = oc_getobj(bo->stats, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	STV_Freestore(o);

	exp_mail_it(oc);

	VSLb(bo->vsl, SLT_ExpKill, "LRU x=%u",
	    oc_getxid(bo->stats, oc) & VSL_IDENTMASK);
	AN(bo->stats);
	AN(oc);
	(void)HSH_DerefObjCore(bo->stats, &oc);
	return (1);
}

/*--------------------------------------------------------------------
 * Nukes an entire LRU
 */

#if 0		// Not yet

#define NUKEBUF 10	/* XXX: Randomly chosen to be bigger than one */

void
EXP_NukeLRU(struct worker *wrk, struct vsl_log *vsl, struct lru *lru)
{
	struct objcore *oc;
	struct objcore *oc_array[NUKEBUF];
	struct object *o;
	int i, n;
	double t;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	memset(oc_array, 0, sizeof oc_array);

	t = VTIM_real();
	Lck_Lock(&lru->mtx);
	while (!VTAILQ_EMPTY(&lru->lru_head)) {
		Lck_Lock(&exphdl->mtx);
		n = 0;
		while (n < NUKEBUF) {
			oc = VTAILQ_FIRST(&lru->lru_head);
			if (oc == NULL)
				break;
			CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
			assert(oc_getlru(oc) == lru);

			/* Remove from the LRU and binheap */
			VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
			assert(oc->timer_idx != BINHEAP_NOIDX);
			binheap_delete(exphdl->heap, oc->timer_idx);
			assert(oc->timer_idx == BINHEAP_NOIDX);

			oc_array[n++] = oc;
			VSC_C_main->n_lru_nuked++;
		}
		assert(n > 0);
		Lck_Unlock(&exphdl->mtx);
		Lck_Unlock(&lru->mtx);

		for (i = 0; i < n; i++) {
			oc = oc_array[i];
			o = oc_getobj(&wrk->stats, oc);
			/* XXX: Not documented in vsl_tags.h */
			VSLb(vsl, SLT_ExpKill, "x=%u t=%.0f LRU",
			    oc_getxid(&wrk->stats, oc) & VSL_IDENTMASK,
			    EXP_Ttl(NULL, o) - t);
			o->exp.ttl = 0.0;
			(void)HSH_DerefObjCore(&wrk->stats, &oc);
		}

		Lck_Lock(&lru->mtx);
	}
	Lck_Unlock(&lru->mtx);

	WRK_SumStat(wrk);
}

#endif


/*--------------------------------------------------------------------
 * Handle stuff in the inbox
 */

static void
exp_inbox(struct exp_priv *ep, struct objcore *oc, double now)
{
	unsigned flags;
	struct lru *lru;
	struct object *o;

	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	VSLb(&ep->vsl, SLT_ExpKill, "EXP_Inbox p=%p e=%.9f f=0x%x", oc,
	    oc->timer_when, oc->flags);

	// AZ(oc->flags & OC_F_BUSY);

	lru = oc_getlru(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	/* Evacuate our action-flags, and put it back on the LRU list */
	Lck_Lock(&lru->mtx);
	flags = oc->exp_flags;
	AN(flags & OC_EF_OFFLRU);
	oc->exp_flags &= ~(OC_EF_INSERT | OC_EF_MOVE);
	oc->last_lru = now;
	if (!(flags & OC_EF_DYING)) {
		VTAILQ_INSERT_TAIL(&lru->lru_head, oc, lru_list);
		oc->exp_flags &= ~OC_EF_OFFLRU;
	}
	Lck_Unlock(&lru->mtx);

	if (flags & OC_EF_DYING) {
		VSLb(&ep->vsl, SLT_ExpKill, "EXP_Kill p=%p e=%.9f f=0x%x", oc,
		    oc->timer_when, oc->flags);
		if (!(flags & OC_EF_INSERT)) {
			assert(oc->timer_idx != BINHEAP_NOIDX);
			binheap_delete(ep->heap, oc->timer_idx);
		}
		assert(oc->timer_idx == BINHEAP_NOIDX);
		(void)HSH_DerefObjCore(&ep->wrk->stats, &oc);
		return;
	}

	if (flags & OC_EF_MOVE) {
		o = oc_getobj(&ep->wrk->stats, oc);
		CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
		oc->timer_when = exp_when(o);
		oc_updatemeta(oc);
	}

	VSLb(&ep->vsl, SLT_ExpKill, "EXP_When p=%p e=%.9f f=0x%x", oc,
	    oc->timer_when, flags);

	/*
	 * XXX: There are some pathological cases here, were we
	 * XXX: insert or move an expired object, only to find out
	 * XXX: the next moment and rip them out again.
	 */

	if (flags & OC_EF_INSERT) {
		assert(oc->timer_idx == BINHEAP_NOIDX);
		binheap_insert(exphdl->heap, oc);
		assert(oc->timer_idx != BINHEAP_NOIDX);
	} else if (flags & OC_EF_MOVE) {
		assert(oc->timer_idx != BINHEAP_NOIDX);
		binheap_reorder(exphdl->heap, oc->timer_idx);
		assert(oc->timer_idx != BINHEAP_NOIDX);
	} else {
		WRONG("Objcore state wrong in inbox");
	}
}

/*--------------------------------------------------------------------
 * Expire stuff from the binheap
 */

static double
exp_expire(struct exp_priv *ep, double now)
{
	struct lru *lru;
	struct objcore *oc;
	struct object *o;

	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);

	oc = binheap_root(ep->heap);
	if (oc == NULL)
		return (now + 355./113.);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	/* Ready ? */
	if (oc->timer_when > now)
		return (oc->timer_when);

	VSC_C_main->n_expired++;

	lru = oc_getlru(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);
	Lck_Lock(&lru->mtx);
	// AZ(oc->flags & OC_F_BUSY);
	oc->exp_flags |= OC_EF_DYING;
	if (oc->exp_flags & OC_EF_OFFLRU)
		oc = NULL;
	else {
		oc->exp_flags |= OC_EF_OFFLRU;
		VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
	}
	Lck_Unlock(&lru->mtx);

	if (oc == NULL)
		return (now + 1e-3);		// XXX ?

	/* Remove from binheap */
	assert(oc->timer_idx != BINHEAP_NOIDX);
	binheap_delete(ep->heap, oc->timer_idx);
	assert(oc->timer_idx == BINHEAP_NOIDX);

	CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);
	o = oc_getobj(&ep->wrk->stats, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	VSLb(&ep->vsl, SLT_ExpKill, "EXP_Expired x=%u t=%.0f",
	    oc_getxid(&ep->wrk->stats, oc) & VSL_IDENTMASK,
	    EXP_Ttl(NULL, o) - now);
	(void)HSH_DerefObjCore(&ep->wrk->stats, &oc);
	return (0);
}

/*--------------------------------------------------------------------
 * This thread monitors the root of the binary heap and whenever an
 * object expires, accounting also for graceability, it is killed.
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

static void * __match_proto__(bgthread_t)
exp_thread(struct worker *wrk, void *priv)
{
	struct objcore *oc;
	double t = 0, tnext = 0;
	struct exp_priv *ep;

	CAST_OBJ_NOTNULL(ep, priv, EXP_PRIV_MAGIC);
	ep->wrk = wrk;
	VSL_Setup(&ep->vsl, NULL, 0);
	ep->heap = binheap_new(NULL, object_cmp, object_update);
	AN(ep->heap);
	while (1) {

		Lck_Lock(&ep->mtx);
		oc = VTAILQ_FIRST(&ep->inbox);
		if (oc != NULL) {
			VTAILQ_REMOVE(&ep->inbox, oc, lru_list);
			VSC_C_main->exp_received++;
			tnext = 0;
		} else if (tnext > t) {
			VSL_Flush(&ep->vsl, 0);
			WRK_SumStat(wrk);
			(void)Lck_CondWait(&ep->condvar, &ep->mtx, tnext);
		}
		Lck_Unlock(&ep->mtx);

		t = VTIM_real();

		if (oc != NULL)
			exp_inbox(ep, oc, t);
		else
			tnext = exp_expire(ep, t);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

void
EXP_Init(void)
{
	struct exp_priv *ep;
	pthread_t pt;

	ALLOC_OBJ(ep, EXP_PRIV_MAGIC);
	AN(ep);

	Lck_New(&ep->mtx, lck_exp);
	AZ(pthread_cond_init(&ep->condvar, NULL));
	VTAILQ_INIT(&ep->inbox);
	exphdl = ep;
	WRK_BgThread(&pt, "cache-timeout", exp_thread, ep);
}
