/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2017 Varnish Software AS
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
 * This file contains the request-handling state engine, which is intended to
 * (over time) be(come) protocol agnostic.
 * We already use this now with ESI:includes, which are for all relevant
 * purposes a different "protocol"
 *
 * A special complication is the fact that we can suspend processing of
 * a request when hash-lookup finds a busy objhdr.
 *
 */

#include "config.h"

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "cache_transport.h"
#include "vcc_interface.h"

#include "http1/cache_http1.h"
#include "storage/storage.h"
#include "vcl.h"
#include "vct.h"
#include "vsha256.h"
#include "vtim.h"

#define REQ_STEPS \
  REQ_STEP(transport,           TRANSPORT,	) \
  REQ_STEP(restart,             RESTART,	static) \
  REQ_STEP(recv,                RECV,		) \
  REQ_STEP(pipe,                PIPE,		static) \
  REQ_STEP(pass,                PASS,		static) \
  REQ_STEP(lookup,              LOOKUP,		static) \
  REQ_STEP(purge,               PURGE,		static) \
  REQ_STEP(miss,                MISS,		static) \
  REQ_STEP(fetch,               FETCH,		static) \
  REQ_STEP(deliver,             DELIVER,	static) \
  REQ_STEP(vclfail,             VCLFAIL,	static) \
  REQ_STEP(synth,               SYNTH,		static) \
  REQ_STEP(transmit,            TRANSMIT,	static) \
  REQ_STEP(finish,              FINISH,		static)

#define REQ_STEP(l, U, priv) \
    static req_state_f cnt_##l; \
    priv const struct req_step R_STP_##U[1] = {{ \
	.name = "Req Step " #l, \
	.func = cnt_##l, \
    }};
REQ_STEPS
#undef REQ_STEP

/*--------------------------------------------------------------------
 * Handle "Expect:" and "Connection:" on incoming request
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_transport(struct worker *wrk, struct req *req)
{
	const char *p;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->http, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(req->transport, TRANSPORT_MAGIC);
	AN(req->req_body_status);

	if (http_GetHdr(req->http, H_Expect, &p)) {
		if (!http_expect_eq(p, 100-continue)) {
			req->doclose = SC_RX_JUNK;
			(void)req->transport->minimal_response(req, 417);
			wrk->stats->client_req_417++;
			return (REQ_FSM_DONE);
		}
		if (req->http->protover >= 11 &&
		    req->htc->pipeline_b == NULL)	// XXX: HTTP1 vs 2 ?
			req->want100cont = 1;
		http_Unset(req->http, H_Expect);
	}

	AZ(req->err_code);

	req->doclose = http_DoConnection(req->http, SC_REQ_CLOSE);
	if (req->doclose == SC_RX_BAD) {
		wrk->stats->client_req_400++;
		(void)req->transport->minimal_response(req, 400);
		return (REQ_FSM_DONE);
	}

	if (req->req_body_status->avail == 1) {
		AN(req->transport->req_body != NULL);
		VFP_Setup(req->vfc, wrk);
		req->vfc->resp = req->http;		// XXX
		req->transport->req_body(req);
	}

	req->ws_req = WS_Snapshot(req->ws);
	HTTP_Clone(req->http0, req->http);	// For ESI & restart
	req->req_step = R_STP_RECV;
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Deliver an object to client
 */

int
Resp_Setup_Deliver(struct req *req)
{
	struct http *h;
	struct objcore *oc;
	const void *hdrs;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	oc = req->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	h = req->resp;

	HTTP_Setup(h, req->ws, req->vsl, SLT_RespMethod);

	hdrs = ObjGetAttr(req->wrk, oc, OA_HEADERS, NULL);
	if (hdrs == NULL || HTTP_Decode(h, hdrs))
		return (-1);

	http_ForceField(h, HTTP_HDR_PROTO, "HTTP/1.1");

	if (req->is_hit)
		http_PrintfHeader(h, "X-Varnish: %ju %ju", VXID(req->vsl->wid),
		    VXID(ObjGetXID(req->wrk, oc)));
	else
		http_PrintfHeader(h, "X-Varnish: %ju", VXID(req->vsl->wid));

	/*
	 * We base Age calculation upon the last timestamp taken during client
	 * request processing. This gives some inaccuracy, but since Age is only
	 * full second resolution that shouldn't matter. (Last request timestamp
	 * could be a Start timestamp taken before the object entered into cache
	 * leading to negative age. Truncate to zero in that case).
	 */
	http_PrintfHeader(h, "Age: %.0f",
	    floor(fmax(0., req->t_prev - oc->t_origin)));

	http_AppendHeader(h, H_Via, http_ViaHeader());

	if (cache_param->http_gzip_support &&
	    ObjCheckFlag(req->wrk, oc, OF_GZIPED) &&
	    !RFC2616_Req_Gzip(req->http))
		RFC2616_Weaken_Etag(h);
	return (0);
}

