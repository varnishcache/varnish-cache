/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_objhead.h"

#include "vbh.h"
#include "vtim.h"

struct exp_priv {
	unsigned			magic;
#define EXP_PRIV_MAGIC			0x9db22482
	/* shared */
	struct lock			mtx;
	VSTAILQ_HEAD(,objcore)		inbox;
	pthread_cond_t			condvar;

	/* owned by exp thread */
	struct worker			*wrk;
	struct vsl_log			vsl;
	struct vbh			*heap;
	pthread_t			thread;
};

static struct exp_priv *exphdl;
static int exp_shutdown = 0;

/*--------------------------------------------------------------------
 * Calculate an object's effective ttl time, taking req.ttl into account
 * if it is available.
 */

vtim_real
EXP_Ttl(const struct req *req, const struct objcore *oc)
{
	vtim_dur r;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	r = oc->ttl;
	if (req != NULL && req->d_ttl > 0. && req->d_ttl < r)
		r = req->d_ttl;
	return (oc->t_origin + r);
}

/*--------------------------------------------------------------------
 * Calculate an object's effective ttl+grace time, taking req.grace into
 * account if it is available.
 */

vtim_real
EXP_Ttl_grace(const struct req *req, const struct objcore *oc)
{
	vtim_dur g;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	g = oc->grace;
	if (req != NULL && req->d_grace >= 0. && req->d_grace < g)
		g = req->d_grace;
	return (EXP_Ttl(req, oc) + g);
}

/*--------------------------------------------------------------------
 * Post an objcore to the exp_thread's inbox.
 */

static void
exp_mail_it(struct objcore *oc, uint8_t cmds)
{
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);
	AZ(cmds & OC_EF_REFD);

	Lck_AssertHeld(&exphdl->mtx);

	if (oc->exp_flags & OC_EF_REFD) {
		if (!(oc->exp_flags & OC_EF_POSTED)) {
			if (cmds & OC_EF_REMOVE)
				VSTAILQ_INSERT_HEAD(&exphdl->inbox,
				    oc, exp_list);
			else
				VSTAILQ_INSERT_TAIL(&exphdl->inbox,
				    oc, exp_list);
			VSC_C_main->exp_mailed++;
		}
		oc->exp_flags |= cmds | OC_EF_POSTED;
		PTOK(pthread_cond_signal(&exphdl->condvar));
	}
}

/*--------------------------------------------------------------------
 * Setup a new ObjCore for control by expire. Should be called with the
 * ObjHead locked by HSH_Unbusy(/HSH_Insert) (in private access).
 */

void
EXP_RefNewObjcore(struct objcore *oc)
{
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	Lck_AssertHeld(&oc->objhead->mtx);

	AZ(oc->exp_flags);
	assert(oc->refcnt >= 1);
	oc->refcnt++;
	oc->exp_flags |= OC_EF_REFD | OC_EF_NEW;
}



/*--------------------------------------------------------------------
 * Call EXP's attention to an oc
 */

void
EXP_Remove(struct objcore *oc, const struct objcore *new_oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_ORNULL(new_oc, OBJCORE_MAGIC);

	if (oc->exp_flags & OC_EF_REFD) {
		Lck_Lock(&exphdl->mtx);
		if (new_oc != NULL)
			VSC_C_main->n_superseded++;
		if (oc->exp_flags & OC_EF_NEW) {
			/* EXP_Insert has not been called for this object
			 * yet. Mark it for removal, and EXP_Insert will
			 * clean up once it is called. */
			AZ(oc->exp_flags & OC_EF_POSTED);
			oc->exp_flags |= OC_EF_REMOVE;
		} else
			exp_mail_it(oc, OC_EF_REMOVE);
		Lck_Unlock(&exphdl->mtx);
	}
}

/*--------------------------------------------------------------------
 * Insert new object.
 *
 * Caller got a oc->refcnt for us.
 */

void
EXP_Insert(struct worker *wrk, struct objcore *oc)
{
	unsigned remove_race = 0;
	struct objcore *tmpoc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AZ(oc->flags & OC_F_BUSY);

	if (!(oc->exp_flags & OC_EF_REFD))
		return;

	/* One ref held by the caller, and one that will be owned by
	 * expiry. */
	assert(oc->refcnt >= 2);

	ObjSendEvent(wrk, oc, OEV_INSERT);

	Lck_Lock(&exphdl->mtx);
	AN(oc->exp_flags & OC_EF_NEW);
	oc->exp_flags &= ~OC_EF_NEW;
	AZ(oc->exp_flags & (OC_EF_INSERT | OC_EF_MOVE | OC_EF_POSTED));
	if (oc->exp_flags & OC_EF_REMOVE) {
		/* We raced some other thread executing EXP_Remove */
		remove_race = 1;
		oc->exp_flags &= ~(OC_EF_REFD | OC_EF_REMOVE);
	} else
		exp_mail_it(oc, OC_EF_INSERT | OC_EF_MOVE);
	Lck_Unlock(&exphdl->mtx);

	if (remove_race) {
		ObjSendEvent(wrk, oc, OEV_EXPIRE);
		tmpoc = oc;
		assert(oc->refcnt >= 2); /* Silence coverity */
		(void)HSH_DerefObjCore(wrk, &oc);
		AZ(oc);
		assert(tmpoc->refcnt >= 1); /* Silence coverity */
	}
}

