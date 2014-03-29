/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
#include "hash/hash_slinger.h"

#include "vcl.h"
#include "vct.h"
#include "vtcp.h"
#include "vtim.h"

/*----------------------------------------------------------------------
 * Collect a request from the client.
 */

static enum req_fsm_nxt
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
	assert(!isnan(sp->t_idle));
	assert(isnan(req->t_first));
	assert(isnan(req->t_prev));
	assert(isnan(req->t_req));

	tmo = (int)(1e3 * cache_param->timeout_linger);
	while (1) {
		pfd[0].fd = sp->fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		j = poll(pfd, 1, tmo);
		assert(j >= 0);
		now = VTIM_real();
		if (j != 0)
			hs = HTTP1_Rx(req->htc);
		else
			hs = HTTP1_Complete(req->htc);
		if (hs == HTTP1_COMPLETE) {
			/* Got it, run with it */
			if (isnan(req->t_first))
				VSLb_ts_req(req, "Start", now);
			VSLb_ts_req(req, "Req", now);
			req->t_req = req->t_prev;
			req->acct.req_hdrbytes += Tlen(req->htc->rxbuf);
			return (REQ_FSM_MORE);
		} else if (hs == HTTP1_ERROR_EOF) {
			why = SC_REM_CLOSE;
			break;
		} else if (hs == HTTP1_OVERFLOW) {
			why = SC_RX_OVERFLOW;
			break;
		} else if (hs == HTTP1_ALL_WHITESPACE) {
			/* Nothing but whitespace */
			when = sp->t_idle + cache_param->timeout_idle;
			if (when < now) {
				why = SC_RX_TIMEOUT;
				break;
			}
			when = sp->t_idle + cache_param->timeout_linger;
			tmo = (int)(1e3 * (when - now));
			if (when < now || tmo == 0) {
				wrk->stats.sess_herd++;
				SES_ReleaseReq(req);
				WAIT_Enter(sp);
				return (REQ_FSM_DONE);
			}
		} else {
			/* Working on it */
			if (isnan(req->t_first))
				/* Timestamp Start on first byte received */
				VSLb_ts_req(req, "Start", now);
			when = sp->t_idle + cache_param->timeout_req;
			tmo = (int)(1e3 * (when - now));
			if (when < now || tmo == 0) {
				why = SC_RX_TIMEOUT;
				break;
			}
		}
	}
	req->acct.req_hdrbytes += Tlen(req->htc->rxbuf);
	CNT_AcctLogCharge(&wrk->stats, req);
	SES_ReleaseReq(req);
	assert(why != SC_NULL);
	SES_Delete(sp, why, now);
	return (REQ_FSM_DONE);
}

/*----------------------------------------------------------------------
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
	req->director_hint = NULL;
	req->restarts = 0;

	AZ(req->esi_level);

	if (req->vcl != NULL) {
		if (wrk->vcl != NULL)
			VCL_Rel(&wrk->vcl);
		wrk->vcl = req->vcl;
		req->vcl = NULL;
	}

	/* Charge and log byte counters */
	AN(req->vsl->wid);
	CNT_AcctLogCharge(&wrk->stats, req);
	req->req_bodybytes = 0;
	req->resp_hdrbytes = 0;
	req->resp_bodybytes = 0;

	/* Nuke the VXID. http1_dissect() will allocate a new one when
	   necessary */
	VSLb(req->vsl, SLT_End, "%s", "");
	VSL_Flush(req->vsl, 0);
	req->vsl->wid = 0;

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

	if (HTTP1_Reinit(req->htc) == HTTP1_COMPLETE) {
		AZ(req->vsl->wid);
		req->vsl->wid = VXID_Get(&wrk->vxid_pool) | VSL_CLIENTMARKER;
		VSLb(req->vsl, SLT_Begin, "req %u rxreq",
		    req->sp->vxid & VSL_IDENTMASK);
		VSL(SLT_Link, req->sp->vxid, "req %u rxreq",
		    req->vsl->wid & VSL_IDENTMASK);
		VSLb_ts_req(req, "Start", sp->t_idle);
		VSLb_ts_req(req, "Req", sp->t_idle);
		req->t_req = req->t_prev;
		wrk->stats.sess_pipeline++;
		req->acct.req_hdrbytes += Tlen(req->htc->rxbuf);
		return (SESS_DONE_RET_START);
	} else {
		if (Tlen(req->htc->rxbuf))
			wrk->stats.sess_readahead++;
		return (SESS_DONE_RET_WAIT);
	}
}

/*----------------------------------------------------------------------
 */

