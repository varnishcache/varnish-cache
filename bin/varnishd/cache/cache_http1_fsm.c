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
 * This file contains the two central state machine for pushing
 * sessions and requests.
 *
 * The first part of the file, entrypoint CNT_Session() and down to
 * the ==== separator, is concerned with sessions.  When a session has
 * a request to deal with, it calls into the second half of the file.
 * This part is for all practical purposes HTTP/1.x specific.
 *
 * The second part of the file, entrypoint CNT_Request() and below the
 * ==== separator, is intended to (over time) be(ome) protocol agnostic.
 * We already use this now with ESI:includes, which are for all relevant
 * purposes a different "protocol"
 *
 * A special complication is the fact that we can suspend processing of
 * a request when hash-lookup finds a busy objhdr.
 *
 * Since the states are rather nasty in detail, I have decided to embedd
 * a dot(1) graph in the source code comments.  So to see the big picture,
 * extract the DOT lines and run though dot(1), for instance with the
 * command:
 *	sed -n '/^DOT/s///p' cache/cache_center.c | dot -Tps > /tmp/_.ps
 */

/*
DOT digraph vcl_center {
xDOT	page="8.2,11.5"
DOT	size="7.2,10.5"
DOT	margin="0.5"
DOT	center="1"
DOT acceptor [
DOT	shape=hexagon
DOT	label="Request received"
DOT ]
DOT ERROR [shape=plaintext]
DOT RESTART [shape=plaintext]
DOT acceptor -> first [style=bold,color=green]
 */

#include "config.h"

#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "hash/hash_slinger.h"
#include "vcl.h"
#include "vcli_priv.h"
#include "vsha256.h"
#include "vtcp.h"
#include "vtim.h"

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif


/*--------------------------------------------------------------------
 * WAIT
 * Collect the request from the client.
 *
DOT subgraph xcluster_wait {
DOT	wait [
DOT		shape=box
DOT		label="cnt_sess_wait:\nwait for\ncomplete\nrequest"
DOT	]
DOT	herding [shape=hexagon]
DOT	wait -> start [label="got req",style=bold,color=green]
DOT	wait -> "SES_Delete()" [label="errors"]
DOT	wait -> herding [label="timeout_linger"]
DOT	herding -> wait [label="fd read_ready"]
DOT }
 */

static int
cnt_sess_wait(struct sess *sp, struct worker *wrk, struct req *req)
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
 *
DOT	DONE [
DOT		shape=record
DOT		label="{cnt_done:|Request completed}"
DOT	]
DOT	ESI_RESP [ shape=hexagon ]
DOT	DONE -> start [label="full pipeline"]
DOT	DONE -> wait
DOT	DONE -> ESI_RESP
 */

enum cnt_sess_done_ret {
	SESS_DONE_RET_GONE,
	SESS_DONE_RET_WAIT,
	SESS_DONE_RET_START,
};

static enum cnt_sess_done_ret
cnt_sess_done(struct sess *sp, struct worker *wrk, struct req *req)
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

void
CNT_Session(struct worker *wrk, struct req *req)
{
	int done;
	struct sess *sp;
	enum cnt_sess_done_ret sdr;

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
		sdr = cnt_sess_done(sp, wrk, req);
		assert(sdr == SESS_DONE_RET_GONE);
		return;
	}

	if (sp->sess_step == S_STP_NEWREQ) {
		HTC_Init(req->htc, req->ws, sp->fd, req->vsl,
		    cache_param->http_req_size,
		    cache_param->http_req_hdr_len);
	}

	while (1) {
		/*
		 * Possible entrance states
		 */

		assert(
		    sp->sess_step == S_STP_NEWREQ ||
		    req->req_step == R_STP_LOOKUP ||
		    req->req_step == R_STP_START);

		if (sp->sess_step == S_STP_WORKING) {
			done = CNT_Request(wrk, req);
			if (done == 2)
				return;
			assert(done == 1);
			sdr = cnt_sess_done(sp, wrk, req);
			switch (sdr) {
			case SESS_DONE_RET_GONE:
				return;
			case SESS_DONE_RET_WAIT:
				sp->sess_step = S_STP_NEWREQ;
				break;
			case SESS_DONE_RET_START:
				sp->sess_step = S_STP_WORKING;
				req->req_step = R_STP_START;
				break;
			default:
				WRONG("Illegal enum cnt_sess_done_ret");
			}
		}

		if (sp->sess_step == S_STP_NEWREQ) {
			done = cnt_sess_wait(sp, wrk, req);
			if (done)
				return;
			sp->sess_step = S_STP_WORKING;
			req->req_step = R_STP_START;
		}
	}
}
