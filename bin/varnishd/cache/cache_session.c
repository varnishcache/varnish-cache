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
 * This is a little bit of a mixed bag, containing both memory management
 * and various state-change functions.
 *
 * The overall picture is complicated by the fact that requests can
 * disembark their worker-threads if they hit a busy object, then come
 * back later in a different worker thread to continue.
 * XXX: I wonder if that complexity pays of any more ?
 *
 */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "waiter/waiter.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

/*--------------------------------------------------------------------*/

struct sesspool {
	unsigned		magic;
#define SESSPOOL_MAGIC		0xd916e202
	struct pool		*pool;
	struct mempool		*mpl_req;
	struct mempool		*mpl_sess;

	struct waiter		*http1_waiter;
};

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

static void
ses_reserve_attr(struct sess *sp, enum sess_attr a, void **dst, int sz)
{
	ssize_t o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(a < SA_LAST);
	assert(sz >= 0);
	AN(dst);
	o = WS_Reserve(sp->ws, sz);
	assert(o >= sz);
	*dst = sp->ws->f;
	o = sp->ws->f - sp->ws->s;
	WS_Release(sp->ws, sz);
	assert(o >= 0 && o <= 0xffff);
	sp->sattr[a] = (uint16_t)o;
}

#define SESS_ATTR(UP, low, typ, len)					\
	int								\
	SES_Get_##low(const struct sess *sp, typ *dst)			\
	{								\
		return (ses_get_attr(sp, SA_##UP, (void**)dst));	\
	}								\
									\
	void								\
	SES_Reserve_##low(struct sess *sp, typ *dst)			\
	{								\
		assert(len >= 0);					\
		ses_reserve_attr(sp, SA_##UP, (void*)dst, len);		\
	}

#include "tbl/sess_attr.h"
#undef SESS_ATTR

void
SES_Set_String_Attr(struct sess *sp, enum sess_attr a, const char *src)
{
	void *q;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(src);

	switch (a) {
#define SESS_ATTR(UP, low, typ, len)	case SA_##UP: assert(len < 0); break;
#include "tbl/sess_attr.h"
#undef SESS_ATTR
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
#undef SESS_ATTR
	default:  WRONG("wrong sess_attr");
	}

	if (ses_get_attr(sp, a, &q) < 0)
		return (NULL);
	return (q);
}

/*--------------------------------------------------------------------*/

void
SES_RxInit(struct http_conn *htc, struct ws *ws, unsigned maxbytes,
    unsigned maxhdr)
{

	htc->magic = HTTP_CONN_MAGIC;
	htc->ws = ws;
	htc->maxbytes = maxbytes;
	htc->maxhdr = maxhdr;

	(void)WS_Reserve(htc->ws, htc->maxbytes);
	htc->rxbuf_b = ws->f;
	htc->rxbuf_e = ws->f;
	*htc->rxbuf_e = '\0';
	htc->pipeline_b = NULL;
	htc->pipeline_e = NULL;
}

/*--------------------------------------------------------------------
 * Start over, and recycle any pipelined input.
 * The WS_Reset is safe, even though the pipelined input is stored in
 * the ws somewhere, because WS_Reset only fiddles pointers.
 */

void
SES_RxReInit(struct http_conn *htc)
{
	ssize_t l;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	(void)WS_Reserve(htc->ws, htc->maxbytes);
	htc->rxbuf_b = htc->ws->f;
	htc->rxbuf_e = htc->ws->f;
	if (htc->pipeline_b != NULL) {
		l = htc->pipeline_e - htc->pipeline_b;
		assert(l > 0);
		memmove(htc->rxbuf_b, htc->pipeline_b, l);
		htc->rxbuf_e += l;
		htc->pipeline_b = NULL;
		htc->pipeline_e = NULL;
	}
	*htc->rxbuf_e = '\0';
}

/*--------------------------------------------------------------------
 * Receive more HTTP protocol bytes
 */

enum htc_status_e
SES_Rx(struct http_conn *htc)
{
	int i;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(htc->ws->r);
	AZ(htc->pipeline_b);
	AZ(htc->pipeline_e);
	i = (htc->ws->r - htc->rxbuf_e) - 1;	/* space for NUL */
	if (i <= 0) {
		WS_ReleaseP(htc->ws, htc->rxbuf_b);
		return (HTC_S_OVERFLOW);
	}
	i = read(htc->fd, htc->rxbuf_e, i);
	if (i <= 0) {
		/*
		 * We wouldn't come here if we had a complete HTTP header
		 * so consequently an EOF can not be OK
		 */
		WS_ReleaseP(htc->ws, htc->rxbuf_b);
		return (HTC_S_EOF);
	}
	htc->rxbuf_e += i;
	*htc->rxbuf_e = '\0';
	return (HTC_S_OK);
}

/*--------------------------------------------------------------------
 * Get a new session, preferably by recycling an already ready one
 *
 * Layout is:
 *	struct sess
 *	workspace
 */

struct sess *
SES_New(struct sesspool *pp)
{
	struct sess *sp;
	unsigned sz;
	char *p, *e;

	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	sp = MPL_Get(pp->mpl_sess, &sz);
	sp->magic = SESS_MAGIC;
	sp->sesspool = pp;
	memset(sp->sattr, 0xff, sizeof sp->sattr);

	e = (char*)sp + sz;
	p = (char*)(sp + 1);
	p = (void*)PRNDUP(p);
	assert(p < e);
	WS_Init(sp->ws, "ses", p, e - p);

	sp->t_open = NAN;
	sp->t_idle = NAN;
	VRTPRIV_init(sp->privs);
	Lck_New(&sp->mtx, lck_sess);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp);
}

/*--------------------------------------------------------------------
 * Call protocol for this request
 */

void __match_proto__(task_func_t)
SES_Proto_Req(struct worker *wrk, void *arg)
{
	struct req *req;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);

	THR_SetRequest(req);
	AZ(wrk->aws->r);
	if (req->sp->sess_step < S_STP_H1_LAST) {
		HTTP1_Session(wrk, req);
		AZ(wrk->v1l);
	} else {
		WRONG("Wrong session step");
	}
	WS_Assert(wrk->aws);
	if (DO_DEBUG(DBG_VCLREL) && wrk->vcl != NULL)
		VCL_Rel(&wrk->vcl);
	THR_SetRequest(NULL);
}

/*--------------------------------------------------------------------
 * Call protocol for this session (new or from waiter)
 *
 * When sessions are rescheduled from the waiter, a struct pool_task
 * is put on the reserved session workspace (for reasons of memory
 * conservation).  This reservation is released as the first thing.
 * The acceptor and any other code which schedules this function
 * must obey this calling convention with a dummy reservation.
 */

void __match_proto__(task_func_t)
SES_Proto_Sess(struct worker *wrk, void *arg)
{
	struct req *req;
	struct sess *sp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(sp, arg, SESS_MAGIC);
	WS_Release(sp->ws, 0);

	/*
	 * Assume we're going to receive something that will likely
	 * involve a request...
	 */
	(void)VTCP_blocking(sp->fd);
	req = SES_GetReq(wrk, sp);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	req->htc->fd = sp->fd;
	SES_RxInit(req->htc, req->ws,
	    cache_param->http_req_size, cache_param->http_req_hdr_len);

	if (sp->sess_step < S_STP_H1_LAST) {
		sp->sess_step = S_STP_H1NEWREQ;
		wrk->task.func = SES_Proto_Req;
		wrk->task.priv = req;
	} else if (sp->sess_step < S_STP_PROXY_LAST) {
		wrk->task.func = VPX_Proto_Sess;
		wrk->task.priv = req;
	} else {
		WRONG("Wrong session step");
	}
}

/*--------------------------------------------------------------------
 * Reschedule a request on a work-thread from its sessions pool
 *
 * This is used to reschedule requests waiting on busy objects
 */

int
SES_Reschedule_Req(struct req *req)
{
	struct sess *sp;
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	req->task.func = SES_Proto_Req;
	req->task.priv = req;

	return (Pool_Task(pp->pool, &req->task, POOL_QUEUE_FRONT));
}

/*--------------------------------------------------------------------
 * Handle a session (from waiter)
 */

static void __match_proto__(waiter_handle_f)
ses_handle(struct waited *wp, enum wait_event ev, double now)
{
	struct sess *sp;
	struct sesspool *pp;
	struct pool_task *tp;

	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	CAST_OBJ_NOTNULL(sp, wp->ptr, SESS_MAGIC);

	AZ(sp->ws->r);

	switch (ev) {
	case WAITER_TIMEOUT:
		SES_Delete(sp, SC_RX_TIMEOUT, now);
		break;
	case WAITER_REMCLOSE:
		SES_Delete(sp, SC_REM_CLOSE, now);
		break;
	case WAITER_ACTION:
		pp = sp->sesspool;
		CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
		AN(pp->pool);
		assert(sizeof *tp == WS_Reserve(sp->ws, sizeof *tp));
		tp = (void*)sp->ws->f;
		tp->func = SES_Proto_Sess;
		tp->priv = sp;
		if (Pool_Task(pp->pool, tp, POOL_QUEUE_FRONT))
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
SES_Wait(struct sess *sp)
{
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	/*
	 * XXX: waiter_epoll prevents us from zeroing the struct because
	 * XXX: it keeps state across calls.
	 */
	sp->waited.magic = WAITED_MAGIC;
	sp->waited.fd = sp->fd;
	sp->waited.ptr = sp;
	sp->waited.idle = sp->t_idle;
	if (Wait_Enter(pp->http1_waiter, &sp->waited)) {
		SES_Delete(sp, SC_PIPE_OVERFLOW, NAN);
	}
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
#undef SESS_CLOSE
	default:
		WRONG("Wrong event in ses_close_acct");
	}
	if (i)
		VSC_C_main->sess_closed_err++;
}

/*--------------------------------------------------------------------
 * Close a sessions connection.
 * XXX: Technically speaking we should catch a t_end timestamp here
 * XXX: for SES_Delete() to use.
 */

void
SES_Close(struct sess *sp, enum sess_close reason)
{
	int i;

	assert(sp->fd >= 0);
	i = close(sp->fd);
	assert(i == 0 || errno != EBADF); /* XXX EINVAL seen */
	sp->fd = -1;
	if (reason != SC_NULL)
		ses_close_acct(reason);
}

/*--------------------------------------------------------------------
 * Report and dismantle a session.
 */

void
SES_Delete(struct sess *sp, enum sess_close reason, double now)
{
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	if (reason != SC_NULL)
		SES_Close(sp, reason);
	assert(sp->fd < 0);

	if (isnan(now))
		now = VTIM_real();
	AZ(isnan(sp->t_open));

	assert(VTAILQ_EMPTY(&sp->privs->privs));
	VSL(SLT_SessClose, sp->vxid, "%s %.3f",
	    sess_close_2str(reason, 0), now - sp->t_open);
	VSL(SLT_End, sp->vxid, "%s", "");

	Lck_Delete(&sp->mtx);
	MPL_Free(pp->mpl_sess, sp);
}

/*--------------------------------------------------------------------
 * Alloc/Free a request
 */

struct req *
SES_GetReq(const struct worker *wrk, struct sess *sp)
{
	struct sesspool *pp;
	struct req *req;
	uint16_t nhttp;
	unsigned sz, hl;
	char *p, *e;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	req = MPL_Get(pp->mpl_req, &sz);
	AN(req);
	req->magic = REQ_MAGIC;
	req->sp = sp;
	req->top = req;	// esi overrides

	e = (char*)req + sz;
	p = (char*)(req + 1);
	p = (void*)PRNDUP(p);
	assert(p < e);

	nhttp = (uint16_t)cache_param->http_max_hdr;
	hl = HTTP_estimate(nhttp);

	req->http = HTTP_create(p, nhttp);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	req->http0 = HTTP_create(p, nhttp);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	req->resp = HTTP_create(p, nhttp);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	sz = cache_param->vsl_buffer;
	VSL_Setup(req->vsl, p, sz);
	p += sz;
	p = (void*)PRNDUP(p);

	assert(p < e);

	WS_Init(req->ws, "req", p, e - p);

	req->req_bodybytes = 0;

	req->t_first = NAN;
	req->t_prev = NAN;
	req->t_req = NAN;

	req->vdp_nxt = 0;
	VTAILQ_INIT(&req->vdp);

	return (req);
}

void
SES_ReleaseReq(struct req *req)
{
	struct sess *sp;
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/* Make sure the request counters have all been zeroed */
#define ACCT(foo) \
	AZ(req->acct.foo);
#include "tbl/acct_fields_req.h"
#undef ACCT

	AZ(req->vcl);
	if (req->vsl->wid)
		VSL_End(req->vsl);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	MPL_AssertSane(req);
	VSL_Flush(req->vsl, 0);
	req->sp = NULL;
	MPL_Free(pp->mpl_req, req);
}

/*--------------------------------------------------------------------
 * Create and delete pools
 */

struct sesspool *
SES_NewPool(struct pool *wp, unsigned pool_no)
{
	struct sesspool *pp;
	char nb[8];

	ALLOC_OBJ(pp, SESSPOOL_MAGIC);
	AN(pp);
	pp->pool = wp;
	bprintf(nb, "req%u", pool_no);
	pp->mpl_req = MPL_New(nb, &cache_param->req_pool,
	    &cache_param->workspace_client);
	bprintf(nb, "sess%u", pool_no);
	pp->mpl_sess = MPL_New(nb, &cache_param->sess_pool,
	    &cache_param->workspace_session);
	pp->http1_waiter = Wait_New(ses_handle, &cache_param->timeout_idle);

	VCA_New_SessPool(wp, pp);
	return (pp);
}

void
SES_DeletePool(struct sesspool *pp)
{

	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	MPL_Destroy(&pp->mpl_sess);
	MPL_Destroy(&pp->mpl_req);
	/* Delete session pool must stop acceptor threads */
	FREE_OBJ(pp);
	INCOMPL();
}
