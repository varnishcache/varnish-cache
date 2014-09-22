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
 */

#include "config.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "vend.h"
#include "hash/hash_slinger.h"
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
	const char *storage_hint;
	double lifetime;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	lifetime = oc->exp.ttl + oc->exp.grace + oc->exp.keep;

	if (bo->uncacheable || lifetime < cache_param->shortlived)
		storage_hint = TRANSIENT_STORAGE;
	else
		storage_hint = bo->storage_hint;

	bo->storage_hint = NULL;

	if (STV_NewObject(bo->fetch_objcore, bo->wrk, storage_hint, l))
		return (1);

	if (storage_hint != NULL && !strcmp(storage_hint, TRANSIENT_STORAGE))
		return (0);

	/*
	 * Try to salvage the transaction by allocating a shortlived object
	 * on Transient storage.
	 */

	if (oc->exp.ttl > cache_param->shortlived)
		oc->exp.ttl = cache_param->shortlived;
	oc->exp.grace = 0.0;
	oc->exp.keep = 0.0;
	return (STV_NewObject(bo->fetch_objcore, bo->wrk,
	    TRANSIENT_STORAGE, l));
}

/*--------------------------------------------------------------------
 * Turn the beresp into a obj
 */

static int
vbf_beresp2obj(struct busyobj *bo)
{
	unsigned l, l2;
	char *b;
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
			l += PRNDUP(varyl);
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
	    bo->uncacheable ? HTTPH_R_PASS : HTTPH_A_INS);
	l += l2;

	if (bo->uncacheable)
		bo->fetch_objcore->flags |= OC_F_PASS;

	if (!vbf_allocobj(bo, l))
		return (-1);

	if (vary != NULL) {
		b = ObjSetattr(bo->wrk, bo->fetch_objcore, OA_VARY, varyl, VSB_data(vary));
		VSB_delete(vary);
	}

	AZ(ObjSetU32(bo->wrk, bo->fetch_objcore, OA_VXID, VXID(bo->vsl->wid)));

	/* for HTTP_Encode() VSLH call */
	bo->beresp->logtag = SLT_ObjMethod;

	/* Filter into object */
	bp = ObjSetattr(bo->wrk, bo->fetch_objcore, OA_HEADERS, l2, NULL);
	AN(bp);
	HTTP_Encode(bo->beresp, bp, l2,
	    bo->uncacheable ? HTTPH_R_PASS : HTTPH_A_INS);

	if (http_GetHdr(bo->beresp, H_Last_Modified, &b))
		AZ(ObjSetDouble(bo->wrk, bo->fetch_objcore, OA_LASTMODIFIED, VTIM_parse(b)));
	else
		AZ(ObjSetDouble(bo->wrk, bo->fetch_objcore, OA_LASTMODIFIED,
		    floor(bo->fetch_objcore->exp.t_origin)));

	return (0);
}

/*--------------------------------------------------------------------
 * Copy req->bereq and release req if not pass fetch
 */

static enum fetch_step
vbf_stp_mkbereq(const struct worker *wrk, struct busyobj *bo)
{
	const char *q;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->req, REQ_MAGIC);

	assert(bo->state == BOS_INVALID);
	AZ(bo->vbc);
	assert(bo->doclose == SC_NULL);
	AZ(bo->storage_hint);

	HTTP_Setup(bo->bereq0, bo->ws, bo->vsl, SLT_BereqMethod);
	http_FilterReq(bo->bereq0, bo->req->http,
	    bo->do_pass ? HTTPH_R_PASS : HTTPH_R_FETCH);

	if (!bo->do_pass) {
		http_ForceField(bo->bereq0, HTTP_HDR_METHOD, "GET");
		http_ForceField(bo->bereq0, HTTP_HDR_PROTO, "HTTP/1.1");
		if (cache_param->http_gzip_support)
			http_ForceHeader(bo->bereq0, H_Accept_Encoding, "gzip");
		AN(bo->req);
		bo->req = NULL;
		http_CopyHome(bo->bereq0);
	}

	if (bo->ims_oc != NULL) {
		q = HTTP_GetHdrPack(bo->wrk, bo->ims_oc, H_Last_Modified);
		if (q != NULL)
			http_PrintfHeader(bo->bereq0,
			    "If-Modified-Since: %s", q);
		q = HTTP_GetHdrPack(bo->wrk, bo->ims_oc, H_ETag);
		if (q != NULL)
			http_PrintfHeader(bo->bereq0,
			    "If-None-Match: %s", q);
	}

	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, SLT_BereqMethod);
	bo->ws_bo = WS_Snapshot(bo->ws);
	HTTP_Copy(bo->bereq, bo->bereq0);

	VBO_setstate(bo, BOS_REQ_DONE);
	return (F_STP_STARTFETCH);
}

