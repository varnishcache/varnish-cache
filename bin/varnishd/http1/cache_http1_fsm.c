/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * This file contains the two central state machine for pushing HTTP1
 * sessions through their states.
 *
 */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"
#include "hash/hash_slinger.h"

#include "vcl.h"
#include "vtcp.h"
#include "vtim.h"

/*----------------------------------------------------------------------
 * Collect a request from the client.
 */

static enum req_fsm_nxt
http1_wait(struct sess *sp, struct worker *wrk, struct req *req)
{
	double tmo;
	double now, when;
	enum sess_close why = SC_NULL;
	enum htc_status_e hs;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	assert(req->sp == sp);

	AZ(req->vcl);
	AZ(req->esi_level);
	AZ(isnan(sp->t_idle));
	assert(isnan(req->t_first));
	assert(isnan(req->t_prev));
	assert(isnan(req->t_req));

	tmo = cache_param->timeout_linger;
	while (1) {
		hs = SES_Rx(req->htc, tmo);
		now = VTIM_real();
		if (hs == HTC_S_OK || hs == HTC_S_TIMEOUT)
			hs = HTTP1_Complete(req->htc);
		if (hs == HTC_S_COMPLETE) {
			/* Got it, run with it */
			if (isnan(req->t_first))
				req->t_first = now;
			if (isnan(req->t_req))
				req->t_req = now;
			req->acct.req_hdrbytes +=
			    req->htc->rxbuf_e - req->htc->rxbuf_b;
			return (REQ_FSM_MORE);
		} else if (hs == HTC_S_EOF) {
			WS_ReleaseP(req->htc->ws, req->htc->rxbuf_b);
			why = SC_REM_CLOSE;
			break;
		} else if (hs == HTC_S_OVERFLOW) {
			WS_ReleaseP(req->htc->ws, req->htc->rxbuf_b);
			why = SC_RX_OVERFLOW;
			break;
		} else if (hs == HTC_S_EMPTY) {
			/* Nothing but whitespace */
			when = sp->t_idle + cache_param->timeout_idle;
			if (when < now) {
				why = SC_RX_TIMEOUT;
				break;
			}
			when = sp->t_idle + cache_param->timeout_linger;
			tmo = when - now;
			if (tmo <= 0) {
				wrk->stats->sess_herd++;
				SES_ReleaseReq(req);
				if (VTCP_nonblocking(sp->fd))
					SES_Close(sp, SC_REM_CLOSE);
				else
					SES_Wait(sp);
				return (REQ_FSM_DONE);
			}
		} else {
			/* Working on it */
			if (isnan(req->t_first))
				/* Record first byte received time stamp */
				req->t_first = now;
			when = req->t_first + cache_param->timeout_req;
			tmo = when - now;
			if (tmo <= 0) {
				why = SC_RX_TIMEOUT;
				break;
			}
		}
	}
	req->acct.req_hdrbytes += req->htc->rxbuf_e - req->htc->rxbuf_b;
	CNT_AcctLogCharge(wrk->stats, req);
	SES_ReleaseReq(req);
	assert(why != SC_NULL);
	SES_Delete(sp, why, now);
	return (REQ_FSM_DONE);
}

/*----------------------------------------------------------------------
 * This is the final state, figure out if we should close or recycle
 * the client connection
 */

