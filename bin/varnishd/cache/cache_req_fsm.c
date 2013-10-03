/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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
 * Deliver an already stored object
 *
DOT	deliver [
DOT		shape=record
DOT		label="{cnt_deliver:|Filter obj.-\>resp.|{vcl_deliver\{\}|{req.|resp.}}|{restart?|<deliver>deliver?}}"
DOT	]
DOT deliver:deliver:s -> DONE [style=bold,color=green]
DOT deliver:deliver:s -> DONE [style=bold,color=red]
DOT deliver:deliver:s -> DONE [style=bold,color=blue]
 *
 */

static enum req_fsm_nxt
cnt_deliver(struct worker *wrk, struct req *req)
{
	char time_str[30];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj->objcore, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj->objcore->objhead, OBJHEAD_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	assert(WRW_IsReleased(wrk));

	assert(req->obj->objcore->refcnt > 0);

	req->t_resp = W_TIM_real(wrk);
	if (!(req->obj->objcore->flags & OC_F_PRIVATE)) {
		if ((req->t_resp - req->obj->objcore->last_lru) >
		    cache_param->lru_timeout && EXP_Touch(req->obj->objcore))
			req->obj->objcore->last_lru = req->t_resp;
	}

	HTTP_Setup(req->resp, req->ws, req->vsl, HTTP_Resp);

	http_ClrHeader(req->resp);
	http_FilterResp(req->obj->http, req->resp, 0);

	http_Unset(req->resp, H_Date);
	VTIM_format(req->t_resp, time_str);
	http_PrintfHeader(req->resp, "Date: %s", time_str);

	if (req->wrk->stats.cache_hit)
		http_PrintfHeader(req->resp,
		    "X-Varnish: %u %u", req->vsl->wid & VSL_IDENTMASK,
		    req->obj->vxid & VSL_IDENTMASK);
	else
		http_PrintfHeader(req->resp,
		    "X-Varnish: %u", req->vsl->wid & VSL_IDENTMASK);

	http_PrintfHeader(req->resp, "Age: %.0f",
	    req->obj->exp.age + req->t_resp - req->obj->exp.entered);

	http_SetHeader(req->resp, "Via: 1.1 varnish");

	VCL_deliver_method(req->vcl, wrk, req, NULL, req->http->ws);

	/* Stop the insanity before it turns "Hotel California" on us */
	if (req->restarts >= cache_param->max_restarts)
		wrk->handling = VCL_RET_DELIVER;

	if (wrk->handling == VCL_RET_RESTART) {
		(void)HSH_DerefObj(&wrk->stats, &req->obj);
		AZ(req->obj);
		http_Teardown(req->resp);
		req->req_step = R_STP_RESTART;
		return (REQ_FSM_MORE);
	}

	assert(wrk->handling == VCL_RET_DELIVER);

	if (!(req->obj->objcore->flags & OC_F_PASS)
	    && req->obj->response == 200
	    && req->http->conds && RFC2616_Do_Cond(req)) {
		req->wantbody = 0;
		http_SetResp(req->resp, "HTTP/1.1", 304, "Not Modified");
		// http_Unset(req->resp, H_Content_Length);
	}

	V1D_Deliver(req);

	/* No point in saving the body if it is hit-for-pass */
	if (req->obj->objcore->flags & OC_F_PASS)
		STV_Freestore(req->obj);

	assert(WRW_IsReleased(wrk));
VSLb(req->vsl, SLT_Debug, "XXX REF %d", req->obj->objcore->refcnt);
	(void)HSH_DerefObj(&wrk->stats, &req->obj);
	http_Teardown(req->resp);
	return (REQ_FSM_DONE);
}

