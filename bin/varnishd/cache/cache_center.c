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
 * This file contains the central state machine for pushing requests.
 *
 * We cannot just use direct calls because it is possible to kick a
 * request back to the lookup stage (usually after a rewrite).  The
 * state engine also allows us to break the processing up into some
 * logical chunks which improves readability a little bit.
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

#include "common/heritage.h"

#include "hash/hash_slinger.h"
#include "vcl.h"
#include "vcli_priv.h"
#include "vsha256.h"
#include "vtcp.h"
#include "vtim.h"

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

static unsigned xids;

/*--------------------------------------------------------------------
 * WAIT
 * Collect the request from the client.
 *
 * We "abuse" sp->t_req a bit here:  On input it means "request reception
 * started at xxx" and is used to trigger timeouts.  On return it means
 * "we had full request headers by xxx" and is used for reporting by
 * later steps.
 *
DOT subgraph xcluster_wait {
DOT	wait [
DOT		shape=box
DOT		label="cnt_wait:\nwait for\ncomplete\nrequest"
DOT	]
DOT	herding [shape=hexagon]
DOT	wait -> start [label="got req",style=bold,color=green]
DOT	wait -> "SES_Delete()" [label="errors"]
DOT	wait -> herding [label="timeout_linger"]
DOT	herding -> wait [label="fd read_ready"]
DOT }
 */

static int
cnt_wait(struct sess *sp, struct worker *wrk, struct req *req)
{
	int i, j, tmo;
	struct pollfd pfd[1];
	double now, when;
	const char *why = NULL;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (req == NULL) {
		SES_GetReq(sp);
		req = sp->req;
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		HTC_Init(req->htc, req->ws, sp->fd, sp->req->vsl,
		    cache_param->http_req_size,
		    cache_param->http_req_hdr_len);
	}

	AZ(req->vcl);
	AZ(req->obj);
	AZ(req->esi_level);
	assert(req->xid == 0);
	req->t_resp = NAN;

	assert(!isnan(sp->t_req));
	tmo = (int)(1e3 * cache_param->timeout_linger);
	while (1) {
		pfd[0].fd = sp->fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		j = poll(pfd, 1, tmo);
		assert(j >= 0);
		now = VTIM_real();
		if (j != 0)
			i = HTC_Rx(req->htc);
		else
			i = HTC_Complete(req->htc);
		if (i == 1) {
			/* Got it, run with it */
			sp->t_req = now;
			sp->step = STP_START;
			return (0);
		} else if (i == -1) {
			why = "EOF";
			break;
		} else if (i == -2) {
			why = "overflow";
			break;
		} else if (i == -3) {
			/* Nothing but whitespace */
			when = sp->t_idle + cache_param->timeout_idle;
			if (when < now) {
				why = "timeout";
				break;
			}
			when = sp->t_idle + cache_param->timeout_linger;
			tmo = (int)(1e3 * (when - now));
			if (when < now || tmo == 0) {
				sp->t_req = NAN;
				wrk->stats.sess_herd++;
				SES_Charge(sp);
				SES_ReleaseReq(sp);
				WAIT_Enter(sp);
				return (1);
			}
		} else {
			/* Working on it */
			when = sp->t_req + cache_param->timeout_req;
			tmo = (int)(1e3 * (when - now));
			if (when < now || tmo == 0) {
				why = "req timeout";
				break;
			}
		}
	}
	SES_Charge(sp);
	SES_Delete(sp, why, now);
	return (1);
}

/*--------------------------------------------------------------------
 * We have a refcounted object on the session, and possibly the busyobj
 * which is fetching it, prepare a response.
 *
DOT subgraph xcluster_prepresp {
DOT	prepresp [
DOT		shape=record
DOT		label="{cnt_prepresp:|Filter obj.-\>resp.|{vcl_deliver\{\}|{req.|resp.}}|{error?|restart?}|stream ?}"
DOT	]
DOT	prepresp -> deliver [style=bold,color=green,label=deliver]
DOT	prepresp -> deliver [style=bold,color=red]
DOT	prepresp -> deliver [style=bold,color=blue]
DOT }
 *
 */

