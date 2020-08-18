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

#include "vrt_obj.h"

#include "vtim.h"
#include "storage/storage.h"
#include "hash/hash_slinger.h"

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
	struct vfp_ctx *vfc;
	uint8_t *ptr;
	enum vfp_status vfps = VFP_ERROR;
	const struct stevedore *stv;
	ssize_t req_bodybytes = 0;

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

	if (STV_NewObject(req->wrk, req->body_oc, stv, 8) == 0) {
		req->req_body_status = BS_ERROR;
		HSH_DerefBoc(req->wrk, req->body_oc);
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc, 0));
		(void)VFP_Error(vfc, "Object allocation failed:"
		    " Ran out of space in %s", stv->vclname);
		return (-1);
	}

	vfc->oc = req->body_oc;

	if (VFP_Open(vfc) < 0) {
		req->req_body_status = BS_ERROR;
		HSH_DerefBoc(req->wrk, req->body_oc);
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc, 0));
		return (-1);
	}

	AN(req->htc);
	yet = req->htc->content_length;
	if (yet != 0 && req->want100cont) {
		req->want100cont = 0;
		(void)req->transport->minimal_response(req, 100);
	}
	if (yet < 0)
		yet = 0;
	do {
		AZ(vfc->failed);
		if (maxsize >= 0 && req_bodybytes > maxsize) {
			(void)VFP_Error(vfc, "Request body too big to cache");
			break;
		}
		l = yet;
		if (VFP_GetStorage(vfc, &l, &ptr) != VFP_OK)
			break;
		AZ(vfc->failed);
		AN(ptr);
		AN(l);
		vfps = VFP_Suck(vfc, ptr, &l);
		if (l > 0 && vfps != VFP_ERROR) {
			req_bodybytes += l;
			req->acct.req_bodybytes += l;
			if (yet >= l)
				yet -= l;
			if (func != NULL) {
				r = func(priv, 1, ptr, l);
				if (r)
					break;
			} else {
				ObjExtend(req->wrk, req->body_oc, l);
			}
		}

	} while (vfps == VFP_OK);
	VFP_Close(vfc);
	VSLb_ts_req(req, "ReqBody", VTIM_real());
	if (func != NULL) {
		HSH_DerefBoc(req->wrk, req->body_oc);
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc, 0));
		if (vfps != VFP_END) {
			req->req_body_status = BS_ERROR;
			if (r == 0)
				r = -1;
		}
		return (r);
	}

	ObjTrimStore(req->wrk, req->body_oc);
	AZ(ObjSetU64(req->wrk, req->body_oc, OA_LEN, req_bodybytes));
	HSH_DerefBoc(req->wrk, req->body_oc);

	if (vfps != VFP_END) {
		req->req_body_status = BS_ERROR;
		AZ(HSH_DerefObjCore(req->wrk, &req->body_oc, 0));
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
 * be able to disuade or terminate its transmission in some protocols.
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

	if (req->doclose)
		return (0);
	if (req->req_body_status->avail > 0)
		(void)VRB_Iterate(req->wrk, req->vsl, req,
		    httpq_req_body_discard, NULL);
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

	r = HSH_DerefObjCore(req->wrk, &req->body_oc, 0);

	// a busyobj may have gained a reference
	assert (r == 0 || r == 1);
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

/*----------------------------------------------------------------------
 * Generic Body Methods
 */

struct http_body *
VRB_Alloc_Body(struct ws *ws)
{
	struct http_body *hb;

	hb = WS_Alloc(ws, sizeof *hb);
	if (hb != NULL) {
		INIT_OBJ(hb, HTTP_BODY_MAGIC);
		hb->ws = ws;
		VTAILQ_INIT(&hb->parts);
	}
	return (hb);
}

void
VRB_Free_Body(struct http_body *hb)
{
	struct http_body_part *hbp, *hbp2;

	CHECK_OBJ_NOTNULL(hb, HTTP_BODY_MAGIC);
	VTAILQ_FOREACH_SAFE(hbp, &hb->parts, list, hbp2) {
		CHECK_OBJ_NOTNULL(hbp, HTTP_BODY_PART_MAGIC);
		CHECK_OBJ_NOTNULL(hbp->methods, HTTP_BODY_PART_METHODS_MAGIC);
		if (hbp->methods->free != NULL)
			hbp->methods->free(hbp->priv);
		VTAILQ_REMOVE(&hb->parts, hbp, list);
	}
}

int
VRB_Copy_To_ObjCore(struct worker *wrk, struct http_body *hb, struct objcore *oc)
{
	struct http_body_part *hbp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(hb, HTTP_BODY_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AZ(oc->boc->len_so_far);
	VTAILQ_FOREACH(hbp, &hb->parts, list) {
		CHECK_OBJ_NOTNULL(hbp, HTTP_BODY_PART_MAGIC);
		CHECK_OBJ_NOTNULL(hbp->methods, HTTP_BODY_PART_METHODS_MAGIC);
		AN(hbp->methods->store);
		if (hbp->methods->store(wrk, oc, hbp->priv) < 0)
			return (-1);
	}
	AZ(ObjSetU64(wrk, oc, OA_LEN, oc->boc->len_so_far));
	return (0);
}

/*--------------------------------------------------------------------*/

static int
vrb_store_strands(struct worker *wrk, struct objcore *oc, void *priv)
{
	int i;
	VCL_STRANDS s;
	ssize_t sz, ln;
	uint8_t *ptr;
	const char *p;

	AN(wrk);
	AN(oc);
	AN(priv);
	s = priv;
	for (i = 0; i < s->n; i++) {
		p = s->p[i];
		if (p == NULL)
			p = "(null)";
		ln = strlen(p);
		sz = ln;
		if (!ObjGetSpace(wrk, oc, &sz, &ptr))
			return (-1);
		AN(ptr);
		xxxassert(sz >= ln);
		memcpy(ptr, p, ln);
		ObjExtend(wrk, oc, ln);
	}
	return (0);
}

static const struct http_body_part_methods vrb_hbpm_strands = {
	0x6f7e9c53,
	vrb_store_strands,
	NULL,
};

static struct strands *
vrb_add_strands(struct http_body *hb, int n)
{

	struct http_body_part *hbp;
	struct strands *s;

	CHECK_OBJ_NOTNULL(hb, HTTP_BODY_MAGIC);
	assert(n > 0);

	s = VRT_AllocStrandsWS(hb->ws, n);
	hbp = WS_Alloc(hb->ws, sizeof *hbp);
	if (s == NULL || hbp == NULL)
		return (NULL);
	INIT_OBJ(hbp, HTTP_BODY_PART_MAGIC);
	hbp->methods = &vrb_hbpm_strands;
	hbp->priv = s;
	VTAILQ_INSERT_TAIL(&hb->parts, hbp, list);
	return (s);
}

void
VRB_Add_Strands(struct http_body *hb, VCL_STRANDS s)
{
	struct strands *s2;
	int i;

	CHECK_OBJ_NOTNULL(hb, HTTP_BODY_MAGIC);
	AN(s);
	s2 = vrb_add_strands(hb, s->n);
	if (s2 != NULL) {
		for (i = 0; i < s->n; i++)
			s2->p[i] = s->p[i];
	}
}

void
VRB_Add_String(struct http_body *hb, const char *str)
{
	struct strands *s;

	CHECK_OBJ_NOTNULL(hb, HTTP_BODY_MAGIC);
	s = vrb_add_strands(hb, 1);
	if (s != NULL)
		s->p[0] = str;
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_synth_page(VRT_CTX, VCL_STRANDS s)
{
	VRB_Add_Strands(ctx->specific, s);
}

/*--------------------------------------------------------------------*/

static VCL_VOID
vrt_l_body(VRT_CTX, enum lbody_e type, const char *str, va_list ap)
{
	const char *p;
	va_list ap2;
	struct strands *s;
	struct http_body *hb;
	int n = 0;

	CAST_OBJ_NOTNULL(hb, ctx->specific, HTTP_BODY_MAGIC);
	assert(type == LBODY_SET || type == LBODY_ADD);
	AN(str);
	if (type == LBODY_SET)
		VRB_Free_Body(ctx->specific);
	va_copy(ap2, ap);
	p = str;
	while (p != vrt_magic_string_end) {
		n++;
		p = va_arg(ap2, const char *);
	}
	va_end(ap2);
	s = vrb_add_strands(hb, n);
	if (s != NULL) {
		n = 0;
		p = str;
		while (p != vrt_magic_string_end) {
			s->p[n++] = p;
			p = va_arg(ap, const char *);
		}
	}
}

#define VRT_BODY_L(which)					\
VCL_VOID							\
VRT_l_##which##_body(VRT_CTX, enum lbody_e type,		\
    const char *str, ...)					\
{								\
	va_list ap;						\
	va_start(ap, str);					\
	vrt_l_body(ctx, type, str, ap);				\
	va_end(ap);						\
}

VRT_BODY_L(beresp)
VRT_BODY_L(resp)
