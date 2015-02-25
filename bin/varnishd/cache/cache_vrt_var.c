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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "common/heritage.h"
#include "hash/hash_slinger.h"

#include "cache_director.h"
#include "vrt.h"
#include "vrt_obj.h"
#include "vsa.h"

static char vrt_hostname[255] = "";

/*--------------------------------------------------------------------
 * VRT variables relating to first line of HTTP/1.1 req/resp
 */

static void
vrt_do_string(const struct http *hp, int fld,
    const char *err, const char *p, va_list ap)
{
	const char *b;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	b = VRT_String(hp->ws, NULL, p, ap);
	if (b == NULL || *b == '\0') {
		VSLb(hp->vsl, SLT_LostHeader, "%s", err);
		WS_MarkOverflow(hp->ws);
		return;
	}
	http_SetH(hp, fld, b);
}

#define VRT_HDR_L(obj, hdr, fld)					\
void									\
VRT_l_##obj##_##hdr(VRT_CTX, const char *p, ...)	\
{									\
	va_list ap;							\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	va_start(ap, p);						\
	vrt_do_string(ctx->http_##obj, fld, #obj "." #hdr, p, ap);	\
	va_end(ap);							\
}

#define VRT_HDR_R(obj, hdr, fld)					\
const char *								\
VRT_r_##obj##_##hdr(VRT_CTX)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	return (ctx->http_##obj->hd[fld].b);				\
}

#define VRT_HDR_LR(obj, hdr, fld)					\
	VRT_HDR_L(obj, hdr, fld)					\
	VRT_HDR_R(obj, hdr, fld)

#define VRT_STATUS_L(obj)						\
void									\
VRT_l_##obj##_status(VRT_CTX, long num)		\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	if (num > 65535) {						\
		VSLb(ctx->vsl, SLT_VCL_Error, "%s.status > 65535", #obj); \
		WS_MarkOverflow(ctx->http_##obj->ws);			\
	} else if ((num % 1000) < 100) {				\
		VSLb(ctx->vsl, SLT_VCL_Error, "illegal %s.status (..0##)", \
		    #obj);						\
		WS_MarkOverflow(ctx->http_##obj->ws);			\
	} else {							\
		http_SetStatus(ctx->http_##obj, (uint16_t)num);		\
	}								\
}

#define VRT_STATUS_R(obj)						\
long									\
VRT_r_##obj##_status(VRT_CTX)				\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	return(ctx->http_##obj->status);				\
}

VRT_HDR_LR(req,    method,	HTTP_HDR_METHOD)
VRT_HDR_LR(req,    url,		HTTP_HDR_URL)
VRT_HDR_LR(req,    proto,	HTTP_HDR_PROTO)

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

long
VRT_r_obj_status(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);

	return (HTTP_GetStatusPack(ctx->req->wrk, ctx->req->objcore));
}

const char *
VRT_r_obj_proto(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);

	return (HTTP_GetHdrPack(ctx->req->wrk, ctx->req->objcore, ":proto"));
}

const char *
VRT_r_obj_reason(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);

	return (HTTP_GetHdrPack(ctx->req->wrk, ctx->req->objcore, ":reason"));
}

/*--------------------------------------------------------------------
 * bool-fields (.do_*)
 */

#define VBERESPW0(field)
#define VBERESPW1(field)						\
void									\
VRT_l_beresp_##field(VRT_CTX, unsigned a)		\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	ctx->bo->field = a ? 1 : 0;					\
}

#define VBERESPR0(field)
#define VBERESPR1(field)						\
unsigned								\
VRT_r_beresp_##field(VRT_CTX)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	return (ctx->bo->field);					\
}

#define BO_FLAG(l, r, w, d) \
	VBERESPR##r(l) \
	VBERESPW##w(l)
#include "tbl/bo_flags.h"
#undef BO_FLAG

/*--------------------------------------------------------------------*/

unsigned
VRT_r_bereq_uncacheable(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->do_pass);
}