static int
cnt_prepresp(struct sess *sp, struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	bo = req->busyobj;
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);

	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);

	req->res_mode = 0;

	if (bo == NULL) {
		if (!req->disable_esi && req->obj->esidata != NULL) {
			/* In ESI mode, we can't know the aggregate length */
			req->res_mode &= ~RES_LEN;
			req->res_mode |= RES_ESI;
		} else {
			req->res_mode |= RES_LEN;
		}
	} else {
		AZ(bo->do_esi);
	}

	if (req->esi_level > 0) {
		/* Included ESI object, always CHUNKED or EOF */
		req->res_mode &= ~RES_LEN;
		req->res_mode |= RES_ESI_CHILD;
	}

	if (cache_param->http_gzip_support && req->obj->gziped &&
	    !RFC2616_Req_Gzip(req->http)) {
		/*
		 * We don't know what it uncompresses to
		 * XXX: we could cache that
		 */
		req->res_mode &= ~RES_LEN;
		req->res_mode |= RES_GUNZIP;
	}

	if (!(req->res_mode & (RES_LEN|RES_CHUNKED|RES_EOF))) {
		/* We havn't chosen yet, do so */
		if (!req->wantbody) {
			/* Nothing */
		} else if (req->http->protover >= 11) {
			req->res_mode |= RES_CHUNKED;
		} else {
			req->res_mode |= RES_EOF;
			req->doclose = "EOF mode";
		}
	}

	req->t_resp = W_TIM_real(wrk);
	if (req->obj->objcore->objhead != NULL) {
		if ((req->t_resp - req->obj->last_lru) >
		    cache_param->lru_timeout &&
		    EXP_Touch(req->obj->objcore))
			req->obj->last_lru = req->t_resp;
		if (!cache_param->obj_readonly)
			req->obj->last_use = req->t_resp; /* XXX: locking ? */
	}
	HTTP_Setup(req->resp, req->ws, req->vsl, HTTP_Resp);
	RES_BuildHttp(sp);
	VCL_deliver_method(sp);
	switch (req->handling) {
	case VCL_RET_DELIVER:
		break;
	case VCL_RET_RESTART:
		if (req->restarts >= cache_param->max_restarts)
			break;
		if (bo != NULL) {
			AN(bo->do_stream);
			(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
			VBO_DerefBusyObj(wrk, &req->busyobj);
		} else {
			(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
		}
		AZ(req->obj);
		http_Teardown(req->resp);
		sp->step = STP_RESTART;
		return (0);
	default:
		WRONG("Illegal action in vcl_deliver{}");
	}
	sp->step = STP_DELIVER;
	return (0);
}

/*--------------------------------------------------------------------
 * Deliver an already stored object
 *
DOT subgraph xcluster_deliver {
DOT	deliver [
DOT		shape=record
DOT		label="{cnt_deliver:|Send body}"
DOT	]
DOT }
DOT deliver -> DONE [style=bold,color=green]
DOT deliver -> DONE [style=bold,color=red]
DOT deliver -> DONE [style=bold,color=blue]
 *
 */

static int
cnt_deliver(struct sess *sp, struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	bo = req->busyobj;
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);

	if (bo != NULL) {
		while (bo->state < BOS_FAILED)
			(void)usleep(10000);
		assert(bo->state >= BOS_FAILED);

		if (bo->state == BOS_FAILED) {
			req->obj = NULL;
			VBO_DerefBusyObj(wrk, &req->busyobj);
			req->err_code = 503;
			sp->step = STP_ERROR;
			return (0);
		}
		VBO_DerefBusyObj(wrk, &req->busyobj);
	}

	AZ(req->busyobj);
	req->director = NULL;
	req->restarts = 0;

	RES_WriteObj(sp);

	/* No point in saving the body if it is hit-for-pass */
	if (req->obj->objcore->flags & OC_F_PASS)
		STV_Freestore(req->obj);

	assert(WRW_IsReleased(wrk));
	(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
	http_Teardown(req->resp);
	sp->step = STP_DONE;
	return (0);
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

static int
cnt_done(struct sess *sp, struct worker *wrk, struct req *req)
{
	double dh, dp, da;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(req->vcl, VCL_CONF_MAGIC);

	AZ(req->obj);
	AZ(req->busyobj);
	req->director = NULL;
	req->restarts = 0;

	SES_Charge(sp);

	/* If we did an ESI include, don't mess up our state */
	if (req->esi_level > 0)
		return (1);

	if (req->vcl != NULL) {
		if (wrk->vcl != NULL)
			VCL_Rel(&wrk->vcl);
		wrk->vcl = req->vcl;
		req->vcl = NULL;
	}


	sp->t_idle = W_TIM_real(wrk);
	if (req->xid == 0) {
		req->t_resp = sp->t_idle;
	} else {
		dp = req->t_resp - sp->t_req;
		da = sp->t_idle - req->t_resp;
		dh = sp->t_req - sp->t_open;
		/* XXX: Add StatReq == StatSess */
		/* XXX: Workaround for pipe */
		if (sp->fd >= 0) {
			VSLb(sp->req->vsl, SLT_Length, "%ju",
			    (uintmax_t)req->req_bodybytes);
		}
		VSLb(sp->req->vsl, SLT_ReqEnd, "%u %.9f %.9f %.9f %.9f %.9f",
		    req->xid, sp->t_req, sp->t_idle, dh, dp, da);
	}
	req->xid = 0;
	VSL_Flush(sp->req->vsl, 0);

	sp->t_req = NAN;
	req->t_resp = NAN;

	req->req_bodybytes = 0;

	req->hash_always_miss = 0;
	req->hash_ignore_busy = 0;

	if (sp->fd >= 0 && req->doclose != NULL) {
		/*
		 * This is an orderly close of the connection; ditch nolinger
		 * before we close, to get queued data transmitted.
		 */
		// XXX: not yet (void)VTCP_linger(sp->fd, 0);
		SES_Close(sp, req->doclose);
	}

	if (sp->fd < 0) {
		wrk->stats.sess_closed++;
		SES_Delete(sp, NULL, NAN);
		return (1);
	}

	if (wrk->stats.client_req >= cache_param->wthread_stats_rate)
		WRK_SumStat(wrk);

	WS_Reset(req->ws, NULL);
	WS_Reset(wrk->aws, NULL);

	sp->t_req = sp->t_idle;
	i = HTC_Reinit(req->htc);
	if (i == 1) {
		wrk->stats.sess_pipeline++;
		sp->step = STP_START;
	} else {
		if (Tlen(req->htc->rxbuf))
			wrk->stats.sess_readahead++;
		sp->step = STP_WAIT;
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Emit an error
 *
DOT subgraph xcluster_error {
DOT	vcl_error [
DOT		shape=record
DOT		label="vcl_error()|resp."
DOT	]
DOT	ERROR -> vcl_error
DOT	vcl_error-> prepresp [label=deliver]
DOT }
DOT vcl_error-> rsterr [label="restart",color=purple]
DOT rsterr [label="RESTART",shape=plaintext]
 */

static int
cnt_error(struct sess *sp, struct worker *wrk, struct req *req)
{
	struct http *h;
	struct busyobj *bo;
	char date[40];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);
	AZ(req->obj);
	AZ(req->busyobj);

	bo = VBO_GetBusyObj(wrk);
	req->busyobj = bo;
	bo->vsl->wid = sp->vsl_id;
	AZ(bo->stats);
	bo->stats = &wrk->stats;
	req->objcore = HSH_NewObjCore(wrk);
	req->obj = STV_NewObject(bo, &req->objcore,
	    TRANSIENT_STORAGE, cache_param->http_resp_size,
	    (uint16_t)cache_param->http_max_hdr);
	bo->stats = NULL;
	if (req->obj == NULL) {
		req->doclose = "Out of objects";
		req->director = NULL;
		http_Teardown(bo->beresp);
		http_Teardown(bo->bereq);
		sp->step = STP_DONE;
		return(0);
	}
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	req->obj->xid = req->xid;
	req->obj->exp.entered = sp->t_req;

	h = req->obj->http;

	if (req->err_code < 100 || req->err_code > 999)
		req->err_code = 501;

	http_PutProtocol(h, "HTTP/1.1");
	http_PutStatus(h, req->err_code);
	VTIM_format(W_TIM_real(wrk), date);
	http_PrintfHeader(h, "Date: %s", date);
	http_SetHeader(h, "Server: Varnish");

	if (req->err_reason != NULL)
		http_PutResponse(h, req->err_reason);
	else
		http_PutResponse(h, http_StatusMessage(req->err_code));
	VCL_error_method(sp);

	if (req->handling == VCL_RET_RESTART &&
	    req->restarts <  cache_param->max_restarts) {
		HSH_Drop(wrk, &sp->req->obj);
		VBO_DerefBusyObj(wrk, &req->busyobj);
		sp->step = STP_RESTART;
		return (0);
	} else if (req->handling == VCL_RET_RESTART)
		req->handling = VCL_RET_DELIVER;


	/* We always close when we take this path */
	req->doclose = "error";
	req->wantbody = 1;

	assert(req->handling == VCL_RET_DELIVER);
	req->err_code = 0;
	req->err_reason = NULL;
	http_Teardown(bo->bereq);
	VBO_DerefBusyObj(wrk, &req->busyobj);
	sp->step = STP_PREPRESP;
	return (0);
}

/*--------------------------------------------------------------------
 * Fetch response headers from the backend
 *
DOT subgraph xcluster_fetch {
DOT	fetch [
DOT		shape=record
DOT		label="{cnt_fetch:|fetch hdr\nfrom backend|(find obj.ttl)|{vcl_fetch\{\}|{req.|bereq.|beresp.}}|{<err>error?|<rst>restart?}}"
DOT	]
DOT }
DOT fetch -> fetchbody [style=bold,color=red]
DOT fetch -> fetchbody [style=bold,color=blue]
 */

static int
cnt_fetch(struct sess *sp, struct worker *wrk, struct req *req)
{
	int i, need_host_hdr;
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	bo = req->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AN(req->director);
	AZ(bo->vbc);
	AZ(bo->should_close);
	AZ(req->storage_hint);

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, HTTP_Beresp);

	need_host_hdr = !http_GetHdr(bo->bereq, H_Host, NULL);

	wrk->acct_tmp.fetch++;

	i = FetchHdr(sp, need_host_hdr, req->objcore->objhead == NULL);
	/*
	 * If we recycle a backend connection, there is a finite chance
	 * that the backend closed it before we get a request to it.
	 * Do a single retry in that case.
	 */
	if (i == 1) {
		VSC_C_main->backend_retry++;
		i = FetchHdr(sp, need_host_hdr, req->objcore->objhead == NULL);
	}

	if (i) {
		req->handling = VCL_RET_ERROR;
		req->err_code = 503;
	} else {
		/*
		 * These two headers can be spread over multiple actual headers
		 * and we rely on their content outside of VCL, so collect them
		 * into one line here.
		 */
		http_CollectHdr(bo->beresp, H_Cache_Control);
		http_CollectHdr(bo->beresp, H_Vary);

		/*
		 * Figure out how the fetch is supposed to happen, before the
		 * headers are adultered by VCL
		 * NB: Also sets other wrk variables
		 */
		bo->body_status = RFC2616_Body(bo, &wrk->stats);

		req->err_code = http_GetStatus(bo->beresp);

		/*
		 * What does RFC2616 think about TTL ?
		 */
		EXP_Clr(&bo->exp);
		bo->exp.entered = W_TIM_real(wrk);
		RFC2616_Ttl(bo, sp->req->xid);

		/* pass from vclrecv{} has negative TTL */
		if (req->objcore->objhead == NULL)
			bo->exp.ttl = -1.;

		AZ(bo->do_esi);
		AZ(bo->do_pass);

		VCL_fetch_method(sp);

		if (bo->do_pass)
			req->objcore->flags |= OC_F_PASS;

		switch (req->handling) {
		case VCL_RET_DELIVER:
			sp->step = STP_FETCHBODY;
			return (0);
		default:
			break;
		}

		/* We are not going to fetch the body, Close the connection */
		VDI_CloseFd(&bo->vbc);
	}

	/* Clean up partial fetch */
	AZ(bo->vbc);

	if (req->objcore->objhead != NULL || req->handling == VCL_RET_ERROR) {
		CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
		AZ(HSH_Deref(&wrk->stats, req->objcore, NULL));
		req->objcore = NULL;
	}
	assert(bo->refcount == 2);
	VBO_DerefBusyObj(wrk, &bo);
	VBO_DerefBusyObj(wrk, &req->busyobj);
	req->director = NULL;
	req->storage_hint = NULL;

	switch (req->handling) {
	case VCL_RET_RESTART:
		sp->step = STP_RESTART;
		return (0);
	case VCL_RET_ERROR:
		sp->step = STP_ERROR;
		return (0);
	default:
		WRONG("Illegal action in vcl_fetch{}");
	}
}

/*--------------------------------------------------------------------
 * Prepare to fetch body from backend
 *
DOT subgraph xcluster_body {
DOT	fetchbody [
DOT		shape=record
DOT		label="{cnt_fetchbody:|start fetch_thread}"
DOT	]
DOT }
DOT fetchbody:out -> prepresp [style=bold,color=red]
DOT fetchbody:out -> prepresp [style=bold,color=blue]
 */

static int
cnt_fetchbody(struct sess *sp, struct worker *wrk, struct req *req)
{
	struct http *hp, *hp2;
	char *b;
	uint16_t nhttp;
	unsigned l;
	struct vsb *vary = NULL;
	int varyl = 0, pass;
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	bo = req->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	assert(req->handling == VCL_RET_DELIVER);

	if (req->objcore->objhead == NULL) {
		/* This is a pass from vcl_recv */
		pass = 1;
		/* VCL may have fiddled this, but that doesn't help */
		bo->exp.ttl = -1.;
	} else if (bo->do_pass) {
		pass = 1;
	} else {
		/* regular object */
		pass = 0;
	}

	/*
	 * The VCL variables beresp.do_g[un]zip tells us how we want the
	 * object processed before it is stored.
	 *
	 * The backend Content-Encoding header tells us what we are going
	 * to receive, which we classify in the following three classes:
	 *
	 *	"Content-Encoding: gzip"	--> object is gzip'ed.
	 *	no Content-Encoding		--> object is not gzip'ed.
	 *	anything else			--> do nothing wrt gzip
	 *
	 */

	/* We do nothing unless the param is set */
	if (!cache_param->http_gzip_support)
		bo->do_gzip = bo->do_gunzip = 0;

	bo->is_gzip = http_HdrIs(bo->beresp, H_Content_Encoding, "gzip");

	bo->is_gunzip = !http_GetHdr(bo->beresp, H_Content_Encoding, NULL);

	/* It can't be both */
	assert(bo->is_gzip == 0 || bo->is_gunzip == 0);

	/* We won't gunzip unless it is gzip'ed */
	if (bo->do_gunzip && !bo->is_gzip)
		bo->do_gunzip = 0;

	/* If we do gunzip, remove the C-E header */
	if (bo->do_gunzip)
		http_Unset(bo->beresp, H_Content_Encoding);

	/* We wont gzip unless it is ungziped */
	if (bo->do_gzip && !bo->is_gunzip)
		bo->do_gzip = 0;

	/* If we do gzip, add the C-E header */
	if (bo->do_gzip)
		http_SetHeader(bo->beresp, "Content-Encoding: gzip");

	/* But we can't do both at the same time */
	assert(bo->do_gzip == 0 || bo->do_gunzip == 0);

	/* ESI takes precedence and handles gzip/gunzip itself */
	if (bo->do_esi)
		bo->vfp = &vfp_esi;
	else if (bo->do_gunzip)
		bo->vfp = &vfp_gunzip;
	else if (bo->do_gzip)
		bo->vfp = &vfp_gzip;
	else if (bo->is_gzip)
		bo->vfp = &vfp_testgzip;

	if (bo->do_esi || req->esi_level > 0)
		bo->do_stream = 0;
	if (!req->wantbody)
		bo->do_stream = 0;

	/* No reason to try streaming a non-existing body */
	if (bo->body_status == BS_NONE)
		bo->do_stream = 0;

	l = http_EstimateWS(bo->beresp,
	    pass ? HTTPH_R_PASS : HTTPH_A_INS, &nhttp);

	/* Create Vary instructions */
	if (req->objcore->objhead != NULL) {
		CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
		vary = VRY_Create(req, bo->beresp);
		if (vary != NULL) {
			varyl = VSB_len(vary);
			assert(varyl > 0);
			l += varyl;
		}
	}

	/*
	 * Space for producing a Content-Length: header including padding
	 * A billion gigabytes is enough for anybody.
	 */
	l += strlen("Content-Length: XxxXxxXxxXxxXxxXxx") + sizeof(void *);

	if (bo->exp.ttl < cache_param->shortlived ||
	    req->objcore == NULL)
		req->storage_hint = TRANSIENT_STORAGE;

	AZ(bo->stats);
	bo->stats = &wrk->stats;
	req->obj = STV_NewObject(bo, &req->objcore, req->storage_hint, l,
	    nhttp);
	if (req->obj == NULL) {
		/*
		 * Try to salvage the transaction by allocating a
		 * shortlived object on Transient storage.
		 */
		req->obj = STV_NewObject(bo, &req->objcore, TRANSIENT_STORAGE,
		    l, nhttp);
		if (bo->exp.ttl > cache_param->shortlived)
			bo->exp.ttl = cache_param->shortlived;
		bo->exp.grace = 0.0;
		bo->exp.keep = 0.0;
	}
	bo->stats = NULL;
	if (req->obj == NULL) {
		req->err_code = 503;
		sp->step = STP_ERROR;
		VDI_CloseFd(&bo->vbc);
		VBO_DerefBusyObj(wrk, &req->busyobj);
		return (0);
	}
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);

	req->storage_hint = NULL;

	AZ(bo->fetch_obj);
	bo->fetch_obj = req->obj;

	if (bo->do_gzip || (bo->is_gzip && !bo->do_gunzip))
		req->obj->gziped = 1;

	if (vary != NULL) {
		req->obj->vary = (void *)WS_Alloc(req->obj->http->ws, varyl);
		AN(req->obj->vary);
		memcpy(req->obj->vary, VSB_data(vary), varyl);
		VRY_Validate(req->obj->vary);
		VSB_delete(vary);
	}

	req->obj->xid = req->xid;
	req->obj->response = req->err_code;
	WS_Assert(req->obj->ws_o);

	/* Filter into object */
	hp = bo->beresp;
	hp2 = req->obj->http;

	hp2->logtag = HTTP_Obj;
	http_FilterResp(hp, hp2, pass ? HTTPH_R_PASS : HTTPH_A_INS);
	http_CopyHome(hp2);

	if (http_GetHdr(hp, H_Last_Modified, &b))
		req->obj->last_modified = VTIM_parse(b);
	else
		req->obj->last_modified = floor(bo->exp.entered);

	assert(WRW_IsReleased(wrk));

	/*
	 * If we can deliver a 304 reply, we don't bother streaming.
	 * Notice that vcl_deliver{} could still nuke the headers
	 * that allow the 304, in which case we return 200 non-stream.
	 */
	if (req->obj->response == 200 &&
	    req->http->conds &&
	    RFC2616_Do_Cond(sp))
		bo->do_stream = 0;

	/*
	 * Ready to fetch the body
	 */
	bo->fetch_task.func = FetchBody;
	bo->fetch_task.priv = bo;

	assert(bo->refcount == 2);	/* one for each thread */

	if (req->obj->objcore->objhead != NULL) {
		EXP_Insert(req->obj);
		AN(req->obj->objcore->ban);
		AZ(req->obj->ws_o->overflow);
		HSH_Unbusy(&wrk->stats, req->obj->objcore);
	}

	if (!bo->do_stream ||
	    Pool_Task(wrk->pool, &bo->fetch_task, POOL_NO_QUEUE))
		FetchBody(wrk, bo);

	if (req->obj->objcore->objhead != NULL)
		HSH_Ref(req->obj->objcore);

	if (bo->state == BOS_FINISHED) {
		VBO_DerefBusyObj(wrk, &req->busyobj);
	} else if (bo->state == BOS_FAILED) {
		/* handle early failures */
		req->obj = NULL;
		VBO_DerefBusyObj(wrk, &req->busyobj);
		req->err_code = 503;
		sp->step = STP_ERROR;
		return (0);
	}

	assert(WRW_IsReleased(wrk));
	sp->step = STP_PREPRESP;
	return (0);
}

/*--------------------------------------------------------------------
 * A freshly accepted socket
 *
DOT subgraph xcluster_first {
DOT	first [
DOT		shape=box
DOT		label="cnt_first:\nrender\naddresses"
DOT	]
DOT }
DOT first -> wait [style=bold,color=green]
 */

static int
cnt_first(struct sess *sp, struct worker *wrk)
{
	char laddr[ADDR_BUFSIZE];
	char lport[PORT_BUFSIZE];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	/* Allocate a request already now, so we can VSL to it */
	SES_GetReq(sp);
	CHECK_OBJ_NOTNULL(sp->req, REQ_MAGIC);
	HTC_Init(sp->req->htc, sp->req->ws, sp->fd, sp->req->vsl,
	    cache_param->http_req_size,
	    cache_param->http_req_hdr_len);

	VTCP_name(&sp->sockaddr, sp->sockaddrlen,
	    sp->addr, sizeof sp->addr, sp->port, sizeof sp->port);
	if (cache_param->log_local_addr) {
		AZ(getsockname(sp->fd, (void*)&sp->mysockaddr,
		    &sp->mysockaddrlen));
		VTCP_name(&sp->mysockaddr, sp->mysockaddrlen,
		    laddr, sizeof laddr, lport, sizeof lport);
		/* XXX: have no req yet */
		VSLb(sp->req->vsl, SLT_SessionOpen, "%s %s %s %s",
		    sp->addr, sp->port, laddr, lport);
	} else {
		/* XXX: have no req yet */
		VSLb(sp->req->vsl, SLT_SessionOpen, "%s %s %s",
		    sp->addr, sp->port, sp->mylsock->name);
	}

	wrk->acct_tmp.sess++;

	sp->t_req = sp->t_open;
	sp->t_idle = sp->t_open;
	sp->step = STP_WAIT;
	return (0);
}

/*--------------------------------------------------------------------
 * HIT
 * We had a cache hit.  Ask VCL, then march off as instructed.
 *
DOT subgraph xcluster_hit {
DOT	hit [
DOT		shape=record
DOT		label="{cnt_hit:|{vcl_hit()|{req.|obj.}}|{<err>error?|<rst>restart?}|{<del>deliver?|<pass>pass?}}"
DOT	]
DOT }
XDOT hit:err -> err_hit [label="error"]
XDOT err_hit [label="ERROR",shape=plaintext]
XDOT hit:rst -> rst_hit [label="restart",color=purple]
XDOT rst_hit [label="RESTART",shape=plaintext]
DOT hit:pass -> pass [label=pass,style=bold,color=red]
DOT hit:del -> prepresp [label="deliver",style=bold,color=green]
 */

static int
cnt_hit(struct sess *sp, struct worker *wrk, struct req *req)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->objcore);
	AZ(req->busyobj);

	assert(!(req->obj->objcore->flags & OC_F_PASS));

	VCL_hit_method(sp);

	if (req->handling == VCL_RET_DELIVER) {
		//AZ(req->busyobj->bereq->ws);
		//AZ(req->busyobj->beresp->ws);
		(void)FetchReqBody(sp, 0);
		sp->step = STP_PREPRESP;
		return (0);
	}

	/* Drop our object, we won't need it */
	(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
	req->objcore = NULL;

	switch(req->handling) {
	case VCL_RET_PASS:
		sp->step = STP_PASS;
		return (0);
	case VCL_RET_ERROR:
		sp->step = STP_ERROR;
		return (0);
	case VCL_RET_RESTART:
		sp->step = STP_RESTART;
		return (0);
	default:
		WRONG("Illegal action in vcl_hit{}");
	}
}