/*--------------------------------------------------------------------
 * Reduce object timers
 */

void
EXP_Reduce(struct objcore *oc, vtim_real now,
    vtim_dur ttl, vtim_dur grace, vtim_dur keep)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);

	if (!isnan(ttl) && now + ttl - oc->t_origin >= oc->ttl)
		ttl = NAN;
	if (!isnan(grace) && grace >= oc->grace)
		grace = NAN;
	if (!isnan(keep) && keep >= oc->keep)
		keep = NAN;

	EXP_Rearm(oc, now, ttl, grace, keep);
}

/*--------------------------------------------------------------------
 * We have changed one or more of the object timers, tell the exp_thread
 *
 */

void
EXP_Rearm(struct objcore *oc, vtim_real now,
    vtim_dur ttl, vtim_dur grace, vtim_dur keep)
{
	vtim_real when;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);

	if (!(oc->exp_flags & OC_EF_REFD))
		return;

	if (!isnan(ttl))
		oc->ttl = now + ttl - oc->t_origin;
	if (!isnan(grace))
		oc->grace = grace;
	if (!isnan(keep))
		oc->keep = keep;

	when = EXP_WHEN(oc);

	VSL(SLT_ExpKill, NO_VXID, "EXP_Rearm p=%p E=%.6f e=%.6f f=0x%x", oc,
	    oc->timer_when, when, oc->flags);

	if (when < oc->t_origin || when < oc->timer_when) {
		Lck_Lock(&exphdl->mtx);
		if (oc->exp_flags & OC_EF_NEW) {
			/* EXP_Insert has not been called yet, do nothing
			 * as the initial insert will execute the move
			 * operation. */
		} else
			exp_mail_it(oc, OC_EF_MOVE);
		Lck_Unlock(&exphdl->mtx);
	}
}

/*--------------------------------------------------------------------
 * Handle stuff in the inbox
 */

static void
exp_inbox(struct exp_priv *ep, struct objcore *oc, unsigned flags, double now)
{

	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);

	VSLb(&ep->vsl, SLT_ExpKill, "EXP_Inbox flg=%x p=%p e=%.6f f=0x%x",
	    flags, oc, oc->timer_when, oc->flags);

	if (flags & OC_EF_REMOVE) {
		if (!(flags & OC_EF_INSERT)) {
			assert(oc->timer_idx != VBH_NOIDX);
			VBH_delete(ep->heap, oc->timer_idx);
		}
		assert(oc->timer_idx == VBH_NOIDX);
		assert(oc->refcnt > 0);
		AZ(oc->exp_flags);
		VSLb(&ep->vsl, SLT_ExpKill, "EXP_Removed x=%ju t=%.0f h=%jd",
		    VXID(ObjGetXID(ep->wrk, oc)), EXP_Ttl(NULL, oc) - now,
		    (intmax_t)oc->hits);
		ObjSendEvent(ep->wrk, oc, OEV_EXPIRE);
		(void)HSH_DerefObjCore(ep->wrk, &oc);
		return;
	}

	if (flags & OC_EF_MOVE) {
		oc->timer_when = EXP_WHEN(oc);
		ObjSendEvent(ep->wrk, oc, OEV_TTLCHG);
	}

	VSLb(&ep->vsl, SLT_ExpKill, "EXP_When p=%p e=%.6f f=0x%x", oc,
	    oc->timer_when, flags);

	/*
	 * XXX: There are some pathological cases here, were we
	 * XXX: insert or move an expired object, only to find out
	 * XXX: the next moment and rip them out again.
	 */

	if (flags & OC_EF_INSERT) {
		assert(oc->timer_idx == VBH_NOIDX);
		VBH_insert(exphdl->heap, oc);
		assert(oc->timer_idx != VBH_NOIDX);
	} else if (flags & OC_EF_MOVE) {
		assert(oc->timer_idx != VBH_NOIDX);
		VBH_reorder(exphdl->heap, oc->timer_idx);
		assert(oc->timer_idx != VBH_NOIDX);
	} else {
		WRONG("Objcore state wrong in inbox");
	}
}

/*--------------------------------------------------------------------
 * Expire stuff from the binheap
 */

