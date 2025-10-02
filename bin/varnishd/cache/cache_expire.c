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
#include "cache_pool.h"		// taskhead

#include "vbh.h"
#include "vtim.h"

VSTAILQ_HEAD(exp_inbox_head, objcore);

struct exp_priv {
	unsigned			magic;
#define EXP_PRIV_MAGIC			0x9db22482
	/* shared */
	struct lock			mtx;
	struct exp_inbox_head		inbox;
	pthread_cond_t			condvar;

	/* shared with exp_remove_oc tasks */
	struct taskhead			free_tasks;

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
 * Finish removal of an oc
 */

static void
exp_remove_oc(struct worker *wrk, struct objcore *oc, vtim_real now)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	assert(oc->timer_idx == VBH_NOIDX);
	assert(oc->refcnt > 0);
	AZ(oc->exp_flags);
	// no objhead check
	VSLb(wrk->vsl, SLT_ExpKill, "EXP_Removed x=%ju t=%.0f h=%jd",
	    VXID(ObjGetXID(wrk, oc)), EXP_Ttl(NULL, oc) - now,
	    (intmax_t)oc->hits);
	ObjSendEvent(wrk, oc, OEV_EXPIRE);
	(void)HSH_DerefObjCore(wrk, &oc);
}

/*--------------------------------------------------------------------
 * Finish expiry of an oc
 * This is subtly different from exp_remove_oc, also besides the VSL:
 * - additional HSH_Kill()
 * - assert ob objhead
 * - do not assert on exp_flags
 */

static void
exp_expire_oc(struct worker *wrk, struct vsl_log *vsl, struct objcore *oc, vtim_real now)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (!(oc->flags & OC_F_DYING))
		HSH_Kill(oc);

	assert(oc->timer_idx == VBH_NOIDX);
	assert(oc->refcnt > 0);
	// no exp_flags check
	CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);
	VSLb(vsl, SLT_ExpKill, "EXP_Expired x=%ju t=%.0f h=%jd",
	    VXID(ObjGetXID(wrk, oc)), EXP_Ttl(NULL, oc) - now,
	    (intmax_t)oc->hits);
	ObjSendEvent(wrk, oc, OEV_EXPIRE);
	(void)HSH_DerefObjCore(wrk, &oc);
}

/*--------------------------------------------------------------------
 * keep track of ocs to deref
 */

struct exp_deref {
	unsigned			magic;
#define EXP_DEREF_MAGIC			0xb5854b59
	unsigned			n;
	struct exp_inbox_head		remove_head;
	struct exp_inbox_head		expire_head;
};

/* take lists from src to dsk */
static void
exp_deref_take(struct exp_deref *dst, struct exp_deref *src)
{

	AN(dst);
	INIT_OBJ(dst, EXP_DEREF_MAGIC);
	CHECK_OBJ_NOTNULL(src, EXP_DEREF_MAGIC);

	dst->n = src->n;
	src->n = 0;
	VSTAILQ_INIT(&dst->remove_head);
	VSTAILQ_SWAP(&dst->remove_head, &src->remove_head, objcore);
	VSTAILQ_INIT(&dst->expire_head);
	VSTAILQ_SWAP(&dst->expire_head, &src->expire_head, objcore);
}

static void
exp_deref_work(struct worker *wrk, struct exp_deref *deref, vtim_real now)
{
	struct objcore *oc, *save;
	unsigned n = 0;

	CHECK_OBJ_NOTNULL(deref, EXP_DEREF_MAGIC);
	VSTAILQ_FOREACH_SAFE(oc, &deref->remove_head, exp_list, save) {
		exp_remove_oc(wrk, oc, now);
		n++;
		if (n % 1024 == 0)
			Pool_Sumstat(wrk);
	}
	VSTAILQ_FOREACH_SAFE(oc, &deref->expire_head, exp_list, save) {
		exp_expire_oc(wrk, wrk->vsl, oc, now);
		n++;
		if (n % 1024 == 0)
			Pool_Sumstat(wrk);
	}
	assert(deref->n == n);
	memset(deref, 0, sizeof *deref);
}

// trivial functions to wrap n increment.
static void
exp_deref_add_remove(struct exp_deref *deref, struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(deref, EXP_DEREF_MAGIC);
	VSTAILQ_INSERT_TAIL(&deref->remove_head, oc, exp_list);
	deref->n++;
}

static void
exp_deref_add_expire(struct exp_deref *deref, struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(deref, EXP_DEREF_MAGIC);
	VSTAILQ_INSERT_TAIL(&deref->expire_head, oc, exp_list);
	deref->n++;
}