/*--------------------------------------------------------------------
 * Emit an error
 *
DOT subgraph xcluster_error {
DOT	vcl_error [
DOT		shape=record
DOT		label="{{vcl_error()|resp.}|{<del>deliver?|<restart>restart?}}"
DOT	]
DOT	ERROR -> vcl_error
DOT	vcl_error:del:s -> deliver [label=deliver]
DOT }
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

	req->acct_req.error++;
	bo = VBO_GetBusyObj(wrk, req);
	AZ(bo->stats);
	bo->stats = &wrk->stats;
	bo->fetch_objcore = HSH_Private(wrk);
	bo->fetch_obj = STV_NewObject(bo,
	    TRANSIENT_STORAGE, cache_param->http_resp_size,
	    (uint16_t)cache_param->http_max_hdr);
	req->obj = bo->fetch_obj;
	bo->stats = NULL;
	if (req->obj == NULL) {
		req->doclose = SC_OVERLOAD;
		req->director = NULL;
		AZ(HSH_DerefObjCore(&wrk->stats, &bo->fetch_objcore));
		bo->fetch_objcore = NULL;
		http_Teardown(bo->beresp);
		http_Teardown(bo->bereq);
		VBO_DerefBusyObj(wrk, &bo);
		return (REQ_FSM_DONE);
	}
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	AZ(req->objcore);
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

	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	HSH_Ref(req->obj->objcore);

	if (req->err_reason != NULL)
		http_PutResponse(h, req->err_reason);
	else
		http_PutResponse(h, http_StatusMessage(req->err_code));
	VCL_error_method(req->vcl, wrk, req, NULL, req->http->ws);

	/* Stop the insanity before it turns "Hotel California" on us */
	if (req->restarts >= cache_param->max_restarts)
		wrk->handling = VCL_RET_DELIVER;

	if (wrk->handling == VCL_RET_RESTART) {
		VBO_DerefBusyObj(wrk, &bo);
		HSH_Drop(wrk, &req->obj);
		req->req_step = R_STP_RESTART;
		return (REQ_FSM_MORE);
	}
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);

	/* We always close when we take this path */
	req->doclose = SC_TX_ERROR;
	req->wantbody = 1;

	assert(wrk->handling == VCL_RET_DELIVER);
	req->err_code = 0;
	req->err_reason = NULL;
	http_Teardown(bo->bereq);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	VBO_DerefBusyObj(wrk, &bo);
	req->req_step = R_STP_DELIVER;
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Initiated a fetch (pass/miss) which we intend to deliver
 *
DOT subgraph xcluster_body {
DOT	fetch [
DOT		shape=record
DOT		label="{cnt_fetch:|wait for fetch|{<ok>OK|<err>Failed}}"
DOT	]
DOT }
DOT fetch:ok:s -> deliver [style=bold,color=red]
DOT fetch:ok:s -> deliver [style=bold,color=blue]
DOT fetch:err:s -> vcl_error
 */

