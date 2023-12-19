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
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "cache_transport.h"

#include "vtim.h"
#include "storage/storage.h"

/*----------------------------------------------------------------------
 * Pull the req.body in via/into a objcore
 *
 * This can be called only once per request
 *
 */

static ssize_t
vrb_pull(struct req *req, ssize_t maxsize, objiterate_f *func, void *priv)
{
	ssize_t l, r = 0, yet;
	struct vrt_ctx ctx[1];
	struct vfp_ctx *vfc;
	uint8_t *ptr;
	enum vfp_status vfps = VFP_ERROR;
	const struct stevedore *stv;
	ssize_t req_bodybytes = 0;
	unsigned flush = OBJ_ITER_FLUSH;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	CHECK_OBJ_NOTNULL(req->htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(req->vfc, VFP_CTX_MAGIC);
	vfc = req->vfc;

	req->body_oc = HSH_Private(req->wrk);
	AN(req->body_oc);

	if (req->storage != NULL)
		stv = req->storage;
	else
		stv = stv_transient;

	req->storage = NULL;

	if (STV_NewObject(req->wrk, req->body_oc, stv, 0) == 0) {
		req->req_body_status = BS_ERROR;
		HSH_DerefBoc(req->wrk, req->body_oc);
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc));
		(void)VFP_Error(vfc, "Object allocation failed:"
		    " Ran out of space in %s", stv->vclname);
		return (-1);
	}

	vfc->oc = req->body_oc;

	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Req2Ctx(ctx, req);

	if (req->vfp_filter_list != NULL &&
	    VCL_StackVFP(vfc, req->vcl, req->vfp_filter_list)) {
		(void)VFP_Error(vfc, "req.body filters failed");
		req->req_body_status = BS_ERROR;
		HSH_DerefBoc(req->wrk, req->body_oc);
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc));
		return (-1);
	}


	if (VFP_Open(ctx, vfc) < 0) {
		req->req_body_status = BS_ERROR;
		HSH_DerefBoc(req->wrk, req->body_oc);
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc));
		return (-1);
	}

	AN(req->htc);
	yet = req->htc->content_length;
	if (yet != 0 && req->want100cont) {
		req->want100cont = 0;
		(void)req->transport->minimal_response(req, 100);
	}
	yet = vmax_t(ssize_t, yet, 0);
	do {
		AZ(vfc->failed);
		if (maxsize >= 0 && req_bodybytes > maxsize) {
			(void)VFP_Error(vfc, "Request body too big to cache");
			break;
		}
		/* NB: only attempt a full allocation when caching. */
		l = maxsize > 0 ? yet : 0;
		if (VFP_GetStorage(vfc, &l, &ptr) != VFP_OK)
			break;
		AZ(vfc->failed);
		AN(ptr);
		AN(l);
		vfps = VFP_Suck(vfc, ptr, &l);
		if (l > 0 && vfps != VFP_ERROR) {
			req_bodybytes += l;
			if (yet >= l)
				yet -= l;
			else if (yet > 0)
				yet = 0;
			if (func != NULL) {
				if (vfps == VFP_END)
					flush |= OBJ_ITER_END;
				r = func(priv, flush, ptr, l);
				if (r)
					break;
			} else {
				ObjExtend(req->wrk, req->body_oc, l,
				    vfps == VFP_END ? 1 : 0);
			}
		}

	} while (vfps == VFP_OK);
	req->acct.req_bodybytes += VFP_Close(vfc);
	VSLb_ts_req(req, "ReqBody", VTIM_real());
	if (func != NULL) {
		HSH_DerefBoc(req->wrk, req->body_oc);
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc));
		if (vfps == VFP_END && r == 0 && (flush & OBJ_ITER_END) == 0)
			r = func(priv, flush | OBJ_ITER_END, NULL, 0);
		if (vfps != VFP_END) {
			req->req_body_status = BS_ERROR;
			if (r == 0)
				r = -1;
		}
		return (r);
	}

	AZ(ObjSetU64(req->wrk, req->body_oc, OA_LEN, req_bodybytes));
	HSH_DerefBoc(req->wrk, req->body_oc);

	if (vfps != VFP_END) {
		req->req_body_status = BS_ERROR;
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc));
		return (-1);
	}

	assert(req_bodybytes >= 0);
	if (req_bodybytes != req->htc->content_length) {
		/* We must update also the "pristine" req.* copy */
		http_Unset(req->http0, H_Content_Length);
		http_Unset(req->http0, H_Transfer_Encoding);
		http_PrintfHeader(req->http0, "Content-Length: %ju",
		    (uintmax_t)req_bodybytes);

		http_Unset(req->http, H_Content_Length);
		http_Unset(req->http, H_Transfer_Encoding);
		http_PrintfHeader(req->http, "Content-Length: %ju",
		    (uintmax_t)req_bodybytes);
	}

	req->req_body_status = BS_CACHED;
	return (req_bodybytes);
}

