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
 * This file contains the two central state machine for pushing HTTP1
 * sessions through their states.
 *
 * The following dot-graph shows the big picture, and the two major
 * complicating features:
 *
 * - The blue path is where a request disembarks its worker thread while
 *   waiting for a busy object to become available:
 *
 * - The green path is where we time out waiting for the next request to
 *   arrive, release the worker thread and hand the session to the waiter.
 *
 * Render the graph with:
 *	sed -n '/^..DOT/s///p' % | dot -Tps > /tmp/_.ps
 *
 *DOT	digraph vcl_center {
 *DOT		size="7.2,10.5"
 *DOT		margin="0.5"
 *DOT		center="1"
 *DOT
 *DOT	acceptor -> http1_wait [label=S_STP_NEWREQ, align=center]
 *DOT	hash -> CNT_Request [label="Busy object\nS_STP_WORKING\nR_STP_LOOKUP"
 *DOT		color=blue]
 *DOT	disembark -> hash [style=dotted, color=blue]
 *DOT	http1_wait -> CNT_Request [label="S_STP_WORKING\nR_STP_RECV"]
 *DOT	http1_wait -> disembark [label="Session close"]
 *DOT	http1_wait -> disembark [label="Timeout" color=green]
 *DOT	disembark -> waiter [style=dotted, color=green]
 *DOT	waiter -> http1_wait [color=green]
 *DOT	CNT_Request -> disembark
 *DOT		[label="Busy object\nS_STP_WORKING\nR_STP_LOOKUP" color=blue]
 *DOT	CNT_Request -> http1_cleanup
 *DOT	http1_cleanup -> disembark [label="Session close"]
 *DOT	http1_cleanup -> CNT_Request [label="S_STP_WORKING\nR_STP_RECV"]
 *DOT	http1_cleanup -> http1_wait [label="S_STP_NEWREQ"]
 *DOT
 *DOT	}
 *
 */

#include "config.h"

#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "vcl.h"
#include "vtcp.h"
#include "vtim.h"

/*--------------------------------------------------------------------
 * Collect a request from the client.
 */

static int
http1_wait(struct sess *sp, struct worker *wrk, struct req *req)
{
	int j, tmo;
	struct pollfd pfd[1];
	double now, when;
	enum sess_close why = SC_NULL;
	enum htc_status_e hs;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	assert(req->sp == sp);

	AZ(req->vcl);
	AZ(req->obj);
	AZ(req->esi_level);
	assert(req->xid == 0);
	assert(isnan(req->t_req));
	assert(isnan(req->t_resp));

	tmo = (int)(1e3 * cache_param->timeout_linger);
	while (1) {
		pfd[0].fd = sp->fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		j = poll(pfd, 1, tmo);
		assert(j >= 0);
		now = VTIM_real();
		if (j != 0)
			hs = HTC_Rx(req->htc);
		else
			hs = HTC_Complete(req->htc);
		if (hs == HTC_COMPLETE) {
			/* Got it, run with it */
			req->t_req = now;
			return (0);
		} else if (hs == HTC_ERROR_EOF) {
			why = SC_REM_CLOSE;
			break;
		} else if (hs == HTC_OVERFLOW) {
			why = SC_RX_OVERFLOW;
			break;
		} else if (hs == HTC_ALL_WHITESPACE) {
			/* Nothing but whitespace */
			when = sp->t_idle + cache_param->timeout_idle;
			if (when < now) {
				why = SC_RX_TIMEOUT;
				break;
			}
			when = sp->t_idle + cache_param->timeout_linger;
			tmo = (int)(1e3 * (when - now));
			if (when < now || tmo == 0) {
				req->t_req = NAN;
				wrk->stats.sess_herd++;
				SES_ReleaseReq(req);
				WAIT_Enter(sp);
				return (1);
			}
		} else {
			/* Working on it */
			if (isnan(req->t_req))
				req->t_req = now;
			when = req->t_req + cache_param->timeout_req;
			tmo = (int)(1e3 * (when - now));
			if (when < now || tmo == 0) {
				why = SC_RX_TIMEOUT;
				break;
			}
		}
	}
	SES_ReleaseReq(req);
	assert(why != SC_NULL);
	SES_Delete(sp, why, now);
	return (1);
}

/*--------------------------------------------------------------------
 * This is the final state, figure out if we should close or recycle
 * the client connection
 */

enum http1_cleanup_ret {
	SESS_DONE_RET_GONE,
	SESS_DONE_RET_WAIT,
	SESS_DONE_RET_START,
};

