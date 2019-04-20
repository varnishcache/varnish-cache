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
 * Session management
 *
 * The overall goal here is to hold as little state as possible for an
 * idle session.  This leads to various nasty-ish overloads of struct
 * sess fields, for instance ->fd being negative ->reason.
 *
 */

#include "config.h"

#include "cache_varnishd.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache_pool.h"
#include "cache_transport.h"

#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"
#include "waiter/waiter.h"

/*--------------------------------------------------------------------*/

void
SES_SetTransport(struct worker *wrk, struct sess *sp, struct req *req,
    const struct transport *xp)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(xp, TRANSPORT_MAGIC);
	assert(xp->number > 0);

	sp->sattr[SA_TRANSPORT] = xp->number;
	req->transport = xp;
	wrk->task.func = xp->new_session;
	wrk->task.priv = req;
}

/*--------------------------------------------------------------------*/

static int
ses_get_attr(const struct sess *sp, enum sess_attr a, void **dst)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(a < SA_LAST);
	AN(dst);

	if (sp->sattr[a] == 0xffff) {
		*dst = NULL;
		return (-1);
	} else {
		*dst = sp->ws->s + sp->sattr[a];
		return (0);
	}
}

static int
ses_set_attr(const struct sess *sp, enum sess_attr a, const void *src, int sz)
{
	void *dst;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(a < SA_LAST);
	AN(src);
	assert(sz > 0);

	if (sp->sattr[a] == 0xffff)
		return (-1);
	dst = sp->ws->s + sp->sattr[a];
	AN(dst);
	memcpy(dst, src, sz);
	return (0);
}

static void
ses_reserve_attr(struct sess *sp, enum sess_attr a, void **dst, int sz)
{
	ssize_t o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(a < SA_LAST);
	assert(sz >= 0);
	AN(dst);
	o = WS_ReserveSize(sp->ws, sz);
	assert(o >= sz);
	*dst = sp->ws->f;
	o = sp->ws->f - sp->ws->s;
	WS_Release(sp->ws, sz);
	assert(o >= 0 && o <= 0xffff);
	sp->sattr[a] = (uint16_t)o;
}