/*----------------------------------------------------------------------
 * Iterate over the req.body.
 *
 * This can be done exactly once if uncached, and multiple times if the
 * req.body is cached.
 *
 * return length or -1 on error
 */

ssize_t
VRB_Iterate(struct worker *wrk, struct vsl_log *vsl,
    struct req *req, objiterate_f *func, void *priv)
{
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(func);

	if (req->req_body_status == BS_CACHED) {
		AN(req->body_oc);
		if (ObjIterate(wrk, req->body_oc, priv, func, 0))
			return (-1);
		return (0);
	}
	if (req->req_body_status == BS_NONE)
		return (0);
	if (req->req_body_status == BS_TAKEN) {
		VSLb(vsl, SLT_VCL_Error,
		    "Uncached req.body can only be consumed once.");
		return (-1);
	}
	if (req->req_body_status == BS_ERROR) {
		VSLb(vsl, SLT_FetchError,
		    "Had failed reading req.body before.");
		return (-1);
	}
	Lck_Lock(&req->sp->mtx);
	if (req->req_body_status->avail > 0) {
		req->req_body_status = BS_TAKEN;
		i = 0;
	} else
		i = -1;
	Lck_Unlock(&req->sp->mtx);
	if (i) {
		VSLb(vsl, SLT_VCL_Error,
		    "Multiple attempts to access non-cached req.body");
		return (i);
	}
	return (vrb_pull(req, -1, func, priv));
}

/*----------------------------------------------------------------------
 * VRB_Ignore() is a dedicated function, because we might
 * be able to dissuade or terminate its transmission in some protocols.
 *
 * For HTTP1, we do nothing if we are going to close the connection anyway or
 * just iterate it into oblivion.
 */

static int v_matchproto_(objiterate_f)
httpq_req_body_discard(void *priv, unsigned flush, const void *ptr, ssize_t len)
{

	(void)priv;
	(void)flush;
	(void)ptr;
	(void)len;
	return (0);
}

int
VRB_Ignore(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->doclose != SC_NULL)
		return (0);
	if (req->req_body_status->avail > 0)
		(void)VRB_Iterate(req->wrk, req->vsl, req,
		    httpq_req_body_discard, NULL);
	if (req->req_body_status == BS_ERROR)
		req->doclose = SC_RX_BODY;
	return (0);
}

/*----------------------------------------------------------------------
 */

void
VRB_Free(struct req *req)
{
	int r;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->body_oc == NULL)
		return;

	r = HSH_DerefObjCore(req->wrk, &req->body_oc);

	// each busyobj may have gained a reference
	assert (r >= 0);
	assert ((unsigned)r <= req->restarts + 1);
}

/*----------------------------------------------------------------------
 * Cache the req.body if it is smaller than the given size
 *
 * This function must be called before any backend fetches are kicked
 * off to prevent parallelism.
 */

ssize_t
VRB_Cache(struct req *req, ssize_t maxsize)
{
	uint64_t u;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	assert (req->req_step == R_STP_RECV);
	assert(maxsize >= 0);

	/*
	 * We only allow caching to happen the first time through vcl_recv{}
	 * where we know we will have no competition or conflicts for the
	 * updates to req.http.* etc.
	 */
	if (req->restarts > 0 && req->req_body_status != BS_CACHED) {
		VSLb(req->vsl, SLT_VCL_Error,
		    "req.body must be cached before restarts");
		return (-1);
	}

	if (req->req_body_status == BS_CACHED) {
		AZ(ObjGetU64(req->wrk, req->body_oc, OA_LEN, &u));
		return (u);
	}

	if (req->req_body_status->avail <= 0)
		return (req->req_body_status->avail);

	if (req->htc->content_length > maxsize) {
		req->req_body_status = BS_ERROR;
		(void)VFP_Error(req->vfc, "Request body too big to cache");
		return (-1);
	}

	return (vrb_pull(req, maxsize, NULL, NULL));
}