/*--------------------------------------------------------------------
 * Start a new VSL transaction and try again
 */

static enum fetch_step
vbf_stp_retry(struct worker *wrk, struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	VSLb_ts_busyobj(bo, "Retry", W_TIM_real(wrk));

	// XXX: BereqEnd + BereqAcct ?
	VSL_ChgId(bo->vsl, "bereq", "retry", VXID_Get(wrk, VSL_BACKENDMARKER));
	VSLb_ts_busyobj(bo, "Start", bo->t_prev);

	return (F_STP_STARTFETCH);
}

/*--------------------------------------------------------------------
 * Setup bereq from bereq0, run vcl_backend_fetch
 */

static enum fetch_step
vbf_stp_startfetch(struct worker *wrk, struct busyobj *bo)
{
	int i, do_ims;
	double now;
	char time_str[VTIM_FORMAT_SIZE];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AZ(bo->vbc);
	assert(bo->doclose == SC_NULL);
	AZ(bo->storage_hint);

	if (bo->do_pass)
		AN(bo->req);
	else
		AZ(bo->req);

	http_PrintfHeader(bo->bereq, "X-Varnish: %u", VXID(bo->vsl->wid));

	VCL_backend_fetch_method(bo->vcl, wrk, NULL, bo, bo->bereq->ws);

	bo->uncacheable = bo->do_pass;
	if (wrk->handling == VCL_RET_ABANDON)
		return (F_STP_FAIL);

	assert (wrk->handling == VCL_RET_FETCH);

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, SLT_BerespMethod);

	assert(bo->state <= BOS_REQ_DONE);

	i = VDI_GetHdr(wrk, bo);

	now = W_TIM_real(wrk);
	VSLb_ts_busyobj(bo, "Beresp", now);

	if (i) {
		AZ(bo->vbc);
		return (F_STP_ERROR);
	}

	AN(bo->vbc);
	http_VSL_log(bo->beresp);

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
		VTIM_format(now, time_str);
		http_PrintfHeader(bo->beresp, "Date: %s", time_str);
	}

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
	 */
	if (!strcasecmp(http_GetMethod(bo->bereq), "head")) {
		/*
		 * A HEAD request can never have a body in the reply,
		 * no matter what the headers might say.
		 * [RFC2516 4.3 p33]
		 */
		wrk->stats->fetch_head++;
		bo->htc->body_status = BS_NONE;
	} else if (http_GetStatus(bo->beresp) <= 199) {
		/*
		 * 1xx responses never have a body.
		 * [RFC2616 4.3 p33]
		 * ... but we should never see them.
		 */
		wrk->stats->fetch_1xx++;
		bo->htc->body_status = BS_ERROR;
	} else if (http_IsStatus(bo->beresp, 204)) {
		/*
		 * 204 is "No Content", obviously don't expect a body.
		 * [RFC2616 10.2.5 p60]
		 */
		wrk->stats->fetch_204++;
		bo->htc->body_status = BS_NONE;
	} else if (http_IsStatus(bo->beresp, 304)) {
		/*
		 * 304 is "Not Modified" it has no body.
		 * [RFC2616 10.3.5 p63]
		 */
		wrk->stats->fetch_304++;
		bo->htc->body_status = BS_NONE;
	} else if (bo->htc->body_status == BS_CHUNKED) {
		wrk->stats->fetch_chunked++;
	} else if (bo->htc->body_status == BS_LENGTH) {
		assert(bo->htc->content_length > 0);
		wrk->stats->fetch_length++;
	} else if (bo->htc->body_status == BS_EOF) {
		wrk->stats->fetch_eof++;
	} else if (bo->htc->body_status == BS_ERROR) {
		wrk->stats->fetch_bad++;
	} else if (bo->htc->body_status == BS_NONE) {
		wrk->stats->fetch_none++;
	} else {
		WRONG("wrong bodystatus");
	}

	if (bo->htc->body_status == BS_ERROR) {
		AN (bo->vbc);
		VDI_CloseFd(&bo->vbc, &bo->acct);
		VSLb(bo->vsl, SLT_Error, "Body cannot be fetched");
		return (F_STP_ERROR);
	}

	/*
	 * What does RFC2616 think about TTL ?
	 */
	EXP_Clr(&bo->fetch_objcore->exp);
	RFC2616_Ttl(bo, now);

	/* private objects have negative TTL */
	if (bo->fetch_objcore->flags & OC_F_PRIVATE)
		bo->fetch_objcore->exp.ttl = -1.;

	AZ(bo->do_esi);

	if (bo->ims_oc != NULL && http_IsStatus(bo->beresp, 304)) {
		if (ObjCheckFlag(bo->wrk, bo->ims_oc, OF_CHGGZIP)) {
			/*
			 * If we changed the gzip status of the object
			 * the stored Content_Encoding controls we
			 * must weaken any new ETag we get.
			 */
			http_Unset(bo->beresp, H_Content_Encoding);
			RFC2616_Weaken_Etag(bo->beresp);
		}
		HTTP_Merge(bo->wrk, bo->ims_oc, bo->beresp);
		assert(http_IsStatus(bo->beresp, 200));
		do_ims = 1;
	} else
		do_ims = 0;

	VFP_Setup(bo->vfc);
	bo->vfc->bo = bo;
	bo->vfc->oc = bo->fetch_objcore;
	bo->vfc->wrk = bo->wrk;
	bo->vfc->http = bo->beresp;
	bo->vfc->esi_req = bo->bereq;

	VCL_backend_response_method(bo->vcl, wrk, NULL, bo, bo->beresp->ws);

	if (wrk->handling == VCL_RET_ABANDON)
		return (F_STP_FAIL);

	if (wrk->handling == VCL_RET_RETRY) {
		AN (bo->vbc);
		VDI_CloseFd(&bo->vbc, &bo->acct);
		bo->doclose = SC_NULL;
		bo->retries++;
		if (bo->retries <= cache_param->max_retries)
			return (F_STP_RETRY);
		VSLb(bo->vsl, SLT_VCL_Error,
		    "Too many retries, delivering 503");
		return (F_STP_ERROR);
	}

	assert(bo->state == BOS_REQ_DONE);

	if (bo->do_esi)
		bo->do_stream = 0;
	if (bo->do_pass || bo->uncacheable)
		bo->fetch_objcore->flags |= OC_F_PASS;

	assert(wrk->handling == VCL_RET_DELIVER);

	return (do_ims ? F_STP_CONDFETCH : F_STP_FETCH);
}