static void
exp_deref_init(struct exp_deref *deref)
{

	INIT_OBJ(deref, EXP_DEREF_MAGIC);
	VSTAILQ_INIT(&deref->remove_head);
	VSTAILQ_INIT(&deref->expire_head);
}

/*--------------------------------------------------------------------
 * task to finish removal of ocs
 *
 * the struct is only needed until the task is running and has taken the
 * remove_head, when it returns the struct to the free_tasks list
 */

struct exp_deref_task_s {
	// initialized once
	unsigned			magic;
#define EXP_DEREF_TASK_MAGIC		0xaf5a6732
	struct exp_priv			*ep;

	// per invocation
	struct exp_deref		deref;
	struct pool_task		task;
};

static void
exp_deref_task(struct worker *wrk, void *priv)
{
	struct exp_deref_task_s *deref_task;
	struct exp_deref deref;
	struct exp_priv *ep;
	struct vsl_log vsl;
	char vsl_buf[4096];

	CAST_OBJ_NOTNULL(deref_task, priv, EXP_DEREF_TASK_MAGIC);
	ep = deref_task->ep;
	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);

	exp_deref_take(&deref, &deref_task->deref);

	Lck_Lock(&ep->mtx);
	VTAILQ_INSERT_TAIL(&ep->free_tasks, &deref_task->task, list);
	PTOK(pthread_cond_signal(&ep->condvar));
	Lck_Unlock(&ep->mtx);

	VSL_Setup(&vsl, vsl_buf, sizeof vsl_buf);
	AZ(wrk->vsl);
	wrk->vsl = &vsl;

	exp_deref_work(wrk, &deref, VTIM_real());
	VSL_Flush(wrk->vsl, 0);
	Pool_Sumstat(wrk);
	wrk->vsl = NULL;
}

/*--------------------------------------------------------------------
 * Handle stuff in the inbox
 */

static void
exp_inbox(struct exp_priv *ep, struct objcore *oc, unsigned flags)
{

	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);

	VSLb(&ep->vsl, SLT_ExpKill, "EXP_Inbox flg=%x p=%p e=%.6f f=0x%x",
	    flags, oc, oc->timer_when, oc->flags);

	AZ(flags & OC_EF_REMOVE); // handled in exp_thread

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
exp_expire(struct exp_priv *ep, struct exp_deref *deref, vtim_real now)
{
	struct objcore *oc;
	vtim_real ret;
	unsigned n = 0;

	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);

	Lck_Lock(&ep->mtx);
	while (1) {
		oc = VBH_root(ep->heap);
		if (oc == NULL) {
			ret = now + 355. / 113.;
			break;
		}
		VSLb(&ep->vsl, SLT_ExpKill, "EXP_Inspect p=%p e=%.6f f=0x%x",
		    oc, oc->timer_when - now, oc->flags);

		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

		/* Ready ? */
		if (oc->timer_when > now) {
			ret = oc->timer_when;
			break;
		}

		n++;

		if (oc->exp_flags & OC_EF_POSTED) {
			if (oc->exp_flags & OC_EF_REMOVE)
				n--;
			VSTAILQ_REMOVE(&ep->inbox, oc, objcore, exp_list);
			oc->exp_flags = 0;
		}
		else
			oc->exp_flags &= ~OC_EF_REFD;

		exp_deref_add_expire(deref, oc);
		/* Remove from binheap */
		assert(oc->timer_idx != VBH_NOIDX);
		VBH_delete(ep->heap, oc->timer_idx);

		assert(oc->timer_idx == VBH_NOIDX);
	}
	Lck_Unlock(&ep->mtx);
	VSC_C_main->n_expired += n;

	return (ret);
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

struct exp_inbox_item {
	unsigned		magic;
#define EXP_INBOX_ITEM_MAGIC	0x0519627d
	unsigned		flags;
	struct objcore		*oc;
};