/*--------------------------------------------------------------------
 * LOOKUP
 * Hash things together and look object up in hash-table.
 *
 * LOOKUP consists of two substates so that we can reenter if we
 * encounter a busy object.
 *
DOT subgraph xcluster_lookup {
DOT	lookup [
DOT		shape=record
DOT		label="{<top>cnt_lookup:|hash lookup|{<busy>busy ?|<miss>miss ?}|{<no>no|obj.f.pass?|<yes>yes}}"
DOT	]
DOT }
DOT lookup:busy -> lookup:top [label="(waitinglist)"]
DOT lookup:miss -> miss [style=bold,color=blue]
DOT lookup:no -> hit [style=bold,color=green]
DOT lookup:yes -> pass [style=bold,color=red]
 */

static int
cnt_lookup(struct sess *sp, struct worker *wrk, struct req *req)
{
	struct objcore *oc;
	struct object *o;
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);

	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->busyobj);

	VRY_Prep(req);

	AZ(req->objcore);
	oc = HSH_Lookup(sp);
	if (oc == NULL) {
		/*
		 * We lost the session to a busy object, disembark the
		 * worker thread.   We return to STP_LOOKUP when the busy
		 * object has been unbusied, and still have the hash digest
		 * around to do the lookup with.
		 * NB:  Do not access sp any more !
		 */
		return (1);
	}
	AZ(req->objcore);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	/* If we inserted a new object it's a miss */
	if (oc->flags & OC_F_BUSY) {
		CHECK_OBJ_NOTNULL(oc->busyobj, BUSYOBJ_MAGIC);
		assert(oc->busyobj == req->busyobj);
		wrk->stats.cache_miss++;

		if (req->vary_l != NULL) {
			assert(oc->busyobj->vary == req->vary_b);
			VRY_Validate(oc->busyobj->vary);
			WS_ReleaseP(req->ws, (void*)req->vary_l);
		} else {
			AZ(oc->busyobj->vary);
			WS_Release(req->ws, 0);
		}
		req->vary_b = NULL;
		req->vary_l = NULL;
		req->vary_e = NULL;

		req->objcore = oc;
		sp->step = STP_MISS;
		return (0);
	}

	/* We are not prepared to do streaming yet */
	XXXAZ(req->busyobj);

	o = oc_getobj(&wrk->stats, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	req->obj = o;

	WS_Release(req->ws, 0);
	req->vary_b = NULL;
	req->vary_l = NULL;
	req->vary_e = NULL;

	if (oc->flags & OC_F_PASS) {
		wrk->stats.cache_hitpass++;
		VSLb(sp->req->vsl, SLT_HitPass, "%u", req->obj->xid);
		(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
		AZ(req->objcore);
		sp->step = STP_PASS;
		return (0);
	}

	wrk->stats.cache_hit++;
	VSLb(sp->req->vsl, SLT_Hit, "%u", req->obj->xid);
	sp->step = STP_HIT;
	return (0);
}

/*--------------------------------------------------------------------
 * We had a miss, ask VCL, proceed as instructed
 *
DOT subgraph xcluster_miss {
DOT	miss [
DOT		shape=record
DOT		label="{cnt_miss:|filter req.-\>bereq.|{vcl_miss\{\}|{req.*|bereq.*}}|{<err>error?|<rst>restart?}|{<pass>pass?|<fetch>fetch?}}"
DOT	]
DOT }
DOT miss:fetch -> fetch [label="fetch",style=bold,color=blue]
DOT miss:pass -> pass [label="pass",style=bold,color=red]
DOT
 */

static int
cnt_miss(struct sess *sp, struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	bo = req->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AZ(req->obj);

	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, HTTP_Bereq);
	http_FilterReq(sp, HTTPH_R_FETCH);
	http_ForceGet(bo->bereq);
	if (cache_param->http_gzip_support) {
		/*
		 * We always ask the backend for gzip, even if the
		 * client doesn't grok it.  We will uncompress for
		 * the minority of clients which don't.
		 */
		http_Unset(bo->bereq, H_Accept_Encoding);
		http_SetHeader(bo->bereq, "Accept-Encoding: gzip");
	}

	VCL_miss_method(sp);

	if (req->handling == VCL_RET_FETCH) {
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		sp->step = STP_FETCH;
		return (0);
	}

	AZ(HSH_Deref(&wrk->stats, req->objcore, NULL));
	req->objcore = NULL;
	http_Teardown(bo->bereq);
	VBO_DerefBusyObj(wrk, &req->busyobj);

	switch(req->handling) {
	case VCL_RET_ERROR:
		sp->step = STP_ERROR;
		break;
	case VCL_RET_PASS:
		sp->step = STP_PASS;
		break;
	case VCL_RET_RESTART:
		sp->step = STP_RESTART;
		break;
	default:
		WRONG("Illegal action in vcl_miss{}");
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Start pass processing by getting headers from backend, then
 * continue in passbody.
 *
DOT subgraph xcluster_pass {
DOT	pass [
DOT		shape=record
DOT		label="{cnt_pass:|(XXX: deref obj.)|filter req.*-\>bereq.|{vcl_pass\{\}|{req.*|bereq.*}}|{<err>error?|<rst>restart?}|<pass>create anon obj}"
DOT	]
DOT }
DOT pass:pass -> fetch [style=bold, color=red]
XDOT pass:rst -> rst_pass [label="restart",color=purple]
XDOT rst_pass [label="RESTART",shape=plaintext]
XDOT pass:err -> err_pass [label="error"]
XDOT err_pass [label="ERROR",shape=plaintext]
 */

static int
cnt_pass(struct sess *sp, struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->objcore);
	AZ(req->obj);
	AZ(req->busyobj);

	req->busyobj = VBO_GetBusyObj(wrk);
	bo = req->busyobj;
	bo->vsl->wid = sp->vsl_id;
	bo->refcount = 2;
	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, HTTP_Bereq);
	http_FilterReq(sp, HTTPH_R_PASS);

	VCL_pass_method(sp);

	if (req->handling == VCL_RET_ERROR) {
		http_Teardown(bo->bereq);
		VBO_DerefBusyObj(wrk, &req->busyobj);
		sp->step = STP_ERROR;
		return (0);
	}
	assert(req->handling == VCL_RET_PASS);
	wrk->acct_tmp.pass++;
	sp->step = STP_FETCH;

	req->objcore = HSH_NewObjCore(wrk);
	req->objcore->busyobj = bo;
	return (0);
}

