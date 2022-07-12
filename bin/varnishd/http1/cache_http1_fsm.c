/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * This file contains the two central state machine for pushing HTTP1
 * sessions through their states.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_objhead.h"
#include "cache/cache_transport.h"
#include "cache_http1.h"

#include "vtcp.h"

static const char H1NEWREQ[] = "HTTP1::NewReq";
static const char H1PROC[] = "HTTP1::Proc";
static const char H1CLEANUP[] = "HTTP1::Cleanup";

static void HTTP1_Session(struct worker *, struct req *);

static void
http1_setstate(const struct sess *sp, const char *s)
{
	uintptr_t p;

	p = (uintptr_t)s;
	AZ(SES_Set_proto_priv(sp, &p));
}

static const char *
http1_getstate(const struct sess *sp)
{
	uintptr_t *p;

	AZ(SES_Get_proto_priv(sp, &p));
	return ((const char *)*p);
}

/*--------------------------------------------------------------------
 * Call protocol for this request
 */

static void v_matchproto_(task_func_t)
http1_req(struct worker *wrk, void *arg)
{
	struct req *req;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);

	THR_SetRequest(req);
	req->transport = &HTTP1_transport;
	assert(!WS_IsReserved(wrk->aws));
	HTTP1_Session(wrk, req);
	AZ(wrk->v1l);
	WS_Assert(wrk->aws);
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

static void v_matchproto_(task_func_t)
http1_new_session(struct worker *wrk, void *arg)
{
	struct sess *sp;
	struct req *req;
	uintptr_t *u;
	ssize_t sz;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	HTC_RxInit(req->htc, req->ws);
	if (!SES_Reserve_proto_priv(sp, &u, &sz)) {
		/* Out of session workspace. Free the req, close the sess,
		 * and do not set a new task func, which will exit the
		 * worker thread. */
		VSL(SLT_Error, req->sp->vxid,
		    "insufficient workspace (proto_priv)");
		WS_Release(req->ws, 0);
		Req_Release(req);
		SES_Delete(sp, SC_RX_JUNK, NAN);
		return;
	}
	assert(sz == sizeof u);
	http1_setstate(sp, H1NEWREQ);
	wrk->task->func = http1_req;
	wrk->task->priv = req;
}

static void v_matchproto_(task_func_t)
http1_unwait(struct worker *wrk, void *arg)
{
	struct sess *sp;
	struct req *req;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(sp, arg, SESS_MAGIC);
	WS_Release(sp->ws, 0);
	req = Req_New(sp);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	req->htc->rfd = &sp->fd;
	HTC_RxInit(req->htc, req->ws);
	http1_setstate(sp, H1NEWREQ);
	wrk->task->func = http1_req;
	wrk->task->priv = req;
}

static void v_matchproto_(vtr_req_body_t)
http1_req_body(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (V1F_Setup_Fetch(req->vfc, req->htc) != 0)
		req->req_body_status = BS_ERROR;
}

static void
http1_sess_panic(struct vsb *vsb, const struct sess *sp)
{

	VSB_printf(vsb, "state = %s\n", http1_getstate(sp));
}

static void
http1_req_panic(struct vsb *vsb, const struct req *req)
{

	VSB_printf(vsb, "state = %s\n", http1_getstate(req->sp));
}

static void v_matchproto_(vtr_req_fail_f)
http1_req_fail(struct req *req, stream_close_t reason)
{
	assert(reason != SC_NULL);
	assert(req->sp->fd != 0);
	if (req->sp->fd > 0)
		SES_Close(req->sp, reason);
}

static int v_matchproto_(vtr_minimal_response_f)
http1_minimal_response(struct req *req, uint16_t status)
{
	ssize_t wl, l;
	char buf[80];
	const char *reason;

	assert(status >= 100);
	assert(status < 1000);

	reason = http_Status2Reason(status, NULL);

	bprintf(buf, "HTTP/1.1 %03d %s\r\n\r\n", status, reason);
	l = strlen(buf);

	VSLb(req->vsl, SLT_RespProtocol, "HTTP/1.1");
	VSLb(req->vsl, SLT_RespStatus, "%03d", status);
	VSLbs(req->vsl, SLT_RespReason, TOSTRAND(reason));

	if (status >= 400)
		req->err_code = status;
	wl = write(req->sp->fd, buf, l);

	if (wl > 0)
		req->acct.resp_hdrbytes += wl;
	if (wl != l) {
		if (wl < 0)
			VTCP_Assert(1);
		if (req->doclose == SC_NULL)
			req->doclose = SC_REM_CLOSE;
		return (-1);
	}
	return (0);
}

