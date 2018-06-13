/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "hash/hash_slinger.h"
#include "storage/storage.h"
#include "vcl.h"
#include "vtim.h"

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
	double lifetime;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	lifetime = oc->ttl + oc->grace + oc->keep;

	if (bo->uncacheable || lifetime < cache_param->shortlived)
		stv = stv_transient;
	else
		stv = bo->storage;

	bo->storage = NULL;

	if (stv == NULL)
		return (0);

	if (STV_NewObject(bo->wrk, bo->fetch_objcore, stv, l))
		return (1);

	if (stv == stv_transient)
		return (0);

	/*
	 * Try to salvage the transaction by allocating a shortlived object
	 * on Transient storage.
	 */

	if (oc->ttl > cache_param->shortlived)
		oc->ttl = cache_param->shortlived;
	oc->grace = 0.0;
	oc->keep = 0.0;
	return (STV_NewObject(bo->wrk, bo->fetch_objcore, stv_transient, l));
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

	l = 0;

	/* Create Vary instructions */
	if (!(bo->fetch_objcore->flags & OC_F_PRIVATE)) {
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
		bo->fetch_objcore->flags |= OC_F_PASS;

	if (!vbf_allocobj(bo, l)) {
		if (vary != NULL)
			VSB_destroy(&vary);
		AZ(vary);
		return (-1);
	}

	if (vary != NULL) {
		AN(ObjSetAttr(bo->wrk, bo->fetch_objcore, OA_VARY, varyl,
			VSB_data(vary)));
		VSB_destroy(&vary);
	}

	AZ(ObjSetU32(bo->wrk, bo->fetch_objcore, OA_VXID, VXID(bo->vsl->wid)));

	/* for HTTP_Encode() VSLH call */
	bo->beresp->logtag = SLT_ObjMethod;

	/* Filter into object */
	bp = ObjSetAttr(bo->wrk, bo->fetch_objcore, OA_HEADERS, l2, NULL);
	AN(bp);
	HTTP_Encode(bo->beresp, bp, l2,
	    bo->uncacheable ? HTTPH_A_PASS : HTTPH_A_INS);

	if (http_GetHdr(bo->beresp, H_Last_Modified, &b))
		AZ(ObjSetDouble(bo->wrk, bo->fetch_objcore, OA_LASTMODIFIED,
		    VTIM_parse(b)));
	else
		AZ(ObjSetDouble(bo->wrk, bo->fetch_objcore, OA_LASTMODIFIED,
		    floor(bo->fetch_objcore->t_origin)));

	return (0);
}

/*--------------------------------------------------------------------
 * Copy req->bereq and release req if no body
 */

static enum fetch_step
vbf_stp_mkbereq(struct worker *wrk, struct busyobj *bo)
{
	const char *q;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->req, REQ_MAGIC);

	assert(bo->fetch_objcore->boc->state == BOS_INVALID);
	AZ(bo->storage);

	HTTP_Setup(bo->bereq0, bo->ws, bo->vsl, SLT_BereqMethod);
	http_FilterReq(bo->bereq0, bo->req->http,
	    bo->do_pass ? HTTPH_R_PASS : HTTPH_R_FETCH);

	if (bo->do_pass)
		AZ(bo->stale_oc);
	else {
		http_ForceField(bo->bereq0, HTTP_HDR_METHOD, "GET");
		if (cache_param->http_gzip_support)
			http_ForceHeader(bo->bereq0, H_Accept_Encoding, "gzip");
	}
	http_ForceField(bo->bereq0, HTTP_HDR_PROTO, "HTTP/1.1");
	http_CopyHome(bo->bereq0);

	if (bo->stale_oc != NULL &&
	    ObjCheckFlag(bo->wrk, bo->stale_oc, OF_IMSCAND) &&
	    (bo->stale_oc->boc != NULL || ObjGetLen(wrk, bo->stale_oc) != 0)) {
		AZ(bo->stale_oc->flags & OC_F_PASS);
		q = HTTP_GetHdrPack(bo->wrk, bo->stale_oc, H_Last_Modified);
		if (q != NULL)
			http_PrintfHeader(bo->bereq0,
			    "If-Modified-Since: %s", q);
		q = HTTP_GetHdrPack(bo->wrk, bo->stale_oc, H_ETag);
		if (q != NULL)
			http_PrintfHeader(bo->bereq0,
			    "If-None-Match: %s", q);
	}

	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, SLT_BereqMethod);
	bo->ws_bo = WS_Snapshot(bo->ws);
	HTTP_Copy(bo->bereq, bo->bereq0);

	if (bo->req->req_body_status == REQ_BODY_NONE) {
		bo->req = NULL;
		ObjSetState(bo->wrk, bo->fetch_objcore, BOS_REQ_DONE);
	}
	return (F_STP_STARTFETCH);
}