/*--------------------------------------------------------------------
 * Ship the request header to the backend unchanged, then pipe
 * until one of the ends close the connection.
 *
DOT subgraph xcluster_pipe {
DOT	pipe [
DOT		shape=ellipse
DOT		label="Filter req.->bereq."
DOT	]
DOT	vcl_pipe [
DOT		shape=record
DOT		label="vcl_pipe()|req.\nbereq\."
DOT	]
DOT	pipe_do [
DOT		shape=ellipse
DOT		label="send bereq.\npipe until close"
DOT	]
DOT	vcl_pipe -> pipe_do [label="pipe",style=bold,color=orange]
DOT	pipe -> vcl_pipe [style=bold,color=orange]
DOT }
DOT pipe_do -> DONE [style=bold,color=orange]
DOT vcl_pipe -> err_pipe [label="error"]
DOT err_pipe [label="ERROR",shape=plaintext]
 */

static int
cnt_pipe(struct sess *sp, struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->busyobj);

	wrk->acct_tmp.pipe++;
	req->busyobj = VBO_GetBusyObj(wrk);
	bo = req->busyobj;
	bo->vsl->wid = sp->vsl_id;
	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, HTTP_Bereq);
	http_FilterReq(sp, 0);

	VCL_pipe_method(sp);

	if (req->handling == VCL_RET_ERROR)
		INCOMPL();
	assert(req->handling == VCL_RET_PIPE);

	PipeSession(sp);
	assert(WRW_IsReleased(wrk));
	http_Teardown(bo->bereq);
	VBO_DerefBusyObj(wrk, &req->busyobj);
	sp->step = STP_DONE;
	return (0);
}