/*--------------------------------------------------------------------
 */

static void
vbf_fetch_body_helper(struct busyobj *bo)
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
		if (bo->abandon) {
			/*
			 * A pass object and delivery was terminted
			 * We don't fail the fetch, in order for hit-for-pass
			 * objects to be created.
			 */
			AN(vfc->oc->flags & OC_F_PASS);
			VSLb(vfc->wrk->vsl, SLT_FetchError,
			    "Pass delivery abandoned");
			vfps = VFP_END;
			bo->doclose = SC_RX_BODY;
			break;
		}
		AZ(vfc->failed);
		l = est;
		assert(l >= 0);
		if (VFP_GetStorage(vfc, &l, &ptr) != VFP_OK) {
			bo->doclose = SC_RX_BODY;
			break;
		}

		AZ(vfc->failed);
		vfps = VFP_Suck(vfc, ptr, &l);
		if (l > 0 && vfps != VFP_ERROR) {
			bo->acct.beresp_bodybytes += l;
			VBO_extend(bo, l);
			if (est >= l)
				est -= l;
			else
				est = 0;
		}
	} while (vfps == VFP_OK);

	if (vfps == VFP_ERROR) {
		AN(vfc->failed);
		(void)VFP_Error(vfc, "Fetch Pipeline failed to process");
		bo->doclose = SC_RX_BODY;
	}

	VFP_Close(vfc);

	if (!bo->do_stream)
		ObjTrimStore(bo->wrk, vfc->oc);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_fetch(struct worker *wrk, struct busyobj *bo)
{
	char *p;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);

	assert(wrk->handling == VCL_RET_DELIVER);

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

	/* We wont gzip unless it is ungziped */
	if (bo->do_gzip && !bo->is_gunzip)
		bo->do_gzip = 0;

	AN(bo->vbc);

	/* But we can't do both at the same time */
	assert(bo->do_gzip == 0 || bo->do_gunzip == 0);

	if (bo->do_gunzip || (bo->is_gzip && bo->do_esi))
		(void)VFP_Push(bo->vfc, &vfp_gunzip, 1);

	if (bo->do_esi && bo->do_gzip) {
		(void)VFP_Push(bo->vfc, &vfp_esi_gzip, 1);
	} else if (bo->do_esi && bo->is_gzip && !bo->do_gunzip) {
		(void)VFP_Push(bo->vfc, &vfp_esi_gzip, 1);
	} else if (bo->do_esi) {
		(void)VFP_Push(bo->vfc, &vfp_esi, 1);
	} else if (bo->do_gzip) {
		(void)VFP_Push(bo->vfc, &vfp_gzip, 1);
	} else if (bo->is_gzip && !bo->do_gunzip) {
		(void)VFP_Push(bo->vfc, &vfp_testgunzip, 1);
	}

	if (bo->fetch_objcore->flags & OC_F_PRIVATE)
		AN(bo->uncacheable);

	/* No reason to try streaming a non-existing body */
	if (bo->htc->body_status == BS_NONE)
		bo->do_stream = 0;

	if (VFP_Open(bo->vfc)) {
		(void)VFP_Error(bo->vfc, "Fetch Pipeline failed to open");
		bo->doclose = SC_RX_BODY;
		return (F_STP_ERROR);
	}

	if (vbf_beresp2obj(bo)) {
		(void)VFP_Error(bo->vfc, "Could not get storage");
		bo->doclose = SC_RX_BODY;
		return (F_STP_ERROR);
	}

	assert(WRW_IsReleased(wrk));


	if (bo->do_gzip || (bo->is_gzip && !bo->do_gunzip))
		ObjSetFlag(bo->wrk, bo->fetch_objcore, OF_GZIPED, 1);

	if (bo->do_gzip || bo->do_gunzip)
		ObjSetFlag(bo->wrk, bo->fetch_objcore, OF_CHGGZIP, 1);

	if (http_IsStatus(bo->beresp, 200) && (
	    http_GetHdr(bo->beresp, H_Last_Modified, &p) ||
	    http_GetHdr(bo->beresp, H_ETag, &p)))
		ObjSetFlag(bo->wrk, bo->fetch_objcore, OF_IMSCAND, 1);

	if (bo->htc->body_status != BS_NONE)
		V1F_Setup_Fetch(bo->vfc, bo->htc);

	/*
	 * Ready to fetch the body
	 */

	assert(bo->refcount >= 1);

	assert (bo->state == BOS_REQ_DONE);

	if (bo->do_stream) {
		HSH_Unbusy(wrk, bo->fetch_objcore);
		VBO_setstate(bo, BOS_STREAM);
	}

	VSLb(bo->vsl, SLT_Fetch_Body, "%u %s %s",
	    bo->htc->body_status, body_status_2str(bo->htc->body_status),
	    bo->do_stream ? "stream" : "-");

	if (bo->htc->body_status != BS_NONE) {
		assert(bo->htc->body_status != BS_ERROR);
		vbf_fetch_body_helper(bo);
	}

	if (bo->vfc->failed && !bo->do_stream) {
		assert(bo->state < BOS_STREAM);
		ObjFreeObj(bo->wrk, bo->fetch_objcore);
		return (F_STP_ERROR);
	}

	if (bo->vfc->failed)
		return (F_STP_FAIL);

	if (bo->do_stream)
		assert(bo->state == BOS_STREAM);
	else {
		assert(bo->state == BOS_REQ_DONE);
		HSH_Unbusy(wrk, bo->fetch_objcore);
	}

	/* Recycle the backend connection before setting BOS_FINISHED to
	   give predictable backend reuse behavior for varnishtest */
	if (bo->vbc != NULL && bo->doclose == SC_NULL) {
		VDI_RecycleFd(&bo->vbc, &bo->acct);
		AZ(bo->vbc);
	}

	VBO_setstate(bo, BOS_FINISHED);
	VSLb_ts_busyobj(bo, "BerespBody", W_TIM_real(wrk));
	if (bo->ims_oc != NULL)
		EXP_Rearm(bo->ims_oc, bo->ims_oc->exp.t_origin, 0, 0, 0);
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_condfetch(struct worker *wrk, struct busyobj *bo)
{
	void *oi;
	void *sp;
	ssize_t sl, al, l;
	uint8_t *ptr;
	uint64_t ol;
	enum objiter_status ois;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AZ(vbf_beresp2obj(bo));

	if (ObjGetattr(bo->wrk, bo->ims_oc, OA_ESIDATA, NULL) != NULL)
		AZ(ObjCopyAttr(bo->wrk, bo->fetch_objcore, bo->ims_oc,
		    OA_ESIDATA));

	AZ(ObjCopyAttr(bo->wrk, bo->fetch_objcore, bo->ims_oc, OA_FLAGS));
	AZ(ObjCopyAttr(bo->wrk, bo->fetch_objcore, bo->ims_oc, OA_GZIPBITS));

	if (bo->do_stream) {
		HSH_Unbusy(wrk, bo->fetch_objcore);
		VBO_setstate(bo, BOS_STREAM);
	}

	al = 0;
	ol = ObjGetLen(bo->wrk, bo->ims_oc);
	oi = ObjIterBegin(wrk, bo->ims_oc);
	do {
		ois = ObjIter(bo->ims_oc, oi, &sp, &sl);
		while (sl > 0) {
			l = ol - al;
			if (VFP_GetStorage(bo->vfc, &l, &ptr) != VFP_OK)
				break;
			if (sl < l)
				l = sl;
			memcpy(ptr, sp, l);
			VBO_extend(bo, l);
			al += l;
			sp = (char *)sp + l;
			sl -= l;
		}
	} while (!bo->vfc->failed && (ois == OIS_DATA || ois == OIS_STREAM));
	ObjIterEnd(bo->ims_oc, &oi);
	if (bo->vfc->failed)
		return (F_STP_FAIL);

	if (!bo->do_stream)
		HSH_Unbusy(wrk, bo->fetch_objcore);

	assert(al == ol);
	assert(ObjGetLen(bo->wrk, bo->fetch_objcore) == al);
	EXP_Rearm(bo->ims_oc, bo->ims_oc->exp.t_origin, 0, 0, 0);

	/* Recycle the backend connection before setting BOS_FINISHED to
	   give predictable backend reuse behavior for varnishtest */
	if (bo->vbc != NULL && bo->doclose == SC_NULL) {
		VDI_RecycleFd(&bo->vbc, &bo->acct);
		AZ(bo->vbc);
	}

	VBO_setstate(bo, BOS_FINISHED);
	VSLb_ts_busyobj(bo, "BerespBody", W_TIM_real(wrk));
	return (F_STP_DONE);
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
	char time_str[VTIM_FORMAT_SIZE];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	now = W_TIM_real(wrk);
	VSLb_ts_busyobj(bo, "Error", now);

	AN(bo->fetch_objcore->flags & OC_F_BUSY);

	AZ(bo->synth_body);
	bo->synth_body = VSB_new_auto();
	AN(bo->synth_body);

	// XXX: reset all beresp flags ?

	HTTP_Setup(bo->beresp, bo->ws, bo->vsl, SLT_BerespMethod);
	http_PutResponse(bo->beresp, "HTTP/1.1", 503, "Backend fetch failed");
	VTIM_format(now, time_str);
	http_PrintfHeader(bo->beresp, "Date: %s", time_str);
	http_SetHeader(bo->beresp, "Server: Varnish");

	bo->fetch_objcore->exp.t_origin = bo->t_prev;
	bo->fetch_objcore->exp.ttl = 0;
	bo->fetch_objcore->exp.grace = 0;
	bo->fetch_objcore->exp.keep = 0;

	VCL_backend_error_method(bo->vcl, wrk, NULL, bo, bo->bereq->ws);

	AZ(VSB_finish(bo->synth_body));

	if (wrk->handling == VCL_RET_RETRY) {
		VSB_delete(bo->synth_body);
		bo->synth_body = NULL;
		if (bo->retries++ < cache_param->max_retries)
			return (F_STP_RETRY);
		return (F_STP_FAIL);
	}

	assert(wrk->handling == VCL_RET_DELIVER);

	VFP_Setup(bo->vfc);
	bo->vfc->bo = bo;
	bo->vfc->wrk = bo->wrk;
	bo->vfc->oc = bo->fetch_objcore;
	bo->vfc->http = bo->beresp;
	bo->vfc->esi_req = bo->bereq;

	if (vbf_beresp2obj(bo))
		return (F_STP_FAIL);

	ll = VSB_len(bo->synth_body);
	o = 0;
	while (ll > 0) {
		l = ll;
		if (VFP_GetStorage(bo->vfc, &l, &ptr) != VFP_OK)
			break;
		memcpy(ptr, VSB_data(bo->synth_body) + o, l);
		VBO_extend(bo, l);
		ll -= l;
		o += l;
	}
	VSB_delete(bo->synth_body);
	bo->synth_body = NULL;

	HSH_Unbusy(wrk, bo->fetch_objcore);
	VBO_setstate(bo, BOS_FINISHED);
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_fail(struct worker *wrk, struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);

	assert(bo->state < BOS_FINISHED);
	HSH_Fail(bo->fetch_objcore);
	if (bo->fetch_objcore->exp_flags & OC_EF_EXP) {
		/* Already unbusied - expire it */
		AN(bo->fetch_objcore);
		EXP_Rearm(bo->fetch_objcore,
		    bo->fetch_objcore->exp.t_origin, 0, 0, 0);
	}
	wrk->stats->fetch_failed++;
	VBO_setstate(bo, BOS_FAILED);
	return (F_STP_DONE);
}

