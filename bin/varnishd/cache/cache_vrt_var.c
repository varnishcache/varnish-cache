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
 *
 * Runtime support for compiled VCL programs
 */
#include "config.h"

#include <stdio.h>

#include "cache_varnishd.h"
#include "common/heritage.h"

#include "vcl.h"

#include "vrt_obj.h"

static char vrt_hostname[255] = "";

/*--------------------------------------------------------------------
 * VRT variables relating to first line of HTTP/1.1 req/resp
 */

static void
vrt_do_string(VRT_CTX, struct http *hp, int fld,
    const char *err, const char *p, va_list ap)
{
	const char *b;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	b = VRT_String(hp->ws, NULL, p, ap);
	if (b == NULL) {
		VRT_fail(ctx, "Workspace overflow (%s)", err);
		WS_MarkOverflow(hp->ws);
		return;
	}
	if (*b == '\0') {
		VRT_fail(ctx, "Setting %s to empty string", err);
		return;
	}
	http_SetH(hp, fld, b);
}

#define VRT_HDR_L(obj, hdr, fld)					\
VCL_VOID								\
VRT_l_##obj##_##hdr(VRT_CTX, const char *p, ...)			\
{									\
	va_list ap;							\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	va_start(ap, p);						\
	vrt_do_string(ctx, ctx->http_##obj, fld, #obj "." #hdr, p, ap);	\
	va_end(ap);							\
}

#define VRT_HDR_R(obj, hdr, fld)					\
VCL_STRING								\
VRT_r_##obj##_##hdr(VRT_CTX)						\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	return (ctx->http_##obj->hd[fld].b);				\
}

#define VRT_HDR_LR(obj, hdr, fld)					\
	VRT_HDR_L(obj, hdr, fld)					\
	VRT_HDR_R(obj, hdr, fld)

#define VRT_STATUS_L(obj)						\
VCL_VOID								\
VRT_l_##obj##_status(VRT_CTX, VCL_INT num)				\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	if (num < 0)							\
		VRT_fail(ctx, "%s.status (%jd) is negative",		\
		    #obj, (intmax_t)num);				\
	else if (num > 65535)						\
		VRT_fail(ctx, "%s.status (%jd) > 65535",		\
		    #obj, (intmax_t)num);				\
	else if ((num % 1000) < 100)					\
		VRT_fail(ctx, "illegal %s.status (%jd) (..0##)",	\
		    #obj, (intmax_t)num);				\
	else								\
		http_SetStatus(ctx->http_##obj, (uint16_t)num);		\
}

#define VRT_STATUS_R(obj)						\
VCL_INT									\
VRT_r_##obj##_status(VRT_CTX)						\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	return (ctx->http_##obj->status);				\
}

VRT_HDR_LR(req,    method,	HTTP_HDR_METHOD)
VRT_HDR_LR(req,    url,		HTTP_HDR_URL)
VRT_HDR_LR(req,    proto,	HTTP_HDR_PROTO)

VRT_HDR_R(req_top,    method,	HTTP_HDR_METHOD)
VRT_HDR_R(req_top,    url,	HTTP_HDR_URL)
VRT_HDR_R(req_top,    proto,	HTTP_HDR_PROTO)

VRT_HDR_LR(resp,   proto,	HTTP_HDR_PROTO)
VRT_HDR_LR(resp,   reason,	HTTP_HDR_REASON)
VRT_STATUS_L(resp)
VRT_STATUS_R(resp)

VRT_HDR_LR(bereq,  method,	HTTP_HDR_METHOD)
VRT_HDR_LR(bereq,  url,		HTTP_HDR_URL)
VRT_HDR_LR(bereq,  proto,	HTTP_HDR_PROTO)
VRT_HDR_LR(beresp, proto,	HTTP_HDR_PROTO)
VRT_HDR_LR(beresp, reason,	HTTP_HDR_REASON)
VRT_STATUS_L(beresp)
VRT_STATUS_R(beresp)

/*--------------------------------------------------------------------
 * Pulling things out of the packed object->http
 */

VCL_INT
VRT_r_obj_status(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);

	return (HTTP_GetStatusPack(ctx->req->wrk, ctx->req->objcore));
}

VCL_STRING
VRT_r_obj_proto(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);

	return (HTTP_GetHdrPack(ctx->req->wrk, ctx->req->objcore,
	    H__Proto));
}