static vtim_real
exp_expire(struct exp_priv *ep, vtim_real now)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);

	oc = VBH_root(ep->heap);
	if (oc == NULL)
		return (now + 355. / 113.);
	VSLb(&ep->vsl, SLT_ExpKill, "EXP_Inspect p=%p e=%.6f f=0x%x", oc,
	    oc->timer_when - now, oc->flags);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	/* Ready ? */
	if (oc->timer_when > now)
		return (oc->timer_when);

	VSC_C_main->n_expired++;

	Lck_Lock(&ep->mtx);
	if (oc->exp_flags & OC_EF_POSTED) {
		oc->exp_flags |= OC_EF_REMOVE;
		oc = NULL;
	} else {
		oc->exp_flags &= ~OC_EF_REFD;
	}
	Lck_Unlock(&ep->mtx);
	if (oc != NULL) {
		if (!(oc->flags & OC_F_DYING))
			HSH_Kill(oc);

		/* Remove from binheap */
		assert(oc->timer_idx != VBH_NOIDX);
		VBH_delete(ep->heap, oc->timer_idx);
		assert(oc->timer_idx == VBH_NOIDX);

		CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);
		VSLb(&ep->vsl, SLT_ExpKill, "EXP_Expired x=%ju t=%.0f h=%jd",
		    VXID(ObjGetXID(ep->wrk, oc)), EXP_Ttl(NULL, oc) - now,
		    (intmax_t)oc->hits);
		ObjSendEvent(ep->wrk, oc, OEV_EXPIRE);
		(void)HSH_DerefObjCore(ep->wrk, &oc);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * This thread monitors the root of the binary heap and whenever an
 * object expires, accounting also for graceability, it is killed.
 */

static int v_matchproto_(vbh_cmp_t)
object_cmp(void *priv, const void *a, const void *b)
{
	const struct objcore *aa, *bb;

	(void)priv;
	CAST_OBJ_NOTNULL(aa, a, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(bb, b, OBJCORE_MAGIC);
	return (aa->timer_when < bb->timer_when);
}

static void v_matchproto_(vbh_update_t)
object_update(void *priv, void *p, unsigned u)
{
	struct objcore *oc;

	(void)priv;
	CAST_OBJ_NOTNULL(oc, p, OBJCORE_MAGIC);
	oc->timer_idx = u;
}

static void * v_matchproto_(bgthread_t)
exp_thread(struct worker *wrk, void *priv)
{
	struct objcore *oc;
	vtim_real t = 0, tnext = 0;
	struct exp_priv *ep;
	unsigned flags = 0;

	CAST_OBJ_NOTNULL(ep, priv, EXP_PRIV_MAGIC);
	ep->wrk = wrk;
	VSL_Setup(&ep->vsl, NULL, 0);
	AZ(wrk->vsl);
	wrk->vsl = &ep->vsl;
	ep->heap = VBH_new(NULL, object_cmp, object_update);
	AN(ep->heap);
	while (exp_shutdown == 0) {

		Lck_Lock(&ep->mtx);
		oc = VSTAILQ_FIRST(&ep->inbox);
		CHECK_OBJ_ORNULL(oc, OBJCORE_MAGIC);
		if (oc != NULL) {
			assert(oc->refcnt >= 1);
			VSTAILQ_REMOVE(&ep->inbox, oc, objcore, exp_list);
			VSC_C_main->exp_received++;
			tnext = 0;
			flags = oc->exp_flags;
			if (flags & OC_EF_REMOVE)
				oc->exp_flags = 0;
			else
				oc->exp_flags &= OC_EF_REFD;
		} else if (tnext > t) {
			VSL_Flush(&ep->vsl, 0);
			Pool_Sumstat(wrk);
			(void)Lck_CondWaitUntil(&ep->condvar, &ep->mtx, tnext);
		}
		Lck_Unlock(&ep->mtx);

		t = VTIM_real();

		if (oc != NULL)
			exp_inbox(ep, oc, flags, t);
		else
			tnext = exp_expire(ep, t);
	}
	wrk->vsl = NULL;
	return (NULL);
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
	PTOK(pthread_cond_init(&ep->condvar, NULL));
	VSTAILQ_INIT(&ep->inbox);
	WRK_BgThread(&pt, "cache-exp", exp_thread, ep);
	ep->thread = pt;
	exphdl = ep;
}

void
EXP_Shutdown(void)
{
	struct exp_priv *ep = exphdl;
	void *status;

	Lck_Lock(&ep->mtx);
	exp_shutdown = 1;
	PTOK(pthread_cond_signal(&ep->condvar));
	Lck_Unlock(&ep->mtx);

	AN(ep->thread);
	PTOK(pthread_join(ep->thread, &status));
	AZ(status);
	memset(&ep->thread, 0, sizeof ep->thread);

	/* XXX could cleanup more - not worth it for now */
}