struct transport HTTP1_transport = {
	.name =			"HTTP/1",
	.proto_ident =		"HTTP",
	.magic =		TRANSPORT_MAGIC,
	.deliver =		V1D_Deliver,
	.minimal_response =	http1_minimal_response,
	.new_session =		http1_new_session,
	.req_body =		http1_req_body,
	.req_fail =		http1_req_fail,
	.req_panic =		http1_req_panic,
	.sess_panic =		http1_sess_panic,
	.unwait =		http1_unwait,
};

/*----------------------------------------------------------------------
 */

static inline void
http1_abort(struct req *req, uint16_t status)
{
	assert(req->doclose != SC_NULL);
	assert(status >= 400);
	(void)http1_minimal_response(req, status);
}

static int
http1_dissect(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->transport, TRANSPORT_MAGIC);

	/* Allocate a new vxid now that we know we'll need it. */
	assert(IS_NO_VXID(req->vsl->wid));
	req->vsl->wid = VXID_Get(wrk, VSL_CLIENTMARKER);

	VSLb(req->vsl, SLT_Begin, "req %ju rxreq", VXID(req->sp->vxid));
	VSL(SLT_Link, req->sp->vxid, "req %ju rxreq", VXID(req->vsl->wid));
	AZ(isnan(req->t_first)); /* First byte timestamp set by http1_wait */
	AZ(isnan(req->t_req));	 /* Complete req rcvd set by http1_wait */
	req->t_prev = req->t_first;
	VSLb_ts_req(req, "Start", req->t_first);
	VSLb_ts_req(req, "Req", req->t_req);

	HTTP_Setup(req->http, req->ws, req->vsl, SLT_ReqMethod);
	req->err_code = HTTP1_DissectRequest(req->htc, req->http);

	/* If we could not even parse the request, just close */
	if (req->err_code != 0) {
		VSLb(req->vsl, SLT_HttpGarbage, "%.*s",
		    (int)(req->htc->rxbuf_e - req->htc->rxbuf_b),
		    req->htc->rxbuf_b);
		wrk->stats->client_req_400++;

		(void)Req_LogStart(wrk, req);

		req->doclose = SC_RX_JUNK;
		http1_abort(req, 400);
		return (-1);
	}

	AZ(req->req_body_status);
	req->req_body_status = req->htc->body_status;
	return (0);
}

/*----------------------------------------------------------------------
 */