static enum http1_cleanup_ret
http1_cleanup(struct sess *sp, struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(req->vcl, VCL_CONF_MAGIC);

	AZ(req->obj);
	AZ(req->busyobj);
	req->director = NULL;
	req->restarts = 0;

	AZ(req->esi_level);

	if (req->vcl != NULL) {
		if (wrk->vcl != NULL)
			VCL_Rel(&wrk->vcl);
		wrk->vcl = req->vcl;
		req->vcl = NULL;
	}

	sp->t_idle = W_TIM_real(wrk);
	if (req->xid == 0)
		req->t_resp = sp->t_idle;
	req->xid = 0;
	VSL_Flush(req->vsl, 0);

	req->t_req = NAN;
	req->t_resp = NAN;

	req->req_bodybytes = 0;

	req->hash_always_miss = 0;
	req->hash_ignore_busy = 0;

	if (sp->fd >= 0 && req->doclose != SC_NULL)
		SES_Close(sp, req->doclose);

	if (sp->fd < 0) {
		wrk->stats.sess_closed++;
		AZ(req->vcl);
		SES_ReleaseReq(req);
		SES_Delete(sp, SC_NULL, NAN);
		return (SESS_DONE_RET_GONE);
	}

	if (wrk->stats.client_req >= cache_param->wthread_stats_rate)
		WRK_SumStat(wrk);

	WS_Reset(req->ws, NULL);
	WS_Reset(wrk->aws, NULL);
	req->vxid = VXID_Get(&wrk->vxid_pool);

	if (HTC_Reinit(req->htc) == HTC_COMPLETE) {
		req->t_req = sp->t_idle;
		wrk->stats.sess_pipeline++;
		return (SESS_DONE_RET_START);
	} else {
		if (Tlen(req->htc->rxbuf))
			wrk->stats.sess_readahead++;
		return (SESS_DONE_RET_WAIT);
	}
}

/*--------------------------------------------------------------------
 */

static int
http1_dissect(struct worker *wrk, struct req *req)
{
	const char *r = "HTTP/1.1 100 Continue\r\n\r\n";
	char *p;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/* Borrow VCL reference from worker thread */
	VCL_Refresh(&wrk->vcl);
	req->vcl = wrk->vcl;
	wrk->vcl = NULL;

	HTTP_Setup(req->http, req->ws, req->vsl, HTTP_Req);
	req->err_code = http_DissectRequest(req);

	/* If we could not even parse the request, just close */
	if (req->err_code == 400) {
		SES_Close(req->sp, SC_RX_JUNK);
		return (1);
	}

	wrk->stats.client_req++;
	req->acct_req.req++;

	req->ws_req = WS_Snapshot(req->ws);
	req->doclose = http_DoConnection(req->http);

	/* XXX: Expect headers are a mess */
	if (req->err_code == 0 && http_GetHdr(req->http, H_Expect, &p)) {
		if (strcasecmp(p, "100-continue")) {
			req->err_code = 417;
		} else if (strlen(r) != write(req->sp->fd, r, strlen(r))) {
			SES_Close(req->sp, SC_REM_CLOSE);
			return (1);
		}
	}
	http_Unset(req->http, H_Expect);
	/* XXX: pull in req-body and make it available instead. */
	req->reqbodydone = 0;

	HTTP_Copy(req->http0, req->http);	// For ESI & restart

	return (0);
}

/*--------------------------------------------------------------------
 */

void
HTTP1_Session(struct worker *wrk, struct req *req)
{
	int done = 0;
	struct sess *sp;
	enum http1_cleanup_ret sdr;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	/*
	 * Whenever we come in from the acceptor or waiter, we need to set
	 * blocking mode, but there is no point in setting it when we come from
	 * ESI or when a parked sessions returns.
	 * It would be simpler to do this in the acceptor or waiter, but we'd
	 * rather do the syscall in the worker thread.
	 * On systems which return errors for ioctl, we close early
	 */
	if (sp->sess_step == S_STP_NEWREQ && VTCP_blocking(sp->fd)) {
		if (errno == ECONNRESET)
			SES_Close(sp, SC_REM_CLOSE);
		else
			SES_Close(sp, SC_TX_ERROR);
		sdr = http1_cleanup(sp, wrk, req);
		assert(sdr == SESS_DONE_RET_GONE);
		return;
	}

	if (sp->sess_step == S_STP_NEWREQ) {
		HTC_Init(req->htc, req->ws, sp->fd, req->vsl,
		    cache_param->http_req_size,
		    cache_param->http_req_hdr_len);
	}

	while (1) {
		assert(
		    sp->sess_step == S_STP_NEWREQ ||
		    req->req_step == R_STP_LOOKUP ||
		    req->req_step == R_STP_RECV);

		if (sp->sess_step == S_STP_WORKING) {
			if (req->req_step == R_STP_RECV)
				done = http1_dissect(wrk, req);
			if (done == 0)
				done = CNT_Request(wrk, req);
			if (done == 2)
				return;
			assert(done == 1);
			sdr = http1_cleanup(sp, wrk, req);
			switch (sdr) {
			case SESS_DONE_RET_GONE:
				return;
			case SESS_DONE_RET_WAIT:
				sp->sess_step = S_STP_NEWREQ;
				break;
			case SESS_DONE_RET_START:
				sp->sess_step = S_STP_WORKING;
				req->req_step = R_STP_RECV;
				break;
			default:
				WRONG("Illegal enum http1_cleanup_ret");
			}
		}

		if (sp->sess_step == S_STP_NEWREQ) {
			done = http1_wait(sp, wrk, req);
			if (done)
				return;
			sp->sess_step = S_STP_WORKING;
			req->req_step = R_STP_RECV;
		}
	}
}
