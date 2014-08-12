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
DOT SYNTH [shape=plaintext]
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
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore->objhead, OBJHEAD_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	assert(WRW_IsReleased(wrk));

	req->obj = ObjGetObj(req->objcore, &wrk->stats);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);

	assert(req->objcore->refcnt > 0);

	if (req->objcore->exp_flags & OC_EF_EXP)
		EXP_Touch(req->objcore, req->t_prev);

	HTTP_Setup(req->resp, req->ws, req->vsl, SLT_RespMethod);
	AZ(HTTP_Decode(req->resp, 
	    ObjGetattr(req->objcore, &req->wrk->stats, OA_HEADERS, NULL)));
	http_ForceField(req->resp, HTTP_HDR_PROTO, "HTTP/1.1");

	if (req->wrk->stats.cache_hit)
		http_PrintfHeader(req->resp,
		    "X-Varnish: %u %u", VXID(req->vsl->wid),
		    ObjGetXID(req->objcore, &wrk->stats));
	else
		http_PrintfHeader(req->resp,
		    "X-Varnish: %u", VXID(req->vsl->wid));

	/* We base Age calculation upon the last timestamp taken during
	   client request processing. This gives some inaccuracy, but
	   since Age is only full second resolution that shouldn't
	   matter. (Last request timestamp could be a Start timestamp
	   taken before the object entered into cache leading to negative
	   age. Truncate to zero in that case).
	*/
	http_PrintfHeader(req->resp, "Age: %.0f",
	    fmax(0., req->t_prev - req->objcore->exp.t_origin));

	http_SetHeader(req->resp, "Via: 1.1 varnish-v4");

	if (cache_param->http_gzip_support &&
	    ObjCheckFlag(req->objcore, &req->wrk->stats, OF_GZIPED) &&
	    !RFC2616_Req_Gzip(req->http))
		RFC2616_Weaken_Etag(req->resp);

	VCL_deliver_method(req->vcl, wrk, req, NULL, req->http->ws);
	VSLb_ts_req(req, "Process", W_TIM_real(wrk));

	/* Stop the insanity before it turns "Hotel California" on us */
	if (req->restarts >= cache_param->max_restarts)
		wrk->handling = VCL_RET_DELIVER;

	if (wrk->handling != VCL_RET_DELIVER) {
		assert(req->objcore == req->objcore);
		(void)HSH_DerefObjCore(&wrk->stats, &req->objcore);
		req->obj = NULL;
		http_Teardown(req->resp);

		switch (wrk->handling) {
		case VCL_RET_RESTART:
			req->req_step = R_STP_RESTART;
			break;
		case VCL_RET_SYNTH:
			req->req_step = R_STP_SYNTH;
			break;
		default:
			INCOMPL();
		}

		return (REQ_FSM_MORE);
	}

	assert(wrk->handling == VCL_RET_DELIVER);

	if (!(req->objcore->flags & OC_F_PASS)
	    && req->esi_level == 0
	    && http_IsStatus(req->resp, 200)
	    && req->http->conds && RFC2616_Do_Cond(req)) {
		http_PutResponse(req->resp, "HTTP/1.1", 304, NULL);
		req->wantbody = 0;
	}

	/* Grab a ref to the bo if there is one, and hand it down */
	bo = HSH_RefBusy(req->objcore);
	V1D_Deliver(req, bo);
	if (bo != NULL)
		VBO_DerefBusyObj(req->wrk, &bo);

	VSLb_ts_req(req, "Resp", W_TIM_real(wrk));

	if (http_HdrIs(req->resp, H_Connection, "close"))
		req->doclose = SC_RESP_CLOSE;

	if (req->objcore->flags & OC_F_PASS) {
		/*
		 * No point in saving the body if it is hit-for-pass,
		 * but we can't yank it until the fetching thread has
		 * finished/abandoned also.
		 */
		while (req->objcore->busyobj != NULL)
			(void)usleep(100000);
		STV_Freestore(req->obj);
	}

	assert(WRW_IsReleased(wrk));
VSLb(req->vsl, SLT_Debug, "XXX REF %d", req->objcore->refcnt);
	assert(req->obj->objcore == req->objcore);
	(void)HSH_DerefObjCore(&wrk->stats, &req->objcore);
	req->obj = NULL;
	http_Teardown(req->resp);
	return (REQ_FSM_DONE);
}