void
VRT_l_beresp_uncacheable(VRT_CTX, unsigned a)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

	if (ctx->bo->uncacheable && !a) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		    "Ignoring attempt to reset beresp.uncacheable");
	} else if (a) {
		ctx->bo->uncacheable = 1;
	}
}

unsigned
VRT_r_beresp_uncacheable(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->uncacheable);
}

/*--------------------------------------------------------------------*/

const char *
VRT_r_client_identity(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (ctx->req->client_identity != NULL)
		return (ctx->req->client_identity);
	else
		return (ctx->req->sp->client_addr_str);
}

void
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
void								\
VRT_l_bereq_##which(VRT_CTX, double num)	\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);		\
	ctx->bo->which = (num > 0.0 ? num : 0.0);		\
}								\
								\
double								\
VRT_r_bereq_##which(VRT_CTX)			\
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

const char *
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
	return (VDI_GetIP(ctx->bo->wrk, ctx->bo));
}

/*--------------------------------------------------------------------*/

const char *
VRT_r_beresp_storage_hint(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->storage_hint != NULL)
		return (ctx->bo->storage_hint);
	else
		return (NULL);
}

void
VRT_l_beresp_storage_hint(VRT_CTX, const char *str, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	va_start(ap, str);
	b = VRT_String(ctx->bo->ws, NULL, str, ap);	// XXX: ctx->ws ?
	va_end(ap);
	if (b == NULL) {
		VSLb(ctx->vsl, SLT_LostHeader, "storage.hint");
		WS_MarkOverflow(ctx->bo->beresp->ws);
		return;
	}
	ctx->bo->storage_hint = b;
}

/*--------------------------------------------------------------------*/

#define REQ_VAR_L(nm, elem, type,extra)					\
									\
void									\
VRT_l_req_##nm(VRT_CTX, type arg)			\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	extra;								\
	ctx->req->elem = arg;						\
}

#define REQ_VAR_R(nm, elem, type)					\
									\
type									\
VRT_r_req_##nm(VRT_CTX)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	return(ctx->req->elem);						\
}

REQ_VAR_L(backend_hint, director_hint, const struct director *,)
REQ_VAR_R(backend_hint, director_hint, const struct director *)
REQ_VAR_L(ttl, d_ttl, double, if (!(arg>0.0)) arg = 0;)
REQ_VAR_R(ttl, d_ttl, double)

/*--------------------------------------------------------------------*/

void
VRT_l_bereq_backend(VRT_CTX, const struct director *be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	ctx->bo->director_req = be;
}

const struct director *
VRT_r_bereq_backend(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->director_req);
}

const struct director *
VRT_r_beresp_backend(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->director_resp);
}

/*--------------------------------------------------------------------*/

void
VRT_l_req_esi(VRT_CTX, unsigned process_esi)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	/*
	 * Only allow you to turn of esi in the main request
	 * else everything gets confused
	 */
	if(ctx->req->esi_level == 0)
		ctx->req->disable_esi = !process_esi;
}

unsigned
VRT_r_req_esi(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (!ctx->req->disable_esi);
}

long
VRT_r_req_esi_level(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return(ctx->req->esi_level);
}

/*--------------------------------------------------------------------*/

unsigned
VRT_r_req_can_gzip(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (RFC2616_Req_Gzip(ctx->req->http));	// XXX ?
}

/*--------------------------------------------------------------------*/

long
VRT_r_req_restarts(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (ctx->req->restarts);
}

long
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
 *	ttl is relative to now
 *	grace&keep are relative to ttl
 */

#define VRT_DO_EXP_L(which, sexp, fld, offset)			\
								\
void								\
VRT_l_##which##_##fld(VRT_CTX, double a)	\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	a += (offset);						\
	if (a < 0.0)						\
		a = 0.0;					\
	sexp.fld = a;						\
	VSLb(ctx->vsl, SLT_TTL, "VCL %.0f %.0f %.0f %.0f",	\
	    sexp.ttl, sexp.grace, sexp.keep, sexp.t_origin);	\
}

#define VRT_DO_EXP_R(which, sexp, fld, offset)			\
								\
