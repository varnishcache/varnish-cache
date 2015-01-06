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
 * Deliver an object to client
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

	assert(req->objcore->refcnt > 0);

	if (req->objcore->exp_flags & OC_EF_EXP)
		EXP_Touch(req->objcore, req->t_prev);

	HTTP_Setup(req->resp, req->ws, req->vsl, SLT_RespMethod);
	AZ(HTTP_Decode(req->resp,
	    ObjGetattr(req->wrk, req->objcore, OA_HEADERS, NULL)));
	http_ForceField(req->resp, HTTP_HDR_PROTO, "HTTP/1.1");

	if (req->is_hit)
		http_PrintfHeader(req->resp,
		    "X-Varnish: %u %u", VXID(req->vsl->wid),
		    ObjGetXID(wrk, req->objcore));
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
	    ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED) &&
	    !RFC2616_Req_Gzip(req->http))
		RFC2616_Weaken_Etag(req->resp);

	VCL_deliver_method(req->vcl, wrk, req, NULL, req->http->ws);
	VSLb_ts_req(req, "Process", W_TIM_real(wrk));

	/* Stop the insanity before it turns "Hotel California" on us */
	if (req->restarts >= cache_param->max_restarts)
		wrk->handling = VCL_RET_DELIVER;

	if (wrk->handling != VCL_RET_DELIVER) {
		(void)HSH_DerefObjCore(wrk, &req->objcore);
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
	if (bo != NULL) {
		if (req->esi_level == 0 && bo->state == BOS_FINISHED) {
			VBO_DerefBusyObj(wrk, &bo);
		} else if (!bo->do_stream) {
			VBO_waitstate(bo, BOS_FINISHED);
			VBO_DerefBusyObj(wrk, &bo);
		}
	}
	V1D_Deliver(req, bo);
	if (bo != NULL)
		VBO_DerefBusyObj(wrk, &bo);

	VSLb_ts_req(req, "Resp", W_TIM_real(wrk));

	if (http_HdrIs(req->resp, H_Connection, "close"))
		req->doclose = SC_RESP_CLOSE;

	if ((req->objcore->flags & OC_F_PASS) && bo != NULL) {
		VBO_waitstate(bo, BOS_FINISHED);
		ObjSlim(wrk, req->objcore);
	}

	(void)HSH_DerefObjCore(wrk, &req->objcore);
	http_Teardown(req->resp);
	return (REQ_FSM_DONE);
}

/*--------------------------------------------------------------------
 * Emit a synthetic response
 */

static enum req_fsm_nxt
cnt_synth(struct worker *wrk, struct req *req)
{
	char date[40];
	struct http *h;
	double now;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	wrk->stats->s_synth++;

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

	/* Discard any lingering request body before delivery */
	(void)VRB_Ignore(req);

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

	req->objcore = HSH_Private(wrk);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	if (STV_NewObject(req->objcore, wrk, TRANSIENT_STORAGE, 1024)) {
		ssize_t sz, szl;
		uint8_t *ptr;

		szl = VSB_len(req->synth_body);
		assert(szl >= 0);
		if (szl > 0) {
			sz = szl;
			AN(ObjGetSpace(wrk, req->objcore, &sz, &ptr));
			assert(sz >= szl);
			memcpy(ptr, VSB_data(req->synth_body), szl);
			ObjExtend(wrk, req->objcore, szl);
		}
		VSB_delete(req->synth_body);
		req->synth_body = NULL;

		V1D_Deliver(req, NULL);
		(void)HSH_DerefObjCore(wrk, &req->objcore);
	}

	VSLb_ts_req(req, "Resp", W_TIM_real(wrk));

	req->err_code = 0;
	req->err_reason = NULL;
	return (REQ_FSM_DONE);
}

/*--------------------------------------------------------------------
 * Initiated a fetch (pass/miss) which we intend to deliver
 */