/*--------------------------------------------------------------------
 */

static enum fetch_step
vbf_stp_done(void)
{
	WRONG("Just plain wrong");
	return (F_STP_DONE);
}

static void
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
	assert(bo->doclose == SC_NULL);
	assert(isnan(bo->t_first));
	assert(isnan(bo->t_prev));
	VSLb_ts_busyobj(bo, "Start", W_TIM_real(wrk));

	bo->wrk = wrk;
	wrk->vsl = bo->vsl;

	while (stp != F_STP_DONE) {
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		assert(bo->refcount >= 1);
		switch(stp) {
#define FETCH_STEP(l, U, arg)						\
		case F_STP_##U:						\
			stp = vbf_stp_##l arg;				\
			break;
#include "tbl/steps.h"
#undef FETCH_STEP
		default:
			WRONG("Illegal fetch_step");
		}
	}
	assert(WRW_IsReleased(wrk));

	if (bo->vbc != NULL) {
		if (bo->doclose != SC_NULL)
			VDI_CloseFd(&bo->vbc, &bo->acct);
		else
			VDI_RecycleFd(&bo->vbc, &bo->acct);
		AZ(bo->vbc);
	}

	http_Teardown(bo->bereq);
	http_Teardown(bo->beresp);

	if (bo->state == BOS_FINISHED) {
		AZ(bo->fetch_objcore->flags & OC_F_FAILED);
		HSH_Complete(bo->fetch_objcore);
		VSLb(bo->vsl, SLT_Length, "%ju",
		    (uintmax_t)ObjGetLen(bo->wrk, bo->fetch_objcore));
	}
	AZ(bo->fetch_objcore->busyobj);

	if (bo->ims_oc != NULL)
		(void)HSH_DerefObjCore(wrk, &bo->ims_oc);


	wrk->vsl = NULL;
	VBO_DerefBusyObj(wrk, &bo);
	THR_SetBusyobj(NULL);
}