static int
http1_cleanup(struct sess *sp, struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(req->vcl, VCL_CONF_MAGIC);

	req->director_hint = NULL;
	req->restarts = 0;

	AZ(req->esi_level);
	assert(req->top == req);

	if (req->vcl != NULL) {
		if (wrk->vcl != NULL)
			VCL_Rel(&wrk->vcl);
		wrk->vcl = req->vcl;
		req->vcl = NULL;
	}

	VRTPRIV_dynamic_kill(sp->privs, (uintptr_t)req);
	VRTPRIV_dynamic_kill(sp->privs, (uintptr_t)&req->top);

	/* Charge and log byte counters */
	AN(req->vsl->wid);
	CNT_AcctLogCharge(wrk->stats, req);
	req->req_bodybytes = 0;

	VSL_End(req->vsl);

	if (!isnan(req->t_prev) && req->t_prev > 0.)
		sp->t_idle = req->t_prev;
	else
		sp->t_idle = W_TIM_real(wrk);

	req->t_first = NAN;
	req->t_prev = NAN;
	req->t_req = NAN;
	req->req_body_status = REQ_BODY_INIT;

	req->hash_always_miss = 0;
	req->hash_ignore_busy = 0;
	req->is_hit = 0;

	if (sp->fd >= 0 && req->doclose != SC_NULL)
		SES_Close(sp, req->doclose);

	if (sp->fd < 0) {
		wrk->stats->sess_closed++;
		AZ(req->vcl);
		SES_ReleaseReq(req);
		SES_Delete(sp, SC_NULL, NAN);
		return (1);
	}

	WS_Reset(req->ws, NULL);
	WS_Reset(wrk->aws, NULL);
	return (0);
}

/*----------------------------------------------------------------------
 */

static enum req_fsm_nxt
http1_dissect(struct worker *wrk, struct req *req)
{
	const char *r_100 = "HTTP/1.1 100 Continue\r\n\r\n";
	const char *r_400 = "HTTP/1.1 400 Bad Request\r\n\r\n";
	const char *r_417 = "HTTP/1.1 417 Expectation Failed\r\n\r\n";
	const char *p;
	ssize_t r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/* Allocate a new vxid now that we know we'll need it. */
	AZ(req->vsl->wid);
	req->vsl->wid = VXID_Get(wrk, VSL_CLIENTMARKER);

	VSLb(req->vsl, SLT_Begin, "req %u rxreq", VXID(req->sp->vxid));
	VSL(SLT_Link, req->sp->vxid, "req %u rxreq", VXID(req->vsl->wid));
	AZ(isnan(req->t_first)); /* First byte timestamp set by http1_wait */
	AZ(isnan(req->t_req));	 /* Complete req rcvd set by http1_wait */
	req->t_prev = req->t_first;
	VSLb_ts_req(req, "Start", req->t_first);
	VSLb_ts_req(req, "Req", req->t_req);

	/* Borrow VCL reference from worker thread */
	VCL_Refresh(&wrk->vcl);
	req->vcl = wrk->vcl;
	wrk->vcl = NULL;

	HTTP_Setup(req->http, req->ws, req->vsl, SLT_ReqMethod);
	req->err_code = HTTP1_DissectRequest(req->htc, req->http);

	/* If we could not even parse the request, just close */
	if (req->err_code != 0) {
		VSLb(req->vsl, SLT_HttpGarbage, "%.*s",
		    (int)(req->htc->rxbuf_e - req->htc->rxbuf_b),
		    req->htc->rxbuf_b);
		wrk->stats->client_req_400++;
		r = write(req->sp->fd, r_400, strlen(r_400));
		if (r > 0)
			req->acct.resp_hdrbytes += r;
		SES_Close(req->sp, SC_RX_JUNK);
		return (REQ_FSM_DONE);
	}

	assert (req->req_body_status == REQ_BODY_INIT);

	if (req->htc->body_status == BS_CHUNKED) {
		req->req_body_status = REQ_BODY_WITHOUT_LEN;
	} else if (req->htc->body_status == BS_LENGTH) {
		req->req_body_status = REQ_BODY_WITH_LEN;
	} else if (req->htc->body_status == BS_NONE) {
		req->req_body_status = REQ_BODY_NONE;
	} else if (req->htc->body_status == BS_EOF) {
		req->req_body_status = REQ_BODY_WITHOUT_LEN;
	} else {
		WRONG("Unknown req.body_length situation");
	}

	if (http_GetHdr(req->http, H_Expect, &p)) {
		if (strcasecmp(p, "100-continue")) {
			wrk->stats->client_req_417++;
			req->err_code = 417;
			r = write(req->sp->fd, r_417, strlen(r_417));
			if (r > 0)
				req->acct.resp_hdrbytes += r;
			SES_Close(req->sp, SC_RX_JUNK);
			return (REQ_FSM_DONE);
		}
		r = write(req->sp->fd, r_100, strlen(r_100));
		if (r > 0)
			req->acct.resp_hdrbytes += r;
		if (r != strlen(r_100)) {
			SES_Close(req->sp, SC_REM_CLOSE);
			return (REQ_FSM_DONE);
		}
		http_Unset(req->http, H_Expect);
	}

	wrk->stats->client_req++;
	wrk->stats->s_req++;

	AZ(req->err_code);
	req->ws_req = WS_Snapshot(req->ws);

	req->doclose = http_DoConnection(req->http);
	if (req->doclose == SC_RX_BAD) {
		r = write(req->sp->fd, r_400, strlen(r_400));
		if (r > 0)
			req->acct.resp_hdrbytes += r;
		SES_Close(req->sp, req->doclose);
		return (REQ_FSM_DONE);
	}

	assert(req->req_body_status != REQ_BODY_INIT);

	HTTP_Copy(req->http0, req->http);	// For ESI & restart

	return (REQ_FSM_MORE);
}

