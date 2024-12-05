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
 * Runtime support for compiled VCL programs
 */
#include "config.h"

#include <stdio.h>

#include "cache_varnishd.h"
#include "cache_objhead.h"
#include "cache_transport.h"
#include "common/heritage.h"

#include "vcl.h"
#include "vtim.h"
#include "vtcp.h"

#include "vrt_obj.h"

#define VRT_TMO(tmo) (isinf(tmo) ? VRT_DECIMAL_MAX : tmo)

static char vrt_hostname[255] = "";

/*--------------------------------------------------------------------
 * VRT variables relating to first line of HTTP/1.1 req/resp
 */

static void
vrt_do_strands(VRT_CTX, struct http *hp, int fld,
    const char *err, const char *str, VCL_STRANDS s)
{
	const char *b;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	b = VRT_StrandsWS(hp->ws, str, s);
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
VRT_l_##obj##_##hdr(VRT_CTX, const char *str, VCL_STRANDS s)		\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	vrt_do_strands(ctx, ctx->http_##obj, fld, #obj "." #hdr, str, s);	\
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
		http_SetStatus(ctx->http_##obj, (uint16_t)num, NULL);	\
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
 * beresp bool-fields
 */

static inline int
beresp_filter_fixed(VRT_CTX, const char *s)
{
	if (ctx->bo->vfp_filter_list == NULL)
		return (0);
	VRT_fail(ctx,
	    "beresp.filters are already fixed, beresp.%s is undefined", s);
	return (1);
}

#define VBERESPWF0(ctx, str) (void) 0
#define VBERESPWF1(ctx, str) do {		\
	if (beresp_filter_fixed((ctx), str))	\
		return;			\
	} while(0)

#define VBERESPW0(field, str, fltchk)
#define VBERESPW1(field, str, fltchk)					\
void									\
VRT_l_beresp_##field(VRT_CTX, VCL_BOOL a)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	VBERESPWF##fltchk(ctx, str);					\
	ctx->bo->field = a ? 1 : 0;					\
}

#define VBERESPRF0(ctx, str) (void) 0
#define VBERESPRF1(ctx, str) do {		\
	if (beresp_filter_fixed((ctx), str))	\
		return (0);			\
	} while(0)

#define VBERESPR1(field, str, fltchk)					\
VCL_BOOL								\
VRT_r_beresp_##field(VRT_CTX)						\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	VBERESPRF##fltchk(ctx, str);					\
	return (ctx->bo->field);					\
}

#define BERESP_FLAG(l, r, w, f, d)		\
	VBERESPR##r(l, #l, f)			\
	VBERESPW##w(l, #l, f)
#include "tbl/beresp_flags.h"

#undef VBERESPWF0
#undef VBERESPWF1
#undef VBERESPW0
#undef VBERESPW1

#undef VBERESPRF0
#undef VBERESPRF1
#undef VBERESPR1

/*--------------------------------------------------------------------
 * bereq bool-fields
 */

#define VBEREQR0(field, str)
#define VBEREQR1(field, str)						\
VCL_BOOL								\
VRT_r_bereq_##field(VRT_CTX)						\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	return (ctx->bo->field);					\
}

#define BEREQ_FLAG(l, r, w, d)		\
	VBEREQR##r(l, #l)
#include "tbl/bereq_flags.h"

#undef VBEREQR0
#undef VBEREQR1
/*--------------------------------------------------------------------*/

VCL_BOOL
VRT_r_bereq_uncacheable(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->uncacheable);
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

VCL_VOID
VRT_l_req_trace(VRT_CTX, VCL_BOOL a)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	ctx->req->trace = a;
	VRT_trace(ctx, a);
}
VCL_VOID
VRT_l_bereq_trace(VRT_CTX, VCL_BOOL a)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	ctx->bo->trace = a;
	VRT_trace(ctx, a);
}

/*--------------------------------------------------------------------*/

VCL_BYTES
VRT_r_beresp_transit_buffer(VRT_CTX)
{
    struct objcore *oc;

    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
    CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

    oc = ctx->bo->fetch_objcore;
    CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

    return oc->boc->transit_buffer;
}

VCL_VOID
VRT_l_beresp_transit_buffer(VRT_CTX, VCL_BYTES value)
{
    struct objcore *oc;

    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
    CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

    oc = ctx->bo->fetch_objcore;
    CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

    oc->boc->transit_buffer = value;
}

/*--------------------------------------------------------------------*/

VCL_STRING
VRT_r_client_identity(VRT_CTX)
{
	const char *id;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->req != NULL) {
		CHECK_OBJ(ctx->req, REQ_MAGIC);
		id = ctx->req->client_identity;
	} else {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		id = ctx->bo->client_identity;
	}
	if (id != NULL)
		return (id);
	return (SES_Get_String_Attr(ctx->sp, SA_CLIENT_IP));
}

VCL_VOID
VRT_l_client_identity(VRT_CTX, const char *str, VCL_STRANDS s)
{
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	b = VRT_StrandsWS(ctx->req->http->ws, str, s);
	if (b == NULL) {
		VSLb(ctx->vsl, SLT_LostHeader, "client.identity");
		WS_MarkOverflow(ctx->req->http->ws);
		return;
	}
	ctx->req->client_identity = b;
}

