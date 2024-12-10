/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "storage/storage.h"
#include "vcl.h"
#include "vtim.h"
#include "vcc_interface.h"

#define FETCH_STEPS \
	FETCH_STEP(mkbereq,           MKBEREQ) \
	FETCH_STEP(retry,             RETRY) \
	FETCH_STEP(startfetch,        STARTFETCH) \
	FETCH_STEP(condfetch,         CONDFETCH) \
	FETCH_STEP(fetch,             FETCH) \
	FETCH_STEP(fetchbody,         FETCHBODY) \
	FETCH_STEP(fetchend,          FETCHEND) \
	FETCH_STEP(error,             ERROR) \
	FETCH_STEP(fail,              FAIL) \
	FETCH_STEP(done,              DONE)

typedef const struct fetch_step *vbf_state_f(struct worker *, struct busyobj *);

struct fetch_step {
	const char	*name;
	vbf_state_f	*func;
};

#define FETCH_STEP(l, U) \
    static vbf_state_f vbf_stp_##l; \
    static const struct fetch_step F_STP_##U[1] = {{ .name = "Fetch Step " #l, .func = vbf_stp_##l, }};
FETCH_STEPS
#undef FETCH_STEP

/*--------------------------------------------------------------------
 * Allocate an object, with fall-back to Transient.
 * XXX: This somewhat overlaps the stuff in stevedore.c
 * XXX: Should this be merged over there ?
 */

static int
vbf_allocobj(struct busyobj *bo, unsigned l)
{
	struct objcore *oc;
	const struct stevedore *stv;
	vtim_dur lifetime;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	lifetime = oc->ttl + oc->grace + oc->keep;

	if (bo->uncacheable) {
		stv = stv_transient;
		bo->wrk->stats->beresp_uncacheable++;
	}
	else if (lifetime < cache_param->shortlived) {
		stv = stv_transient;
		bo->wrk->stats->beresp_shortlived++;
	}
	else
		stv = bo->storage;

	bo->storage = NULL;

	if (stv == NULL)
		return (0);

	if (STV_NewObject(bo->wrk, oc, stv, l))
		return (1);

	if (stv == stv_transient)
		return (0);

	/*
	 * Try to salvage the transaction by allocating a shortlived object
	 * on Transient storage.
	 */

	oc->ttl = vmin_t(float, oc->ttl, cache_param->shortlived);
	oc->grace = 0.0;
	oc->keep = 0.0;
	return (STV_NewObject(bo->wrk, oc, stv_transient, l));
}

static void
vbf_cleanup(struct busyobj *bo)
{
	struct vfp_ctx *vfc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	vfc = bo->vfc;
	CHECK_OBJ_NOTNULL(vfc, VFP_CTX_MAGIC);

	bo->acct.beresp_bodybytes += VFP_Close(vfc);
	bo->vfp_filter_list = NULL;

	if (bo->director_state != DIR_S_NULL)
		VDI_Finish(bo);
}

void
Bereq_Rollback(VRT_CTX)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	bo = ctx->bo;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	if (bo->htc != NULL &&
	    bo->htc->body_status != BS_NONE &&
	    bo->htc->body_status != BS_TAKEN)
		bo->htc->doclose = SC_RESP_CLOSE;

	vbf_cleanup(bo);
	VCL_TaskLeave(ctx, bo->privs);
	VCL_TaskEnter(bo->privs);
	HTTP_Clone(bo->bereq, bo->bereq0);
	bo->vfp_filter_list = NULL;
	bo->err_reason = NULL;
	AN(bo->ws_bo);
	WS_Rollback(bo->ws, bo->ws_bo);
}

/*--------------------------------------------------------------------
 * Turn the beresp into a obj
 */