/*--------------------------------------------------------------------
 *
DOT subgraph xcluster_restart {
DOT	restart [
DOT		shape=record
DOT		label="{cnt_restart}"
DOT	]
DOT }
DOT RESTART -> restart [color=purple]
DOT restart -> recv [color=purple]
 */

static int
cnt_restart(struct sess *sp, const struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	req->director = NULL;
	if (++req->restarts >= cache_param->max_restarts) {
		req->err_code = 503;
		sp->step = STP_ERROR;
	} else {
		req->err_code = 0;
		sp->step = STP_RECV;
	}
	return (0);
}

/*--------------------------------------------------------------------
 * RECV
 * We have a complete request, set everything up and start it.
 * We can come here both with a request from the client and with
 * a interior request during ESI delivery.
 *
DOT subgraph xcluster_recv {
DOT	recv [
DOT		shape=record
DOT		label="{cnt_recv:|{vcl_recv\{\}|req.*}|{<pipe>pipe?|<pass>pass?|<error>error?|<lookup>lookup?}}"
DOT	]
DOT }
DOT subgraph xcluster_hash {
DOT	hash [
DOT		shape=record
DOT		label="{cnt_recv:|{vcl_hash\{\}|req.*}}"
DOT	]
DOT }
DOT ESI_REQ [ shape=hexagon ]
DOT ESI_REQ -> recv
DOT recv:pipe -> pipe [style=bold,color=orange]
DOT recv:pass -> pass [style=bold,color=red]
#DOT recv:error -> err_recv
#DOT err_recv [label="ERROR",shape=plaintext]
DOT recv:lookup -> hash [style=bold,color=green]
DOT hash -> lookup [label="hash",style=bold,color=green]
 */