static enum req_fsm_nxt
cnt_fetch(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	AZ(req->obj);

	req->acct_req.fetch++;
	(void)HTTP1_DiscardReqBody(req);

	if (req->objcore->flags & OC_F_FAILED) {
		req->err_code = 503;
		req->req_step = R_STP_ERROR;
		(void)HSH_DerefObjCore(&wrk->stats, &req->objcore);
		req->objcore = NULL;
		return (REQ_FSM_MORE);
	}

	req->obj = oc_getobj(&wrk->stats, req->objcore);
	req->objcore = NULL;
	req->err_code = req->obj->response;
	req->req_step = R_STP_DELIVER;
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
DOT		label="{<top>cnt_lookup:|hash lookup|{<busy>busy?|<e>exp?|<eb>exp+busy?|<h>hit?|<miss>miss?|<hfp>hit-for-pass?}}"
DOT	]
DOT	lookup2 [
DOT		shape=record
DOT		label="{<top>cnt_lookup:|{vcl_hit\{\}|req.*, obj.*}|{<deliver>deliver?|error?|restart?|<fetch>fetch?|<pass>pass?}}"
DOT	]
DOT }
DOT lookup:busy:w -> lookup:top:w [label="(waitinglist)"]
DOT lookup:miss:s -> miss [style=bold,color=blue]
DOT lookup:hfp:s -> pass [style=bold,color=red]
DOT lookup:e:s -> lookup2 [style=bold,color=green]
DOT lookup:eb:s -> lookup2 [style=bold,color=green]
DOT lookup:h:s -> lookup2 [style=bold,color=green]
DOT lookup2:pass:s -> pass [style=bold,color=red]
DOT lookup2:fetch:s -> miss [style=bold,color=blue]
DOT lookup2:deliver:s -> deliver:n [style=bold,color=green]
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

	if (boc == NULL) {
		VRY_Finish(req, DISCARD);
	} else {
		AN(boc->flags & OC_F_BUSY);
		VRY_Finish(req, KEEP);
	}

	AZ(req->objcore);
	if (lr == HSH_MISS) {
		/* Found nothing */
		VSLb(req->vsl, SLT_Debug, "XXXX MISS");
		AZ(oc);
		AN(boc);
		AN(boc->flags & OC_F_BUSY);
		req->objcore = boc;
		req->req_step = R_STP_MISS;
		return (REQ_FSM_MORE);
	}

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ (oc->flags & OC_F_BUSY);
	AZ(req->objcore);

	if (oc->flags & OC_F_PASS) {
		/* Found a hit-for-pass */
		VSLb(req->vsl, SLT_Debug, "XXXX HIT-FOR-PASS");
		AZ(boc);
		(void)HSH_DerefObjCore(&wrk->stats, &oc);
		req->objcore = NULL;
		wrk->stats.cache_hitpass++;
		req->req_step = R_STP_PASS;
		return (REQ_FSM_MORE);
	}

	oh = oc->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	o = oc_getobj(&wrk->stats, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	req->obj = o;

	VSLb(req->vsl, SLT_Hit, "%u", req->obj->vxid);

	VCL_hit_method(req->vcl, wrk, req, NULL, req->http->ws);

	switch (wrk->handling) {
	case VCL_RET_DELIVER:
		if (boc != NULL) {
			VBF_Fetch(wrk, req, boc, o, VBF_BACKGROUND);
		} else {
			(void)HTTP1_DiscardReqBody(req);// XXX: handle err
		}
		wrk->stats.cache_hit++;
		req->req_step = R_STP_DELIVER;
		return (REQ_FSM_MORE);
	case VCL_RET_FETCH:
		(void)HSH_DerefObj(&wrk->stats, &req->obj);
		req->objcore = boc;
		if (req->objcore != NULL)
			req->req_step = R_STP_MISS;
		else {
			/*
			 * We don't have a busy object, so treat this
			 * lige a pass
			 */
			VSLb(req->vsl, SLT_VCL_Error,
			    "vcl_hit{} returns fetch without busy object."
			    "  Doing pass.");
			req->req_step = R_STP_PASS;
		}
		return (REQ_FSM_MORE);
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_ERROR:
		req->req_step = R_STP_ERROR;
		break;
	case VCL_RET_PASS:
		wrk->stats.cache_hit++;
		req->req_step = R_STP_PASS;
		break;
	default:
		INCOMPL();
	}

	/* Drop our object, we won't need it */
	(void)HSH_DerefObj(&wrk->stats, &req->obj);
	req->objcore = NULL;

	if (boc != NULL) {
		(void)HSH_DerefObjCore(&wrk->stats, &boc);
		free(req->vary_b);
		req->vary_b = NULL;
	}

	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * We had a miss, ask VCL, proceed as instructed
 *
DOT subgraph xcluster_miss {
DOT	miss [
DOT		shape=record
DOT		label="{cnt_miss:|{vcl_miss\{\}|req.*}|{<fetch>fetch?|<err>error?|<rst>restart?|<pass>pass?}}"
DOT	]
DOT }
DOT miss:fetch:s -> fetch [label="fetch",style=bold,color=blue]
DOT miss:pass:s -> pass [label="pass",style=bold,color=red]
DOT
 */

static enum req_fsm_nxt
cnt_miss(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	AZ(req->obj);

	VCL_miss_method(req->vcl, wrk, req, NULL, req->http->ws);
	switch (wrk->handling) {
	case VCL_RET_FETCH:
		wrk->stats.cache_miss++;
		VBF_Fetch(wrk, req, req->objcore, NULL, VBF_NORMAL);
		req->req_step = R_STP_FETCH;
		return (REQ_FSM_MORE);
	case VCL_RET_ERROR:
		req->req_step = R_STP_ERROR;
		break;
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_PASS:
		req->req_step = R_STP_PASS;
		break;
	default:
		WRONG("Illegal return from vcl_miss{}");
	}
	free(req->vary_b);
	AZ(HSH_DerefObjCore(&wrk->stats, &req->objcore));
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Start pass processing by getting headers from backend, then
 * continue in passbody.
 *
DOT subgraph xcluster_pass {
DOT	pass [
DOT		shape=record
DOT		label="{cnt_pass:|{vcl_pass\{\}|req.*}|{<fetch>fetch?|<err>error?|<rst>restart?}}"
DOT	]
DOT }
DOT pass:fetch:s -> fetch:n [style=bold, color=red]
XDOT pass:rst -> rst_pass [label="restart",color=purple]
XDOT rst_pass [label="RESTART",shape=plaintext]
XDOT pass:err -> err_pass [label="error"]
XDOT err_pass [label="ERROR",shape=plaintext]
 */

static enum req_fsm_nxt
cnt_pass(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->objcore);
	AZ(req->obj);

	VCL_pass_method(req->vcl, wrk, req, NULL, req->http->ws);
	switch (wrk->handling) {
	case VCL_RET_ERROR:
		req->req_step = R_STP_ERROR;
		break;
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_FETCH:
		req->acct_req.pass++;
		req->objcore = HSH_Private(wrk);
		CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
		VBF_Fetch(wrk, req, req->objcore, NULL, VBF_PASS);
		req->req_step = R_STP_FETCH;
		break;
	default:
		WRONG("Illegal return from cnt_pass{}");
	}
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Ship the request header to the backend unchanged, then pipe
 * until one of the ends close the connection.
 *
DOT subgraph xcluster_pipe {
DOT	pipe [
DOT		shape=record
DOT		label="{cnt_pipe:|filter req.*-\>bereq.*|{vcl_pipe()|req.*, bereq\.*}|{<pipe>pipe?|<error>error?}}"
DOT	]
DOT	pipe_do [
DOT		shape=ellipse
DOT		label="send bereq.\npipe until close"
DOT	]
DOT	pipe:pipe -> pipe_do [label="pipe",style=bold,color=orange]
DOT }
DOT pipe_do -> DONE [style=bold,color=orange]
 */

static enum req_fsm_nxt
cnt_pipe(struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);

	req->acct_req.pipe++;
	bo = VBO_GetBusyObj(wrk, req);
	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, HTTP_Bereq);
	http_FilterReq(bo->bereq, req->http, 0);	// XXX: 0 ?
	http_PrintfHeader(bo->bereq,
	    "X-Varnish: %u", req->vsl->wid & VSL_IDENTMASK);

	VCL_pipe_method(req->vcl, wrk, req, bo, req->http->ws);

	if (wrk->handling == VCL_RET_ERROR)
		INCOMPL();
	assert(wrk->handling == VCL_RET_PIPE);

	PipeRequest(req, bo);
	assert(WRW_IsReleased(wrk));
	http_Teardown(bo->bereq);
	VBO_DerefBusyObj(wrk, &bo);
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
DOT		label="{cnt_recv:|{vcl_recv\{\}|req.*}|{vcl_hash\{\}|req.*}|{<lookup>lookup?|<pass>pass?|<pipe>pipe?|<error>error?|<purge>purge?}}"
DOT	]
DOT }
DOT recv:pipe -> pipe [style=bold,color=orange]
DOT recv:pass -> pass [style=bold,color=red]
DOT recv:lookup:s -> lookup [style=bold,color=green]
DOT recv:purge:s -> purge [style=bold,color=purple]
#DOT recv:error -> err_recv
#DOT err_recv [label="ERROR",shape=plaintext]
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
		if (req->esi_level == 0) {
			req->req_step = R_STP_PIPE;
			return (REQ_FSM_MORE);
		}
		VSLb(req->vsl, SLT_VCL_Error,
		    "vcl_recv{} returns pipe for ESI included object."
		    "  Doing pass.");
		req->req_step = R_STP_PASS;
		return (REQ_FSM_DONE);
	case VCL_RET_PASS:
		req->req_step = R_STP_PASS;
		return (REQ_FSM_MORE);
	case VCL_RET_ERROR:
		req->req_step = R_STP_ERROR;
		return (REQ_FSM_MORE);
	default:
		WRONG("Illegal return from vcl_recv{}");
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

	VRY_Prep(req);

	AZ(req->objcore);
	lr = HSH_Lookup(req, &oc, &boc, 1, 1);
	assert (lr == HSH_MISS);
	AZ(oc);
	CHECK_OBJ_NOTNULL(boc, OBJCORE_MAGIC);
	VRY_Finish(req, DISCARD);

	HSH_Purge(wrk, boc->objhead, 0, 0);

	AZ(HSH_DerefObjCore(&wrk->stats, &boc));

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
		req->wrk = NULL;
	}

	AZ(req->wrk);
	assert(WRW_IsReleased(wrk));
	return (nxt);
}

/*
DOT }
*/