VCL_STRING
VRT_r_obj_reason(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);

	return (HTTP_GetHdrPack(ctx->req->wrk, ctx->req->objcore,
	    H__Reason));
}

/*--------------------------------------------------------------------
 * bool-fields (.do_*)
 */

#define VBERESPW0(field)
#define VBERESPW1(field)						\
void									\
VRT_l_beresp_##field(VRT_CTX, VCL_BOOL a)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	ctx->bo->field = a ? 1 : 0;					\
}

#define VBERESPR0(field)
#define VBERESPR1(field)						\
VCL_BOOL								\
VRT_r_beresp_##field(VRT_CTX)						\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	return (ctx->bo->field);					\
}

#define BO_FLAG(l, r, w, d) \
	VBERESPR##r(l) \
	VBERESPW##w(l)
#include "tbl/bo_flags.h"

/*--------------------------------------------------------------------*/

VCL_BOOL
VRT_r_bereq_is_bgfetch(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->is_bgfetch);
}

VCL_BOOL
VRT_r_bereq_uncacheable(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->do_pass);
}

VCL_VOID
VRT_l_beresp_uncacheable(VRT_CTX, VCL_BOOL a)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo->fetch_objcore, OBJCORE_MAGIC);

	if (ctx->bo->uncacheable && !a) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		    "Ignoring attempt to reset beresp.uncacheable");
	} else if (a) {
		ctx->bo->uncacheable = 1;
	}
	oc = ctx->bo->fetch_objcore;

	VSLb(ctx->vsl, SLT_TTL, "VCL %.0f %.0f %.0f %.0f %s",	\
	    oc->ttl, oc->grace, oc->keep, oc->t_origin,		\
	    ctx->bo->uncacheable ? "uncacheable" : "cacheable");\
}

VCL_BOOL
VRT_r_beresp_uncacheable(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->uncacheable);
}

/*--------------------------------------------------------------------*/

VCL_STRING
VRT_r_client_identity(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (ctx->req->client_identity != NULL)
		return (ctx->req->client_identity);
	return (SES_Get_String_Attr(ctx->req->sp, SA_CLIENT_IP));
}

VCL_VOID
VRT_l_client_identity(VRT_CTX, const char *str, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	va_start(ap, str);
	b = VRT_String(ctx->req->http->ws, NULL, str, ap);
	va_end(ap);
	if (b == NULL) {
		VSLb(ctx->vsl, SLT_LostHeader, "client.identity");
		WS_MarkOverflow(ctx->req->http->ws);
		return;
	}
	ctx->req->client_identity = b;
}

/*--------------------------------------------------------------------*/

#define BEREQ_TIMEOUT(which)					\
VCL_VOID							\
VRT_l_bereq_##which(VRT_CTX, VCL_DURATION num)			\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);		\
	ctx->bo->which = (num > 0.0 ? num : 0.0);		\
}								\
								\
VCL_DURATION							\
VRT_r_bereq_##which(VRT_CTX)					\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);		\
	return (ctx->bo->which);				\
}

BEREQ_TIMEOUT(connect_timeout)
BEREQ_TIMEOUT(first_byte_timeout)
BEREQ_TIMEOUT(between_bytes_timeout)

/*--------------------------------------------------------------------*/

VCL_STRING
VRT_r_beresp_backend_name(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->director_resp != NULL)
		return (ctx->bo->director_resp->vcl_name);
	return (NULL);
}

/*--------------------------------------------------------------------
 * Backends do not in general have a IP number (any more) and this
 * variable is really not about the backend, but the backend connection.
 * XXX: we may need a more general beresp.backend.{details|ident}
 */

VCL_IP
VRT_r_beresp_backend_ip(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (VDI_GetIP(ctx->bo));
}

/*--------------------------------------------------------------------*/

VCL_STEVEDORE
VRT_r_req_storage(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (ctx->req->storage);
}

VCL_VOID
VRT_l_req_storage(VRT_CTX, VCL_STEVEDORE stv)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	ctx->req->storage = stv;
}

/*--------------------------------------------------------------------*/

VCL_STEVEDORE
VRT_r_beresp_storage(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->storage);
}

VCL_VOID
VRT_l_beresp_storage(VRT_CTX, VCL_STEVEDORE stv)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	ctx->bo->storage = stv;
}

/*--------------------------------------------------------------------
 * VCL <= 4.0 ONLY
 */

#include "storage/storage.h"

