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
 */

#include "config.h"

#include <stdlib.h>

#include "cache.h"
#include "cache_filter.h"
#include "vtim.h"
#include "hash/hash_slinger.h"

/*----------------------------------------------------------------------
 * Iterate over the req.body.
 *
 * This can be done exactly once if uncached, and multiple times if the
 * req.body is cached.
 *
 * return length or -1 on error
 */

ssize_t
VRB_Iterate(struct req *req, req_body_iter_f *func, void *priv)
{
	char buf[8192];
	ssize_t l, ll = 0;
	void *p;
	int i;
	struct vfp_ctx *vfc;
	enum vfp_status vfps = VFP_ERROR;
	void *oi;
	enum objiter_status ois;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(func);

	switch(req->req_body_status) {
	case REQ_BODY_CACHED:
		oi = ObjIterBegin(req->wrk, req->body_oc);
		AN(oi);
		do {
			ois = ObjIter(req->body_oc, oi, &p, &l);
			ll += l;
			if (l > 0 && func(req, priv, p, l))
				break;
		} while (ois == OIS_DATA);
		ObjIterEnd(req->body_oc, &oi);
		return (ois == OIS_DONE ? ll : -1);
	case REQ_BODY_NONE:
		return (0);
	case REQ_BODY_WITH_LEN:
	case REQ_BODY_WITHOUT_LEN:
		break;
	case REQ_BODY_TAKEN:
		VSLb(req->vsl, SLT_VCL_Error,
		    "Uncached req.body can only be consumed once.");
		return (-1);
	case REQ_BODY_FAIL:
		VSLb(req->vsl, SLT_FetchError,
		    "Had failed reading req.body before.");
		return (-1);
	default:
		WRONG("Wrong req_body_status in VRB_IterateReqBody()");
	}
	Lck_Lock(&req->sp->mtx);
	if (req->req_body_status == REQ_BODY_WITH_LEN ||
	    req->req_body_status == REQ_BODY_WITHOUT_LEN) {
		req->req_body_status = REQ_BODY_TAKEN;
		i = 0;
	} else
		i = -1;
	Lck_Unlock(&req->sp->mtx);
	if (i) {
		VSLb(req->vsl, SLT_VCL_Error,
		    "Multiple attempts to access non-cached req.body");
		return (i);
	}

	CHECK_OBJ_NOTNULL(req->htc, HTTP_CONN_MAGIC);
	vfc = req->htc->vfc;
	VFP_Setup(vfc);
	vfc->http = req->http;
	vfc->wrk = req->wrk;
	V1F_Setup_Fetch(vfc, req->htc);
	if (VFP_Open(vfc) < 0) {
		VSLb(req->vsl, SLT_FetchError, "Could not open Fetch Pipeline");
		return (-1);
	}

	do {
		l = sizeof buf;
		vfps = VFP_Suck(vfc, buf, &l);
		if (vfps == VFP_ERROR) {
			req->req_body_status = REQ_BODY_FAIL;
			ll = -1;
			break;
		} else if (l > 0) {
			req->req_bodybytes += l;
			req->acct.req_bodybytes += l;
			ll += l;
			l = func(req, priv, buf, l);
			if (l) {
				req->req_body_status = REQ_BODY_FAIL;
				ll = -1;
				break;
			}
		}
	} while (vfps == VFP_OK);
	VFP_Close(vfc);
	VSLb_ts_req(req, "ReqBody", VTIM_real());

	return (ll);
}

/*----------------------------------------------------------------------
 * DiscardReqBody() is a dedicated function, because we might
 * be able to disuade or terminate its transmission in some protocols.
 * For HTTP1 we have no such luck, and we just iterate it into oblivion.
 */

static int __match_proto__(req_body_iter_f)
httpq_req_body_discard(struct req *req, void *priv, void *ptr, size_t len)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	(void)priv;
	(void)ptr;
	(void)len;
	return (0);
}