static int
vbf_beresp2obj(struct busyobj *bo)
{
	unsigned l, l2;
	const char *b;
	uint8_t *bp;
	struct vsb *vary = NULL;
	int varyl = 0;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	l = 0;

	/* Create Vary instructions */
	if (!(oc->flags & OC_F_PRIVATE)) {
		varyl = VRY_Create(bo, &vary);
		if (varyl > 0) {
			AN(vary);
			assert(varyl == VSB_len(vary));
			l += PRNDUP((intptr_t)varyl);
		} else if (varyl < 0) {
			/*
			 * Vary parse error
			 * Complain about it, and make this a pass.
			 */
			VSLb(bo->vsl, SLT_Error,
			    "Illegal 'Vary' header from backend, "
			    "making this a pass.");
			bo->uncacheable = 1;
			AZ(vary);
		} else
			/* No vary */
			AZ(vary);
	}

	l2 = http_EstimateWS(bo->beresp,
	    bo->uncacheable ? HTTPH_A_PASS : HTTPH_A_INS);
	l += l2;

	if (bo->uncacheable)
		oc->flags |= OC_F_HFM;

	if (!vbf_allocobj(bo, l)) {
		if (vary != NULL)
			VSB_destroy(&vary);
		AZ(vary);
		return (VFP_Error(bo->vfc, "Could not get storage"));
	}

	if (vary != NULL) {
		AN(ObjSetAttr(bo->wrk, oc, OA_VARY, varyl, VSB_data(vary)));
		VSB_destroy(&vary);
	}

	AZ(ObjSetXID(bo->wrk, oc, bo->vsl->wid));

	/* for HTTP_Encode() VSLH call */
	bo->beresp->logtag = SLT_ObjMethod;

	/* Filter into object */
	bp = ObjSetAttr(bo->wrk, oc, OA_HEADERS, l2, NULL);
	AN(bp);
	HTTP_Encode(bo->beresp, bp, l2,
	    bo->uncacheable ? HTTPH_A_PASS : HTTPH_A_INS);

	if (http_GetHdr(bo->beresp, H_Last_Modified, &b))
		AZ(ObjSetDouble(bo->wrk, oc, OA_LASTMODIFIED, VTIM_parse(b)));
	else
		AZ(ObjSetDouble(bo->wrk, oc, OA_LASTMODIFIED,
		    floor(oc->t_origin)));

	return (0);
}

/*--------------------------------------------------------------------
 * Copy req->bereq and release req if no body
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_mkbereq(struct worker *wrk, struct busyobj *bo)
{
	const char *q;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->req, REQ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	assert(oc->boc->state == BOS_INVALID);
	AZ(bo->storage);

	HTTP_Setup(bo->bereq0, bo->ws, bo->vsl, SLT_BereqMethod);
	http_FilterReq(bo->bereq0, bo->req->http,
	    bo->uncacheable ? HTTPH_R_PASS : HTTPH_R_FETCH);

	if (bo->uncacheable)
		AZ(bo->stale_oc);
	else {
		http_ForceField(bo->bereq0, HTTP_HDR_METHOD, "GET");
		if (cache_param->http_gzip_support)
			http_ForceHeader(bo->bereq0, H_Accept_Encoding, "gzip");
	}
	http_ForceField(bo->bereq0, HTTP_HDR_PROTO, "HTTP/1.1");

	if (bo->stale_oc != NULL &&
	    ObjCheckFlag(bo->wrk, bo->stale_oc, OF_IMSCAND) &&
	    (bo->stale_oc->boc != NULL || ObjGetLen(wrk, bo->stale_oc) != 0)) {
		AZ(bo->stale_oc->flags & (OC_F_HFM|OC_F_PRIVATE));
		q = RFC2616_Strong_LM(NULL, wrk, bo->stale_oc);
		if (q != NULL)
			http_PrintfHeader(bo->bereq0,
			    "If-Modified-Since: %s", q);
		q = HTTP_GetHdrPack(bo->wrk, bo->stale_oc, H_ETag);
		if (q != NULL)
			http_PrintfHeader(bo->bereq0,
			    "If-None-Match: %s", q);
	}

	http_CopyHome(bo->bereq0);
	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, SLT_BereqMethod);
	bo->ws_bo = WS_Snapshot(bo->ws);
	HTTP_Clone(bo->bereq, bo->bereq0);

	if (bo->req->req_body_status->avail == 0) {
		VBO_SetState(bo->wrk, bo, BOS_REQ_DONE);
	} else if (bo->req->req_body_status == BS_CACHED) {
		AN(bo->req->body_oc);
		bo->bereq_body = bo->req->body_oc;
		HSH_Ref(bo->bereq_body);
		VBO_SetState(bo->wrk, bo, BOS_REQ_DONE);
	}
	return (F_STP_STARTFETCH);
}

/*--------------------------------------------------------------------
 * Start a new VSL transaction and try again
 * Prepare the busyobj and fetch processors
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_retry(struct worker *wrk, struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	assert(bo->fetch_objcore->boc->state <= BOS_REQ_DONE);

	if (bo->no_retry != NULL) {
		VSLb(bo->vsl, SLT_Error,
		    "Retry not possible, %s", bo->no_retry);
		return (F_STP_FAIL);
	}

	VSLb_ts_busyobj(bo, "Retry", W_TIM_real(wrk));

	/* VDI_Finish (via vbf_cleanup) must have been called before */
	assert(bo->director_state == DIR_S_NULL);

	/* reset other bo attributes - See VBO_GetBusyObj */
	bo->storage = NULL;
	bo->do_esi = 0;
	bo->do_stream = 1;
	bo->was_304 = 0;
	bo->err_code = 0;
	bo->err_reason = NULL;
	bo->connect_timeout = NAN;
	bo->first_byte_timeout = NAN;
	bo->between_bytes_timeout = NAN;
	if (bo->htc != NULL)
		bo->htc->doclose = SC_NULL;

	// XXX: BereqEnd + BereqAcct ?
	VSL_ChgId(bo->vsl, "bereq", "retry", VXID_Get(wrk, VSL_BACKENDMARKER));
	VSLb_ts_busyobj(bo, "Start", bo->t_prev);
	http_VSL_log(bo->bereq);

	return (F_STP_STARTFETCH);
}

