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
 * Session management
 *
 * The overall goal here is to hold as little state as possible for an
 * idle session.  This leads to various nasty-ish overloads of struct
 * sess fields, for instance ->fd being negative ->reason.
 *
 */
//lint --e{766}

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

static const struct {
	const char		*type;
} sess_attr[SA_LAST] = {
#define SESS_ATTR(UC, lc, typ, len) [SA_##UC] = { #typ },
#include "tbl/sess_attr.h"
};

enum sess_close {
	SCE_NULL = 0,
#define SESS_CLOSE(nm, stat, err, desc) SCE_##nm,
#include "tbl/sess_close.h"
	SCE_MAX,
};

const struct stream_close SC_NULL[1] = {{
	.magic = STREAM_CLOSE_MAGIC,
	.idx = SCE_NULL,
	.is_err = 0,
	.name = "null",
	.desc = "Not Closing",
}};

#define SESS_CLOSE(nm, stat, err, text) \
	const struct stream_close SC_##nm[1] = {{ \
		.magic = STREAM_CLOSE_MAGIC, \
		.idx = SCE_##nm, \
		.is_err = err, \
		.name = #nm, \
		.desc = text, \
	}};
#include "tbl/sess_close.h"

static const stream_close_t sc_lookup[SCE_MAX] = {
	[SCE_NULL] = SC_NULL,
#define SESS_CLOSE(nm, stat, err, desc) \
	[SCE_##nm] = SC_##nm,
#include "tbl/sess_close.h"
};

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
	wrk->task->func = xp->new_session;
	wrk->task->priv = req;
}

/*--------------------------------------------------------------------*/

#define SES_NOATTR_OFFSET 0xffff

static int
ses_get_attr(const struct sess *sp, enum sess_attr a, void **dst)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(a < SA_LAST);
	AN(dst);

	if (sp->sattr[a] == SES_NOATTR_OFFSET) {
		*dst = NULL;
		return (-1);
	}
	*dst = WS_AtOffset(sp->ws, sp->sattr[a], 0);
	return (0);
}

static int
ses_set_attr(const struct sess *sp, enum sess_attr a, const void *src, int sz)
{
	void *dst;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(a < SA_LAST);
	AN(src);
	assert(sz > 0);

	if (sp->sattr[a] == SES_NOATTR_OFFSET)
		return (-1);
	dst = WS_AtOffset(sp->ws, sp->sattr[a], sz);
	AN(dst);
	memcpy(dst, src, sz);
	return (0);
}

static int
ses_res_attr(struct sess *sp, enum sess_attr a, void **dst, ssize_t *szp)
{
	unsigned o;
	ssize_t sz;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(a < SA_LAST);
	AN(dst);
	sz = *szp;
	*szp = 0;
	assert(sz >= 0);
	if (WS_ReserveSize(sp->ws, sz) == 0)
		return (0);
	o = WS_ReservationOffset(sp->ws);
	if (o >= SES_NOATTR_OFFSET) {
		WS_Release(sp->ws, 0);
		return (0);
	}
	*dst = WS_Reservation(sp->ws);
	*szp = sz;
	sp->sattr[a] = (uint16_t)o;
	WS_Release(sp->ws, sz);
	return (1);
}