VCL_STRING
VRT_r_beresp_storage_hint(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->storage == NULL)
		return (NULL);
	CHECK_OBJ_NOTNULL(ctx->bo->storage, STEVEDORE_MAGIC);
	return (ctx->bo->storage->vclname);
}

VCL_VOID
VRT_l_beresp_storage_hint(VRT_CTX, const char *str, ...)
{
	const char *p;
	va_list ap;
	uintptr_t sn;
	VCL_STEVEDORE stv;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

	sn = WS_Snapshot(ctx->ws);
	va_start(ap, str);
	p = VRT_String(ctx->ws, NULL, str, ap);
	va_end(ap);

	if (p == NULL) {
		VSLb(ctx->vsl, SLT_LostHeader, "storage_hint");
		WS_Reset(ctx->ws, sn);
		WS_MarkOverflow(ctx->ws);
		return;
	}

	stv = VRT_stevedore(p);
	if (stv != NULL)
		ctx->bo->storage = stv;

	WS_Reset(ctx->ws, sn);
}

/*--------------------------------------------------------------------*/

VCL_STEVEDORE
VRT_r_obj_storage(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (ctx->req->objcore->stobj->stevedore);
}

/*--------------------------------------------------------------------*/

#define REQ_VAR_L(nm, elem, type, extra)				\
									\
VCL_VOID								\
VRT_l_req_##nm(VRT_CTX, type arg)					\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	extra;								\
	ctx->req->elem = arg;						\
}

#define REQ_VAR_R(nm, elem, type)					\
									\
type									\
VRT_r_req_##nm(VRT_CTX)							\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	return (ctx->req->elem);					\
}

REQ_VAR_L(backend_hint, director_hint, VCL_BACKEND,)
REQ_VAR_R(backend_hint, director_hint, VCL_BACKEND)
REQ_VAR_L(ttl, d_ttl, VCL_DURATION, if (!(arg>0.0)) arg = 0;)
REQ_VAR_R(ttl, d_ttl, VCL_DURATION)
REQ_VAR_L(grace, d_grace, VCL_DURATION, if (!(arg>0.0)) arg = 0;)
REQ_VAR_R(grace, d_grace, VCL_DURATION)

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_l_bereq_backend(VRT_CTX, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	ctx->bo->director_req = be;
}

VCL_BACKEND
VRT_r_bereq_backend(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->director_req);
}

VCL_BACKEND
VRT_r_beresp_backend(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->director_resp);
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_u_bereq_body(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->req != NULL) {
		CHECK_OBJ_NOTNULL(ctx->bo->req, REQ_MAGIC);
		ctx->bo->req = NULL;
		ObjSetState(ctx->bo->wrk,
		    ctx->bo->fetch_objcore, BOS_REQ_DONE);
		http_Unset(ctx->bo->bereq, H_Content_Length);
	}
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_l_req_esi(VRT_CTX, VCL_BOOL process_esi)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	assert(ctx->syntax <= 40);
	/*
	 * Only allow you to turn of esi in the main request
	 * else everything gets confused
	 */
	if (ctx->req->esi_level == 0)
		ctx->req->disable_esi = !process_esi;
}

VCL_BOOL
VRT_r_req_esi(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	assert(ctx->syntax <= 40);
	return (!ctx->req->disable_esi);
}

VCL_INT
VRT_r_req_esi_level(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (ctx->req->esi_level);
}

/*--------------------------------------------------------------------*/

VCL_BOOL
VRT_r_req_can_gzip(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (RFC2616_Req_Gzip(ctx->req->http));	// XXX ?
}

/*--------------------------------------------------------------------*/

VCL_INT
VRT_r_req_restarts(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (ctx->req->restarts);
}

VCL_INT
VRT_r_bereq_retries(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->retries);
}

/*--------------------------------------------------------------------
 * In exp.*:
 *	t_origin is absolute
 *	ttl is relative to t_origin
 *	grace&keep are relative to ttl
 * In VCL:
 *	ttl is relative to "ttl_now", which is t_req on the client
 *	side, except in vcl_deliver, where it is ctx->now. On the
 *	fetch side "ttl_now" is ctx->now (which is bo->t_prev).
 *	grace&keep are relative to ttl
 */

static double
ttl_now(VRT_CTX)
{
	if (ctx->bo) {
		return (ctx->now);
	} else {
		CHECK_OBJ(ctx->req, REQ_MAGIC);
		return (ctx->method == VCL_MET_DELIVER
		    ? ctx->now : ctx->req->t_req);
	}
}