/*--------------------------------------------------------------------
 * 304 setup logic
 */

static int
vbf_304_logic(struct busyobj *bo)
{
	if (bo->stale_oc != NULL &&
	    ObjCheckFlag(bo->wrk, bo->stale_oc, OF_IMSCAND)) {
		AZ(bo->stale_oc->flags & (OC_F_HFM|OC_F_PRIVATE));
		if (ObjCheckFlag(bo->wrk, bo->stale_oc, OF_CHGCE)) {
			/*
			 * If a VFP changed C-E in the stored
			 * object, then don't overwrite C-E from
			 * the IMS fetch, and we must weaken any
			 * new ETag we get.
			 */
			RFC2616_Weaken_Etag(bo->beresp);
		}
		http_Unset(bo->beresp, H_Content_Encoding);
		http_Unset(bo->beresp, H_Content_Length);
		HTTP_Merge(bo->wrk, bo->stale_oc, bo->beresp);
		assert(http_IsStatus(bo->beresp, 200));
		bo->was_304 = 1;
	} else if (!bo->uncacheable) {
		/*
		 * Backend sent unallowed 304
		 */
		VSLb(bo->vsl, SLT_Error,
		    "304 response but not conditional fetch");
		bo->htc->doclose = SC_RX_BAD;
		vbf_cleanup(bo);
		return (-1);
	}
	return (1);
}

/*--------------------------------------------------------------------
 * Setup bereq from bereq0, run vcl_backend_fetch
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_startfetch(struct worker *wrk, struct busyobj *bo)
{
	int i;
	vtim_real now;
	unsigned handling;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AZ(bo->storage);
	bo->storage = bo->uncacheable ? stv_transient : STV_next();

	if (bo->retries > 0)
		http_Unset(bo->bereq, "\012X-Varnish:");

	http_PrintfHeader(bo->bereq, "X-Varnish: %ju", VXID(bo->vsl->wid));

	VCL_backend_fetch_method(bo->vcl, wrk, NULL, bo, NULL);

	if (wrk->vpi->handling == VCL_RET_ABANDON ||
	    wrk->vpi->handling == VCL_RET_FAIL)
		return (F_STP_FAIL);

	assert (wrk->vpi->handling == VCL_RET_FETCH ||
	    wrk->vpi->handling == VCL_RET_ERROR);

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, SLT_BerespMethod);

	assert(oc->boc->state <= BOS_REQ_DONE);

	AZ(bo->htc);

	VFP_Setup(bo->vfc, wrk);
	bo->vfc->oc = oc;
	bo->vfc->resp = bo->beresp;
	bo->vfc->req = bo->bereq;

	if (wrk->vpi->handling == VCL_RET_ERROR)
		return (F_STP_ERROR);

	VSLb_ts_busyobj(bo, "Fetch", W_TIM_real(wrk));
	i = VDI_GetHdr(bo);
	if (bo->htc != NULL)
		CHECK_OBJ_NOTNULL(bo->htc->doclose, STREAM_CLOSE_MAGIC);

	bo->t_resp = now = W_TIM_real(wrk);
	VSLb_ts_busyobj(bo, "Beresp", now);

	if (i) {
		assert(bo->director_state == DIR_S_NULL);
		return (F_STP_ERROR);
	}

	if (bo->htc != NULL && bo->htc->body_status == BS_ERROR) {
		bo->htc->doclose = SC_RX_BODY;
		vbf_cleanup(bo);
		VSLb(bo->vsl, SLT_Error, "Body cannot be fetched");
		assert(bo->director_state == DIR_S_NULL);
		return (F_STP_ERROR);
	}

	if (!http_GetHdr(bo->beresp, H_Date, NULL)) {
		/*
		 * RFC 2616 14.18 Date: The Date general-header field
		 * represents the date and time at which the message was
		 * originated, having the same semantics as orig-date in
		 * RFC 822. ... A received message that does not have a
		 * Date header field MUST be assigned one by the recipient
		 * if the message will be cached by that recipient or
		 * gatewayed via a protocol which requires a Date.
		 *
		 * If we didn't get a Date header, we assign one here.
		 */
		http_TimeHeader(bo->beresp, "Date: ", now);
	}

	/*
	 * These two headers can be spread over multiple actual headers
	 * and we rely on their content outside of VCL, so collect them
	 * into one line here.
	 */
	http_CollectHdr(bo->beresp, H_Cache_Control);
	http_CollectHdr(bo->beresp, H_Vary);

	/* What does RFC2616 think about TTL ? */
	RFC2616_Ttl(bo, now,
	    &oc->t_origin,
	    &oc->ttl,
	    &oc->grace,
	    &oc->keep);

	AZ(bo->do_esi);
	AZ(bo->was_304);

	if (http_IsStatus(bo->beresp, 304) && vbf_304_logic(bo) < 0)
		return (F_STP_ERROR);

	if (bo->htc != NULL && bo->htc->doclose == SC_NULL &&
	    http_GetHdrField(bo->bereq, H_Connection, "close", NULL))
		bo->htc->doclose = SC_REQ_CLOSE;

	VCL_backend_response_method(bo->vcl, wrk, NULL, bo, NULL);

	if (bo->htc != NULL && bo->htc->doclose == SC_NULL &&
	    http_GetHdrField(bo->beresp, H_Connection, "close", NULL))
		bo->htc->doclose = SC_RESP_CLOSE;

	if (VRG_CheckBo(bo) < 0) {
		if (bo->director_state != DIR_S_NULL)
			VDI_Finish(bo);
		return (F_STP_ERROR);
	}

	if (wrk->vpi->handling == VCL_RET_ABANDON ||
	    wrk->vpi->handling == VCL_RET_FAIL ||
	    wrk->vpi->handling == VCL_RET_ERROR) {
		/* do not count deliberately ending the backend connection as
		 * fetch failure
		 */
		handling = wrk->vpi->handling;
		if (bo->htc)
			bo->htc->doclose = SC_RESP_CLOSE;
		vbf_cleanup(bo);
		wrk->vpi->handling = handling;

		if (wrk->vpi->handling == VCL_RET_ERROR)
			return (F_STP_ERROR);
		else
			return (F_STP_FAIL);
	}

	if (wrk->vpi->handling == VCL_RET_RETRY) {
		if (bo->htc && bo->htc->body_status != BS_NONE)
			bo->htc->doclose = SC_RESP_CLOSE;
		vbf_cleanup(bo);

		if (bo->retries++ < cache_param->max_retries)
			return (F_STP_RETRY);

		VSLb(bo->vsl, SLT_VCL_Error,
		    "Too many retries, delivering 503");
		assert(bo->director_state == DIR_S_NULL);
		return (F_STP_ERROR);
	}

	VSLb_ts_busyobj(bo, "Process", W_TIM_real(wrk));
	assert(oc->boc->state <= BOS_REQ_DONE);
	if (oc->boc->state != BOS_REQ_DONE)
		VBO_SetState(wrk, bo, BOS_REQ_DONE);

	if (bo->do_esi)
		bo->do_stream = 0;
	if (wrk->vpi->handling == VCL_RET_PASS) {
		oc->flags |= OC_F_HFP;
		bo->uncacheable = 1;
		wrk->vpi->handling = VCL_RET_DELIVER;
	}
	if (!bo->uncacheable || !bo->do_stream)
		oc->boc->transit_buffer = 0;
	if (bo->uncacheable)
		oc->flags |= OC_F_HFM;

	assert(wrk->vpi->handling == VCL_RET_DELIVER);

	return (bo->was_304 ? F_STP_CONDFETCH : F_STP_FETCH);
}