/*--------------------------------------------------------------------*/

#define BEREQ_TIMEOUT(prefix, which)				\
VCL_VOID							\
VRT_l_bereq_##which(VRT_CTX, VCL_DURATION num)			\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);		\
	ctx->bo->which = num;					\
}								\
								\
VCL_DURATION							\
VRT_r_bereq_##which(VRT_CTX)					\
{								\
	vtim_dur res;						\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);		\
	res = BUSYOBJ_TMO(ctx->bo, prefix, which);		\
	return (VRT_TMO(res));					\
}								\
								\
VCL_VOID							\
VRT_u_bereq_##which(VRT_CTX)					\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);		\
	ctx->bo->which = NAN;					\
}

BEREQ_TIMEOUT(, connect_timeout)
BEREQ_TIMEOUT(, first_byte_timeout)
BEREQ_TIMEOUT(, between_bytes_timeout)
BEREQ_TIMEOUT(pipe_, task_deadline)


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
VRT_l_beresp_storage_hint(VRT_CTX, const char *str, VCL_STRANDS s)
{
	const char *p;
	VCL_STEVEDORE stv;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

	p = VRT_StrandsWS(ctx->ws, str, s);

	if (p == NULL) {
		VSLb(ctx->vsl, SLT_LostHeader, "storage_hint");
		WS_MarkOverflow(ctx->ws);
		return;
	}

	stv = VRT_stevedore(p);
	if (stv != NULL)
		ctx->bo->storage = stv;
}

/*--------------------------------------------------------------------*/

VCL_STEVEDORE
VRT_r_obj_storage(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
	AN(ctx->req->objcore->stobj);
	CHECK_OBJ_NOTNULL(ctx->req->objcore->stobj->stevedore,
	    STEVEDORE_MAGIC);
	return (ctx->req->objcore->stobj->stevedore);
}

/*--------------------------------------------------------------------*/

VCL_BOOL
VRT_r_obj_can_esi(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
	return (ObjHasAttr(ctx->req->wrk, ctx->req->objcore, OA_ESIDATA));
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

#define REQ_VAR_U(nm, elem, val)					\
									\
VCL_VOID								\
VRT_u_req_##nm(VRT_CTX)							\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	ctx->req->elem = val;						\
}

REQ_VAR_R(backend_hint, director_hint, VCL_BACKEND)

REQ_VAR_L(ttl, d_ttl, VCL_DURATION, if (!(arg>0.0)) arg = 0;)
REQ_VAR_R(ttl, d_ttl, VCL_DURATION)
REQ_VAR_U(ttl, d_ttl, -1)
REQ_VAR_L(grace, d_grace, VCL_DURATION, if (!(arg>0.0)) arg = 0;)
REQ_VAR_R(grace, d_grace, VCL_DURATION)
REQ_VAR_U(grace, d_grace, -1)