#define SESS_ATTR(UP, low, typ, len)					\
	int								\
	SES_Set_##low(const struct sess *sp, const typ *src)		\
	{								\
		assert(len > 0);					\
		return (ses_set_attr(sp, SA_##UP, src, len));		\
	}								\
									\
	int								\
	SES_Get_##low(const struct sess *sp, typ **dst)			\
	{								\
		assert(len > 0);					\
		return (ses_get_attr(sp, SA_##UP, (void**)dst));	\
	}								\
									\
	int								\
	SES_Reserve_##low(struct sess *sp, typ **dst, ssize_t *sz)	\
	{								\
		assert(len > 0);					\
		AN(sz);							\
		*sz = len;						\
		return (ses_res_attr(sp, SA_##UP, (void**)dst, sz));	\
	}

#include "tbl/sess_attr.h"

int
SES_Set_String_Attr(struct sess *sp, enum sess_attr a, const char *src)
{
	void *q;
	ssize_t l, sz;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(src);

	assert(a <  SA_LAST);
	if (strcmp(sess_attr[a].type, "char"))
		WRONG("wrong sess_attr: not char");

	l = sz = strlen(src) + 1;
	if (! ses_res_attr(sp, a, &q, &sz))
		return (0);
	assert(l == sz);
	strcpy(q, src);
	return (1);
}

const char *
SES_Get_String_Attr(const struct sess *sp, enum sess_attr a)
{
	void *q;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	assert(a <  SA_LAST);
	if (strcmp(sess_attr[a].type, "char"))
		WRONG("wrong sess_attr: not char");

	if (ses_get_attr(sp, a, &q) < 0)
		return (NULL);
	return (q);
}

/*--------------------------------------------------------------------*/

void
HTC_Status(enum htc_status_e e, const char **name, const char **desc)
{

	switch (e) {
#define HTC_STATUS(e, n, s, l)				\
	case HTC_S_ ## e:				\
		*name = s;				\
		*desc = l;				\
		return;
#include "tbl/htc.h"
	default:
		WRONG("HTC_Status");
	}
}

/*--------------------------------------------------------------------*/

void
HTC_RxInit(struct http_conn *htc, struct ws *ws)
{
	unsigned rollback;
	int l;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	htc->ws = ws;

	/* NB: HTTP/1 keep-alive triggers a rollback, so does the first
	 * request of a session or an h2 request where the rollback is a
	 * no-op in terms of workspace usage.
	 */
	rollback = !strcasecmp(ws->id, "req") && htc->body_status == NULL;
	l = WS_Pipeline(htc->ws, htc->pipeline_b, htc->pipeline_e, rollback);
	xxxassert(l >= 0);

	htc->rxbuf_b = WS_Reservation(ws);
	htc->rxbuf_e = htc->rxbuf_b + l;
	htc->pipeline_b = NULL;
	htc->pipeline_e = NULL;
}

void
HTC_RxPipeline(struct http_conn *htc, char *p)
{

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	assert(p >= htc->rxbuf_b);
	assert(p <= htc->rxbuf_e);
	if (p == htc->rxbuf_e) {
		htc->pipeline_b = NULL;
		htc->pipeline_e = NULL;
	} else {
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
	unsigned l, r;
	ssize_t z;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(htc->rfd);
	assert(*htc->rfd > 0);
	AN(htc->rxbuf_b);
	AN(WS_Reservation(htc->ws));

	l = pdiff(htc->rxbuf_b, htc->rxbuf_e);
	r = WS_ReservationSize(htc->ws);
	assert(l <= r);

	AZ(isnan(tn) && isnan(td));
	if (t1 != NULL)
		assert(isnan(*t1));

	if (l == r) {
		/* Can't work with a zero size buffer */
		WS_ReleaseP(htc->ws, htc->rxbuf_b);
		return (HTC_S_OVERFLOW);
	}
	z = r;
	if (z < maxbytes)
		maxbytes = z;	/* Cap maxbytes at available WS */

	while (1) {
		now = VTIM_real();
		AZ(htc->pipeline_b);
		AZ(htc->pipeline_e);
		l = pdiff(htc->rxbuf_b, htc->rxbuf_e);
		assert(l <= r);

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
	AN(sp);
	INIT_OBJ(sp, SESS_MAGIC);
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
	sp->timeout_linger = NAN;
	sp->send_timeout = NAN;
	sp->idle_send_timeout = NAN;
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
	CAST_OBJ_NOTNULL(xp, wp->priv2, TRANSPORT_MAGIC);
	assert(WS_Reservation(sp->ws) == wp);
	FINI_OBJ(wp);

	/* The WS was reserved in SES_Wait() */
	WS_Release(sp->ws, 0);

	switch (ev) {
	case WAITER_TIMEOUT:
		SES_Delete(sp, SC_RX_CLOSE_IDLE, now);
		break;
	case WAITER_REMCLOSE:
		SES_Delete(sp, SC_REM_CLOSE, now);
		break;
	case WAITER_ACTION:
		pp = sp->pool;
		CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
		/* SES_Wait() guarantees the next will not assert. */
		assert(sizeof *tp <= WS_ReserveSize(sp->ws, sizeof *tp));
		tp = WS_Reservation(sp->ws);
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
	unsigned u;

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
	 * Put struct waited on the workspace. Make sure that the
	 * workspace can hold enough space for both struct waited
	 * and pool_task, as pool_task will be needed when coming
	 * off the waiter again.
	 */
	u = WS_ReserveAll(sp->ws);
	if (u < sizeof (struct waited) || u < sizeof(struct pool_task)) {
		WS_MarkOverflow(sp->ws);
		SES_Delete(sp, SC_OVERLOAD, NAN);
		return;
	}

	wp = WS_Reservation(sp->ws);
	INIT_OBJ(wp, WAITED_MAGIC);
	wp->fd = sp->fd;
	wp->priv1 = sp;
	wp->priv2 = xp;
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
ses_close_acct(stream_close_t reason)
{

	CHECK_OBJ_NOTNULL(reason, STREAM_CLOSE_MAGIC);
	switch (reason->idx) {
#define SESS_CLOSE(reason, stat, err, desc)		\
	case SCE_ ## reason:				\
		VSC_C_main->sc_ ## stat++;		\
		break;
#include "tbl/sess_close.h"

	default:
		WRONG("Wrong event in ses_close_acct");
	}
	if (reason->is_err)
		VSC_C_main->sess_closed_err++;
}

/*--------------------------------------------------------------------
 * Close a session's connection.
 * XXX: Technically speaking we should catch a t_end timestamp here
 * XXX: for SES_Delete() to use.
 */

void
SES_Close(struct sess *sp, stream_close_t reason)
{
	int i;

	CHECK_OBJ_NOTNULL(reason, STREAM_CLOSE_MAGIC);
	assert(reason->idx > 0);
	assert(sp->fd > 0);
	i = close(sp->fd);
	assert(i == 0 || errno != EBADF); /* XXX EINVAL seen */
	sp->fd = -reason->idx;
	ses_close_acct(reason);
}

/*--------------------------------------------------------------------
 * Report and dismantle a session.
 */

void
SES_Delete(struct sess *sp, stream_close_t reason, vtim_real now)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(reason, STREAM_CLOSE_MAGIC);

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

	if (reason == SC_NULL) {
		assert(sp->fd < 0 && -sp->fd < SCE_MAX);
		reason = sc_lookup[-sp->fd];
	}

	CHECK_OBJ_NOTNULL(reason, STREAM_CLOSE_MAGIC);
	VSL(SLT_SessClose, sp->vxid, "%s %.3f", reason->name, now - sp->t_open);
	VSL(SLT_End, sp->vxid, "%s", "");
	if (WS_Overflowed(sp->ws))
		VSC_C_main->ws_session_overflow++;
	SES_Rel(sp);
}

void
SES_DeleteHS(struct sess *sp, enum htc_status_e hs, vtim_real now)
{
	stream_close_t reason;

	switch (hs) {
	case HTC_S_JUNK:
		reason = SC_RX_JUNK;
		break;
	case HTC_S_CLOSE:
		reason = SC_REM_CLOSE;
		break;
	case HTC_S_TIMEOUT:
		reason = SC_RX_TIMEOUT;
		break;
	case HTC_S_OVERFLOW:
		reason = SC_RX_OVERFLOW;
		break;
	case HTC_S_EOF:
		reason = SC_REM_CLOSE;
		break;
	default:
		WRONG("htc_status (bad)");
	}
	SES_Delete(sp, reason, now);
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
#ifdef ENABLE_WORKSPACE_EMULATOR
	WS_Rollback(sp->ws, 0);
#endif
	MPL_Free(sp->pool->mpl_sess, sp);
}

/*--------------------------------------------------------------------
 * Create and delete pools
 */

void
SES_NewPool(struct pool *pp, unsigned pool_no)
{
	char nb[4 /* "sess" */ + 10 /* "%u" */ + 1];

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	bprintf(nb, "req%u", pool_no);
	pp->mpl_req = MPL_New(nb, &cache_param->pool_req,
	    &cache_param->workspace_client);
	bprintf(nb, "sess%u", pool_no);
	pp->mpl_sess = MPL_New(nb, &cache_param->pool_sess,
	    &cache_param->workspace_session);

	bprintf(nb, "pool%u", pool_no);
	pp->waiter = Waiter_New(nb);
}

void
SES_DestroyPool(struct pool *pp)
{
	MPL_Destroy(&pp->mpl_req);
	MPL_Destroy(&pp->mpl_sess);
	Waiter_Destroy(&pp->waiter);
}