/*----------------------------------------------------------------------
 */

void
HTTP1_Session(struct worker *wrk, struct req *req)
{
	enum req_fsm_nxt nxt = REQ_FSM_MORE;
	struct sess *sp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	/*
	 * Whenever we come in from the acceptor or waiter, we need to set
	 * blocking mode.  It would be simpler to do this in the acceptor
	 * or waiter, but we'd rather do the syscall in the worker thread.
	 * On systems which return errors for ioctl, we close early
	 */
	if (sp->sess_step == S_STP_H1NEWREQ && VTCP_blocking(sp->fd)) {
		if (errno == ECONNRESET)
			SES_Close(sp, SC_REM_CLOSE);
		else
			SES_Close(sp, SC_TX_ERROR);
		AN(http1_cleanup(sp, wrk, req));
		return;
	}

	/*
	 * Return from waitinglist. Check to see if the remote has left.
	 */
	if (req->req_step == R_STP_LOOKUP && VTCP_check_hup(sp->fd)) {
		AN(req->hash_objhead);
		(void)HSH_DerefObjHead(wrk, &req->hash_objhead);
		AZ(req->hash_objhead);
		SES_Close(sp, SC_REM_CLOSE);
		AN(http1_cleanup(sp, wrk, req));
		return;
	}

	while (1) {
		assert(
		    sp->sess_step == S_STP_H1NEWREQ ||
		    req->req_step == R_STP_LOOKUP ||
		    req->req_step == R_STP_RECV);

		if (sp->sess_step == S_STP_H1WORKING) {
			if (req->req_step == R_STP_RECV)
				nxt = http1_dissect(wrk, req);
			if (nxt == REQ_FSM_MORE)
				nxt = CNT_Request(wrk, req);
			if (nxt == REQ_FSM_DISEMBARK)
				return;
			assert(nxt == REQ_FSM_DONE);
			if (http1_cleanup(sp, wrk, req))
				return;
			SES_RxReInit(req->htc);
			if (HTTP1_Complete(req->htc) == HTC_S_COMPLETE) {
				AZ(req->vsl->wid);
				req->t_first = req->t_req = sp->t_idle;
				wrk->stats->sess_pipeline++;
				req->acct.req_hdrbytes +=
				    req->htc->rxbuf_e - req->htc->rxbuf_b;
				sp->sess_step = S_STP_H1WORKING;
				req->req_step = R_STP_RECV;
			} else {
				if (req->htc->rxbuf_e != req->htc->rxbuf_b)
					wrk->stats->sess_readahead++;
				sp->sess_step = S_STP_H1NEWREQ;
			}
		}

		if (sp->sess_step == S_STP_H1NEWREQ) {
			nxt = http1_wait(sp, wrk, req);
			if (nxt != REQ_FSM_MORE)
				return;
			sp->sess_step = S_STP_H1WORKING;
			req->req_step = R_STP_RECV;
		}
	}
}