static int
cnt_recv(struct sess *sp, const struct worker *wrk, struct req *req)
{
	unsigned recv_handling;
	struct SHA256Context sha256ctx;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->obj);
	AZ(req->busyobj);

	/* By default we use the first backend */
	AZ(req->director);
	req->director = req->vcl->director[0];
	AN(req->director);

	req->disable_esi = 0;
	req->hash_always_miss = 0;
	req->hash_ignore_busy = 0;
	req->client_identity = NULL;

	http_CollectHdr(req->http, H_Cache_Control);

	VCL_recv_method(sp);
	recv_handling = req->handling;

	if (cache_param->http_gzip_support &&
	     (recv_handling != VCL_RET_PIPE) &&
	     (recv_handling != VCL_RET_PASS)) {
		if (RFC2616_Req_Gzip(req->http)) {
			http_Unset(req->http, H_Accept_Encoding);
			http_SetHeader(req->http, "Accept-Encoding: gzip");
		} else {
			http_Unset(req->http, H_Accept_Encoding);
		}
	}

	req->sha256ctx = &sha256ctx;	/* so HSH_AddString() can find it */
	SHA256_Init(req->sha256ctx);
	VCL_hash_method(sp);
	assert(req->handling == VCL_RET_HASH);
	SHA256_Final(req->digest, req->sha256ctx);
	req->sha256ctx = NULL;

	if (!strcmp(req->http->hd[HTTP_HDR_REQ].b, "HEAD"))
		req->wantbody = 0;
	else
		req->wantbody = 1;

	switch(recv_handling) {
	case VCL_RET_LOOKUP:
		sp->step = STP_LOOKUP;
		return (0);
	case VCL_RET_PIPE:
		if (req->esi_level > 0) {
			/* XXX: VSL something */
			INCOMPL();
			/* sp->step = STP_DONE; */
			return (1);
		}
		sp->step = STP_PIPE;
		return (0);
	case VCL_RET_PASS:
		sp->step = STP_PASS;
		return (0);
	case VCL_RET_ERROR:
		sp->step = STP_ERROR;
		return (0);
	default:
		WRONG("Illegal action in vcl_recv{}");
	}
}