/*--------------------------------------------------------------------
 */

void
VBF_Fetch(struct worker *wrk, struct req *req, struct objcore *oc,
    struct objcore *oldoc, enum vbf_fetch_mode_e mode)
{
	struct busyobj *bo;
	const char *how;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_ORNULL(oldoc, OBJCORE_MAGIC);


	switch(mode) {
	case VBF_PASS:		how = "pass"; break;
	case VBF_NORMAL:	how = "fetch"; break;
	case VBF_BACKGROUND:	how = "bgfetch"; break;
	default:		WRONG("Wrong fetch mode");
	}

	bo = VBO_GetBusyObj(wrk, req);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	VSLb(bo->vsl, SLT_Begin, "bereq %u %s", VXID(req->vsl->wid), how);
	VSLb(req->vsl, SLT_Link, "bereq %u %s", VXID(bo->vsl->wid), how);

	THR_SetBusyobj(bo);

	bo->refcount = 2;

	oc->busyobj = bo;

	CHECK_OBJ_NOTNULL(bo->vcl, VCL_CONF_MAGIC);

	if (mode == VBF_PASS)
		bo->do_pass = 1;

	bo->vary = req->vary_b;
	req->vary_b = NULL;

	if (mode != VBF_BACKGROUND)
		HSH_Ref(oc);
	bo->fetch_objcore = oc;

	AZ(bo->ims_oc);
	if (oldoc != NULL && ObjCheckFlag(req->wrk, oldoc, OF_IMSCAND)) {
		assert(oldoc->refcnt > 0);
		HSH_Ref(oldoc);
		bo->ims_oc = oldoc;
	}

	AZ(bo->req);
	bo->req = req;

	bo->fetch_task.priv = bo;
	bo->fetch_task.func = vbf_fetch_thread;

	if (Pool_Task(wrk->pool, &bo->fetch_task, POOL_QUEUE_FRONT))
		vbf_fetch_thread(wrk, bo);
	if (mode == VBF_BACKGROUND) {
		VBO_waitstate(bo, BOS_REQ_DONE);
	} else {
		VBO_waitstate(bo, BOS_STREAM);
		if (bo->state == BOS_FAILED) {
			AN((oc->flags & OC_F_FAILED));
		} else {
			AZ(bo->fetch_objcore->flags & OC_F_BUSY);
		}
	}
	VSLb_ts_req(req, "Fetch", W_TIM_real(wrk));
	THR_SetBusyobj(NULL);
	VBO_DerefBusyObj(wrk, &bo);
}