void
Resp_Setup_Synth(struct req *req)
{
	struct http *h;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	h = req->resp;

	HTTP_Setup(h, req->ws, req->vsl, SLT_RespMethod);

	AZ(req->objcore);
	http_PutResponse(h, "HTTP/1.1", req->err_code, req->err_reason);

	http_TimeHeader(h, "Date: ", W_TIM_real(req->wrk));
	http_SetHeader(h, "Server: Varnish");
	http_PrintfHeader(h, "X-Varnish: %ju", VXID(req->vsl->wid));

	/*
	 * For late 100-continue, we suggest to VCL to close the connection to
	 * neither send a 100-continue nor drain-read the request. But VCL has
	 * the option to veto by removing Connection: close
	 */
	if (req->want100cont)
		http_SetHeader(h, "Connection: close");
}

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_deliver(struct worker *wrk, struct req *req)
{
	unsigned status;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore->objhead, OBJHEAD_MAGIC);
	AZ(req->stale_oc);
	AN(req->vcl);

	assert(req->objcore->refcnt > 0);

	ObjTouch(req->wrk, req->objcore, req->t_prev);

	if (Resp_Setup_Deliver(req)) {
		(void)HSH_DerefObjCore(wrk, &req->objcore);
		req->err_code = 500;
		req->req_step = R_STP_SYNTH;
		return (REQ_FSM_MORE);
	}

	status = http_GetStatus(req->resp);
	if (cache_param->http_range_support && status == 200 &&
	    !(req->objcore->flags & OC_F_PRIVATE))
		http_ForceHeader(req->resp, H_Accept_Ranges, "bytes");

	req->t_resp = W_TIM_real(wrk);
	VCL_deliver_method(req->vcl, wrk, req, NULL, NULL);

	assert(req->restarts <= cache_param->max_restarts);

	if (wrk->vpi->handling != VCL_RET_DELIVER) {
		HSH_Cancel(wrk, req->objcore, NULL);
		(void)HSH_DerefObjCore(wrk, &req->objcore);
		http_Teardown(req->resp);

		switch (wrk->vpi->handling) {
		case VCL_RET_RESTART:
			req->req_step = R_STP_RESTART;
			break;
		case VCL_RET_FAIL:
			req->req_step = R_STP_VCLFAIL;
			break;
		case VCL_RET_SYNTH:
			req->req_step = R_STP_SYNTH;
			break;
		default:
			WRONG("Illegal return from vcl_deliver{}");
		}

		return (REQ_FSM_MORE);
	}

	VSLb_ts_req(req, "Process", W_TIM_real(wrk));

	assert(wrk->vpi->handling == VCL_RET_DELIVER);

	if (IS_TOPREQ(req) && RFC2616_Do_Cond(req))
		http_PutResponse(req->resp, "HTTP/1.1", 304, NULL);

	req->req_step = R_STP_TRANSMIT;
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * VCL failed, die horribly
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_vclfail(struct worker *wrk, struct req *req)
{
	struct vrt_ctx ctx[1];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	AZ(req->objcore);
	AZ(req->stale_oc);

	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Req2Ctx(ctx, req);

	Req_Rollback(ctx);

	if (req->req_reset) {
		req->err_code = 408;
		req->err_reason = "Client disconnected";
	} else {
		req->err_code = 503;
		req->err_reason = "VCL failed";
	}
	req->req_step = R_STP_SYNTH;
	req->doclose = SC_VCL_FAILURE;
	req->vdp_filter_list = NULL;
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Emit a synthetic response
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_synth(struct worker *wrk, struct req *req)
{
	struct vsb *synth_body;
	ssize_t sz, szl;
	uint16_t status;
	uint8_t *ptr;
	const char *body;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	AZ(req->objcore);
	AZ(req->stale_oc);

	wrk->stats->s_synth++;

	if (req->err_code < 100)
		req->err_code = 501;

	Resp_Setup_Synth(req);

	req->vdp_filter_list = NULL;
	synth_body = VSB_new_auto();
	AN(synth_body);

	req->t_resp = W_TIM_real(wrk);
	VCL_synth_method(req->vcl, wrk, req, NULL, synth_body);

	AZ(VSB_finish(synth_body));

	VSLb_ts_req(req, "Process", W_TIM_real(wrk));

	while (wrk->vpi->handling == VCL_RET_FAIL) {
		if (req->esi_level > 0) {
			wrk->vpi->handling = VCL_RET_DELIVER;
			break;
		}
		VSB_destroy(&synth_body);
		(void)VRB_Ignore(req);
		status = req->req_reset ? 408 : 500;
		(void)req->transport->minimal_response(req, status);
		req->doclose = SC_VCL_FAILURE; // XXX: Not necessary any more ?
		VSLb_ts_req(req, "Resp", W_TIM_real(wrk));
		http_Teardown(req->resp);
		return (REQ_FSM_DONE);
	}

	if (wrk->vpi->handling == VCL_RET_RESTART &&
	    req->restarts > cache_param->max_restarts)
		wrk->vpi->handling = VCL_RET_DELIVER;

	if (wrk->vpi->handling == VCL_RET_RESTART) {
		/*
		 * XXX: Should we reset req->doclose = SC_VCL_FAILURE
		 * XXX: If so, to what ?
		 */
		HTTP_Setup(req->resp, req->ws, req->vsl, SLT_RespMethod);
		VSB_destroy(&synth_body);
		req->req_step = R_STP_RESTART;
		return (REQ_FSM_MORE);
	}
	assert(wrk->vpi->handling == VCL_RET_DELIVER);

	http_Unset(req->resp, H_Content_Length);
	http_PrintfHeader(req->resp, "Content-Length: %zd",
	    VSB_len(synth_body));

	if (req->doclose == SC_NULL &&
	    http_HdrIs(req->resp, H_Connection, "close"))
		req->doclose = SC_RESP_CLOSE;

	/* Discard any lingering request body before delivery */
	(void)VRB_Ignore(req);

	req->objcore = HSH_Private(wrk);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	szl = -1;
	if (STV_NewObject(wrk, req->objcore, stv_transient, 0)) {
		body = VSB_data(synth_body);
		szl = VSB_len(synth_body);
		assert(szl >= 0);
		while (szl > 0) {
			sz = szl;
			if (! ObjGetSpace(wrk, req->objcore, &sz, &ptr)) {
				szl = -1;
				break;
			}
			if (sz > szl)
				sz = szl;
			szl -= sz;
			memcpy(ptr, body, sz);
			ObjExtend(wrk, req->objcore, sz, szl == 0 ? 1 : 0);
			body += sz;
		}
	}

	if (szl >= 0)
		AZ(ObjSetU64(wrk, req->objcore, OA_LEN, VSB_len(synth_body)));
	HSH_DerefBoc(wrk, req->objcore);
	VSB_destroy(&synth_body);

	if (szl < 0) {
		VSLb(req->vsl, SLT_Error, "Could not get storage");
		req->doclose = SC_OVERLOAD;
		VSLb_ts_req(req, "Resp", W_TIM_real(wrk));
		(void)HSH_DerefObjCore(wrk, &req->objcore);
		http_Teardown(req->resp);
		return (REQ_FSM_DONE);
	}

	req->req_step = R_STP_TRANSMIT;
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * The mechanics of sending a response (from deliver or synth)
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_transmit(struct worker *wrk, struct req *req)
{
	enum req_fsm_nxt nxt = REQ_FSM_MORE;
	enum vtr_deliver_e dnxt;
	uint16_t status;
	int sendbody, head;
	intmax_t clval;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->transport, TRANSPORT_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	AZ(req->stale_oc);
	AZ(req->res_mode);
	AZ(req->boc);
	req->req_step = R_STP_FINISH;

	/* Grab a ref to the bo if there is one (=streaming) */
	req->boc = HSH_RefBoc(req->objcore);
	if (req->boc && req->boc->state < BOS_STREAM)
		ObjWaitState(req->objcore, BOS_STREAM);
	clval = http_GetContentLength(req->resp);
	/* RFC 7230, 3.3.3 */
	status = http_GetStatus(req->resp);
	head = http_method_eq(req->http0->hd[HTTP_HDR_METHOD].b, HEAD);

	if (req->boc != NULL || (req->objcore->flags & (OC_F_FAILED)))
		req->resp_len = clval;
	else
		req->resp_len = ObjGetLen(req->wrk, req->objcore);

	if (head || status < 200 || status == 204 || status == 304) {
		// rfc7230,l,1748,1752
		sendbody = 0;
	} else {
		sendbody = 1;
	}

	VDP_Init(req->vdc, req->wrk, req->vsl, req, NULL, &req->resp_len);
	if (req->vdp_filter_list == NULL)
		req->vdp_filter_list = resp_Get_Filter_List(req);
	if (req->vdp_filter_list == NULL ||
	    VCL_StackVDP(req->vdc, req->vcl, req->vdp_filter_list, req, NULL)) {
		VSLb(req->vsl, SLT_Error, "Failure to push processors");
		req->doclose = SC_OVERLOAD;
		req->acct.resp_bodybytes +=
			VDP_Close(req->vdc, req->objcore, req->boc);
	} else {
		if (status < 200 || status == 204) {
			// rfc7230,l,1691,1695
			http_Unset(req->resp, H_Content_Length);
		} else if (status == 304) {
			// rfc7230,l,1675,1677
			http_Unset(req->resp, H_Content_Length);
		} else if (clval >= 0 && clval == req->resp_len) {
			/* Reuse C-L header */
		} else if (head && req->objcore->flags & OC_F_HFM) {
			/*
			 * Don't touch C-L header (debatable)
			 *
			 * The only way to do it correctly would be to GET
			 * to the backend, and discard the body once the
			 * filters have had a chance to chew on it, but that
			 * would negate the "pass for huge objects" use case.
			 */
		} else {
			http_Unset(req->resp, H_Content_Length);
			if (req->resp_len >= 0)
				http_PrintfHeader(req->resp,
				    "Content-Length: %jd", req->resp_len);
		}
		if (req->resp_len == 0)
			sendbody = 0;
		dnxt = req->transport->deliver(req, sendbody);
		if (dnxt == VTR_D_DISEMBARK)
			nxt = REQ_FSM_DISEMBARK;
		else
			assert(dnxt == VTR_D_DONE);
	}
	return (nxt);
}

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_finish(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	VSLb_ts_req(req, "Resp", W_TIM_real(wrk));

	if (req->doclose == SC_NULL && (req->objcore->flags & OC_F_FAILED)) {
		/* The object we delivered failed due to a streaming error.
		 * Fail the request. */
		req->doclose = SC_TX_ERROR;
	}

	if (req->boc != NULL) {
		HSH_DerefBoc(wrk, req->objcore);
		req->boc = NULL;
	}

	(void)HSH_DerefObjCore(wrk, &req->objcore);
	http_Teardown(req->resp);

	req->vdp_filter_list = NULL;
	req->res_mode = 0;
	return (REQ_FSM_DONE);
}

/*--------------------------------------------------------------------
 * Initiated a fetch (pass/miss) which we intend to deliver
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_fetch(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	AZ(req->stale_oc);

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

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_lookup(struct worker *wrk, struct req *req)
{
	struct objcore *oc, *busy;
	enum lookup_e lr;
	int had_objcore = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->stale_oc);

	AN(req->vcl);

	VRY_Prep(req);

	if (req->waitinglist_gen) {
		CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
		req->waitinglist_gen = 0;
		had_objcore = 1;
	} else
		AZ(req->objcore);

	wrk->strangelove = 0;
	lr = HSH_Lookup(req, &oc, &busy);
	if (lr == HSH_BUSY) {
		/*
		 * We lost the session to a busy object, disembark the
		 * worker thread.   We return to STP_LOOKUP when the busy
		 * object has been unbusied, and still have the objhead
		 * around to restart the lookup with.
		 */
		return (REQ_FSM_DISEMBARK);
	}
	assert(wrk->strangelove >= 0);
	if ((unsigned)wrk->strangelove >= cache_param->vary_notice)
		VSLb(req->vsl, SLT_Notice, "vsl: High number of variants (%d)",
		    wrk->strangelove);
	if (had_objcore)
		VSLb_ts_req(req, "Waitinglist", W_TIM_real(wrk));

	if (req->vcf != NULL) {
		(void)req->vcf->func(req, NULL, NULL, 2);
		req->vcf = NULL;
	}

	if (busy == NULL) {
		VRY_Finish(req, DISCARD);
	} else {
		AN(busy->flags & OC_F_BUSY);
		VRY_Finish(req, KEEP);
	}

	AZ(req->objcore);
	if (lr == HSH_MISS || lr == HSH_HITMISS) {
		AN(busy);
		AN(busy->flags & OC_F_BUSY);
		req->objcore = busy;
		req->stale_oc = oc;
		req->req_step = R_STP_MISS;
		if (lr == HSH_HITMISS)
			req->is_hitmiss = 1;
		return (REQ_FSM_MORE);
	}
	if (lr == HSH_HITPASS) {
		AZ(busy);
		AZ(oc);
		req->req_step = R_STP_PASS;
		req->is_hitpass = 1;
		return (REQ_FSM_MORE);
	}

	assert(lr == HSH_HIT || lr == HSH_GRACE);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->flags & OC_F_BUSY);
	req->objcore = oc;
	AZ(oc->flags & OC_F_HFM);

	VCL_hit_method(req->vcl, wrk, req, NULL, NULL);

	switch (wrk->vpi->handling) {
	case VCL_RET_DELIVER:
		if (busy != NULL) {
			AZ(oc->flags & OC_F_HFM);
			CHECK_OBJ_NOTNULL(busy->boc, BOC_MAGIC);
			// XXX: shouldn't we go to miss?
			VBF_Fetch(wrk, req, busy, oc, VBF_BACKGROUND);
			wrk->stats->s_fetch++;
			wrk->stats->s_bgfetch++;
		} else {
			(void)VRB_Ignore(req);// XXX: handle err
		}
		wrk->stats->cache_hit++;
		req->is_hit = 1;
		if (lr == HSH_GRACE)
			wrk->stats->cache_hit_grace++;
		req->req_step = R_STP_DELIVER;
		return (REQ_FSM_MORE);
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_FAIL:
		req->req_step = R_STP_VCLFAIL;
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
		WRONG("Illegal return from vcl_hit{}");
	}

	/* Drop our object, we won't need it */
	(void)HSH_DerefObjCore(wrk, &req->objcore);

	if (busy != NULL) {
		HSH_Withdraw(wrk, &busy);
		VRY_Clear(req);
	}

	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Cache miss.
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_miss(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(req->vcl);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	CHECK_OBJ_ORNULL(req->stale_oc, OBJCORE_MAGIC);

	VCL_miss_method(req->vcl, wrk, req, NULL, NULL);
	switch (wrk->vpi->handling) {
	case VCL_RET_FETCH:
		wrk->stats->cache_miss++;
		VBF_Fetch(wrk, req, req->objcore, req->stale_oc, VBF_NORMAL);
		if (req->stale_oc != NULL)
			(void)HSH_DerefObjCore(wrk, &req->stale_oc);
		req->req_step = R_STP_FETCH;
		return (REQ_FSM_MORE);
	case VCL_RET_FAIL:
		req->req_step = R_STP_VCLFAIL;
		break;
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
	if (req->stale_oc != NULL)
		(void)HSH_DerefObjCore(wrk, &req->stale_oc);
	HSH_Withdraw(wrk, &req->objcore);
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Pass processing
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_pass(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(req->vcl);
	AZ(req->objcore);
	AZ(req->stale_oc);

	VCL_pass_method(req->vcl, wrk, req, NULL, NULL);
	switch (wrk->vpi->handling) {
	case VCL_RET_FAIL:
		req->req_step = R_STP_VCLFAIL;
		break;
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

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_pipe(struct worker *wrk, struct req *req)
{
	struct busyobj *bo;
	enum req_fsm_nxt nxt;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);
	AZ(req->stale_oc);
	AN(req->vcl);

	wrk->stats->s_pipe++;
	bo = VBO_GetBusyObj(wrk, req);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	VSLb(bo->vsl, SLT_Begin, "bereq %ju pipe", VXID(req->vsl->wid));
	VSLb(req->vsl, SLT_Link, "bereq %ju pipe", VXID(bo->vsl->wid));
	VSLb_ts_busyobj(bo, "Start", W_TIM_real(wrk));
	THR_SetBusyobj(bo);
	bo->sp = req->sp;
	SES_Ref(bo->sp);

	HTTP_Setup(bo->bereq, req->ws, bo->vsl, SLT_BereqMethod);
	http_FilterReq(bo->bereq, req->http, 0);	// XXX: 0 ?
	http_PrintfHeader(bo->bereq, "X-Varnish: %ju", VXID(req->vsl->wid));
	http_ForceHeader(bo->bereq, H_Connection, "close");

	if (req->want100cont) {
		http_SetHeader(bo->bereq, "Expect: 100-continue");
		req->want100cont = 0;
	}

	bo->wrk = wrk;
	bo->task_deadline = NAN; /* XXX: copy req->task_deadline */
	if (WS_Overflowed(req->ws))
		wrk->vpi->handling = VCL_RET_FAIL;
	else
		VCL_pipe_method(req->vcl, wrk, req, bo, NULL);

	switch (wrk->vpi->handling) {
	case VCL_RET_SYNTH:
		req->req_step = R_STP_SYNTH;
		nxt = REQ_FSM_MORE;
		break;
	case VCL_RET_PIPE:
		VSLb_ts_req(req, "Process", W_TIM_real(wrk));
		VSLb_ts_busyobj(bo, "Process", wrk->lastused);
		if (V1P_Enter() == 0) {
			AZ(bo->req);
			bo->req = req;
			bo->wrk = wrk;
			/* Unless cached, reqbody is not our job */
			if (req->req_body_status != BS_CACHED)
				req->req_body_status = BS_NONE;
			SES_Close(req->sp, VDI_Http1Pipe(req, bo));
			nxt = REQ_FSM_DONE;
			V1P_Leave();
			break;
		}
		wrk->stats->pipe_limited++;
		/* fall through */
	case VCL_RET_FAIL:
		req->req_step = R_STP_VCLFAIL;
		nxt = REQ_FSM_MORE;
		break;
	default:
		WRONG("Illegal return from vcl_pipe{}");
	}
	http_Teardown(bo->bereq);
	SES_Rel(bo->sp);
	VBO_ReleaseBusyObj(wrk, &bo);
	THR_SetBusyobj(NULL);
	return (nxt);
}

/*--------------------------------------------------------------------
 * Handle restart events
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_restart(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);
	AZ(req->stale_oc);

	if (++req->restarts > cache_param->max_restarts) {
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

/*
 * prepare the request for vcl_recv, either initially or after a reset
 * e.g. due to vcl switching
 *
 * TODO
 * - make restarts == 0 bit re-usable for rollback
 * - remove duplication with Req_Cleanup()
 */

static void v_matchproto_(req_state_f)
cnt_recv_prep(struct req *req, const char *ci)
{

	if (req->restarts == 0) {
		/*
		 * This really should be done earlier, but we want to capture
		 * it in the VSL log.
		 */
		http_AppendHeader(req->http, H_X_Forwarded_For, ci);
		http_AppendHeader(req->http, H_Via, http_ViaHeader());
		http_CollectHdr(req->http, H_Cache_Control);

		/* By default we use the first backend */
		VRT_Assign_Backend(&req->director_hint,
		    VCL_DefaultDirector(req->vcl));

		req->d_ttl = -1;
		req->d_grace = -1;
		req->disable_esi = 0;
		req->hash_always_miss = 0;
		req->hash_ignore_busy = 0;
		req->hash_ignore_vary = 0;
		req->client_identity = NULL;
		req->storage = NULL;
		req->trace = FEATURE(FEATURE_TRACE);
	}

	req->is_hit = 0;
	req->is_hitmiss = 0;
	req->is_hitpass = 0;
	req->err_code = 0;
	req->err_reason = NULL;

	req->vfp_filter_list = NULL;
}

/*--------------------------------------------------------------------
 * We have a complete request, set everything up and start it.
 * We can come here both with a request from the client and with
 * a interior request during ESI delivery.
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_recv(struct worker *wrk, struct req *req)
{
	unsigned recv_handling;
	struct VSHA256Context sha256ctx;
	const char *ci;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(req->vcl);
	AZ(req->objcore);
	AZ(req->stale_oc);
	AZ(req->err_code);

	AZ(isnan(req->t_first));
	AZ(isnan(req->t_prev));
	AZ(isnan(req->t_req));

	ci = Req_LogStart(wrk, req);
	http_VSL_log(req->http);

	if (http_CountHdr(req->http0, H_Host) > 1) {
		VSLb(req->vsl, SLT_BogoHeader, "Multiple Host: headers");
		wrk->stats->client_req_400++;
		(void)req->transport->minimal_response(req, 400);
		return (REQ_FSM_DONE);
	}

	if (http_CountHdr(req->http0, H_Content_Length) > 1) {
		VSLb(req->vsl, SLT_BogoHeader, "Multiple Content-Length: headers");
		wrk->stats->client_req_400++;
		(void)req->transport->minimal_response(req, 400);
		return (REQ_FSM_DONE);
	}

	cnt_recv_prep(req, ci);

	if (req->req_body_status == BS_ERROR) {
		req->doclose = SC_OVERLOAD;
		return (REQ_FSM_DONE);
	}

	VCL_recv_method(req->vcl, wrk, req, NULL, NULL);

	if (wrk->vpi->handling == VCL_RET_FAIL) {
		req->req_step = R_STP_VCLFAIL;
		return (REQ_FSM_MORE);
	}

	if (wrk->vpi->handling == VCL_RET_VCL && req->restarts == 0) {
		// Req_Rollback has happened in VPI_vcl_select
		assert(WS_Snapshot(req->ws) == req->ws_req);
		cnt_recv_prep(req, ci);
		VCL_recv_method(req->vcl, wrk, req, NULL, NULL);
	}

	if (req->want100cont && !req->late100cont) {
		req->want100cont = 0;
		if (req->transport->minimal_response(req, 100)) {
			req->doclose = SC_REM_CLOSE;
			return (REQ_FSM_DONE);
		}
	}

	/* Attempts to cache req.body may fail */
	if (req->req_body_status == BS_ERROR) {
		req->doclose = SC_RX_BODY;
		return (REQ_FSM_DONE);
	}

	recv_handling = wrk->vpi->handling;

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

	VSHA256_Init(&sha256ctx);
	VCL_hash_method(req->vcl, wrk, req, NULL, &sha256ctx);
	if (wrk->vpi->handling == VCL_RET_FAIL)
		recv_handling = wrk->vpi->handling;
	else
		assert(wrk->vpi->handling == VCL_RET_LOOKUP);
	VSHA256_Final(req->digest, &sha256ctx);

	switch (recv_handling) {
	case VCL_RET_VCL:
		VSLb(req->vsl, SLT_VCL_Error,
		    "Illegal return(vcl): %s",
		    req->restarts ? "Not after restarts" :
		    "Only from active VCL");
		req->err_code = 503;
		req->req_step = R_STP_SYNTH;
		break;
	case VCL_RET_PURGE:
		req->req_step = R_STP_PURGE;
		break;
	case VCL_RET_HASH:
		req->req_step = R_STP_LOOKUP;
		break;
	case VCL_RET_PIPE:
		if (!IS_TOPREQ(req)) {
			VSLb(req->vsl, SLT_VCL_Error,
			    "vcl_recv{} returns pipe for ESI included object."
			    "  Doing pass.");
			req->req_step = R_STP_PASS;
		} else if (req->http0->protover > 11) {
			VSLb(req->vsl, SLT_VCL_Error,
			    "vcl_recv{} returns pipe for HTTP/2 request."
			    "  Doing pass.");
			req->req_step = R_STP_PASS;
		} else {
			req->req_step = R_STP_PIPE;
		}
		break;
	case VCL_RET_PASS:
		req->req_step = R_STP_PASS;
		break;
	case VCL_RET_SYNTH:
		req->req_step = R_STP_SYNTH;
		break;
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_FAIL:
		req->req_step = R_STP_VCLFAIL;
		break;
	default:
		WRONG("Illegal return from vcl_recv{}");
	}
	return (REQ_FSM_MORE);
}

/*--------------------------------------------------------------------
 * Find the objhead, purge it.
 *
 * In VCL, a restart is necessary to get a new object
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_purge(struct worker *wrk, struct req *req)
{
	struct objcore *oc, *boc;
	enum lookup_e lr;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AZ(req->objcore);
	AZ(req->stale_oc);

	AN(req->vcl);

	VRY_Prep(req);

	AZ(req->objcore);
	req->hash_always_miss = 1;
	lr = HSH_Lookup(req, &oc, &boc);
	assert (lr == HSH_MISS);
	AZ(oc);
	CHECK_OBJ_NOTNULL(boc, OBJCORE_MAGIC);
	VRY_Finish(req, DISCARD);

	(void)HSH_Purge(wrk, boc->objhead, req->t_req, 0, 0, 0);

	HSH_Withdraw(wrk, &boc);

	VCL_purge_method(req->vcl, wrk, req, NULL, NULL);
	switch (wrk->vpi->handling) {
	case VCL_RET_RESTART:
		req->req_step = R_STP_RESTART;
		break;
	case VCL_RET_FAIL:
		req->req_step = R_STP_VCLFAIL;
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

static void v_matchproto_(req_state_f)
cnt_diag(struct req *req, const char *state)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	VSLb(req->vsl,  SLT_Debug, "vxid %ju STP_%s sp %p vcl %p",
	    VXID(req->vsl->wid), state, req->sp, req->vcl);
	VSL_Flush(req->vsl, 0);
}

void
CNT_Embark(struct worker *wrk, struct req *req)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/* wrk can have changed for restarts */
	req->vfc->wrk = req->wrk = wrk;
	wrk->vsl = req->vsl;
	if (req->req_step == R_STP_TRANSPORT && req->vcl == NULL) {
		VCL_Refresh(&wrk->wpriv->vcl);
		req->vcl = wrk->wpriv->vcl;
		wrk->wpriv->vcl = NULL;
		VSLbs(req->vsl, SLT_VCL_use, TOSTRAND(VCL_Name(req->vcl)));
	}

	AN(req->vcl);
}

enum req_fsm_nxt
CNT_Request(struct req *req)
{
	struct vrt_ctx ctx[1];
	struct worker *wrk;
	enum req_fsm_nxt nxt;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	CHECK_OBJ_NOTNULL(req->transport, TRANSPORT_MAGIC);
	AN(req->transport->deliver);
	AN(req->transport->minimal_response);

	/*
	 * Possible entrance states
	 */
	assert(
	    req->req_step == R_STP_LOOKUP ||
	    req->req_step == R_STP_FINISH ||
	    req->req_step == R_STP_TRANSPORT);

	AN(VXID_TAG(req->vsl->wid) & VSL_CLIENTMARKER);
	AN(req->vcl);

	for (nxt = REQ_FSM_MORE; nxt == REQ_FSM_MORE; ) {
		/*
		 * This is a good place to be paranoid about the various
		 * pointers still pointing to the things we expect.
		 */
		CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
		CHECK_OBJ_NOTNULL(wrk->wpriv, WORKER_PRIV_MAGIC);
		CHECK_OBJ_ORNULL(wrk->wpriv->nobjhead, OBJHEAD_MAGIC);
		CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(req->doclose, STREAM_CLOSE_MAGIC);

		AN(req->req_step);
		AN(req->req_step->name);
		AN(req->req_step->func);
		if (DO_DEBUG(DBG_REQ_STATE))
			cnt_diag(req, req->req_step->name);
		nxt = req->req_step->func(wrk, req);
		CHECK_OBJ_ORNULL(wrk->wpriv->nobjhead, OBJHEAD_MAGIC);
	}
	wrk->vsl = NULL;
	if (nxt == REQ_FSM_DONE) {
		INIT_OBJ(ctx, VRT_CTX_MAGIC);
		VCL_Req2Ctx(ctx, req);
		if (IS_TOPREQ(req)) {
			VCL_TaskLeave(ctx, req->top->privs);
			if (req->top->vcl0 != NULL)
				VCL_Recache(wrk, &req->top->vcl0);
		}
		VCL_TaskLeave(ctx, req->privs);
		assert(!IS_NO_VXID(req->vsl->wid));
		VRB_Free(req);
		VRT_Assign_Backend(&req->director_hint, NULL);
		req->wrk = NULL;
	}
	assert(nxt == REQ_FSM_DISEMBARK || !WS_IsReserved(req->ws));
	return (nxt);
}