int
VRB_Ignore(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->req_body_status == REQ_BODY_WITH_LEN ||
	    req->req_body_status == REQ_BODY_WITHOUT_LEN)
		(void)VRB_Iterate(req, httpq_req_body_discard, NULL);
	return(0);
}

/*----------------------------------------------------------------------
 */

void
VRB_Free(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->body_oc != NULL) {
		ObjFreeObj(req->wrk,req->body_oc);
		FREE_OBJ(req->body_oc);
		req->body_oc = NULL;
	}
}

/*----------------------------------------------------------------------
 * Cache the req.body if it is smaller than the given size
 *
 * This function must be called before any backend fetches are kicked
 * off to prevent parallelism.
 */

int
VRB_Cache(struct req *req, ssize_t maxsize)
{
	ssize_t l, yet;
	struct vfp_ctx *vfc;
	uint8_t *ptr;
	enum vfp_status vfps = VFP_ERROR;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	assert (req->req_step == R_STP_RECV);
	switch(req->req_body_status) {
	case REQ_BODY_CACHED:
		return (req->req_bodybytes);
	case REQ_BODY_FAIL:
		return (-1);
	case REQ_BODY_NONE:
		return (0);
	case REQ_BODY_WITHOUT_LEN:
	case REQ_BODY_WITH_LEN:
		break;
	default:
		WRONG("Wrong req_body_status in VRB_Cache()");
	}

	CHECK_OBJ_NOTNULL(req->htc, HTTP_CONN_MAGIC);
	vfc = req->htc->vfc;

	if (req->htc->content_length > maxsize) {
		req->req_body_status = REQ_BODY_FAIL;
		(void)VFP_Error(vfc, "Request body too big to cache");
		return (-1);
	}

	req->body_oc = HSH_NewObjCore(req->wrk);
	AN(req->body_oc);
	XXXAN(STV_NewObject(req->body_oc, req->wrk, TRANSIENT_STORAGE, 8));

	VFP_Setup(vfc);
	vfc->http = req->http;
	vfc->wrk = req->wrk;
	vfc->oc = req->body_oc;
	V1F_Setup_Fetch(vfc, req->htc);

	if (VFP_Open(vfc) < 0) {
		req->req_body_status = REQ_BODY_FAIL;
		return (-1);
	}

	AN(req->htc);
	yet = req->htc->content_length;
	if (yet < 0)
		yet = 0;
	do {
		AZ(vfc->failed);
		l = yet;
		if (VFP_GetStorage(vfc, &l, &ptr) != VFP_OK)
			break;
		AZ(vfc->failed);
		AN(ptr);
		AN(l);
		vfps = VFP_Suck(vfc, ptr, &l);
		if (l > 0 && vfps != VFP_ERROR) {
			req->req_bodybytes += l;
			req->acct.req_bodybytes += l;
			if (yet >= l)
				yet -= l;
			ObjExtend(req->wrk, req->body_oc, l);
		}

	} while (vfps == VFP_OK);
	VFP_Close(vfc);

	if (vfps == VFP_END) {

		if (req->req_bodybytes != req->htc->content_length) {
			/* We must update also the "pristine" req.* copy */
			http_Unset(req->http0, H_Content_Length);
			http_Unset(req->http0, H_Transfer_Encoding);
			http_PrintfHeader(req->http0, "Content-Length: %ju",
			    (uintmax_t)req->req_bodybytes);

			http_Unset(req->http, H_Content_Length);
			http_Unset(req->http, H_Transfer_Encoding);
			http_PrintfHeader(req->http, "Content-Length: %ju",
			    (uintmax_t)req->req_bodybytes);
		}

		req->req_body_status = REQ_BODY_CACHED;
	} else {
		req->req_body_status = REQ_BODY_FAIL;
	}
	VSLb_ts_req(req, "ReqBody", VTIM_real());
	return (vfps == VFP_END ? req->req_bodybytes : -1);
}