static enum req_fsm_nxt
cnt_fetch(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	wrk->stats->s_fetch++;
	(void)VRB_Ignore(req);

	if (req->objcore->flags & OC_F_FAILED) {
		req->err_code = 503;
		req->req_step = R_STP_SYNTH;
		(void)HSH_DerefObjCore(wrk, &req->objcore);
		AZ(req->objcore);
		return (REQ_FSM_MORE);
	}

	req->req_step = R_STP_DELIVER;
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Attempt to lookup objhdr from hash.  We disembark and reenter
 * this state if we get suspended on a busy objhdr.
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
		    ObjGetXID(wrk, req->objcore));
		AZ(boc);
		(void)HSH_DerefObjCore(wrk, &req->objcore);
		wrk->stats->cache_hitpass++;
		req->req_step = R_STP_PASS;
		return (REQ_FSM_MORE);
	}

	VSLb(req->vsl, SLT_Hit, "%u", ObjGetXID(wrk, req->objcore));

	VCL_hit_method(req->vcl, wrk, req, NULL, req->http->ws);

	switch (wrk->handling) {
	case VCL_RET_DELIVER:
		if (boc != NULL) {
			AZ(oc->flags & OC_F_PASS);
			AZ(oc->exp_flags & OC_EF_DYING);
			AZ(boc->busyobj);
			VBF_Fetch(wrk, req, boc, oc, VBF_BACKGROUND);
		} else {
			(void)VRB_Ignore(req);// XXX: handle err
		}
		wrk->stats->cache_hit++;
		req->is_hit = 1;
		req->req_step = R_STP_DELIVER;
		return (REQ_FSM_MORE);
	case VCL_RET_FETCH:
		if (boc != NULL) {
			req->objcore = boc;
			req->ims_oc = oc;
			req->req_step = R_STP_MISS;
		} else {
			(void)HSH_DerefObjCore(wrk, &req->objcore);
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
		wrk->stats->cache_hit++;
		req->is_hit = 1;
		req->req_step = R_STP_PASS;
		break;
	default:
		INCOMPL();
	}

	/* Drop our object, we won't need it */
	(void)HSH_DerefObjCore(wrk, &req->objcore);

	if (boc != NULL) {
		(void)HSH_DerefObjCore(wrk, &boc);
		VRY_Clear(req);
	}

	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Cache miss.
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
		wrk->stats->cache_miss++;
		VBF_Fetch(wrk, req, req->objcore, req->ims_oc, VBF_NORMAL);
		req->req_step = R_STP_FETCH;
		if (req->ims_oc != NULL)
			(void)HSH_DerefObjCore(wrk, &req->ims_oc);
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
		(void)HSH_DerefObjCore(wrk, &req->ims_oc);
	AZ(HSH_DerefObjCore(wrk, &req->objcore));
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Pass processing
 */

static enum req_fsm_nxt
cnt_pass(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->objcore);

	VCL_pass_method(req->vcl, wrk, req, NULL, req->http->ws);
	switch (wrk->handling) {
	case VCL_RET_SYNTH:
		req->req_step = R_STP_SYNTH;
		break;
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_FETCH:
		wrk->stats->s_pass++;
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
 * Pipe mode
 */

static enum req_fsm_nxt
cnt_pipe(struct worker *wrk, struct req *req)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);

	wrk->stats->s_pipe++;
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

	V1P_Process(req, bo);
	http_Teardown(bo->bereq);
	VBO_DerefBusyObj(wrk, &bo);
	THR_SetBusyobj(NULL);
	return (REQ_FSM_DONE);
}

/*--------------------------------------------------------------------
 * Handle restart events
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
	req->is_hit = 0;
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * We have a complete request, set everything up and start it.
 * We can come here both with a request from the client and with
 * a interior request during ESI delivery.
 */

static enum req_fsm_nxt
cnt_recv(struct worker *wrk, struct req *req)
{
	unsigned recv_handling;
	struct SHA256Context sha256ctx;
	const char *xff;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcl, VCL_CONF_MAGIC);
	AZ(req->objcore);
	AZ(req->err_code);

	AZ(isnan(req->t_first));
	AZ(isnan(req->t_prev));
	AZ(isnan(req->t_req));

	VSLb(req->vsl, SLT_ReqStart, "%s %s",
	    req->sp->client_addr_str, req->sp->client_port_str);

	http_VSL_log(req->http);

	if (req->restarts == 0) {
		/*
		 * This really should be done earlier, but we want to capture
		 * it in the VSL log.
		 */
		if (http_GetHdr(req->http, H_X_Forwarded_For, &xff)) {
			http_Unset(req->http, H_X_Forwarded_For);
			http_PrintfHeader(req->http, "X-Forwarded-For: %s, %s",
			    xff, req->sp->client_addr_str);
		} else {
			http_PrintfHeader(req->http, "X-Forwarded-For: %s",
			    req->sp->client_addr_str);
		}
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

	http_CollectHdr(req->http, H_Cache_Control);

	VCL_recv_method(req->vcl, wrk, req, NULL, req->http->ws);

	/* Attempts to cache req.body may fail */
	if (req->req_body_status == REQ_BODY_FAIL)
		return (REQ_FSM_DONE);

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
 * Find the objhead, purge it.
 *
 * XXX: We should ask VCL if we should fetch a new copy of the object.
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

	AZ(HSH_DerefObjCore(wrk, &boc));

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

	VSLb(req->vsl,  SLT_Debug, "vxid %u STP_%s sp %p vcl %p",
	    req->vsl->wid, state, req->sp, req->vcl);
	VSL_Flush(req->vsl, 0);
}

enum req_fsm_nxt
CNT_Request(struct worker *wrk, struct req *req)
{
	enum req_fsm_nxt nxt;

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
	wrk->vsl = req->vsl;

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
	wrk->vsl = NULL;
	if (nxt == REQ_FSM_DONE) {
		AN(req->vsl->wid);
		VRB_Free(req);
		req->wrk = NULL;
	}
	return (nxt);
}

void
CNT_AcctLogCharge(struct dstat *ds, struct req *req)
{
	struct acct_req *a;

	AN(ds);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	a = &req->acct;

	if (req->vsl->wid && !(req->res_mode & RES_PIPE)) {
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
