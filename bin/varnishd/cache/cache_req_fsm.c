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
 * This file contains the request-handling state engine, which is intended to
 * (over time) be(ome) protocol agnostic.
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
 *	sed -n '/^DOT/s///p' cache/cache_req_fsm.c | dot -Tps > /tmp/_.ps
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
DOT ESI_REQ [ shape=hexagon ]
DOT ESI_REQ -> recv
DOT ERROR [shape=plaintext]
DOT RESTART [shape=plaintext]
DOT acceptor -> recv [style=bold,color=green]
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "hash/hash_slinger.h"
#include "vcl.h"
#include "vsha256.h"
#include "vtim.h"

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

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

static enum req_fsm_nxt
cnt_prepresp(struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

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
			req->doclose = SC_TX_EOF;
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
	RES_BuildHttp(req);

	VCL_deliver_method(req->vcl, wrk, req, NULL, req->http->ws);
	switch (wrk->handling) {
	case VCL_RET_DELIVER:
		break;
	case VCL_RET_RESTART:
		if (req->restarts >= cache_param->max_restarts)
			break;
		if (bo != NULL) {
			AN(bo->do_stream);
			assert(req->obj == bo->fetch_obj);
			req->obj = NULL;
			VBO_DerefBusyObj(wrk, &req->busyobj);
		} else {
			(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
		}
		AZ(req->obj);
		http_Teardown(req->resp);
		req->req_step = R_STP_RESTART;
		return (REQ_FSM_MORE);
	default:
		WRONG("Illegal action in vcl_deliver{}");
	}
	req->req_step = R_STP_DELIVER;
	return (REQ_FSM_MORE);
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

static enum req_fsm_nxt
cnt_deliver(struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

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
			(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
			VBO_DerefBusyObj(wrk, &req->busyobj);
			req->err_code = 503;
			req->req_step = R_STP_ERROR;
			return (REQ_FSM_MORE);
		}
		VBO_DerefBusyObj(wrk, &req->busyobj);
	}

	AZ(req->busyobj);
	req->director = NULL;
	req->restarts = 0;

	RES_WriteObj(req);

	/* No point in saving the body if it is hit-for-pass */
	if (req->obj->objcore->flags & OC_F_PASS)
		STV_Freestore(req->obj);

	assert(WRW_IsReleased(wrk));
	(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
	http_Teardown(req->resp);
	return (REQ_FSM_DONE);
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

static enum req_fsm_nxt
cnt_error(struct worker *wrk, struct req *req)
{
	struct http *h;
	struct busyobj *bo;
	char date[40];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);
	AZ(req->obj);
	AZ(req->busyobj);

	req->acct_req.error++;
	bo = VBO_GetBusyObj(wrk, req);
	req->busyobj = bo;
	AZ(bo->stats);
	bo->stats = &wrk->stats;
	bo->fetch_objcore = HSH_NewObjCore(wrk);
	req->obj = STV_NewObject(bo,
	    TRANSIENT_STORAGE, cache_param->http_resp_size,
	    (uint16_t)cache_param->http_max_hdr);
	bo->stats = NULL;
	if (req->obj == NULL) {
		req->doclose = SC_OVERLOAD;
		req->director = NULL;
		AZ(HSH_Deref(&wrk->stats, bo->fetch_objcore, NULL));
		bo->fetch_objcore = NULL;
		http_Teardown(bo->beresp);
		http_Teardown(bo->bereq);
		VBO_DerefBusyObj(wrk, &req->busyobj);
		AZ(req->busyobj);
		return (REQ_FSM_DONE);
	}
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	AZ(req->objcore);
	AZ(bo->fetch_objcore);
	req->obj->vxid = bo->vsl->wid;
	req->obj->exp.entered = req->t_req;

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
	VCL_error_method(req->vcl, wrk, req, NULL, req->http->ws);

	if (wrk->handling == VCL_RET_RESTART &&
	    req->restarts <  cache_param->max_restarts) {
		HSH_Drop(wrk, &req->obj);
		VBO_DerefBusyObj(wrk, &req->busyobj);
		req->req_step = R_STP_RESTART;
		return (REQ_FSM_MORE);
	} else if (wrk->handling == VCL_RET_RESTART)
		wrk->handling = VCL_RET_DELIVER;


	/* We always close when we take this path */
	req->doclose = SC_TX_ERROR;
	req->wantbody = 1;

	assert(wrk->handling == VCL_RET_DELIVER);
	req->err_code = 0;
	req->err_reason = NULL;
	http_Teardown(bo->bereq);
	VBO_DerefBusyObj(wrk, &req->busyobj);
	req->req_step = R_STP_PREPRESP;
	return (REQ_FSM_MORE);
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

static enum req_fsm_nxt
cnt_fetch(struct worker *wrk, struct req *req)
{
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	i = VBF_Fetch(wrk, req);
	if (i < 0) {
		VBO_DerefBusyObj(wrk, &req->busyobj);
		req->err_code = 503;
		req->req_step = R_STP_ERROR;
	} else {
		AZ(i);
		assert(WRW_IsReleased(wrk));
		req->req_step = R_STP_PREPRESP;
	}
	return (REQ_FSM_MORE);
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

static enum req_fsm_nxt
cnt_lookup(struct worker *wrk, struct req *req)
{
	struct objcore *oc, *boc;
	struct object *o;
	struct objhead *oh;
	enum lookup_e lr;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);

	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->busyobj);

	VRY_Prep(req);

	AZ(req->objcore);
	lr = HSH_Lookup(req, &oc, &boc,
	    req->esi_level == 0 ? 1 : 0,
	    req->hash_always_miss ? 1 : 0
	);
	if (lr == HSH_BUSY) {
		/*
		 * We lost the session to a busy object, disembark the
		 * worker thread.   We return to STP_LOOKUP when the busy
		 * object has been unbusied, and still have the objhead
		 * around to restart the lookup with.
		 */
		return (REQ_FSM_DISEMBARK);
	}
	AZ(req->objcore);


	switch (lr) {
	case HSH_EXP:
		VSLb(req->vsl, SLT_Debug, "XXXX EXP\n");
		AN(oc);
		AZ(boc);
		break;
	case HSH_EXPBUSY:
		VSLb(req->vsl, SLT_Debug, "XXXX EXPBUSY\n");
		AN(oc);
		AN(boc);
		if (VDI_Healthy(req->director, req->digest)) {
			VSLb(req->vsl, SLT_Debug, "XXX EXPBUSY deref oc\n");
			(void)HSH_Deref(&wrk->stats, oc, NULL);
			oc = boc;
			boc = NULL;
		} else {
			VSLb(req->vsl, SLT_Debug, "XXX EXPBUSY drop boc\n");
			(void)HSH_Deref(&wrk->stats, boc, NULL);
			boc = NULL;
		}
		break;
	case HSH_MISS:
		VSLb(req->vsl, SLT_Debug, "XXXX MISS\n");
		AZ(oc);
		AN(boc);
		oc = boc;
		boc = NULL;
		AN(oc->flags & OC_F_BUSY);
		break;
	case HSH_HIT:
		VSLb(req->vsl, SLT_Debug, "XXXX HIT\n");
		AN(oc);
		AZ(boc);
		break;
	default:
		INCOMPL();
	}

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	/* If we inserted a new object it's a miss */
	if (oc->flags & OC_F_BUSY) {
		req->objcore = oc;
		req->req_step = R_STP_MISS;
		return (REQ_FSM_MORE);
	}

	/* We are not prepared to do streaming yet */
	XXXAZ(req->busyobj);

	o = oc_getobj(&wrk->stats, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	req->obj = o;

	VRY_Finish(req, NULL);

	if (oc->flags & OC_F_PASS)
		wrk->stats.cache_hitpass++;
	else
		wrk->stats.cache_hit++;
	VSLb(req->vsl, SLT_Hit, "%u", req->obj->vxid);

	AZ(req->objcore);
	AZ(req->busyobj);

	VCL_lookup_method(req->vcl, wrk, req, NULL, req->http->ws);

	if ((req->obj->objcore->flags & OC_F_PASS) &&
	    wrk->handling == VCL_RET_DELIVER) {
		VSLb(req->vsl, SLT_VCL_Error,
		    "obj.uncacheable set, but vcl_lookup{} returned 'deliver'"
		    ", changing to 'pass'");
		wrk->handling = VCL_RET_PASS;
	}

	if (wrk->handling == VCL_RET_DELIVER) {
		//AZ(req->busyobj->bereq->ws);
		//AZ(req->busyobj->beresp->ws);
		(void)HTTP1_DiscardReqBody(req);	// XXX: handle err
		req->req_step = R_STP_PREPRESP;
		return (REQ_FSM_MORE);
	}

	/* Drop our object, we won't need it */
	(void)HSH_Deref(&wrk->stats, NULL, &req->obj);
	req->objcore = NULL;

	switch(wrk->handling) {
	case VCL_RET_PASS:
		req->req_step = R_STP_PASS;
		break;
	case VCL_RET_ERROR:
		req->req_step = R_STP_ERROR;
		break;
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	default:
		WRONG("Illegal action in vcl_lookup{}");
	}
	return (REQ_FSM_MORE);
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

static enum req_fsm_nxt
cnt_miss(struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	AZ(req->obj);
	AZ(req->busyobj);

	/*
	 * We optimistically expect to need this most of the time
	 * (This allows us to put the predictive Vary directly on the bo->ws)
	 */
	bo = VBO_GetBusyObj(wrk, req);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	req->busyobj = bo;
	VRY_Finish(req, bo);

	VCL_miss_method(req->vcl, wrk, req, NULL, req->http->ws);
	switch (wrk->handling) {
	case VCL_RET_ERROR:
		VBO_DerefBusyObj(wrk, &req->busyobj);
		AZ(HSH_Deref(&wrk->stats, req->objcore, NULL));
		req->objcore = NULL;
		req->req_step = R_STP_ERROR;
		return (REQ_FSM_MORE);
	case VCL_RET_RESTART:
		VBO_DerefBusyObj(wrk, &req->busyobj);
		AZ(HSH_Deref(&wrk->stats, req->objcore, NULL));
		req->objcore = NULL;
		req->req_step = R_STP_RESTART;
		return (REQ_FSM_MORE);
	case VCL_RET_PASS:
		AZ(HSH_Deref(&wrk->stats, req->objcore, NULL));
		req->objcore = HSH_NewObjCore(wrk);
		bo->do_pass = 1;
		break;
	case VCL_RET_FETCH:
		break;
	default:
		WRONG("wrong return from vcl_miss{}");
	}

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	/* One ref for req, one for FetchBody */
	bo->refcount = 2;

	AN (req->objcore);
	req->objcore->busyobj = bo;
	wrk->stats.cache_miss++;

	req->req_step = R_STP_FETCH;
	return (REQ_FSM_MORE);
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

static enum req_fsm_nxt
cnt_pass(struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->objcore);
	AZ(req->obj);
	AZ(req->busyobj);

	VCL_pass_method(req->vcl, wrk, req, NULL, req->http->ws);
	if (wrk->handling == VCL_RET_ERROR) {
		req->req_step = R_STP_ERROR;
		return (REQ_FSM_MORE);
	}
	if (wrk->handling == VCL_RET_RESTART) {
		INCOMPL();
	}
	assert (wrk->handling == VCL_RET_FETCH);
	req->acct_req.pass++;

	bo = VBO_GetBusyObj(wrk, req);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	req->busyobj = bo;
	bo->do_pass = 1;
	req->objcore = HSH_NewObjCore(wrk);
	req->objcore->busyobj = bo;
	bo->refcount = 2;

	req->req_step = R_STP_FETCH;
	return (REQ_FSM_MORE);
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

static enum req_fsm_nxt
cnt_pipe(struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->busyobj);

	req->acct_req.pipe++;
	req->busyobj = VBO_GetBusyObj(wrk, req);
	bo = req->busyobj;
	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, HTTP_Bereq);
	http_FilterReq(bo->bereq, req->http, 0);	// XXX: 0 ?
	http_PrintfHeader(bo->bereq,
	    "X-Varnish: %u", req->vsl->wid & VSL_IDENTMASK);

	VCL_pipe_method(req->vcl, wrk, req, NULL, req->http->ws);

	if (wrk->handling == VCL_RET_ERROR)
		INCOMPL();
	assert(wrk->handling == VCL_RET_PIPE);

	PipeRequest(req);
	assert(WRW_IsReleased(wrk));
	http_Teardown(bo->bereq);
	VBO_DerefBusyObj(wrk, &req->busyobj);
	return (REQ_FSM_DONE);
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
DOT restart -> err_restart
DOT err_restart [label="ERROR",shape=plaintext]
 */

static enum req_fsm_nxt
cnt_restart(const struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	req->director = NULL;
	if (++req->restarts >= cache_param->max_restarts) {
		req->err_code = 503;
		req->req_step = R_STP_ERROR;
	} else {
		req->err_code = 0;
		req->req_step = R_STP_RECV;
	}
	return (REQ_FSM_MORE);
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
DOT recv:pipe -> pipe [style=bold,color=orange]
DOT recv:pass -> pass [style=bold,color=red]
#DOT recv:error -> err_recv
#DOT err_recv [label="ERROR",shape=plaintext]
DOT recv:lookup -> hash [style=bold,color=green]
DOT hash -> lookup [label="hash",style=bold,color=green]
 */

static enum req_fsm_nxt
cnt_recv(struct worker *wrk, struct req *req)
{
	unsigned recv_handling;
	struct SHA256Context sha256ctx;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->objcore);
	AZ(req->obj);
	AZ(req->objcore);
	AZ(req->busyobj);

	VSLb(req->vsl, SLT_ReqStart, "%s %s", req->sp->addr, req->sp->port);

	if (req->err_code) {
		req->req_step = R_STP_ERROR;
		return (REQ_FSM_MORE);
	}

	/* By default we use the first backend */
	AZ(req->director);
	req->director = req->vcl->director[0];
	AN(req->director);

	EXP_Clr(&req->exp);

	req->disable_esi = 0;
	req->hash_always_miss = 0;
	req->hash_ignore_busy = 0;
	req->client_identity = NULL;

	http_CollectHdr(req->http, H_Cache_Control);

	VCL_recv_method(req->vcl, wrk, req, NULL, req->http->ws);
	recv_handling = wrk->handling;

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
	VCL_hash_method(req->vcl, wrk, req, NULL, req->http->ws);
	assert(wrk->handling == VCL_RET_LOOKUP);
	SHA256_Final(req->digest, req->sha256ctx);
	req->sha256ctx = NULL;

	if (!strcmp(req->http->hd[HTTP_HDR_METHOD].b, "HEAD"))
		req->wantbody = 0;
	else
		req->wantbody = 1;

	switch(recv_handling) {
	case VCL_RET_PURGE:
		req->req_step = R_STP_PURGE;
		return (REQ_FSM_MORE);
	case VCL_RET_HASH:
		req->req_step = R_STP_LOOKUP;
		return (REQ_FSM_MORE);
	case VCL_RET_PIPE:
		if (req->esi_level > 0) {
			/* XXX: VSL something */
			INCOMPL();
			return (REQ_FSM_DONE);
		}
		req->req_step = R_STP_PIPE;
		return (REQ_FSM_MORE);
	case VCL_RET_PASS:
		req->req_step = R_STP_PASS;
		return (REQ_FSM_MORE);
	case VCL_RET_ERROR:
		req->req_step = R_STP_ERROR;
		return (REQ_FSM_MORE);
	default:
		WRONG("Illegal action in vcl_recv{}");
	}
}

/*--------------------------------------------------------------------
 * PURGE
 * Find the objhead, purge it and ask VCL if we should fetch or
 * just return.
 * XXX: fetching not implemented yet.
 */

static enum req_fsm_nxt
cnt_purge(struct worker *wrk, struct req *req)
{
	struct objcore *oc, *boc;
	enum lookup_e lr;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);

	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->busyobj);

	VRY_Prep(req);

	AZ(req->objcore);
	lr = HSH_Lookup(req, &oc, &boc, 1, 1);
	assert (lr == HSH_MISS);
	AZ(oc);
	CHECK_OBJ_NOTNULL(boc, OBJCORE_MAGIC);
	VRY_Finish(req, NULL);

	HSH_Purge(wrk, boc->objhead, 0, 0);

	AZ(HSH_Deref(&wrk->stats, boc, NULL));

	VCL_purge_method(req->vcl, wrk, req, NULL, req->http->ws);
	req->req_step = R_STP_ERROR;
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Central state engine dispatcher.
 *
 * Kick the session around until it has had enough.
 *
 */

static void
cnt_diag(struct req *req, const char *state)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	VSLb(req->vsl,  SLT_Debug, "vxid %u STP_%s sp %p obj %p vcl %p",
	    req->vsl->wid, state, req->sp, req->obj, req->vcl);
	VSL_Flush(req->vsl, 0);
}

enum req_fsm_nxt
CNT_Request(struct worker *wrk, struct req *req)
{
	enum req_fsm_nxt nxt;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/*
	 * Possible entrance states
	 */
	assert(
	    req->req_step == R_STP_LOOKUP ||
	    req->req_step == R_STP_RECV);

	AN(req->vsl->wid & VSL_CLIENTMARKER);

	req->wrk = wrk;

	for (nxt = REQ_FSM_MORE; nxt == REQ_FSM_MORE; ) {
		/*
		 * This is a good place to be paranoid about the various
		 * pointers still pointing to the things we expect.
		 */
		CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
		CHECK_OBJ_ORNULL(wrk->nobjhead, OBJHEAD_MAGIC);
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

		/*
		 * We don't want the thread workspace to be used for
		 * anything of long duration, so mandate that it be
		 * empty on state-transitions.
		 */
		WS_Assert(wrk->aws);
		assert(wrk->aws->s == wrk->aws->f);

		switch (req->req_step) {
#define REQ_STEP(l,u,arg) \
		    case R_STP_##u: \
			if (DO_DEBUG(DBG_REQ_STATE)) \
				cnt_diag(req, #u); \
			nxt = cnt_##l arg; \
		        break;
#include "tbl/steps.h"
#undef REQ_STEP
		default:
			WRONG("State engine misfire");
		}
		WS_Assert(wrk->aws);
		CHECK_OBJ_ORNULL(wrk->nobjhead, OBJHEAD_MAGIC);
	}
	if (nxt == REQ_FSM_DONE) {
		/* XXX: Workaround for pipe */
		if (req->sp->fd >= 0) {
			VSLb(req->vsl, SLT_Length, "%ju",
			    (uintmax_t)req->resp_bodybytes);
		}
		VSLb(req->vsl, SLT_ReqEnd, "%.9f %.9f %.9f %.9f %.9f",
		    req->t_req,
		    req->sp->t_idle,
		    req->sp->t_idle - req->t_resp,
		    req->t_resp - req->t_req,
		    req->sp->t_idle - req->t_resp);

		while (!VTAILQ_EMPTY(&req->body)) {
			st = VTAILQ_FIRST(&req->body);
			VTAILQ_REMOVE(&req->body, st, list);
			STV_free(st);
		}

		/* done == 2 was charged by cache_hash.c */
		SES_Charge(wrk, req);

		/*
		 * Nuke the VXID, cache_http1_fsm.c::http1_dissect() will
		 * allocate a new one when necessary.
		 */
		VSLb(req->vsl, SLT_End, "%s", "");
		req->vsl->wid = 0;
	}

	req->wrk = NULL;

	assert(WRW_IsReleased(wrk));
	return (nxt);
}

/*
DOT }
*/