/*--------------------------------------------------------------------
 * START
 * First time we see a request
 *
DOT start [
DOT	shape=box
DOT	label="cnt_start:\nDissect request\nHandle expect"
DOT ]
DOT start -> recv [style=bold,color=green]
DOT start -> DONE [label=errors]
 */

static int
cnt_start(struct sess *sp, struct worker *wrk, struct req *req)
{
	char *p;
	const char *r = "HTTP/1.1 100 Continue\r\n\r\n";

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->restarts);
	AZ(req->obj);
	AZ(req->vcl);
	AZ(req->esi_level);
	assert(!isnan(sp->t_req));

	/* Update stats of various sorts */
	wrk->stats.client_req++;
	wrk->acct_tmp.req++;

	/* Assign XID and log */
	req->xid = ++xids;				/* XXX not locked */
	VSLb(sp->req->vsl, SLT_ReqStart, "%s %s %u",
	    sp->addr, sp->port,  req->xid);

	/* Borrow VCL reference from worker thread */
	VCL_Refresh(&wrk->vcl);
	req->vcl = wrk->vcl;
	wrk->vcl = NULL;

	EXP_Clr(&req->exp);

	HTTP_Setup(req->http, req->ws, req->vsl, HTTP_Req);
	req->err_code = http_DissectRequest(sp);

	/* If we could not even parse the request, just close */
	if (req->err_code == 400) {
		sp->step = STP_DONE;
		SES_Close(sp, "junk");
		return (0);
	}

	req->ws_req = WS_Snapshot(req->ws);

	req->doclose = http_DoConnection(req->http);

	/*
	 * We want to deal with Expect: headers the first time we
	 * attempt the request, and remove them before we move on.
	 */
	if (req->err_code == 0 && http_GetHdr(req->http, H_Expect, &p)) {
		if (strcasecmp(p, "100-continue")) {
			req->err_code = 417;
		} else if (strlen(r) != write(sp->fd, r, strlen(r))) {
			sp->step = STP_DONE;
			SES_Close(sp, "remote closed");
			return (0);
		}
	}
	http_Unset(req->http, H_Expect);

	/* XXX: pull in req-body and make it available instead. */
	req->reqbodydone = 0;

	HTTP_Copy(req->http0, req->http);	/* Copy for restart/ESI use */

	if (req->err_code)
		sp->step = STP_ERROR;
	else
		sp->step = STP_RECV;
	return (0);
}