#define SESS_ATTR(UP, low, typ, len)					\
	int								\
	SES_Set_##low(const struct sess *sp, const typ *src)		\
	{								\
		return (ses_set_attr(sp, SA_##UP, src, len));		\
	}								\
									\
	int								\
	SES_Get_##low(const struct sess *sp, typ **dst)			\
	{								\
		return (ses_get_attr(sp, SA_##UP, (void**)dst));	\
	}								\
									\
	void								\
	SES_Reserve_##low(struct sess *sp, typ **dst)			\
	{								\
		assert(len >= 0);					\
		ses_reserve_attr(sp, SA_##UP, (void**)dst, len);	\
	}

#include "tbl/sess_attr.h"

void
SES_Set_String_Attr(struct sess *sp, enum sess_attr a, const char *src)
{
	void *q;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(src);

	switch (a) {
#define SESS_ATTR(UP, low, typ, len)	case SA_##UP: assert(len < 0); break;
#include "tbl/sess_attr.h"
	default:  WRONG("wrong sess_attr");
	}

	ses_reserve_attr(sp, a, &q, strlen(src) + 1);
	strcpy(q, src);
}

const char *
SES_Get_String_Attr(const struct sess *sp, enum sess_attr a)
{
	void *q;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	switch (a) {
#define SESS_ATTR(UP, low, typ, len)	case SA_##UP: assert(len < 0); break;
#include "tbl/sess_attr.h"
	default:  WRONG("wrong sess_attr");
	}

	if (ses_get_attr(sp, a, &q) < 0)
		return (NULL);
	return (q);
}

/*--------------------------------------------------------------------*/

const char *
HTC_Status(enum htc_status_e e)
{
	switch (e) {
#define HTC_STATUS(e, n, s, l)				\
		case HTC_S_ ## e:	return (s);
#include "tbl/htc.h"
	default:
		WRONG("HTC_Status");
	}
	NEEDLESS(return (NULL));
}

/*--------------------------------------------------------------------*/

void
HTC_RxInit(struct http_conn *htc, struct ws *ws)
{
	ssize_t l;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	htc->ws = ws;
	(void)WS_ReserveAll(htc->ws);
	htc->rxbuf_b = ws->f;
	htc->rxbuf_e = ws->f;
	if (htc->pipeline_b != NULL) {
		AN(htc->pipeline_e);
		// assert(WS_Inside(ws, htc->pipeline_b, htc->pipeline_e));
		l = htc->pipeline_e - htc->pipeline_b;
		assert(l > 0);
		assert(l <= ws->r - htc->rxbuf_b);
		memmove(htc->rxbuf_b, htc->pipeline_b, l);
		htc->rxbuf_e += l;
		htc->pipeline_b = NULL;
		htc->pipeline_e = NULL;
	}
}

void
HTC_RxPipeline(struct http_conn *htc, void *p)
{

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	if (p == NULL || (char*)p == htc->rxbuf_e) {
		htc->pipeline_b = NULL;
		htc->pipeline_e = NULL;
	} else {
		assert((char*)p >= htc->rxbuf_b);
		assert((char*)p < htc->rxbuf_e);
		htc->pipeline_b = p;
		htc->pipeline_e = htc->rxbuf_e;
	}
}

/*----------------------------------------------------------------------
 * Receive a request/packet/whatever, with timeouts
 *
 * maxbytes is the maximum number of bytes the caller expects to need to
 * reach a complete work unit. Note that due to pipelining the actual
 * number of bytes passed to func in htc->rxbuf_b through htc->rxbuf_e may
 * be larger.
 *
 * t0 is when we start
 * *t1 becomes time of first non-idle rx
 * *t2 becomes time of complete rx
 * ti is when we return IDLE if nothing has arrived
 * tn is when we timeout on non-complete (total timeout)
 * td is max timeout between reads
 */

enum htc_status_e
HTC_RxStuff(struct http_conn *htc, htc_complete_f *func,
    vtim_real *t1, vtim_real *t2, vtim_real ti, vtim_real tn, vtim_dur td,
    int maxbytes)
{
	vtim_dur tmo;
	vtim_real now;
	enum htc_status_e hs;
	ssize_t z;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(htc->rfd);
	assert(*htc->rfd > 0);
	AN(htc->ws->r);
	AN(htc->rxbuf_b);
	assert(htc->rxbuf_b <= htc->rxbuf_e);
	assert(htc->rxbuf_e <= htc->ws->r);

	AZ(isnan(tn) && isnan(td));
	if (t1 != NULL)
		assert(isnan(*t1));

	if (htc->rxbuf_e == htc->ws->r) {
		/* Can't work with a zero size buffer */
		WS_ReleaseP(htc->ws, htc->rxbuf_b);
		return (HTC_S_OVERFLOW);
	}
	z = (htc->ws->r - htc->rxbuf_b);
	if (z < maxbytes)
		maxbytes = z;	/* Cap maxbytes at available WS */

	while (1) {
		now = VTIM_real();
		AZ(htc->pipeline_b);
		AZ(htc->pipeline_e);
		assert(htc->rxbuf_e <= htc->ws->r);

		hs = func(htc);
		if (hs == HTC_S_OVERFLOW || hs == HTC_S_JUNK) {
			WS_ReleaseP(htc->ws, htc->rxbuf_b);
			return (hs);
		}
		if (hs == HTC_S_COMPLETE) {
			WS_ReleaseP(htc->ws, htc->rxbuf_e);
			/* Got it, run with it */
			if (t1 != NULL && isnan(*t1))
				*t1 = now;
			if (t2 != NULL)
				*t2 = now;
			return (HTC_S_COMPLETE);
		}
		if (hs == HTC_S_MORE) {
			/* Working on it */
			if (t1 != NULL && isnan(*t1))
				*t1 = now;
		} else if (hs == HTC_S_EMPTY)
			htc->rxbuf_e = htc->rxbuf_b;
		else
			WRONG("htc_status_e");

		if (hs == HTC_S_EMPTY && !isnan(ti) && (isnan(tn) || ti < tn))
			tmo = ti - now;
		else if (isnan(tn))
			tmo = td;
		else if (isnan(td))
			tmo = tn - now;
		else if (td < tn - now)
			tmo = td;
		else
			tmo = tn - now;

		AZ(isnan(tmo));
		z = maxbytes - (htc->rxbuf_e - htc->rxbuf_b);
		if (z <= 0) {
			/* maxbytes reached but not HTC_S_COMPLETE. Return
			 * overflow. */
			WS_ReleaseP(htc->ws, htc->rxbuf_b);
			return (HTC_S_OVERFLOW);
		}
		if (tmo <= 0.0)
			tmo = 1e-3;
		z = VTCP_read(*htc->rfd, htc->rxbuf_e, z, tmo);
		if (z == 0 || z == -1) {
			WS_ReleaseP(htc->ws, htc->rxbuf_b);
			return (HTC_S_EOF);
		} else if (z > 0)
			htc->rxbuf_e += z;
		else if (z == -2) {
			WS_ReleaseP(htc->ws, htc->rxbuf_b);
			if (hs == HTC_S_EMPTY)
				return (HTC_S_IDLE);
			else
				return (HTC_S_TIMEOUT);
		}
	}
}

/*--------------------------------------------------------------------
 * Get a new session, preferably by recycling an already ready one
 *
 * Layout is:
 *	struct sess
 *	workspace
 */

struct sess *
SES_New(struct pool *pp)
{
	struct sess *sp;
	unsigned sz;
	char *p, *e;

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	sp = MPL_Get(pp->mpl_sess, &sz);
	sp->magic = SESS_MAGIC;
	sp->pool = pp;
	sp->refcnt = 1;
	memset(sp->sattr, 0xff, sizeof sp->sattr);

	e = (char*)sp + sz;
	p = (char*)(sp + 1);
	p = (void*)PRNDUP(p);
	assert(p < e);
	WS_Init(sp->ws, "ses", p, e - p);

	sp->t_open = NAN;
	sp->t_idle = NAN;
	sp->timeout_idle = NAN;
	Lck_New(&sp->mtx, lck_sess);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp);
}

/*--------------------------------------------------------------------
 * Handle a session (from waiter)
 */

static void v_matchproto_(waiter_handle_f)
ses_handle(struct waited *wp, enum wait_event ev, vtim_real now)
{
	struct sess *sp;
	struct pool *pp;
	struct pool_task *tp;
	const struct transport *xp;

	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	CAST_OBJ_NOTNULL(sp, wp->priv1, SESS_MAGIC);
	CAST_OBJ_NOTNULL(xp, (const void*)wp->priv2, TRANSPORT_MAGIC);
	AN(wp->priv2);
	assert((void *)sp->ws->f == wp);
	wp->magic = 0;
	wp = NULL;

	WS_Release(sp->ws, 0);

	switch (ev) {
	case WAITER_TIMEOUT:
		SES_Delete(sp, SC_RX_TIMEOUT, now);
		break;
	case WAITER_REMCLOSE:
		SES_Delete(sp, SC_REM_CLOSE, now);
		break;
	case WAITER_ACTION:
		pp = sp->pool;
		CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
		assert(sizeof *tp <= WS_ReserveSize(sp->ws, sizeof *tp));
		tp = (void*)sp->ws->f;
		tp->func = xp->unwait;
		tp->priv = sp;
		if (Pool_Task(pp, tp, TASK_QUEUE_REQ))
			SES_Delete(sp, SC_OVERLOAD, now);
		break;
	case WAITER_CLOSE:
		WRONG("Should not see WAITER_CLOSE on client side");
		break;
	default:
		WRONG("Wrong event in ses_handle");
	}
}

/*--------------------------------------------------------------------
 */

void
SES_Wait(struct sess *sp, const struct transport *xp)
{
	struct pool *pp;
	struct waited *wp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(xp, TRANSPORT_MAGIC);
	pp = sp->pool;
	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	assert(sp->fd > 0);
	/*
	 * XXX: waiter_epoll prevents us from zeroing the struct because
	 * XXX: it keeps state across calls.
	 */
	VTCP_nonblocking(sp->fd);

	/*
	 * put struct waited on the workspace
	 */
	if (WS_ReserveSize(sp->ws, sizeof(struct waited))
	    < sizeof(struct waited)) {
		SES_Delete(sp, SC_OVERLOAD, NAN);
		return;
	}
	wp = (void*)sp->ws->f;
	INIT_OBJ(wp, WAITED_MAGIC);
	wp->fd = sp->fd;
	wp->priv1 = sp;
	wp->priv2 = (uintptr_t)xp;
	wp->idle = sp->t_idle;
	wp->func = ses_handle;
	wp->tmo = SESS_TMO(sp, timeout_idle);
	if (Wait_Enter(pp->waiter, wp))
		SES_Delete(sp, SC_PIPE_OVERFLOW, NAN);
}

/*--------------------------------------------------------------------
 * Update sc_ counters by reason
 *
 * assuming that the approximation of non-atomic global counters is sufficient.
 * if not: update to per-wrk
 */

static void
ses_close_acct(enum sess_close reason)
{
	int i = 0;

	assert(reason != SC_NULL);
	switch (reason) {
#define SESS_CLOSE(reason, stat, err, desc)		\
	case SC_ ## reason:				\
		VSC_C_main->sc_ ## stat++;		\
		i = err;				\
		break;
#include "tbl/sess_close.h"

	default:
		WRONG("Wrong event in ses_close_acct");
	}
	if (i)
		VSC_C_main->sess_closed_err++;
}

/*--------------------------------------------------------------------
 * Close a session's connection.
 * XXX: Technically speaking we should catch a t_end timestamp here
 * XXX: for SES_Delete() to use.
 */

void
SES_Close(struct sess *sp, enum sess_close reason)
{
	int i;

	assert(reason > 0);
	assert(sp->fd > 0);
	i = close(sp->fd);
	assert(i == 0 || errno != EBADF); /* XXX EINVAL seen */
	sp->fd = -(int)reason;
	ses_close_acct(reason);
}

/*--------------------------------------------------------------------
 * Report and dismantle a session.
 */

void
SES_Delete(struct sess *sp, enum sess_close reason, vtim_real now)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	if (reason != SC_NULL)
		SES_Close(sp, reason);
	assert(sp->fd < 0);

	if (isnan(now))
		now = VTIM_real();
	AZ(isnan(sp->t_open));
	if (now < sp->t_open) {
		VSL(SLT_Debug, sp->vxid,
		    "Clock step (now=%f < t_open=%f)",
		    now, sp->t_open);
		if (now + cache_param->clock_step < sp->t_open)
			WRONG("Clock step detected");
		now = sp->t_open; /* Do not log negatives */
	}

	if (reason == SC_NULL)
		reason = (enum sess_close)-sp->fd;

	VSL(SLT_SessClose, sp->vxid, "%s %.3f",
	    sess_close_2str(reason, 0), now - sp->t_open);
	VSL(SLT_End, sp->vxid, "%s", "");
	if (WS_Overflowed(sp->ws))
		VSC_C_main->ws_session_overflow++;
	SES_Rel(sp);
}

/*--------------------------------------------------------------------
 */

void
SES_Ref(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	Lck_Lock(&sp->mtx);
	assert(sp->refcnt > 0);
	sp->refcnt++;
	Lck_Unlock(&sp->mtx);
}

void
SES_Rel(struct sess *sp)
{
	int i;
	struct pool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->pool;
	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);

	Lck_Lock(&sp->mtx);
	assert(sp->refcnt > 0);
	i = --sp->refcnt;
	Lck_Unlock(&sp->mtx);
	if (i)
		return;
	Lck_Delete(&sp->mtx);
	MPL_Free(sp->pool->mpl_sess, sp);
}

/*--------------------------------------------------------------------
 * Create and delete pools
 */

void
SES_NewPool(struct pool *pp, unsigned pool_no)
{
	char nb[8];

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	bprintf(nb, "req%u", pool_no);
	pp->mpl_req = MPL_New(nb, &cache_param->req_pool,
	    &cache_param->workspace_client);
	bprintf(nb, "sess%u", pool_no);
	pp->mpl_sess = MPL_New(nb, &cache_param->sess_pool,
	    &cache_param->workspace_session);

	pp->waiter = Waiter_New();
}

void
SES_DestroyPool(struct pool *pp)
{
	MPL_Destroy(&pp->mpl_req);
	MPL_Destroy(&pp->mpl_sess);
	Waiter_Destroy(&pp->waiter);
}