/*--------------------------------------------------------------------
 * Emit a synthetic response
 *
DOT subgraph xcluster_synth {
DOT	synth [
DOT		shape=record
DOT		label="{cnt_synth:|{vcl_synth\{\}|resp.}|{<del>deliver?|<restart>restart?}}"
DOT	]
DOT	SYNTH -> synth
DOT	synth:del:s -> deliver [label=deliver]
DOT }
 */

static enum req_fsm_nxt
cnt_synth(struct worker *wrk, struct req *req)
{
	char date[40];
	struct http *h;
	double now;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	wrk->stats.s_synth++;


	now = W_TIM_real(wrk);
	VSLb_ts_req(req, "Process", now);

	if (req->err_code < 100 || req->err_code > 999)
		req->err_code = 501;

	HTTP_Setup(req->resp, req->ws, req->vsl, SLT_RespMethod);
	h = req->resp;
	VTIM_format(now, date);
	http_PrintfHeader(h, "Date: %s", date);
	http_SetHeader(h, "Server: Varnish");
	http_PrintfHeader(req->resp, "X-Varnish: %u", VXID(req->vsl->wid));
	http_PutResponse(h, "HTTP/1.1", req->err_code, req->err_reason);

	AZ(req->synth_body);
	req->synth_body = VSB_new_auto();
	AN(req->synth_body);

	VCL_synth_method(req->vcl, wrk, req, NULL, req->http->ws);

	http_Unset(h, H_Content_Length);

	AZ(VSB_finish(req->synth_body));

	if (wrk->handling == VCL_RET_RESTART) {
		HTTP_Setup(h, req->ws, req->vsl, SLT_RespMethod);
		VSB_delete(req->synth_body);
		req->synth_body = NULL;
		req->req_step = R_STP_RESTART;
		return (REQ_FSM_MORE);
	}
	assert(wrk->handling == VCL_RET_DELIVER);

	if (http_HdrIs(req->resp, H_Connection, "close"))
		req->doclose = SC_RESP_CLOSE;

	V1D_Deliver_Synth(req);

	VSLb_ts_req(req, "Resp", W_TIM_real(wrk));

	VSB_delete(req->synth_body);
	req->synth_body = NULL;

	req->err_code = 0;
	req->err_reason = NULL;
	return (REQ_FSM_DONE);
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

	wrk->stats.s_fetch++;
	(void)HTTP1_DiscardReqBody(req);

	if (req->objcore->flags & OC_F_FAILED) {
		req->err_code = 503;
		req->req_step = R_STP_SYNTH;
		(void)HSH_DerefObjCore(&wrk->stats, &req->objcore);
		AZ(req->objcore);
		return (REQ_FSM_MORE);
	}

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
DOT		label="{<top>cnt_lookup:|{vcl_hit\{\}|req.*, obj.*}|{<deliver>deliver?|synth?|restart?|<fetch>fetch?|<pass>pass?}}"
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
	enum lookup_e lr;
	int had_objhead = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);

	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);

	VRY_Prep(req);

	AZ(req->objcore);
	if (req->hash_objhead)
		had_objhead = 1;
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
	if (had_objhead)
		VSLb_ts_req(req, "Waitinglist", W_TIM_real(wrk));

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
		AZ(req->obj);
		AN(boc);
		AN(boc->flags & OC_F_BUSY);
		req->objcore = boc;
		req->req_step = R_STP_MISS;
		return (REQ_FSM_MORE);
	}

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->flags & OC_F_BUSY);
	req->objcore = oc;

	if (oc->flags & OC_F_PASS) {
		/* Found a hit-for-pass */
		VSLb(req->vsl, SLT_Debug, "XXXX HIT-FOR-PASS");
		VSLb(req->vsl, SLT_HitPass, "%u",
		    ObjGetXID(req->objcore, &wrk->stats));
		AZ(boc);
		AZ(req->obj);
		(void)HSH_DerefObjCore(&wrk->stats, &req->objcore);
		wrk->stats.cache_hitpass++;
		req->req_step = R_STP_PASS;
		return (REQ_FSM_MORE);
	}

	VSLb(req->vsl, SLT_Hit, "%u",
	    ObjGetXID(req->objcore, &wrk->stats));

	VCL_hit_method(req->vcl, wrk, req, NULL, req->http->ws);

	switch (wrk->handling) {
	case VCL_RET_DELIVER:
		if (boc != NULL) {
			AZ(oc->flags & (OC_F_FAILED|OC_F_PASS));
			AZ(oc->exp_flags & OC_EF_DYING);
			AZ(boc->busyobj);
			VBF_Fetch(wrk, req, boc, oc, VBF_BACKGROUND);
		} else {
			(void)HTTP1_DiscardReqBody(req);// XXX: handle err
		}
		wrk->stats.cache_hit++;
		req->req_step = R_STP_DELIVER;
		return (REQ_FSM_MORE);
	case VCL_RET_FETCH:
		AZ(req->obj);
		if (boc != NULL) {
			req->objcore = boc;
			req->ims_oc = oc;
			req->req_step = R_STP_MISS;
		} else {
			(void)HSH_DerefObjCore(&wrk->stats, &req->objcore);
			/*
			 * We don't have a busy object, so treat this
			 * like a pass
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
	case VCL_RET_SYNTH:
		req->req_step = R_STP_SYNTH;
		break;
	case VCL_RET_PASS:
		wrk->stats.cache_hit++;
		req->req_step = R_STP_PASS;
		break;
	default:
		INCOMPL();
	}

	/* Drop our object, we won't need it */
	AZ(req->obj);
	(void)HSH_DerefObjCore(&wrk->stats, &req->objcore);

	if (boc != NULL) {
		(void)HSH_DerefObjCore(&wrk->stats, &boc);
		VRY_Clear(req);
	}

	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * We had a miss, ask VCL, proceed as instructed
 *
DOT subgraph xcluster_miss {
DOT	miss [
DOT		shape=record
DOT		label="{cnt_miss:|{vcl_miss\{\}|req.*}|{<fetch>fetch?|<synth>synth?|<rst>restart?|<pass>pass?}}"
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

	VCL_miss_method(req->vcl, wrk, req, NULL, req->http->ws);
	switch (wrk->handling) {
	case VCL_RET_FETCH:
		wrk->stats.cache_miss++;
		VBF_Fetch(wrk, req, req->objcore, req->ims_oc, VBF_NORMAL);
		req->req_step = R_STP_FETCH;
		if (req->ims_oc != NULL)
			(void)HSH_DerefObjCore(&wrk->stats, &req->ims_oc);
		return (REQ_FSM_MORE);
	case VCL_RET_SYNTH:
		req->req_step = R_STP_SYNTH;
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
	VRY_Clear(req);
	if (req->ims_oc != NULL)
		(void)HSH_DerefObjCore(&wrk->stats, &req->ims_oc);
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
DOT		label="{cnt_pass:|{vcl_pass\{\}|req.*}|{<fetch>fetch?|<synth>synth?|<rst>restart?}}"
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
	case VCL_RET_SYNTH:
		req->req_step = R_STP_SYNTH;
		break;
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_FETCH:
		wrk->stats.s_pass++;
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
DOT		label="{cnt_pipe:|filter req.*-\>bereq.*|{vcl_pipe()|req.*, bereq\.*}|{<pipe>pipe?|<synth>synth?}}"
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

	wrk->stats.s_pipe++;
	bo = VBO_GetBusyObj(wrk, req);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	VSLb(bo->vsl, SLT_Begin, "bereq %u pipe", VXID(req->vsl->wid));
	VSLb(req->vsl, SLT_Link, "bereq %u pipe", VXID(bo->vsl->wid));
	THR_SetBusyobj(bo);

	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, SLT_BereqMethod);
	http_FilterReq(bo->bereq, req->http, 0);	// XXX: 0 ?
	http_PrintfHeader(bo->bereq, "X-Varnish: %u", VXID(req->vsl->wid));
	http_SetHeader(bo->bereq, "Connection: close");

	VCL_pipe_method(req->vcl, wrk, req, bo, req->http->ws);

	if (wrk->handling == VCL_RET_SYNTH)
		INCOMPL();
	assert(wrk->handling == VCL_RET_PIPE);

	PipeRequest(req, bo);
	assert(WRW_IsReleased(wrk));
	http_Teardown(bo->bereq);
	THR_SetBusyobj(NULL);
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
DOT err_restart [label="SYNTH",shape=plaintext]
 */

static enum req_fsm_nxt
cnt_restart(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	req->director_hint = NULL;
	if (++req->restarts >= cache_param->max_restarts) {
		VSLb(req->vsl, SLT_VCL_Error, "Too many restarts");
		req->err_code = 503;
		req->req_step = R_STP_SYNTH;
	} else {
		// XXX: ReqEnd + ReqAcct ?
		VSLb_ts_req(req, "Restart", W_TIM_real(wrk));
		VSL_ChgId(req->vsl, "req", "restart",
		    VXID_Get(wrk, VSL_CLIENTMARKER));
		VSLb_ts_req(req, "Start", req->t_prev);
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
DOT		label="{cnt_recv:|{vcl_recv\{\}|req.*}|{vcl_hash\{\}|req.*}|{<lookup>lookup?|<pass>pass?|<pipe>pipe?|<synth>synth?|<purge>purge?}}"
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
	char *xff;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->objcore);
	AZ(req->obj);

	AZ(isnan(req->t_first));
	AZ(isnan(req->t_prev));
	AZ(isnan(req->t_req));

	VSLb(req->vsl, SLT_ReqStart, "%s %s",
	    req->sp->client_addr_str, req->sp->client_port_str);

	http_VSL_log(req->http);

	if (req->err_code) {
		req->req_step = R_STP_SYNTH;
		return (REQ_FSM_MORE);
	}

	/* By default we use the first backend */
	AZ(req->director_hint);
	req->director_hint = req->vcl->director[0];
	AN(req->director_hint);

	req->d_ttl = -1;
	req->disable_esi = 0;
	req->hash_always_miss = 0;
	req->hash_ignore_busy = 0;
	req->client_identity = NULL;
	if (req->restarts == 0) {
		if (http_GetHdr(req->http, H_X_Forwarded_For, &xff)) {
			http_Unset(req->http, H_X_Forwarded_For);
			http_PrintfHeader(req->http, "X-Forwarded-For: %s, %s", xff,
					  req->sp->client_addr_str);
		} else {
			http_PrintfHeader(req->http, "X-Forwarded-For: %s",
					  req->sp->client_addr_str);
		}
	}

	http_CollectHdr(req->http, H_Cache_Control);

	VCL_recv_method(req->vcl, wrk, req, NULL, req->http->ws);

	/* Attempts to cache req.body may fail */
	if (req->req_body_status == REQ_BODY_FAIL) {
		return (REQ_FSM_DONE);
	}
	recv_handling = wrk->handling;

	/* We wash the A-E header here for the sake of VRY */
	if (cache_param->http_gzip_support &&
	     (recv_handling != VCL_RET_PIPE) &&
	     (recv_handling != VCL_RET_PASS)) {
		if (RFC2616_Req_Gzip(req->http)) {
			http_ForceHeader(req->http, H_Accept_Encoding, "gzip");
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
	case VCL_RET_SYNTH:
		req->req_step = R_STP_SYNTH;
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
 *
DOT subgraph xcluster_purge {
DOT	purge [
DOT		shape=record
DOT		label="{cnt_purge:|{vcl_purge\{\}|req.*}|{<synth>synth?}}"
DOT	]
DOT }
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

	HSH_Purge(wrk, boc->objhead, 0, 0, 0);

	AZ(HSH_DerefObjCore(&wrk->stats, &boc));

	VCL_purge_method(req->vcl, wrk, req, NULL, req->http->ws);
	switch (wrk->handling) {
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_SYNTH:
		req->req_step = R_STP_SYNTH;
		break;
	default:
		WRONG("Illegal return from vcl_purge{}");
	}
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
		AN(req->vsl->wid);
		if (req->res_mode & (RES_ESI|RES_ESI_CHILD))
			VSLb(req->vsl, SLT_ESI_BodyBytes, "%ju",
			    (uintmax_t)req->resp_bodybytes);

		while (!VTAILQ_EMPTY(&req->body->list)) {
			st = VTAILQ_FIRST(&req->body->list);
			VTAILQ_REMOVE(&req->body->list, st, list);
			STV_free(st);
		}
		req->wrk = NULL;
	}
	assert(WRW_IsReleased(wrk));
	return (nxt);
}

void
CNT_AcctLogCharge(struct dstat *ds, struct req *req)
{
	struct acct_req *a;

	AN(ds);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	a = &req->acct;

	if (!(req->res_mode & RES_PIPE)) {
		VSLb(req->vsl, SLT_ReqAcct, "%ju %ju %ju %ju %ju %ju",
		    (uintmax_t)a->req_hdrbytes,
		    (uintmax_t)a->req_bodybytes,
		    (uintmax_t)(a->req_hdrbytes + a->req_bodybytes),
		    (uintmax_t)a->resp_hdrbytes,
		    (uintmax_t)a->resp_bodybytes,
		    (uintmax_t)(a->resp_hdrbytes + a->resp_bodybytes));
	}

#define ACCT(foo)			\
	ds->s_##foo += a->foo;		\
	a->foo = 0;
#include "tbl/acct_fields_req.h"
#undef ACCT
}

/*
DOT }
*/