#define VRT_DO_EXP_L(which, oc, fld, offset)			\
								\
VCL_VOID							\
VRT_l_##which##_##fld(VRT_CTX, VCL_DURATION a)			\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	a += (offset);						\
	if (a < 0.0)						\
		a = 0.0;					\
	oc->fld = a;						\
	VSLb(ctx->vsl, SLT_TTL, "VCL %.0f %.0f %.0f %.0f %s",	\
	    oc->ttl, oc->grace, oc->keep, oc->t_origin,		\
	    ctx->bo->uncacheable ? "uncacheable" : "cacheable");\
}

#define VRT_DO_EXP_R(which, oc, fld, offset)			\
								\
VCL_DURATION							\
VRT_r_##which##_##fld(VRT_CTX)					\
{								\
	double d;						\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	d = oc->fld;						\
	if (d <= 0.0)						\
		d = 0.0;					\
	d -= (offset);						\
	return (d);						\
}

VRT_DO_EXP_R(obj, ctx->req->objcore, ttl,
    ttl_now(ctx) - ctx->req->objcore->t_origin)
VRT_DO_EXP_R(obj, ctx->req->objcore, grace, 0)
VRT_DO_EXP_R(obj, ctx->req->objcore, keep, 0)
VRT_DO_EXP_L(beresp, ctx->bo->fetch_objcore, ttl,
    ttl_now(ctx) - ctx->bo->fetch_objcore->t_origin)
VRT_DO_EXP_R(beresp, ctx->bo->fetch_objcore, ttl,
    ttl_now(ctx) - ctx->bo->fetch_objcore->t_origin)

VRT_DO_EXP_L(beresp, ctx->bo->fetch_objcore, grace, 0)
VRT_DO_EXP_R(beresp, ctx->bo->fetch_objcore, grace, 0)
VRT_DO_EXP_L(beresp, ctx->bo->fetch_objcore, keep, 0)
VRT_DO_EXP_R(beresp, ctx->bo->fetch_objcore, keep, 0)

/*--------------------------------------------------------------------
 */

#define VRT_DO_AGE_R(which, oc)					\
								\
VCL_DURATION							\
VRT_r_##which##_##age(VRT_CTX)					\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	return (ttl_now(ctx) - oc->t_origin);			\
}

VRT_DO_AGE_R(obj, ctx->req->objcore)
VRT_DO_AGE_R(beresp, ctx->bo->fetch_objcore)

/*--------------------------------------------------------------------
 * [[be]req|sess].xid
 */

VCL_STRING
VRT_r_req_xid(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);

	return (WS_Printf(ctx->req->http->ws, "%u",
	    VXID(ctx->req->vsl->wid)));
}

VCL_STRING
VRT_r_bereq_xid(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

	return (WS_Printf(ctx->bo->bereq->ws, "%u",
	    VXID(ctx->bo->vsl->wid)));
}

VCL_STRING
VRT_r_sess_xid(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (VALID_OBJ(ctx->req, REQ_MAGIC))
		return (WS_Printf(ctx->req->http->ws, "%u",
		    VXID(ctx->req->sp->vxid)));

	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (WS_Printf(ctx->bo->bereq->ws, "%u",
	    VXID(ctx->bo->sp->vxid)));
}

/*--------------------------------------------------------------------
 * req fields
 */

#define VREQW0(field)
#define VREQW1(field)							\
VCL_VOID								\
VRT_l_req_##field(VRT_CTX, VCL_BOOL a)					\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	ctx->req->field = a ? 1 : 0;					\
}

#define VREQR0(field)
#define VREQR1(field)							\
VCL_BOOL								\
VRT_r_req_##field(VRT_CTX)						\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	return (ctx->req->field);					\
}

#define REQ_FLAG(l, r, w, d) \
	VREQR##r(l) \
	VREQW##w(l)
#include "tbl/req_flags.h"

/*--------------------------------------------------------------------*/

#define GIP(fld)						\
	VCL_IP							\
	VRT_r_##fld##_ip(VRT_CTX)				\
	{							\
		struct suckaddr *sa;				\
								\
		CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);		\
		CHECK_OBJ_NOTNULL(ctx->sp, SESS_MAGIC);		\
		AZ(SES_Get_##fld##_addr(ctx->sp, &sa));		\
		return (sa);					\
	}