static enum req_body_state_e
http1_req_body_status(struct req *req)
{
	char *ptr, *endp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (http_GetHdr(req->http, H_Content_Length, &ptr)) {
		AN(ptr);
		if (*ptr == '\0')
			return (REQ_BODY_FAIL);
		req->req_bodybytes = strtoul(ptr, &endp, 10);
		if (*endp != '\0' && !vct_islws(*endp))
			return (REQ_BODY_FAIL);
		if (req->req_bodybytes == 0)
			return (REQ_BODY_NONE);
		req->h1.bytes_yet = req->req_bodybytes - req->h1.bytes_done;
		return (REQ_BODY_PRESENT);
	}
	if (http_GetHdr(req->http, H_Transfer_Encoding, NULL))
		return (REQ_BODY_FAIL);
	return (REQ_BODY_NONE);
}

/*----------------------------------------------------------------------
 */

static enum req_fsm_nxt
http1_dissect(struct worker *wrk, struct req *req)
{
	const char *r_100 = "HTTP/1.1 100 Continue\r\n\r\n";
	const char *r_400 = "HTTP/1.1 400 Bad Request\r\n\r\n";
	const char *r_411 = "HTTP/1.1 411 Length Required\r\n\r\n";
	const char *r_417 = "HTTP/1.1 417 Expectation Failed\r\n\r\n";
	char *p;
	ssize_t r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	memset(&req->h1, 0, sizeof req->h1);

	/*
	 * Cache_req_fsm zeros the vxid once a requests is processed.
	 * Allocate a new one only now that we know will need it.
	 */
	if (req->vsl->wid == 0) {
		req->vsl->wid = VXID_Get(&wrk->vxid_pool) | VSL_CLIENTMARKER;
		VSLb(req->vsl, SLT_Begin, "req %u rxreq",
		    req->sp->vxid & VSL_IDENTMASK);
		VSL(SLT_Link, req->sp->vxid, "req %u rxreq",
		    req->vsl->wid & VSL_IDENTMASK);
	}

	/* Borrow VCL reference from worker thread */
	VCL_Refresh(&wrk->vcl);
	req->vcl = wrk->vcl;
	wrk->vcl = NULL;

	HTTP_Setup(req->http, req->ws, req->vsl, SLT_ReqMethod);
	req->err_code = HTTP1_DissectRequest(req);

	/* If we could not even parse the request, just close */
	if (req->err_code != 0) {
		wrk->stats.client_req_400++;
		r = write(req->sp->fd, r_400, strlen(r_400));
		if (r > 0)
			req->acct.resp_hdrbytes += r;
		SES_Close(req->sp, SC_RX_JUNK);
		return (REQ_FSM_DONE);
	}

	if (req->req_body_status == REQ_BODY_INIT)
		req->req_body_status = http1_req_body_status(req);
	else
		assert(req->req_body_status == REQ_BODY_NONE);	// ESI

	if (req->req_body_status == REQ_BODY_FAIL) {
		wrk->stats.client_req_411++;
		r = write(req->sp->fd, r_411, strlen(r_411));
		if (r > 0)
			req->acct.resp_hdrbytes += r;
		SES_Close(req->sp, SC_RX_JUNK);
		return (REQ_FSM_DONE);
	}

	if (http_GetHdr(req->http, H_Expect, &p)) {
		if (strcasecmp(p, "100-continue")) {
			wrk->stats.client_req_417++;
			req->err_code = 417;
			r = write(req->sp->fd, r_417, strlen(r_417));
			if (r > 0)
				req->resp_hdrbytes += r;
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
	}

	wrk->stats.client_req++;
	wrk->stats.s_req++;

	AZ(req->err_code);
	req->ws_req = WS_Snapshot(req->ws);
	req->doclose = http_DoConnection(req->http);

	http_Unset(req->http, H_Expect);

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

	/*
	 * Return from waitinglist. Check to see if the remote has left.
	 */
	if (req->req_step == R_STP_LOOKUP && VTCP_check_hup(sp->fd)) {
		AN(req->hash_objhead);
		(void)HSH_DerefObjHead(&wrk->stats, &req->hash_objhead);
		AZ(req->hash_objhead);
		SES_Close(sp, SC_REM_CLOSE);
		sdr = http1_cleanup(sp, wrk, req);
		assert(sdr == SESS_DONE_RET_GONE);
		return;
	}

	if (sp->sess_step == S_STP_NEWREQ) {
		HTTP1_Init(req->htc, req->ws, sp->fd, req->vsl,
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
				nxt = http1_dissect(wrk, req);
			if (nxt == REQ_FSM_MORE)
				nxt = CNT_Request(wrk, req);
			if (nxt == REQ_FSM_DISEMBARK)
				return;
			assert(nxt == REQ_FSM_DONE);
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
			nxt = http1_wait(sp, wrk, req);
			if (nxt != REQ_FSM_MORE)
				return;
			sp->sess_step = S_STP_WORKING;
			req->req_step = R_STP_RECV;
		}
	}
}

static ssize_t
http1_iter_req_body(struct req *req, void *buf, ssize_t len)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	AN(req->req_bodybytes);
	AN(len);
	AN(buf);
	if (len > req->req_bodybytes - req->h1.bytes_done)
		len = req->req_bodybytes - req->h1.bytes_done;
	if (len == 0) {
		req->req_body_status = REQ_BODY_DONE;
		return (0);
	}
	len = HTTP1_Read(req->htc, buf, len);
	if (len <= 0) {
		req->req_body_status = REQ_BODY_FAIL;
		return (-1);
	}
	req->h1.bytes_done += len;
	req->h1.bytes_yet = req->req_bodybytes - req->h1.bytes_done;
	req->acct.req_bodybytes += len;
	return (len);
}