/*--------------------------------------------------------------------
 * Central state engine dispatcher.
 *
 * Kick the session around until it has had enough.
 *
 */

static void
cnt_diag(struct sess *sp, const char *state)
{
	void *vcl;
	void *obj;

	if (sp->req == NULL) {
		vcl = NULL;
		obj = NULL;
	} else {
		vcl = sp->req->vcl;
		obj = sp->req->obj;
	}

	if (sp->req != NULL) {
		VSLb(sp->req->vsl,  SLT_Debug,
		    "vsl_id %u STP_%s sp %p obj %p vcl %p",
		    sp->vsl_id, state, sp, obj, vcl);
		VSL_Flush(sp->req->vsl, 0);
	} else {
		VSL(SLT_Debug, sp->vsl_id,
		    "vsl_id %u STP_%s sp %p obj %p vcl %p",
		    sp->vsl_id, state, sp, obj, vcl);
	}
}

void
CNT_Session(struct sess *sp)
{
	int done;
	struct worker *wrk;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	wrk = sp->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	/*
	 * Possible entrance states
	 */
	assert(
	    sp->step == STP_FIRST ||
	    sp->step == STP_WAIT ||
	    sp->step == STP_LOOKUP ||
	    sp->step == STP_RECV);

	/*
	 * Whenever we come in from the acceptor or waiter, we need to set
	 * blocking mode, but there is no point in setting it when we come from
	 * ESI or when a parked sessions returns.
	 * It would be simpler to do this in the acceptor or waiter, but we'd
	 * rather do the syscall in the worker thread.
	 * On systems which return errors for ioctl, we close early
	 */
	if ((sp->step == STP_FIRST || sp->step == STP_START) &&
	    VTCP_blocking(sp->fd)) {
		if (errno == ECONNRESET)
			SES_Close(sp, "remote closed");
		else
			SES_Close(sp, "error");
		sp->step = STP_DONE;
	}

	/*
	 * NB: Once done is set, we can no longer touch sp!
	 */
	for (done = 0; !done; ) {
		assert(sp->wrk == wrk);
		/*
		 * This is a good place to be paranoid about the various
		 * pointers still pointing to the things we expect.
		 */
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
		CHECK_OBJ_ORNULL(wrk->nobjhead, OBJHEAD_MAGIC);
		WS_Assert(wrk->aws);

		switch (sp->step) {
#define STEP(l,u,arg) \
		    case STP_##u: \
			if (cache_param->diag_bitmap & 0x01) \
				cnt_diag(sp, #u); \
			done = cnt_##l arg; \
		        break;
#include "tbl/steps.h"
#undef STEP
		default:
			WRONG("State engine misfire");
		}
		WS_Assert(wrk->aws);
		CHECK_OBJ_ORNULL(wrk->nobjhead, OBJHEAD_MAGIC);
	}
#define ACCT(foo)	AZ(wrk->acct_tmp.foo);
#include "tbl/acct_fields.h"
#undef ACCT
	assert(WRW_IsReleased(wrk));
}

/*
DOT }
*/

/*--------------------------------------------------------------------
 * Debugging aids
 */

static void
cli_debug_xid(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	if (av[2] != NULL)
		xids = strtoul(av[2], NULL, 0);
	VCLI_Out(cli, "XID is %u", xids);
}

/*
 * Default to seed=1, this is the only seed value POSIXl guarantees will
 * result in a reproducible random number sequence.
 */
static void
cli_debug_srandom(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	unsigned seed = 1;

	if (av[2] != NULL)
		seed = strtoul(av[2], NULL, 0);
	srandom(seed);
	srand48(random());
	VCLI_Out(cli, "Random(3) seeded with %u", seed);
}

static struct cli_proto debug_cmds[] = {
	{ "debug.xid", "debug.xid",
		"\tExamine or set XID\n", 0, 1, "d", cli_debug_xid },
	{ "debug.srandom", "debug.srandom",
		"\tSeed the random(3) function\n", 0, 1, "d",
		cli_debug_srandom },
	{ NULL }
};

/*--------------------------------------------------------------------
 *
 */

void
CNT_Init(void)
{

	srandomdev();
	srand48(random());
	xids = random();
	CLI_AddFuncs(debug_cmds);
}