VCL_VOID
VRT_l_req_backend_hint(VRT_CTX, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	VRT_Assign_Backend(&ctx->req->director_hint, be);
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_l_bereq_backend(VRT_CTX, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	VRT_Assign_Backend(&ctx->bo->director_req, be);
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
	if (ctx->bo->bereq_body != NULL) {
		(void)HSH_DerefObjCore(ctx->bo->wrk, &ctx->bo->bereq_body);
		http_Unset(ctx->bo->bereq, H_Content_Length);
	}

	if (ctx->bo->req != NULL) {
		CHECK_OBJ(ctx->bo->req, REQ_MAGIC);
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
	 * NOTE: this is not true, but we do not change behavior
	 * for vcl 4.0. For 4.1, see VRT_l_resp_do_esi()
	 */
	if (IS_TOPREQ(ctx->req))
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

/*--------------------------------------------------------------------*/

VCL_STRING
VRT_r_req_transport(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->transport, TRANSPORT_MAGIC);
	return (ctx->req->transport->name);
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

/*lint -save -e835 */	// Zero right hand arg to '-'

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

/*lint -restore */

// XXX more assertions?
#define VRT_DO_TIME_R(which, where, field)				\
									\
VCL_TIME								\
VRT_r_##which##_time(VRT_CTX)						\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	AN((ctx)->where);						\
									\
	return ((ctx)->where->field);					\
}

VRT_DO_TIME_R(req, req, t_req)
VRT_DO_TIME_R(req_top, req->top->topreq, t_req)
VRT_DO_TIME_R(resp, req, t_resp)
VRT_DO_TIME_R(bereq, bo, t_first)
VRT_DO_TIME_R(beresp, bo, t_resp)
VRT_DO_TIME_R(obj, req->objcore, t_origin)

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

VCL_INT
VRT_r_req_xid(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->http, HTTP_MAGIC);
	AN(ctx->req->vsl);
	return (VXID(ctx->req->vsl->wid));
}

VCL_INT
VRT_r_bereq_xid(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	AN(ctx->bo->vsl);

	return (VXID(ctx->bo->vsl->wid));
}

VCL_INT
VRT_r_sess_xid(VRT_CTX)
{
	struct sess *sp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (ctx->req) {
		CHECK_OBJ(ctx->req, REQ_MAGIC);
		sp = ctx->req->sp;
	} else {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		sp = ctx->bo->sp;
	}

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (VXID(sp->vxid));
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
	if (ctx->req) {						\
		CHECK_OBJ(ctx->req, REQ_MAGIC);			\
		sp = ctx->req->sp;				\
	} else {						\
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
	if (ctx->method == VCL_MET_HIT)
		return (ctx->req->objcore->hits);
	return (ctx->req->is_hit ? ctx->req->objcore->hits : 0);
}

VCL_BOOL
VRT_r_obj_uncacheable(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
	return (ctx->req->objcore->flags & OC_F_HFM ? 1 : 0);
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

static inline int
resp_filter_fixed(VRT_CTX, const char *s)
{
	if (ctx->req->vdp_filter_list == NULL)
		return (0);
	VRT_fail(ctx, "resp.filters are already fixed, %s is undefined", s);
	return (1);
}

VCL_VOID
VRT_l_resp_do_esi(VRT_CTX, VCL_BOOL process_esi)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	assert(ctx->syntax >= 41);
	if (resp_filter_fixed(ctx, "resp.do_esi"))
		return;
	ctx->req->disable_esi = !process_esi;
}

VCL_BOOL
VRT_r_resp_do_esi(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	assert(ctx->syntax >= 41);
	if (resp_filter_fixed(ctx, "resp.do_esi"))
		return (0);
	return (!ctx->req->disable_esi);
}

/*--------------------------------------------------------------------*/

#define VRT_BODY_L(which)					\
VCL_VOID							\
VRT_l_##which##_body(VRT_CTX, enum lbody_e type,		\
    const char *str, VCL_BODY body)				\
{								\
	int n;							\
	struct vsb *vsb;					\
	VCL_STRANDS s;						\
	VCL_BLOB b;						\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	AN(body);						\
	CAST_OBJ_NOTNULL(vsb, ctx->specific, VSB_MAGIC);	\
	if (type == LBODY_SET_STRING || type == LBODY_SET_BLOB)	\
		VSB_clear(vsb);					\
	if (type == LBODY_SET_BLOB || type == LBODY_ADD_BLOB) {	\
		AZ(str);					\
		b = body;					\
		VSB_bcat(vsb, b->blob, b->len);			\
		return;						\
	}							\
	if (str != NULL)					\
		VSB_cat(vsb, str);				\
	assert(type == LBODY_SET_STRING ||			\
	    type == LBODY_ADD_STRING);				\
	s = body;						\
	for (n = 0; s != NULL && n < s->n; n++)			\
		if (s->p[n] != NULL)				\
			VSB_cat(vsb, s->p[n]);			\
}

VRT_BODY_L(beresp)
VRT_BODY_L(resp)

/*--------------------------------------------------------------------*/

			/* digest */
#define BLOB_HASH_TYPE 0x00d16357

VCL_BLOB
VRT_r_req_hash(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (VRT_blob(ctx, "req.hash", ctx->req->digest, DIGEST_LEN,
	    BLOB_HASH_TYPE));
}

VCL_BLOB
VRT_r_bereq_hash(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (VRT_blob(ctx, "bereq.hash", ctx->bo->digest, DIGEST_LEN,
	    BLOB_HASH_TYPE));
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

static inline void
set_idle_send_timeout(const struct sess *sp, VCL_DURATION d)
{
	struct timeval tv = VTIM_timeval_sock(d);
	VTCP_Assert(setsockopt(sp->fd, SOL_SOCKET, SO_SNDTIMEO,
	    &tv, sizeof tv));
}

#define SESS_VAR_DUR(x, setter)				\
VCL_VOID						\
VRT_l_sess_##x(VRT_CTX, VCL_DURATION d)			\
{							\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);		\
	CHECK_OBJ_NOTNULL(ctx->sp, SESS_MAGIC);		\
	d = vmax(d, 0.0);				\
	setter;						\
	ctx->sp->x = d;					\
}							\
							\
VCL_DURATION						\
VRT_r_sess_##x(VRT_CTX)					\
{							\
	vtim_dur res;					\
							\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);		\
	CHECK_OBJ_NOTNULL(ctx->sp, SESS_MAGIC);		\
	res = SESS_TMO(ctx->sp, x);			\
	return (VRT_TMO(res));				\
}							\
							\
VCL_VOID						\
VRT_u_sess_##x(VRT_CTX)					\
{							\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);		\
	CHECK_OBJ_NOTNULL(ctx->sp, SESS_MAGIC);		\
	ctx->sp->x = NAN;				\
}

SESS_VAR_DUR(timeout_idle, )
SESS_VAR_DUR(timeout_linger, )
SESS_VAR_DUR(send_timeout, )
SESS_VAR_DUR(idle_send_timeout, set_idle_send_timeout(ctx->sp, d))