/*----------------------------------------------------------------------
 * Iterate over the req.body.
 *
 * This can be done exactly once if uncached, and multiple times if the
 * req.body is cached.
 */

int
HTTP1_IterateReqBody(struct req *req, req_body_iter_f *func, void *priv)
{
	char buf[8192];
	struct storage *st;
	ssize_t l;
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(func);

	switch(req->req_body_status) {
	case REQ_BODY_CACHED:
		VTAILQ_FOREACH(st, &req->body, list) {
			i = func(req, priv, st->ptr, st->len);
			if (i)
				return (i);
		}
		return (0);
	case REQ_BODY_NONE:
		return (0);
	case REQ_BODY_PRESENT:
		break;
	case REQ_BODY_DONE:
	case REQ_BODY_TAKEN:
		VSLb(req->vsl, SLT_VCL_Error,
		    "Uncached req.body can only be consumed once.");
		return (-1);
	default:
		WRONG("Wrong req_body_status in HTTP1_IterateReqBody()");
	}
	Lck_Lock(&req->sp->mtx);
	if (req->req_body_status == REQ_BODY_PRESENT) {
		req->req_body_status = REQ_BODY_TAKEN;
		i = 0;
	} else
		i = -1;
	Lck_Unlock(&req->sp->mtx);
	if (i) {
		VSLb(req->vsl, SLT_VCL_Error,
		    "Multiple attempts to access non-cached req.body");
		return (i);
	}

	do {
		l = http1_iter_req_body(req, buf, sizeof buf);
		if (l < 0) {
			req->doclose = SC_RX_BODY;
			break;
		}
		if (l > 0) {
			i = func(req, priv, buf, l);
			if (i) {
				l = i;
				break;
			}
		}
	} while (l > 0);
	VSLb_ts_req(req, "ReqBody", VTIM_real());

	return (l);
}

/*----------------------------------------------------------------------
 * DiscardReqBody() is a dedicated function, because we might
 * be able to disuade or terminate its transmission in some protocols.
 * For HTTP1 we have no such luck, and we just iterate it into oblivion.
 */

static int __match_proto__(req_body_iter_f)
httpq_req_body_discard(struct req *req, void *priv, void *ptr, size_t len)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	(void)priv;
	(void)ptr;
	(void)len;
	return (0);
}

int
HTTP1_DiscardReqBody(struct req *req)
{

	if (req->req_body_status == REQ_BODY_DONE)
		return(0);
	if (req->req_body_status == REQ_BODY_FAIL)
		return(0);
	if (req->req_body_status == REQ_BODY_TAKEN)
		return(0);
	return(HTTP1_IterateReqBody(req, httpq_req_body_discard, NULL));
}

/*----------------------------------------------------------------------
 * Cache the req.body if it is smaller than the given size
 *
 * This function must be called before any backend fetches are kicked
 * off to prevent parallelism.
 */

int
HTTP1_CacheReqBody(struct req *req, ssize_t maxsize)
{
	struct storage *st;
	ssize_t l;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	assert (req->req_step == R_STP_RECV);
	switch(req->req_body_status) {
	case REQ_BODY_CACHED:
	case REQ_BODY_FAIL:
		return (-1);
	case REQ_BODY_NONE:
		return (0);
	case REQ_BODY_PRESENT:
		break;
	default:
		WRONG("Wrong req_body_status in HTTP1_CacheReqBody()");
	}

	if (req->req_bodybytes > maxsize) {
		req->req_body_status = REQ_BODY_FAIL;
		return (-1);
	}

	st = NULL;
	do {
		if (st == NULL) {
			st = STV_alloc_transient(
			    req->h1.bytes_yet ?
			    req->h1.bytes_yet : cache_param->fetch_chunksize);
			if (st == NULL) {
				req->req_body_status = REQ_BODY_FAIL;
				l = -1;
				break;
			} else {
				VTAILQ_INSERT_TAIL(&req->body, st, list);
			}
		}

		l = st->space - st->len;
		l = http1_iter_req_body(req, st->ptr + st->len, l);
		if (l < 0) {
			req->doclose = SC_RX_BODY;
			break;
		}
		if (req->req_bodybytes > maxsize) {
			req->req_body_status = REQ_BODY_FAIL;
			l = -1;
			break;
		}
		if (l > 0) {
			st->len += l;
			if (st->space == st->len)
				st = NULL;
		}
	} while (l > 0);
	if (l == 0)
		req->req_body_status = REQ_BODY_CACHED;
	VSLb_ts_req(req, "ReqBody", VTIM_real());
	return (l);
}