static void * v_matchproto_(bgthread_t)
exp_thread(struct worker *wrk, void *priv)
{
	struct exp_inbox_item batch[1024];
	struct exp_inbox_item * const batch_lim =
	    batch + sizeof batch / sizeof *batch;
	struct exp_inbox_item *todo, *item;
	/* a small number of task structs is sufficient because
	 * exp_deref_task() immediately returns the task struct to the free
	 * tasks list.
	 */
	const unsigned ntasks = 4;
	struct exp_deref_task_s tasks[ntasks];
	struct exp_deref_task_s *deref_task;
	struct pool_task *task;
	struct exp_deref deref;
	struct objcore *oc;
	vtim_real t = 0, tnext = 0;
	struct exp_priv *ep;

	CAST_OBJ_NOTNULL(ep, priv, EXP_PRIV_MAGIC);
	ep->wrk = wrk;
	VSL_Setup(&ep->vsl, NULL, 0);
	AZ(wrk->vsl);
	wrk->vsl = &ep->vsl;
	ep->heap = VBH_new(NULL, object_cmp, object_update);
	AN(ep->heap);

	for (item = batch; item < batch_lim; item++)
		INIT_OBJ(item, EXP_INBOX_ITEM_MAGIC);

	VTAILQ_INIT(&ep->free_tasks);
	for (deref_task = tasks; deref_task < tasks + ntasks; deref_task++) {
		INIT_OBJ(deref_task, EXP_DEREF_TASK_MAGIC);
		deref_task->ep = ep;
		VTAILQ_INSERT_TAIL(&ep->free_tasks, &deref_task->task, list);
	}

	while (exp_shutdown == 0) {
		exp_deref_init(&deref);
		todo = batch;

		Lck_Lock(&ep->mtx);
		while ((oc = VSTAILQ_FIRST(&ep->inbox)) != NULL &&
		    todo < batch_lim) {
			unsigned flags;

			CHECK_OBJ(oc, OBJCORE_MAGIC);
			CHECK_OBJ(todo, EXP_INBOX_ITEM_MAGIC);
			AZ(todo->flags);
			AZ(todo->oc);

			assert(oc->refcnt >= 1);
			assert(oc->exp_flags & OC_EF_POSTED);
			VSTAILQ_REMOVE(&ep->inbox, oc, objcore, exp_list);
			VSC_C_main->exp_received++;
			tnext = 0;
			flags = oc->exp_flags;
			assert(flags & OC_EF_POSTED);
			if (flags & OC_EF_REMOVE) {
				oc->exp_flags = 0;
				exp_deref_add_remove(&deref, oc);
				if (flags & OC_EF_INSERT)
					continue;
			}
			else
				oc->exp_flags &= OC_EF_REFD;

			todo->flags = flags;
			todo->oc = oc;
			todo++;
		}
		if (todo == batch && deref.n == 0 && tnext > t) {
			VSL_Flush(&ep->vsl, 0);
			Pool_Sumstat(wrk);
			(void)Lck_CondWaitUntil(&ep->condvar, &ep->mtx, tnext);
		}
		Lck_Unlock(&ep->mtx);

		for (item = batch; item < todo; item++) {
			CHECK_OBJ(item, EXP_INBOX_ITEM_MAGIC);
			if (item->flags & OC_EF_REMOVE) {
				AZ(item->flags & OC_EF_INSERT);
				VBH_delete(ep->heap, item->oc->timer_idx);
			}
			else
				exp_inbox(ep, item->oc, item->flags);
			INIT_OBJ(item, EXP_INBOX_ITEM_MAGIC);
		}

		t = VTIM_real();
		tnext = exp_expire(ep, &deref, t);

		if (deref.n == 0)
			continue;

		/* arbitrary threshold to not start a task,
		 * probably does not matter much */
		if (deref.n < 128) {
			exp_deref_work(ep->wrk, &deref, t);
			continue;
		}

		/* call exp_deref_work() in a task */
		Lck_Lock(&ep->mtx);
		while ((task = VTAILQ_FIRST(&ep->free_tasks)) == NULL) {
			VSL_Flush(&ep->vsl, 0);
			Pool_Sumstat(wrk);
			(void)Lck_CondWait(&ep->condvar, &ep->mtx);
		}
		VTAILQ_REMOVE(&ep->free_tasks, task, list);
		Lck_Unlock(&ep->mtx);

		deref_task =
		    __containerof(task, struct exp_deref_task_s, task);
		CHECK_OBJ(deref_task, EXP_DEREF_TASK_MAGIC);
		exp_deref_take(&deref_task->deref, &deref);
		task->func = exp_deref_task;
		task->priv = deref_task;
		AZ(Pool_Task_Any(task, TASK_QUEUE_BO));
	}

	Lck_Lock(&ep->mtx);
	unsigned u = 0;
	do {
		u = 0;
		VTAILQ_FOREACH(task, &ep->free_tasks, list)
			u++;
		if (u == ntasks)
			break;
		(void)Lck_CondWait(&ep->condvar, &ep->mtx);
	} while (u < ntasks);
	Lck_Unlock(&ep->mtx);

	VTAILQ_INIT(&ep->free_tasks);

	VSL_Flush(&ep->vsl, 0);
	Pool_Sumstat(wrk);
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