/*--------------------------------------------------------------------
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_fetchbody(struct worker *wrk, struct busyobj *bo)
{
	ssize_t l;
	uint8_t *ptr;
	enum vfp_status vfps = VFP_ERROR;
	ssize_t est;
	struct vfp_ctx *vfc;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	vfc = bo->vfc;
	CHECK_OBJ_NOTNULL(vfc, VFP_CTX_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AN(vfc->vfp_nxt);

	est = bo->htc->content_length;
	if (est < 0)
		est = 0;

	do {
		if (oc->flags & OC_F_CANCEL) {
			/*
			 * A pass object and delivery was terminated
			 * We don't fail the fetch, in order for HitMiss
			 * objects to be created.
			 */
			AN(oc->flags & OC_F_HFM);
			VSLb(wrk->vsl, SLT_Debug,
			    "Fetch: Pass delivery abandoned");
			bo->htc->doclose = SC_RX_BODY;
			break;
		}
		AZ(vfc->failed);
		l = est;
		assert(l >= 0);
		if (VFP_GetStorage(vfc, &l, &ptr) != VFP_OK) {
			bo->htc->doclose = SC_RX_BODY;
			break;
		}

		AZ(vfc->failed);
		vfps = VFP_Suck(vfc, ptr, &l);
		if (l >= 0 && vfps != VFP_ERROR) {
			VFP_Extend(vfc, l, vfps);
			if (est >= l)
				est -= l;
			else
				est = 0;
		}
	} while (vfps == VFP_OK);

	if (vfc->failed) {
		(void)VFP_Error(vfc, "Fetch pipeline failed to process");
		bo->htc->doclose = SC_RX_BODY;
		vbf_cleanup(bo);
		if (!bo->do_stream) {
			assert(oc->boc->state < BOS_STREAM);
			// XXX: doclose = ?
			return (F_STP_ERROR);
		} else {
			wrk->stats->fetch_failed++;
			return (F_STP_FAIL);
		}
	}

	return (F_STP_FETCHEND);
}

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_fetch(struct worker *wrk, struct busyobj *bo)
{
	struct vrt_ctx ctx[1];
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	assert(wrk->vpi->handling == VCL_RET_DELIVER);

	if (bo->htc == NULL) {
		(void)VFP_Error(bo->vfc, "No backend connection (rollback?)");
		vbf_cleanup(bo);
		return (F_STP_ERROR);
	}

	/* No body -> done */
	if (bo->htc->body_status == BS_NONE || bo->htc->content_length == 0) {
		http_Unset(bo->beresp, H_Content_Encoding);
		bo->do_gzip = bo->do_gunzip = 0;
		bo->do_stream = 0;
		bo->vfp_filter_list = "";
	} else if (bo->vfp_filter_list == NULL) {
		bo->vfp_filter_list = VBF_Get_Filter_List(bo);
	}

	if (bo->vfp_filter_list == NULL ||
	    VCL_StackVFP(bo->vfc, bo->vcl, bo->vfp_filter_list)) {
		(bo)->htc->doclose = SC_OVERLOAD;
		vbf_cleanup(bo);
		return (F_STP_ERROR);
	}

	if (oc->flags & OC_F_PRIVATE)
		AN(bo->uncacheable);

	oc->boc->fetched_so_far = 0;

	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Bo2Ctx(ctx, bo);

	if (VFP_Open(ctx, bo->vfc)) {
		(void)VFP_Error(bo->vfc, "Fetch pipeline failed to open");
		bo->htc->doclose = SC_RX_BODY;
		vbf_cleanup(bo);
		return (F_STP_ERROR);
	}

	if (vbf_beresp2obj(bo)) {
		bo->htc->doclose = SC_RX_BODY;
		vbf_cleanup(bo);
		return (F_STP_ERROR);
	}

#define OBJ_FLAG(U, l, v)						\
	if (bo->vfc->obj_flags & OF_##U)				\
		ObjSetFlag(bo->wrk, oc, OF_##U, 1);
#include "tbl/obj_attr.h"

	if (!(oc->flags & OC_F_HFM) &&
	    http_IsStatus(bo->beresp, 200) && (
	      RFC2616_Strong_LM(bo->beresp, NULL, NULL) != NULL ||
	      http_GetHdr(bo->beresp, H_ETag, NULL)))
		ObjSetFlag(bo->wrk, oc, OF_IMSCAND, 1);

	assert(oc->boc->refcount >= 1);

	assert(oc->boc->state == BOS_REQ_DONE);

	VBO_SetState(wrk, bo, BOS_STREAM);

	VSLb(bo->vsl, SLT_Fetch_Body, "%u %s %s",
	    bo->htc->body_status->nbr, bo->htc->body_status->name,
	    bo->do_stream ? "stream" : "-");

	if (bo->htc->body_status != BS_NONE) {
		assert(bo->htc->body_status != BS_ERROR);
		return (F_STP_FETCHBODY);
	}
	AZ(bo->vfc->failed);
	return (F_STP_FETCHEND);
}

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_fetchend(struct worker *wrk, struct busyobj *bo)
{

	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AZ(bo->vfc->failed);

	/* Recycle the backend connection before setting BOS_FINISHED to
	   give predictable backend reuse behavior for varnishtest */
	vbf_cleanup(bo);

	AZ(ObjSetU64(wrk, oc, OA_LEN, oc->boc->fetched_so_far));

	if (bo->do_stream)
		assert(oc->boc->state == BOS_STREAM);
	else
		assert(oc->boc->state == BOS_REQ_DONE);

	VBO_SetState(wrk, bo, BOS_FINISHED);
	VSLb_ts_busyobj(bo, "BerespBody", W_TIM_real(wrk));
	if (bo->stale_oc != NULL) {
		VSL(SLT_ExpKill, NO_VXID, "VBF_Superseded x=%ju n=%ju",
		    VXID(ObjGetXID(wrk, bo->stale_oc)),
		    VXID(ObjGetXID(wrk, bo->fetch_objcore)));
		HSH_Replace(bo->stale_oc, bo->fetch_objcore);
	}
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

struct vbf_objiter_priv {
	unsigned		magic;
#define VBF_OBITER_PRIV_MAGIC	0x3c272a17
	struct busyobj		*bo;
	// not yet allocated
	ssize_t		l;
	// current allocation
	uint8_t		*p;
	ssize_t		pl;
};

static int v_matchproto_(objiterate_f)
vbf_objiterate(void *priv, unsigned flush, const void *ptr, ssize_t len)
{
	struct vbf_objiter_priv *vop;
	ssize_t l;
	const uint8_t *ps = ptr;

	CAST_OBJ_NOTNULL(vop, priv, VBF_OBITER_PRIV_MAGIC);
	CHECK_OBJ_NOTNULL(vop->bo, BUSYOBJ_MAGIC);

	flush &= OBJ_ITER_END;

	while (len > 0) {
		if (vop->pl == 0) {
			vop->p = NULL;
			AN(vop->l);
			vop->pl = vop->l;
			if (VFP_GetStorage(vop->bo->vfc, &vop->pl, &vop->p)
			    != VFP_OK)
				return (1);
			if (vop->pl < vop->l)
				vop->l -= vop->pl;
			else
				vop->l = 0;
		}
		AN(vop->pl);
		AN(vop->p);

		l = vmin(vop->pl, len);
		memcpy(vop->p, ps, l);
		VFP_Extend(vop->bo->vfc, l,
			   flush && l == len ? VFP_END : VFP_OK);
		ps += l;
		vop->p += l;
		len -= l;
		vop->pl -= l;
	}
	if (flush)
		AZ(vop->l);
	return (0);
}

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_condfetch(struct worker *wrk, struct busyobj *bo)
{
	struct boc *stale_boc;
	enum boc_state_e stale_state;
	struct objcore *oc, *stale_oc;
	struct vbf_objiter_priv vop[1];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	stale_oc = bo->stale_oc;
	CHECK_OBJ_NOTNULL(stale_oc, OBJCORE_MAGIC);

	stale_boc = HSH_RefBoc(stale_oc);
	CHECK_OBJ_ORNULL(stale_boc, BOC_MAGIC);
	if (stale_boc) {
		/* Wait for the stale object to become fully fetched, so
		 * that we can catch fetch errors, before we unbusy the
		 * new object. This serves two purposes. First it helps
		 * with request coalescing, and stops long chains of
		 * IMS-updated short-TTL objects all streaming from a
		 * single slow body fetch. Second it makes sure that all
		 * the object attributes are complete when we copy them
		 * (this would be an issue for ie OA_GZIPBITS). */
		VSLb(bo->vsl, SLT_Notice,
		    "vsl: Conditional fetch wait for streaming object");
		ObjWaitState(stale_oc, BOS_FINISHED);
		stale_state = stale_boc->state;
		HSH_DerefBoc(bo->wrk, stale_oc);
		stale_boc = NULL;
		if (stale_state != BOS_FINISHED) {
			assert(stale_state == BOS_FAILED);
			AN(stale_oc->flags & OC_F_FAILED);
		}
	}

	AZ(stale_boc);
	if (stale_oc->flags & OC_F_FAILED) {
		(void)VFP_Error(bo->vfc, "Template object failed");
		vbf_cleanup(bo);
		wrk->stats->fetch_failed++;
		return (F_STP_FAIL);
	}

	if (vbf_beresp2obj(bo)) {
		vbf_cleanup(bo);
		wrk->stats->fetch_failed++;
		return (F_STP_FAIL);
	}

	if (ObjHasAttr(bo->wrk, stale_oc, OA_ESIDATA))
		AZ(ObjCopyAttr(bo->wrk, oc, stale_oc, OA_ESIDATA));

	AZ(ObjCopyAttr(bo->wrk, oc, stale_oc, OA_FLAGS));
	if (oc->flags & OC_F_HFM)
		ObjSetFlag(bo->wrk, oc, OF_IMSCAND, 0);
	AZ(ObjCopyAttr(bo->wrk, oc, stale_oc, OA_GZIPBITS));

	VBO_SetState(wrk, bo, BOS_STREAM);

	INIT_OBJ(vop, VBF_OBITER_PRIV_MAGIC);
	vop->bo = bo;
	vop->l = ObjGetLen(bo->wrk, stale_oc);
	if (ObjIterate(wrk, stale_oc, vop, vbf_objiterate, 0))
		(void)VFP_Error(bo->vfc, "Template object failed");

	if (bo->vfc->failed) {
		vbf_cleanup(bo);
		wrk->stats->fetch_failed++;
		return (F_STP_FAIL);
	}
	return (F_STP_FETCHEND);
}

/*--------------------------------------------------------------------
 * Create synth object
 *
 * replaces a stale object unless
 * - abandoning the bereq or
 * - leaving vcl_backend_error with return (deliver)
 *
 * We do want the stale replacement to avoid an object pileup with short ttl and
 * long grace/keep, yet there could exist cases where a cache object is
 * deliberately created to momentarily override a stale object.
 *
 * If this case exists, we should add a vcl veto (e.g. beresp.replace_stale with
 * default true)
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_error(struct worker *wrk, struct busyobj *bo)
{
	ssize_t l, ll, o;
	vtim_real now;
	uint8_t *ptr;
	struct vsb *synth_body;
	struct objcore *stale, *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->boc, BOC_MAGIC);
	assert(oc->boc->state < BOS_STREAM);
	assert(bo->director_state == DIR_S_NULL);

	if (wrk->vpi->handling != VCL_RET_ERROR)
		wrk->stats->fetch_failed++;

	now = W_TIM_real(wrk);
	VSLb_ts_busyobj(bo, "Error", now);

	if (oc->stobj->stevedore != NULL) {
		oc->boc->fetched_so_far = 0;
		ObjFreeObj(bo->wrk, oc);
	}

	if (bo->storage == NULL)
		bo->storage = STV_next();

	// XXX: reset all beresp flags ?

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, SLT_BerespMethod);
	if (bo->err_code > 0)
		http_PutResponse(bo->beresp, "HTTP/1.1", bo->err_code,
		    bo->err_reason);
	else
		http_PutResponse(bo->beresp, "HTTP/1.1", 503,
		    "Backend fetch failed");

	http_TimeHeader(bo->beresp, "Date: ", now);
	http_SetHeader(bo->beresp, "Server: Varnish");

	stale = bo->stale_oc;
	oc->t_origin = now;
	oc->ttl = 0;
	oc->grace = 0;
	oc->keep = 0;

	synth_body = VSB_new_auto();
	AN(synth_body);

	VCL_backend_error_method(bo->vcl, wrk, NULL, bo, synth_body);

	AZ(VSB_finish(synth_body));

	if (wrk->vpi->handling == VCL_RET_ABANDON || wrk->vpi->handling == VCL_RET_FAIL) {
		VSB_destroy(&synth_body);
		return (F_STP_FAIL);
	}

	if (wrk->vpi->handling == VCL_RET_RETRY) {
		VSB_destroy(&synth_body);
		if (bo->retries++ < cache_param->max_retries)
			return (F_STP_RETRY);
		VSLb(bo->vsl, SLT_VCL_Error, "Too many retries, failing");
		return (F_STP_FAIL);
	}

	assert(wrk->vpi->handling == VCL_RET_DELIVER);

	assert(bo->vfc->wrk == bo->wrk);
	assert(bo->vfc->oc == oc);
	assert(bo->vfc->resp == bo->beresp);
	assert(bo->vfc->req == bo->bereq);

	if (vbf_beresp2obj(bo)) {
		VSB_destroy(&synth_body);
		return (F_STP_FAIL);
	}

	oc->boc->transit_buffer = 0;

	ll = VSB_len(synth_body);
	o = 0;
	while (ll > 0) {
		l = ll;
		if (VFP_GetStorage(bo->vfc, &l, &ptr) != VFP_OK)
			break;
		l = vmin(l, ll);
		memcpy(ptr, VSB_data(synth_body) + o, l);
		VFP_Extend(bo->vfc, l, l == ll ? VFP_END : VFP_OK);
		ll -= l;
		o += l;
	}
	AZ(ObjSetU64(wrk, oc, OA_LEN, o));
	VSB_destroy(&synth_body);
	VBO_SetState(wrk, bo, BOS_STREAM);
	if (stale != NULL && oc->ttl > 0)
		HSH_Kill(stale);
	VBO_SetState(wrk, bo, BOS_FINISHED);
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_fail(struct worker *wrk, struct busyobj *bo)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	assert(oc->boc->state < BOS_FINISHED);
	VBO_SetState(wrk, bo, BOS_FAILED);
	HSH_Kill(oc);
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_done(struct worker *wrk, struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	WRONG("Just plain wrong");
	NEEDLESS(return (F_STP_DONE));
}

static void v_matchproto_(task_func_t)
vbf_fetch_thread(struct worker *wrk, void *priv)
{
	struct vrt_ctx ctx[1];
	struct busyobj *bo;
	struct objcore *oc;
	const struct fetch_step *stp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->req, REQ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	THR_SetBusyobj(bo);
	stp = F_STP_MKBEREQ;
	assert(isnan(bo->t_first));
	assert(isnan(bo->t_prev));
	VSLb_ts_busyobj(bo, "Start", W_TIM_real(wrk));

	bo->wrk = wrk;
	wrk->vsl = bo->vsl;

#if 0
	if (bo->stale_oc != NULL) {
		CHECK_OBJ_NOTNULL(bo->stale_oc, OBJCORE_MAGIC);
		/* We don't want the oc/stevedore ops in fetching thread */
		if (!ObjCheckFlag(wrk, bo->stale_oc, OF_IMSCAND))
			(void)HSH_DerefObjCore(wrk, &bo->stale_oc, 0);
	}
#endif

	VCL_TaskEnter(bo->privs);
	while (stp != F_STP_DONE) {
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		assert(oc->boc->refcount >= 1);
		if (oc->boc->state < BOS_REQ_DONE)
			AN(bo->req);
		else
			AZ(bo->req);
		AN(stp);
		AN(stp->name);
		AN(stp->func);
		stp = stp->func(wrk, bo);
	}

	assert(bo->director_state == DIR_S_NULL);

	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Bo2Ctx(ctx, bo);
	VCL_TaskLeave(ctx, bo->privs);
	http_Teardown(bo->bereq);
	http_Teardown(bo->beresp);
	// cannot make assumptions about the number of references here #3434
	if (bo->bereq_body != NULL)
		(void)HSH_DerefObjCore(bo->wrk, &bo->bereq_body);

	if (oc->boc->state == BOS_FINISHED) {
		AZ(oc->flags & OC_F_FAILED);
		VSLb(bo->vsl, SLT_Length, "%ju",
		    (uintmax_t)ObjGetLen(bo->wrk, oc));
	}
	// AZ(oc->boc);	// XXX

	if (bo->stale_oc != NULL)
		(void)HSH_DerefObjCore(wrk, &bo->stale_oc);

	wrk->vsl = NULL;
	HSH_DerefBoc(wrk, oc);
	SES_Rel(bo->sp);
	VBO_ReleaseBusyObj(wrk, &bo);
	THR_SetBusyobj(NULL);
}

/*--------------------------------------------------------------------
 */

void
VBF_Fetch(struct worker *wrk, struct req *req, struct objcore *oc,
    struct objcore *oldoc, enum vbf_fetch_mode_e mode)
{
	struct boc *boc;
	struct busyobj *bo;
	enum task_prio prio;
	const char *how;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_ORNULL(oldoc, OBJCORE_MAGIC);

	bo = VBO_GetBusyObj(wrk, req);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AN(bo->vcl);

	boc = HSH_RefBoc(oc);
	CHECK_OBJ_NOTNULL(boc, BOC_MAGIC);
	assert(boc->state < BOS_STREAM);

	switch (mode) {
	case VBF_PASS:
		prio = TASK_QUEUE_BO;
		how = "pass";
		bo->uncacheable = 1;
		break;
	case VBF_NORMAL:
		prio = TASK_QUEUE_BO;
		how = "fetch";
		break;
	case VBF_BACKGROUND:
		prio = TASK_QUEUE_BG;
		how = "bgfetch";
		bo->is_bgfetch = 1;
		break;
	default:
		WRONG("Wrong fetch mode");
	}

#define REQ_BEREQ_FLAG(l, r, w, d) bo->l = req->l;
#include "tbl/req_bereq_flags.h"

	VSLb(bo->vsl, SLT_Begin, "bereq %ju %s", VXID(req->vsl->wid), how);
	VSLbs(bo->vsl, SLT_VCL_use, TOSTRAND(VCL_Name(bo->vcl)));
	VSLb(req->vsl, SLT_Link, "bereq %ju %s", VXID(bo->vsl->wid), how);

	THR_SetBusyobj(bo);

	bo->sp = req->sp;
	SES_Ref(bo->sp);

	oc->boc->vary = req->vary_b;
	req->vary_b = NULL;

	HSH_Ref(oc);
	AZ(bo->fetch_objcore);
	bo->fetch_objcore = oc;

	AZ(bo->stale_oc);
	if (oldoc != NULL) {
		assert(oldoc->refcnt > 0);
		HSH_Ref(oldoc);
		bo->stale_oc = oldoc;
	}

	AZ(bo->req);
	bo->req = req;

	bo->fetch_task->priv = bo;
	bo->fetch_task->func = vbf_fetch_thread;

	if (Pool_Task(wrk->pool, bo->fetch_task, prio)) {
		wrk->stats->bgfetch_no_thread++;
		VSLb(bo->vsl, SLT_FetchError,
		    "No thread available for bgfetch");
		(void)vbf_stp_fail(req->wrk, bo);
		if (bo->stale_oc != NULL)
			(void)HSH_DerefObjCore(wrk, &bo->stale_oc);
		HSH_DerefBoc(wrk, oc);
		SES_Rel(bo->sp);
		THR_SetBusyobj(NULL);
		VBO_ReleaseBusyObj(wrk, &bo);
	} else {
		THR_SetBusyobj(NULL);
		bo = NULL; /* ref transferred to fetch thread */
		if (mode == VBF_BACKGROUND) {
			ObjWaitState(oc, BOS_REQ_DONE);
			(void)VRB_Ignore(req);
		} else {
			ObjWaitState(oc, BOS_STREAM);
			AZ(oc->flags & OC_F_BUSY);
			if (oc->boc->state == BOS_FAILED)
				AN(oc->flags & OC_F_FAILED);
		}
	}
	AZ(bo);
	VSLb_ts_req(req, "Fetch", W_TIM_real(wrk));
	assert(oc->boc == boc);
	HSH_DerefBoc(wrk, oc);
	if (mode == VBF_BACKGROUND)
		(void)HSH_DerefObjCore(wrk, &oc);
}