double								\
VRT_r_##which##_##fld(VRT_CTX)		\
{								\
	double d;						\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	d = sexp.fld;						\
	if (d <= 0.0)						\
		d = 0.0;					\
	d -= (offset);						\
	return(d);						\
}

VRT_DO_EXP_R(obj, ctx->req->objcore->exp, ttl,
    ctx->now - ctx->req->objcore->exp.t_origin)
VRT_DO_EXP_R(obj, ctx->req->objcore->exp, grace, 0)
VRT_DO_EXP_R(obj, ctx->req->objcore->exp, keep, 0)

VRT_DO_EXP_L(beresp, ctx->bo->fetch_objcore->exp, ttl,
    ctx->now - ctx->bo->fetch_objcore->exp.t_origin)
VRT_DO_EXP_R(beresp, ctx->bo->fetch_objcore->exp, ttl,
    ctx->now - ctx->bo->fetch_objcore->exp.t_origin)
VRT_DO_EXP_L(beresp, ctx->bo->fetch_objcore->exp, grace, 0)
VRT_DO_EXP_R(beresp, ctx->bo->fetch_objcore->exp, grace, 0)
VRT_DO_EXP_L(beresp, ctx->bo->fetch_objcore->exp, keep, 0)
VRT_DO_EXP_R(beresp, ctx->bo->fetch_objcore->exp, keep, 0)

/*--------------------------------------------------------------------
 */

#define VRT_DO_AGE_R(which, sexp)				\
								\
double								\
VRT_r_##which##_##age(VRT_CTX)		\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	return(ctx->now - sexp.t_origin);			\
}

VRT_DO_AGE_R(obj, ctx->req->objcore->exp)
VRT_DO_AGE_R(beresp, ctx->bo->fetch_objcore->exp)

/*--------------------------------------------------------------------
 * [be]req.xid
 */

const char *
VRT_r_req_xid(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);

	return (WS_Printf(ctx->req->http->ws, "%u", VXID(ctx->req->vsl->wid)));
}

const char *
VRT_r_bereq_xid(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

	return (WS_Printf(ctx->bo->bereq->ws, "%u", VXID(ctx->bo->vsl->wid)));
}

/*--------------------------------------------------------------------
 * req fields
 */

#define VREQW0(field)
#define VREQW1(field)						\
void									\
VRT_l_req_##field(VRT_CTX, unsigned a)		\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);			\
	ctx->req->field = a ? 1 : 0;					\
}

#define VREQR0(field)
#define VREQR1(field)						\
unsigned								\
VRT_r_req_##field(VRT_CTX)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);			\
	return (ctx->req->field);					\
}

#define REQ_FLAG(l, r, w, d) \
	VREQR##r(l) \
	VREQW##w(l)
#include "tbl/req_flags.h"
#undef REQ_FLAG

/*--------------------------------------------------------------------*/

VCL_IP
VRT_r_client_ip(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->sp, SESS_MAGIC);
	return (sess_remote_addr(ctx->req->sp));
}

VCL_IP
VRT_r_server_ip(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->sp, SESS_MAGIC);
	return (sess_local_addr(ctx->req->sp));
}

const char*
VRT_r_server_identity(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (heritage.identity[0] != '\0')
		return (heritage.identity);
	else
		return (heritage.name);
}

const char*
VRT_r_server_hostname(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (vrt_hostname[0] == '\0')
		AZ(gethostname(vrt_hostname, sizeof(vrt_hostname)));
	return (vrt_hostname);
}

/*--------------------------------------------------------------------*/

long
VRT_r_obj_hits(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
	return (ctx->req->is_hit ? ctx->req->objcore->hits : 0);
}

unsigned
VRT_r_obj_uncacheable(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
	return (ctx->req->objcore->flags & OC_F_PASS ? 1 : 0);
}

/*--------------------------------------------------------------------*/

#define HTTP_VAR(x)						\
struct http *							\
VRT_r_##x(VRT_CTX)				\
{								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->http_##x, HTTP_MAGIC);		\
	return (ctx->http_##x);					\
}

HTTP_VAR(req)
HTTP_VAR(resp)
HTTP_VAR(bereq)
HTTP_VAR(beresp)
