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

#include <math.h>
#include <stdlib.h>

#include "cache.h"
#include "vtim.h"
#include "storage/storage.h"

/*----------------------------------------------------------------------
 * Iterate over the req.body.
 *
 * This can be done exactly once if uncached, and multiple times if the
 * req.body is cached.
 */

int
VRB_Iterate(struct req *req, req_body_iter_f *func, void *priv)
{
	char buf[8192];
	struct storage *st;
	ssize_t l;
	int i;
	struct vfp_ctx *vfc;
	enum vfp_status vfps = VFP_ERROR;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(func);

	switch(req->req_body_status) {
	case REQ_BODY_CACHED:
		VTAILQ_FOREACH(st, &req->body->list, list) {
			i = func(req, priv, st->ptr, st->len);
			if (i)
				return (i);
		}
		return (0);
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
	vfc->vsl = req->vsl;
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
			l = -1;
			break;
		} else if (l > 0) {
			req->req_bodybytes += l;
			req->acct.req_bodybytes += l;
			l = func(req, priv, buf, l);
			if (l) {
				req->req_body_status = REQ_BODY_FAIL;
				break;
			}
		}
	} while (vfps == VFP_OK);
	VFP_Close(vfc);
	VSLb_ts_req(req, "ReqBody", VTIM_real());

	return (l);
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
	struct storage *st;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	while (!VTAILQ_EMPTY(&req->body->list)) {
		st = VTAILQ_FIRST(&req->body->list);
		VTAILQ_REMOVE(&req->body->list, st, list);
		STV_free(st);
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
	struct storage *st;
	ssize_t l, yet;
	struct vfp_ctx *vfc;
	enum vfp_status vfps = VFP_ERROR;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	assert (req->req_step == R_STP_RECV);
	switch(req->req_body_status) {
	case REQ_BODY_CACHED:
		return (0);
	case REQ_BODY_FAIL:
		return (-1);
	case REQ_BODY_NONE:
		return (0);
	case REQ_BODY_WITHOUT_LEN:
	case REQ_BODY_WITH_LEN:
		break;
	default:
		WRONG("Wrong req_body_status in VRB_CacheReqBody()");
	}

	CHECK_OBJ_NOTNULL(req->htc, HTTP_CONN_MAGIC);
	vfc = req->htc->vfc;

	if (req->htc->content_length > maxsize) {
		req->req_body_status = REQ_BODY_FAIL;
		(void)VFP_Error(vfc, "Request body too big to cache");
		return (-1);
	}

	VFP_Setup(vfc);
	vfc->http = req->http;
	vfc->vsl = req->vsl;
	V1F_Setup_Fetch(vfc, req->htc);

	if (VFP_Open(vfc) < 0) {
		req->req_body_status = REQ_BODY_FAIL;
		return (-1);
	}

	yet = req->htc->content_length;
	if (yet < 0)
		yet = 0;
	st = NULL;
	do {
		if (st == NULL) {
			st = STV_alloc_transient(
			    yet ?  yet : cache_param->fetch_chunksize);
			if (st == NULL) {
				req->req_body_status = REQ_BODY_FAIL;
				l = -1;
				break;
			} else {
				VTAILQ_INSERT_TAIL(&req->body->list, st, list);
			}
		}
		l = st->space - st->len;
		vfps = VFP_Suck(vfc, st->ptr + st->len, &l);
		if (vfps == VFP_ERROR) {
			req->req_body_status = REQ_BODY_FAIL;
			l = -1;
			break;
		}
		if (l > 0) {
			req->req_bodybytes += l;
			req->acct.req_bodybytes += l;
			if (yet > 0)
				yet -= l;
			st->len += l;
			if (st->space == st->len)
				st = NULL;
			l = 0;
		}
		if (req->req_bodybytes > maxsize) {
			req->req_body_status = REQ_BODY_FAIL;
			l = -1;
			break;
		}
	} while (vfps == VFP_OK);
	VFP_Close(vfc);

	if (l == 0) {

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
	}
	VSLb_ts_req(req, "ReqBody", VTIM_real());
	return (l);
}