/*--------------------------------------------------------------------
 * Start a new VSL transaction and try again
 * Prepare the busyobj and fetch processors
 */

static enum fetch_step
vbf_stp_retry(struct worker *wrk, struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	assert(bo->fetch_objcore->boc->state <= BOS_REQ_DONE);

	VSLb_ts_busyobj(bo, "Retry", W_TIM_real(wrk));

	/* VDI_Finish must have been called before */
	assert(bo->director_state == DIR_S_NULL);

	/* reset other bo attributes - See VBO_GetBusyObj */
	bo->storage = NULL;
	bo->do_esi = 0;
	bo->do_stream = 1;
	bo->filter_list = NULL;
	bo->was_304 = 0;

	// XXX: BereqEnd + BereqAcct ?
	VSL_ChgId(bo->vsl, "bereq", "retry", VXID_Get(wrk, VSL_BACKENDMARKER));
	VSLb_ts_busyobj(bo, "Start", bo->t_prev);
	http_VSL_log(bo->bereq);

	return (F_STP_STARTFETCH);
}

/*--------------------------------------------------------------------
 * Setup bereq from bereq0, run vcl_backend_fetch
 */

static enum fetch_step
vbf_stp_startfetch(struct worker *wrk, struct busyobj *bo)
{
	int i;
	double now;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AZ(bo->storage);
	bo->storage = bo->do_pass ? stv_transient : STV_next();

	if (bo->retries > 0)
		http_Unset(bo->bereq, "\012X-Varnish:");

	http_PrintfHeader(bo->bereq, "X-Varnish: %u", VXID(bo->vsl->wid));

	VCL_backend_fetch_method(bo->vcl, wrk, NULL, bo, NULL);

	bo->uncacheable = bo->do_pass;
	if (wrk->handling == VCL_RET_ABANDON || wrk->handling == VCL_RET_FAIL)
		return (F_STP_FAIL);

	assert (wrk->handling == VCL_RET_FETCH);

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, SLT_BerespMethod);

	assert(bo->fetch_objcore->boc->state <= BOS_REQ_DONE);

	AZ(bo->htc);

	VFP_Setup(bo->vfc, wrk);
	bo->vfc->oc = bo->fetch_objcore;
	bo->vfc->resp = bo->beresp;
	bo->vfc->req = bo->bereq;

	i = VDI_GetHdr(bo);

	now = W_TIM_real(wrk);
	VSLb_ts_busyobj(bo, "Beresp", now);

	if (i) {
		assert(bo->director_state == DIR_S_NULL);
		return (F_STP_ERROR);
	}

	http_VSL_log(bo->beresp);

	if (bo->htc->body_status == BS_ERROR) {
		bo->htc->doclose = SC_RX_BODY;
		VDI_Finish(bo);
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

	if (bo->fetch_objcore->flags & OC_F_PRIVATE) {
		/* private objects have negative TTL */
		bo->fetch_objcore->t_origin = now;
		bo->fetch_objcore->ttl = -1.;
		bo->fetch_objcore->grace = 0;
		bo->fetch_objcore->keep = 0;
	} else {
		/* What does RFC2616 think about TTL ? */
		RFC2616_Ttl(bo, now,
		    &bo->fetch_objcore->t_origin,
		    &bo->fetch_objcore->ttl,
		    &bo->fetch_objcore->grace,
		    &bo->fetch_objcore->keep
		    );
	}

	AZ(bo->do_esi);
	AZ(bo->was_304);

	if (http_IsStatus(bo->beresp, 304)) {
		if (bo->stale_oc != NULL &&
		    ObjCheckFlag(bo->wrk, bo->stale_oc, OF_IMSCAND)) {
			if (ObjCheckFlag(bo->wrk, bo->stale_oc, OF_CHGGZIP)) {
				/*
				 * If we changed the gzip status of the object
				 * the stored Content_Encoding controls we
				 * must weaken any new ETag we get.
				 */
				http_Unset(bo->beresp, H_Content_Encoding);
				RFC2616_Weaken_Etag(bo->beresp);
			}
			http_Unset(bo->beresp, H_Content_Length);
			HTTP_Merge(bo->wrk, bo->stale_oc, bo->beresp);
			assert(http_IsStatus(bo->beresp, 200));
			bo->was_304 = 1;
		} else if (!bo->do_pass) {
			/*
			 * Backend sent unallowed 304
			 */
			VSLb(bo->vsl, SLT_Error,
			    "304 response but not conditional fetch");
			bo->htc->doclose = SC_RX_BAD;
			VDI_Finish(bo);
			return (F_STP_ERROR);
		}
	}

	VCL_backend_response_method(bo->vcl, wrk, NULL, bo, NULL);

	if (wrk->handling == VCL_RET_ABANDON || wrk->handling == VCL_RET_FAIL) {
		bo->htc->doclose = SC_RESP_CLOSE;
		VDI_Finish(bo);
		return (F_STP_FAIL);
	}

	if (wrk->handling == VCL_RET_RETRY) {
		if (bo->htc->body_status != BS_NONE)
			bo->htc->doclose = SC_RESP_CLOSE;
		if (bo->director_state != DIR_S_NULL)
			VDI_Finish(bo);

		if (bo->retries++ < cache_param->max_retries)
			return (F_STP_RETRY);

		VSLb(bo->vsl, SLT_VCL_Error,
		    "Too many retries, delivering 503");
		assert(bo->director_state == DIR_S_NULL);
		return (F_STP_ERROR);
	}

	assert(bo->fetch_objcore->boc->state <= BOS_REQ_DONE);
	if (bo->fetch_objcore->boc->state != BOS_REQ_DONE) {
		bo->req = NULL;
		ObjSetState(wrk, bo->fetch_objcore, BOS_REQ_DONE);
	}

	if (bo->do_esi)
		bo->do_stream = 0;
	if (wrk->handling == VCL_RET_PASS) {
		bo->fetch_objcore->flags |= OC_F_HFP;
		bo->uncacheable = 1;
		wrk->handling = VCL_RET_DELIVER;
	}
	if (bo->do_pass || bo->uncacheable)
		bo->fetch_objcore->flags |= OC_F_PASS;

	assert(wrk->handling == VCL_RET_DELIVER);

	return (bo->was_304 ? F_STP_CONDFETCH : F_STP_FETCH);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_fetchbody(struct worker *wrk, struct busyobj *bo)
{
	ssize_t l;
	uint8_t *ptr;
	enum vfp_status vfps = VFP_ERROR;
	ssize_t est;
	struct vfp_ctx *vfc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	vfc = bo->vfc;
	CHECK_OBJ_NOTNULL(vfc, VFP_CTX_MAGIC);

	AN(vfc->vfp_nxt);

	est = bo->htc->content_length;
	if (est < 0)
		est = 0;

	do {
		if (vfc->oc->flags & OC_F_ABANDON) {
			/*
			 * A pass object and delivery was terminated
			 * We don't fail the fetch, in order for hit-for-pass
			 * objects to be created.
			 */
			AN(vfc->oc->flags & OC_F_PASS);
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
		if (l > 0 && vfps != VFP_ERROR) {
			bo->acct.beresp_bodybytes += l;
			VFP_Extend(vfc, l);
			if (est >= l)
				est -= l;
			else
				est = 0;
		}
	} while (vfps == VFP_OK);

	if (vfc->failed) {
		(void)VFP_Error(vfc, "Fetch pipeline failed to process");
		bo->htc->doclose = SC_RX_BODY;
		VFP_Close(vfc);
		VDI_Finish(bo);
		if (!bo->do_stream) {
			assert(bo->fetch_objcore->boc->state < BOS_STREAM);
			// XXX: doclose = ?
			return (F_STP_ERROR);
		} else {
			wrk->stats->fetch_failed++;
			return (F_STP_FAIL);
		}
	}

	ObjTrimStore(wrk, vfc->oc);
	return (F_STP_FETCHEND);
}

/*--------------------------------------------------------------------
 */

static void
vbf_default_filter_list(const struct busyobj *bo, struct vsb *vsb)
{
	const char *p;
	int do_gzip = bo->do_gzip;
	int do_gunzip = bo->do_gunzip;
	int is_gzip = 0, is_gunzip = 0;

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
	 */

	/* No body -> done */
	if (bo->htc->body_status == BS_NONE || bo->htc->content_length == 0)
		return;

	if (!cache_param->http_gzip_support)
		do_gzip = do_gunzip = 0;

	if (http_GetHdr(bo->beresp, H_Content_Encoding, &p))
		is_gzip = !strcasecmp(p, "gzip");
	else
		is_gunzip = 1;

	/* We won't gunzip unless it is gzip'ed */
	if (do_gunzip && !is_gzip)
		do_gunzip = 0;

	/* We wont gzip unless if it already is gzip'ed */
	if (do_gzip && !is_gunzip)
		do_gzip = 0;

	if (do_gunzip || (is_gzip && bo->do_esi))
		VSB_cat(vsb, " gunzip");

	if (bo->do_esi && (do_gzip || (is_gzip && !do_gunzip))) {
		VSB_cat(vsb, " esi_gzip");
		return;
	}

	if (bo->do_esi) {
		VSB_cat(vsb, " esi");
		return;
	}

	if (do_gzip)
		VSB_cat(vsb, " gzip");

	if (is_gzip && !do_gunzip)
		VSB_cat(vsb, " testgunzip");
}

const char *
VBF_Get_Filter_List(struct busyobj *bo)
{
	unsigned u;
	struct vsb vsb[1];

	u = WS_Reserve(bo->ws, 0);
	if (u == 0) {
		WS_Release(bo->ws, 0);
		WS_MarkOverflow(bo->ws);
		return (NULL);
	}
	AN(VSB_new(vsb, bo->ws->f, u, VSB_FIXEDLEN));
	vbf_default_filter_list(bo, vsb);
	if (VSB_finish(vsb)) {
		WS_Release(bo->ws, 0);
		WS_MarkOverflow(bo->ws);
		return (NULL);
	}
	if (VSB_len(vsb)) {
		WS_Release(bo->ws, VSB_len(vsb) + 1);
		return (VSB_data(vsb) + 1);
	}
	WS_Release(bo->ws, 0);
	return ("");
}

static enum fetch_step
vbf_stp_fetch(struct worker *wrk, struct busyobj *bo)
{
	const char *p;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);

	assert(wrk->handling == VCL_RET_DELIVER);

	/* No body -> done */
	if (bo->htc->body_status == BS_NONE || bo->htc->content_length == 0) {
		http_Unset(bo->beresp, H_Content_Encoding);
		bo->do_gzip = bo->do_gunzip = 0;
		bo->do_stream = 0;
		bo->filter_list = "";
	} else if (bo->filter_list == NULL) {
		bo->filter_list = VBF_Get_Filter_List(bo);
	}

	if (bo->filter_list == NULL ||
	    VCL_StackVFP(bo->vfc, bo->vcl, bo->filter_list)) {
		(bo)->htc->doclose = SC_OVERLOAD;
		VDI_Finish(bo);
		return (F_STP_ERROR);
	}

	if (bo->fetch_objcore->flags & OC_F_PRIVATE)
		AN(bo->uncacheable);

	bo->fetch_objcore->boc->len_so_far = 0;

	if (VFP_Open(bo->vfc)) {
		(void)VFP_Error(bo->vfc, "Fetch pipeline failed to open");
		bo->htc->doclose = SC_RX_BODY;
		VDI_Finish(bo);
		return (F_STP_ERROR);
	}

	if (vbf_beresp2obj(bo)) {
		(void)VFP_Error(bo->vfc, "Could not get storage");
		bo->htc->doclose = SC_RX_BODY;
		VFP_Close(bo->vfc);
		VDI_Finish(bo);
		return (F_STP_ERROR);
	}

#define OBJ_FLAG(U, l, v)						\
	if (bo->vfc->obj_flags & OF_##U)				\
		ObjSetFlag(bo->wrk, bo->fetch_objcore, OF_##U, 1);
#include "tbl/obj_attr.h"

	if (!(bo->fetch_objcore->flags & OC_F_PASS) &&
	    http_IsStatus(bo->beresp, 200) && (
	      http_GetHdr(bo->beresp, H_Last_Modified, &p) ||
	      http_GetHdr(bo->beresp, H_ETag, &p)))
		ObjSetFlag(bo->wrk, bo->fetch_objcore, OF_IMSCAND, 1);

	assert(bo->fetch_objcore->boc->refcount >= 1);

	assert(bo->fetch_objcore->boc->state == BOS_REQ_DONE);

	if (bo->do_stream) {
		ObjSetState(wrk, bo->fetch_objcore, BOS_PREP_STREAM);
		HSH_Unbusy(wrk, bo->fetch_objcore);
		ObjSetState(wrk, bo->fetch_objcore, BOS_STREAM);
	}

	VSLb(bo->vsl, SLT_Fetch_Body, "%u %s %s",
	    bo->htc->body_status, body_status_2str(bo->htc->body_status),
	    bo->do_stream ? "stream" : "-");

	if (bo->htc->body_status != BS_NONE) {
		assert(bo->htc->body_status != BS_ERROR);
		return (F_STP_FETCHBODY);
	}
	AZ(bo->vfc->failed);
	return (F_STP_FETCHEND);
}

static enum fetch_step
vbf_stp_fetchend(struct worker *wrk, struct busyobj *bo)
{

	AZ(bo->vfc->failed);
	VFP_Close(bo->vfc);

	AZ(ObjSetU64(wrk, bo->fetch_objcore, OA_LEN,
	    bo->fetch_objcore->boc->len_so_far));

	if (bo->do_stream)
		assert(bo->fetch_objcore->boc->state == BOS_STREAM);
	else {
		assert(bo->fetch_objcore->boc->state == BOS_REQ_DONE);
		HSH_Unbusy(wrk, bo->fetch_objcore);
	}

	/* Recycle the backend connection before setting BOS_FINISHED to
	   give predictable backend reuse behavior for varnishtest */
	VDI_Finish(bo);

	ObjSetState(wrk, bo->fetch_objcore, BOS_FINISHED);
	VSLb_ts_busyobj(bo, "BerespBody", W_TIM_real(wrk));
	if (bo->stale_oc != NULL)
		HSH_Kill(bo->stale_oc);
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static int
vbf_objiterator(void *priv, int flush, const void *ptr, ssize_t len)
{
	struct busyobj *bo;
	ssize_t l;
	const uint8_t *ps = ptr;
	uint8_t *pd;

	(void)flush;
	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);

	while (len > 0) {
		l = len;
		if (VFP_GetStorage(bo->vfc, &l, &pd) != VFP_OK)
			return (1);
		if (len < l)
			l = len;
		memcpy(pd, ps, l);
		VFP_Extend(bo->vfc, l);
		ps += l;
		len -= l;
	}
	return (0);
}

static enum fetch_step
vbf_stp_condfetch(struct worker *wrk, struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AZ(vbf_beresp2obj(bo));

	if (ObjHasAttr(bo->wrk, bo->stale_oc, OA_ESIDATA))
		AZ(ObjCopyAttr(bo->wrk, bo->fetch_objcore, bo->stale_oc,
		    OA_ESIDATA));

	AZ(ObjCopyAttr(bo->wrk, bo->fetch_objcore, bo->stale_oc, OA_FLAGS));
	AZ(ObjCopyAttr(bo->wrk, bo->fetch_objcore, bo->stale_oc, OA_GZIPBITS));

	if (bo->do_stream) {
		ObjSetState(wrk, bo->fetch_objcore, BOS_PREP_STREAM);
		HSH_Unbusy(wrk, bo->fetch_objcore);
		ObjSetState(wrk, bo->fetch_objcore, BOS_STREAM);
	}

	if (ObjIterate(wrk, bo->stale_oc, bo, vbf_objiterator, 0))
		(void)VFP_Error(bo->vfc, "Template object failed");

	if (bo->stale_oc->flags & OC_F_FAILED)
		(void)VFP_Error(bo->vfc, "Template object failed");
	if (bo->vfc->failed) {
		VDI_Finish(bo);
		wrk->stats->fetch_failed++;
		return (F_STP_FAIL);
	}
	return (F_STP_FETCHEND);
}

/*--------------------------------------------------------------------
 * Create synth object
 */

static enum fetch_step
vbf_stp_error(struct worker *wrk, struct busyobj *bo)
{
	ssize_t l, ll, o;
	double now;
	uint8_t *ptr;
	struct vsb *synth_body;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);
	AN(bo->fetch_objcore->flags & OC_F_BUSY);
	assert(bo->director_state == DIR_S_NULL);

	wrk->stats->fetch_failed++;

	now = W_TIM_real(wrk);
	VSLb_ts_busyobj(bo, "Error", now);

	if (bo->fetch_objcore->stobj->stevedore != NULL)
		ObjFreeObj(bo->wrk, bo->fetch_objcore);

	if (bo->storage == NULL)
		bo->storage = STV_next();

	// XXX: reset all beresp flags ?

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, SLT_BerespMethod);
	http_PutResponse(bo->beresp, "HTTP/1.1", 503, "Backend fetch failed");
	http_TimeHeader(bo->beresp, "Date: ", now);
	http_SetHeader(bo->beresp, "Server: Varnish");

	bo->fetch_objcore->t_origin = now;
	if (!VTAILQ_EMPTY(&bo->fetch_objcore->objhead->waitinglist)) {
		/*
		 * If there is a waitinglist, it means that there is no
		 * grace-able object, so cache the error return for a
		 * short time, so the waiting list can drain, rather than
		 * each objcore on the waiting list sequentially attempt
		 * to fetch from the backend.
		 */
		bo->fetch_objcore->ttl = 1;
		bo->fetch_objcore->grace = 5;
		bo->fetch_objcore->keep = 5;
	} else {
		bo->fetch_objcore->ttl = 0;
		bo->fetch_objcore->grace = 0;
		bo->fetch_objcore->keep = 0;
	}

	synth_body = VSB_new_auto();
	AN(synth_body);

	VCL_backend_error_method(bo->vcl, wrk, NULL, bo, synth_body);

	AZ(VSB_finish(synth_body));

	if (wrk->handling == VCL_RET_ABANDON || wrk->handling == VCL_RET_FAIL) {
		VSB_destroy(&synth_body);
		return (F_STP_FAIL);
	}

	if (wrk->handling == VCL_RET_RETRY) {
		VSB_destroy(&synth_body);
		if (bo->retries++ < cache_param->max_retries)
			return (F_STP_RETRY);
		VSLb(bo->vsl, SLT_VCL_Error, "Too many retries, failing");
		return (F_STP_FAIL);
	}

	assert(wrk->handling == VCL_RET_DELIVER);

	assert(bo->vfc->wrk == bo->wrk);
	assert(bo->vfc->oc == bo->fetch_objcore);
	assert(bo->vfc->resp == bo->beresp);
	assert(bo->vfc->req == bo->bereq);

	if (vbf_beresp2obj(bo)) {
		(void)VFP_Error(bo->vfc, "Could not get storage");
		VSB_destroy(&synth_body);
		return (F_STP_FAIL);
	}

	ll = VSB_len(synth_body);
	o = 0;
	while (ll > 0) {
		l = ll;
		if (VFP_GetStorage(bo->vfc, &l, &ptr) != VFP_OK)
			break;
		if (l > ll)
			l = ll;
		memcpy(ptr, VSB_data(synth_body) + o, l);
		VFP_Extend(bo->vfc, l);
		ll -= l;
		o += l;
	}
	AZ(ObjSetU64(wrk, bo->fetch_objcore, OA_LEN, o));
	VSB_destroy(&synth_body);
	HSH_Unbusy(wrk, bo->fetch_objcore);
	ObjSetState(wrk, bo->fetch_objcore, BOS_FINISHED);
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_fail(struct worker *wrk, const struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);

	assert(bo->fetch_objcore->boc->state < BOS_FINISHED);
	HSH_Fail(bo->fetch_objcore);
	if (!(bo->fetch_objcore->flags & OC_F_BUSY))
		HSH_Kill(bo->fetch_objcore);
	ObjSetState(wrk, bo->fetch_objcore, BOS_FAILED);
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_done(void)
{
	WRONG("Just plain wrong");
	NEEDLESS(return(F_STP_DONE));
}

static void v_matchproto_(task_func_t)
vbf_fetch_thread(struct worker *wrk, void *priv)
{
	struct busyobj *bo;
	enum fetch_step stp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);

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

	while (stp != F_STP_DONE) {
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		assert(bo->fetch_objcore->boc->refcount >= 1);
		if (bo->fetch_objcore->boc->state < BOS_REQ_DONE)
			AN(bo->req);
		else
			AZ(bo->req);
		switch (stp) {
#define FETCH_STEP(l, U, arg)						\
		case F_STP_##U:						\
			stp = vbf_stp_##l arg;				\
			break;
#include "tbl/steps.h"
		default:
			WRONG("Illegal fetch_step");
		}
	}

	assert(bo->director_state == DIR_S_NULL);

	http_Teardown(bo->bereq);
	http_Teardown(bo->beresp);

	if (bo->fetch_objcore->boc->state == BOS_FINISHED) {
		AZ(bo->fetch_objcore->flags & OC_F_FAILED);
		VSLb(bo->vsl, SLT_Length, "%ju",
		    (uintmax_t)ObjGetLen(bo->wrk, bo->fetch_objcore));
	}
	// AZ(bo->fetch_objcore->boc);	// XXX

	if (bo->stale_oc != NULL)
		(void)HSH_DerefObjCore(wrk, &bo->stale_oc, 0);

	wrk->vsl = NULL;
	HSH_DerefBoc(wrk, bo->fetch_objcore);
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
	const char *how;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(oc->flags & OC_F_BUSY);
	CHECK_OBJ_ORNULL(oldoc, OBJCORE_MAGIC);

	bo = VBO_GetBusyObj(wrk, req);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	boc = HSH_RefBoc(oc);
	CHECK_OBJ_NOTNULL(boc, BOC_MAGIC);

	switch (mode) {
	case VBF_PASS:
		how = "pass";
		bo->do_pass = 1;
		break;
	case VBF_NORMAL:
		how = "fetch";
		break;
	case VBF_BACKGROUND:
		how = "bgfetch";
		bo->is_bgfetch = 1;
		break;
	default:
		WRONG("Wrong fetch mode");
	}

	VSLb(bo->vsl, SLT_Begin, "bereq %u %s", VXID(req->vsl->wid), how);
	VSLb(req->vsl, SLT_Link, "bereq %u %s", VXID(bo->vsl->wid), how);

	THR_SetBusyobj(bo);

	bo->sp = req->sp;
	SES_Ref(bo->sp);

	AN(bo->vcl);

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

	bo->fetch_task.priv = bo;
	bo->fetch_task.func = vbf_fetch_thread;

	if (Pool_Task(wrk->pool, &bo->fetch_task, TASK_QUEUE_BO)) {
		wrk->stats->fetch_no_thread++;
		(void)vbf_stp_fail(req->wrk, bo);
		if (bo->stale_oc != NULL)
			(void)HSH_DerefObjCore(wrk, &bo->stale_oc, 0);
		HSH_DerefBoc(wrk, oc);
		SES_Rel(bo->sp);
		VBO_ReleaseBusyObj(wrk, &bo);
	} else {
		bo = NULL; /* ref transferred to fetch thread */
		if (mode == VBF_BACKGROUND) {
			ObjWaitState(oc, BOS_REQ_DONE);
			(void)VRB_Ignore(req);
		} else {
			ObjWaitState(oc, BOS_STREAM);
			if (oc->boc->state == BOS_FAILED) {
				AN((oc->flags & OC_F_FAILED));
			} else {
				AZ(oc->flags & OC_F_BUSY);
			}
		}
	}
	AZ(bo);
	VSLb_ts_req(req, "Fetch", W_TIM_real(wrk));
	assert(oc->boc == boc);
	HSH_DerefBoc(wrk, oc);
	if (mode == VBF_BACKGROUND)
		(void)HSH_DerefObjCore(wrk, &oc, HSH_RUSH_POLICY);
	THR_SetBusyobj(NULL);
}