static void
HTTP1_Session(struct worker *wrk, struct req *req)
{
	enum htc_status_e hs;
	struct sess *sp;
	const char *st;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	/*
	 * Whenever we come in from the acceptor or waiter, we need to set
	 * blocking mode.  It would be simpler to do this in the acceptor
	 * or waiter, but we'd rather do the syscall in the worker thread.
	 */
	if (http1_getstate(sp) == H1NEWREQ) {
		VTCP_blocking(sp->fd);
		if (!cache_param->accept_traffic) {
			assert(!WS_IsReserved(wrk->aws));
			assert(WS_IsReserved(req->ws));
			WS_Release(req->htc->ws, 0);
			req->doclose = SC_TRAFFIC_REFUSE;
			http1_abort(req, 503);
			Req_AcctLogCharge(wrk->stats, req);
			SES_Close(req->sp, req->doclose);
			http1_setstate(sp, H1CLEANUP);
		}
	}

	req->transport = &HTTP1_transport;

	while (1) {
		st = http1_getstate(sp);
		if (st == H1NEWREQ) {
			CHECK_OBJ_NOTNULL(req->transport, TRANSPORT_MAGIC);
			assert(isnan(req->t_prev));
			assert(isnan(req->t_req));
			AZ(req->vcl);
			AZ(req->esi_level);
			AN(WS_Reservation(req->htc->ws));

			hs = HTC_RxStuff(req->htc, HTTP1_Complete,
			    &req->t_first, &req->t_req,
			    sp->t_idle + SESS_TMO(sp, timeout_linger),
			    sp->t_idle + SESS_TMO(sp, timeout_idle),
			    NAN,
			    cache_param->http_req_size);
			assert(!WS_IsReserved(req->htc->ws));
			if (hs < HTC_S_EMPTY) {
				req->acct.req_hdrbytes +=
				    req->htc->rxbuf_e - req->htc->rxbuf_b;
				Req_AcctLogCharge(wrk->stats, req);
				Req_Release(req);
				SES_DeleteHS(sp, hs, NAN);
				return;
			}
			if (hs == HTC_S_IDLE) {
				wrk->stats->sess_herd++;
				Req_Release(req);
				SES_Wait(sp, &HTTP1_transport);
				return;
			}
			if (hs != HTC_S_COMPLETE)
				WRONG("htc_status (nonbad)");

			if (H2_prism_complete(req->htc) == HTC_S_COMPLETE) {
				if (!FEATURE(FEATURE_HTTP2)) {
					SES_Close(req->sp, SC_REQ_HTTP20);
					assert(!WS_IsReserved(req->ws));
					assert(!WS_IsReserved(wrk->aws));
					http1_setstate(sp, H1CLEANUP);
					continue;
				}
				http1_setstate(sp, NULL);
				H2_PU_Sess(wrk, sp, req);
				return;
			}

			i = http1_dissect(wrk, req);
			req->acct.req_hdrbytes +=
			    req->htc->rxbuf_e - req->htc->rxbuf_b;
			if (i) {
				assert(req->doclose != SC_NULL);
				SES_Close(req->sp, req->doclose);
				assert(!WS_IsReserved(req->ws));
				assert(!WS_IsReserved(wrk->aws));
				http1_setstate(sp, H1CLEANUP);
				continue;
			}
			if (http_HdrIs(req->http, H_Upgrade, "h2c")) {
				if (!FEATURE(FEATURE_HTTP2)) {
					VSLb(req->vsl, SLT_Debug,
					    "H2 upgrade attempt");
				} else if (req->htc->body_status != BS_NONE) {
					VSLb(req->vsl, SLT_Debug,
					    "H2 upgrade attempt has body");
				} else {
					http1_setstate(sp, NULL);
					req->err_code = 2;
					H2_OU_Sess(wrk, sp, req);
					return;
				}
			}
			assert(req->req_step == R_STP_TRANSPORT);
			VCL_TaskEnter(req->privs);
			VCL_TaskEnter(req->top->privs);
			http1_setstate(sp, H1PROC);
		} else if (st == H1PROC) {
			req->task->func = http1_req;
			req->task->priv = req;
			CNT_Embark(wrk, req);
			if (CNT_Request(req) == REQ_FSM_DISEMBARK)
				return;
			wrk->stats->client_req++;
			AZ(req->top->vcl0);
			req->task->func = NULL;
			req->task->priv = NULL;
			assert(!WS_IsReserved(req->ws));
			assert(!WS_IsReserved(wrk->aws));
			http1_setstate(sp, H1CLEANUP);
		} else if (st == H1CLEANUP) {

			assert(!WS_IsReserved(wrk->aws));
			assert(!WS_IsReserved(req->ws));

			if (!cache_param->accept_traffic &&
			    req->doclose == SC_NULL)
				req->doclose = SC_TRAFFIC_REFUSE;

			if (sp->fd >= 0 && req->doclose != SC_NULL)
				SES_Close(sp, req->doclose);

			if (sp->fd < 0) {
				wrk->stats->sess_closed++;
				Req_Cleanup(sp, wrk, req);
				Req_Release(req);
				SES_Delete(sp, SC_NULL, NAN);
				return;
			}

			Req_Cleanup(sp, wrk, req);
			HTC_RxInit(req->htc, req->ws);
			if (req->htc->rxbuf_e != req->htc->rxbuf_b)
				wrk->stats->sess_readahead++;
			if (FEATURE(FEATURE_BUSY_STATS_RATE))
				WRK_AddStat(wrk);
			http1_setstate(sp, H1NEWREQ);
		} else {
			WRONG("Wrong H1 session state");
		}
	}
}