GIP(local)
GIP(remote)
GIP(client)
GIP(server)
#undef GIP

/*--------------------------------------------------------------------
 * local.[endpoint|socket]
 */

#define LOC(var,fld)						\
VCL_STRING							\
VRT_r_local_##var(VRT_CTX)					\
{								\
	struct sess *sp;					\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	if (VALID_OBJ(ctx->req, REQ_MAGIC))			\
		sp = ctx->req->sp;				\
	else {							\
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);	\
		sp = ctx->bo->sp;				\
	}							\
								\
	CHECK_OBJ_NOTNULL(sp->listen_sock, LISTEN_SOCK_MAGIC);	\
	AN(sp->listen_sock->fld);				\
	return (sp->listen_sock->fld);				\
}

LOC(endpoint, endpoint)
LOC(socket, name)
#undef LOC

/*--------------------------------------------------------------------*/

VCL_STRING
VRT_r_server_identity(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (heritage.identity != NULL)
		return (heritage.identity);
	else
		return ("varnishd");
}

VCL_STRING
VRT_r_server_hostname(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (vrt_hostname[0] == '\0')
		AZ(gethostname(vrt_hostname, sizeof(vrt_hostname)));
	return (vrt_hostname);
}

/*--------------------------------------------------------------------*/

VCL_INT
VRT_r_obj_hits(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
	return (ctx->req->is_hit ? ctx->req->objcore->hits : 0);
}

VCL_BOOL
VRT_r_obj_uncacheable(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
	return (ctx->req->objcore->flags & OC_F_PASS ? 1 : 0);
}

/*--------------------------------------------------------------------*/

VCL_BOOL
VRT_r_resp_is_streaming(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (ctx->req->objcore == NULL)
		return (0);	/* When called from vcl_synth */
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
	return (ctx->req->objcore->boc == NULL ? 0 : 1);
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_l_resp_do_esi(VRT_CTX, VCL_BOOL process_esi)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	assert(ctx->syntax >= 41);
	/*
	 * Only allow you to turn of esi in the main request
	 * else everything gets confused
	 */
	ctx->req->disable_esi = !process_esi;
}

VCL_BOOL
VRT_r_resp_do_esi(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	assert(ctx->syntax >= 41);
	return (!ctx->req->disable_esi);
}

/*--------------------------------------------------------------------*/

#define VRT_BODY_L(which)					\
VCL_VOID							\
VRT_l_##which##_body(VRT_CTX, const char *str, ...)		\
{								\
	va_list ap;						\
	const char *p;						\
	struct vsb *vsb;					\
								\
	CAST_OBJ_NOTNULL(vsb, ctx->specific, VSB_MAGIC);	\
	va_start(ap, str);					\
	p = str;						\
	while (p != vrt_magic_string_end) {			\
		if (p == NULL)					\
			p = "(null)";				\
		VSB_cat(vsb, p);				\
		p = va_arg(ap, const char *);			\
	}							\
	va_end(ap);						\
}

VRT_BODY_L(beresp)
VRT_BODY_L(resp)

/*--------------------------------------------------------------------*/

VCL_BLOB
VRT_r_req_hash(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (VRT_blob(ctx, "req.hash", ctx->req->digest, DIGEST_LEN));
}

VCL_BLOB
VRT_r_bereq_hash(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (VRT_blob(ctx, "bereq.hash", ctx->bo->digest, DIGEST_LEN));
}

/*--------------------------------------------------------------------*/

#define HTTP_VAR(x)						\
VCL_HTTP							\
VRT_r_##x(VRT_CTX)						\
{								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->http_##x, HTTP_MAGIC);		\
	return (ctx->http_##x);					\
}

HTTP_VAR(req)
HTTP_VAR(resp)
HTTP_VAR(bereq)
HTTP_VAR(beresp)

/*--------------------------------------------------------------------*/

VCL_STRING
VRT_r_beresp_filters(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->filter_list != NULL)
		return(ctx->bo->filter_list);
	/* We do not set bo->filter_list yet, things might still change */
	return (VBF_Get_Filter_List(ctx->bo));
}

VCL_VOID
VRT_l_beresp_filters(VRT_CTX, const char *str, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	va_start(ap, str);
	b = VRT_String(ctx->bo->ws, NULL, str, ap);
	va_end(ap);
	if (b == NULL) {
		WS_MarkOverflow(ctx->bo->ws);
		return;
	}
	ctx->bo->filter_list = b;
}
