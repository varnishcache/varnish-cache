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
 * Runtime support for compiled VCL programs
 */
#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "common/heritage.h"
#include "hash/hash_slinger.h"

#include "cache_backend.h"
#include "vrt.h"
#include "vrt_obj.h"
#include "vsa.h"

static char vrt_hostname[255] = "";

/*--------------------------------------------------------------------*/

static void
vrt_do_string(const struct http *hp, int fld,
    const char *err, const char *p, va_list ap)
{
	const char *b;

	AN(hp);
	b = VRT_String(hp->ws, NULL, p, ap);
	if (b == NULL || *b == '\0') {
		VSLb(hp->vsl, SLT_LostHeader, "%s", err);
	} else {
		http_SetH(hp, fld, b);
	}
	va_end(ap);
}

#define VRT_HDR_L(obj, hdr, fld)					\
void									\
VRT_l_##obj##_##hdr(const struct vrt_ctx *ctx, const char *p, ...)	\
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
VRT_r_##obj##_##hdr(const struct vrt_ctx *ctx)				\
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
VRT_l_##obj##_status(const struct vrt_ctx *ctx, long num)		\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	assert(num >= 100 && num <= 999);				\
	ctx->http_##obj->status = (uint16_t)num;			\
}

#define VRT_STATUS_R(obj)						\
long									\
VRT_r_##obj##_status(const struct vrt_ctx *ctx)				\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	return(ctx->http_##obj->status);				\
}

VRT_HDR_LR(req,    method,	HTTP_HDR_METHOD)
VRT_HDR_LR(req,    url,		HTTP_HDR_URL)
VRT_HDR_LR(req,    proto,	HTTP_HDR_PROTO)

VRT_HDR_R(obj,    proto,	HTTP_HDR_PROTO)
VRT_HDR_R(obj,    reason,	HTTP_HDR_RESPONSE)
VRT_STATUS_R(obj)

VRT_HDR_LR(resp,   proto,	HTTP_HDR_PROTO)
VRT_HDR_LR(resp,   reason,	HTTP_HDR_RESPONSE)
VRT_STATUS_L(resp)
VRT_STATUS_R(resp)

VRT_HDR_LR(bereq,  method,	HTTP_HDR_METHOD)
VRT_HDR_LR(bereq,  url,		HTTP_HDR_URL)
VRT_HDR_LR(bereq,  proto,	HTTP_HDR_PROTO)
VRT_HDR_LR(beresp, proto,	HTTP_HDR_PROTO)
VRT_HDR_LR(beresp, reason,	HTTP_HDR_RESPONSE)
VRT_STATUS_L(beresp)
VRT_STATUS_R(beresp)

/*--------------------------------------------------------------------*/

#define VBERESPW0(field)
#define VBERESPW1(field)						\
void									\
VRT_l_beresp_##field(const struct vrt_ctx *ctx, unsigned a)		\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	ctx->bo->field = a ? 1 : 0;					\
}

#define VBERESPR0(field)
#define VBERESPR1(field)						\
unsigned								\
VRT_r_beresp_##field(const struct vrt_ctx *ctx)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	return (ctx->bo->field);					\
}

#define BO_FLAG(l, r, w, d) \
	VBERESPR##r(l) \
	VBERESPW##r(l)
#include "tbl/bo_flags.h"
#undef BO_FLAG

/*--------------------------------------------------------------------*/

void
VRT_l_bereq_uncacheable(const struct vrt_ctx *ctx, unsigned a)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

	if (ctx->bo->do_pass && !a) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		    "Ignoring attempt to reset bereq.uncacheable");
	} else if (a) {
		ctx->bo->do_pass = 1;
	}
}

unsigned
VRT_r_bereq_uncacheable(const struct vrt_ctx *ctx)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->do_pass);
}

void
VRT_l_beresp_uncacheable(const struct vrt_ctx *ctx, unsigned a)
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
VRT_r_beresp_uncacheable(const struct vrt_ctx *ctx)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->uncacheable);
}

/*--------------------------------------------------------------------*/

const char *
VRT_r_client_identity(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (ctx->req->client_identity != NULL)
		return (ctx->req->client_identity);
	else
		return (ctx->req->sp->client_addr_str);
}

void
VRT_l_client_identity(const struct vrt_ctx *ctx, const char *str, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	va_start(ap, str);
	// XXX ?
	b = VRT_String(ctx->req->http->ws, NULL, str, ap);
	va_end(ap);
	ctx->req->client_identity = b;
}

/*--------------------------------------------------------------------*/

#define BEREQ_TIMEOUT(which)					\
void								\
VRT_l_bereq_##which(const struct vrt_ctx *ctx, double num)	\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);		\
	ctx->bo->which = (num > 0.0 ? num : 0.0);		\
}								\
								\
double								\
VRT_r_bereq_##which(const struct vrt_ctx *ctx)			\
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
VRT_r_beresp_backend_name(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->vbc != NULL) {
		CHECK_OBJ_NOTNULL(ctx->bo->vbc, VBC_MAGIC);
		return (ctx->bo->vbc->backend->vcl_name);
	}
	if (ctx->bo->director != NULL) {
		return (ctx->bo->director->vcl_name);
	}
	return (NULL);
}

VCL_IP
VRT_r_beresp_backend_ip(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->vbc != NULL) {
		CHECK_OBJ_NOTNULL(ctx->bo->vbc, VBC_MAGIC);
		return(ctx->bo->vbc->addr);
	} else
		return (NULL);
}

const char *
VRT_r_beresp_storage_hint(const struct vrt_ctx *ctx)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->storage_hint != NULL)
		return (ctx->bo->storage_hint);
	else
		return (NULL);
}

void
VRT_l_beresp_storage_hint(const struct vrt_ctx *ctx, const char *str, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	va_start(ap, str);
	b = VRT_String(ctx->bo->ws, NULL, str, ap);	// XXX: ctx->ws ?
	va_end(ap);
	ctx->bo->storage_hint = b;
}

/*--------------------------------------------------------------------*/

#define REQ_VAR_L(nm, elem, type,extra)					\
									\
void									\
VRT_l_req_##nm(const struct vrt_ctx *ctx, type arg)			\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	extra;								\
	ctx->req->elem = arg;						\
}

#define REQ_VAR_R(nm, elem, type)					\
									\
type									\
VRT_r_req_##nm(const struct vrt_ctx *ctx)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	return(ctx->req->elem);						\
}

REQ_VAR_L(backend_hint, director_hint, struct director *,)
REQ_VAR_R(backend_hint, director_hint, struct director *)
REQ_VAR_L(ttl, d_ttl, double, if (!(arg>0.0)) arg = 0;)
REQ_VAR_R(ttl, d_ttl, double)

/*--------------------------------------------------------------------*/

void
VRT_l_bereq_backend(const struct vrt_ctx *ctx, struct director *be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	AN(ctx->bo->director);
	ctx->bo->director = be;
}

struct director *
VRT_r_bereq_backend(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	AN(ctx->bo->director);
	return (ctx->bo->director);
}

/*--------------------------------------------------------------------*/

void
VRT_l_req_esi(const struct vrt_ctx *ctx, unsigned process_esi)
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
VRT_r_req_esi(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (!ctx->req->disable_esi);
}

long
VRT_r_req_esi_level(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return(ctx->req->esi_level);
}

/*--------------------------------------------------------------------*/

unsigned
VRT_r_req_can_gzip(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (RFC2616_Req_Gzip(ctx->req->http));	// XXX ?
}


/*--------------------------------------------------------------------*/

long
VRT_r_req_restarts(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (ctx->req->restarts);
}

long
VRT_r_bereq_retries(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return (ctx->bo->retries);
}

/*--------------------------------------------------------------------
 * NB: TTL is relative to when object was created, whereas grace and
 * keep are relative to ttl.
 */

#define VRT_DO_EXP_L(which, sexp, fld)				\
								\
void								\
VRT_l_##which##_##fld(const struct vrt_ctx *ctx, double a)	\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	if (a < 0.0)						\
		a = 0.0;					\
	sexp.fld = a;						\
	VSLb(ctx->vsl, SLT_TTL, "VCL %.0f %.0f %.0f %.0f",	\
	    sexp.ttl, sexp.grace, sexp.keep, sexp.t_origin);	\
}

#define VRT_DO_EXP_R(which, sexp, fld, offset)			\
								\
double								\
VRT_r_##which##_##fld(const struct vrt_ctx *ctx)		\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	if (sexp.fld > 0.0)					\
		return(sexp.fld - offset);			\
	return(0.0);						\
}

VRT_DO_EXP_R(obj, ctx->req->obj->exp, ttl,
   (ctx->req->t_req - ctx->req->obj->exp.t_origin))
VRT_DO_EXP_R(obj, ctx->req->obj->exp, grace, 0)
VRT_DO_EXP_R(obj, ctx->req->obj->exp, keep, 0)

VRT_DO_EXP_L(beresp, ctx->bo->exp, ttl)
VRT_DO_EXP_R(beresp, ctx->bo->exp, ttl, 0)
VRT_DO_EXP_L(beresp, ctx->bo->exp, grace)
VRT_DO_EXP_R(beresp, ctx->bo->exp, grace, 0)
VRT_DO_EXP_L(beresp, ctx->bo->exp, keep)
VRT_DO_EXP_R(beresp, ctx->bo->exp, keep, 0)

/*--------------------------------------------------------------------
 * [be]req.xid
 */

const char *
VRT_r_req_xid(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);

	return (WS_Printf(ctx->req->http->ws, "%u",
	    ctx->req->vsl->wid & VSL_IDENTMASK));
}

const char *
VRT_r_bereq_xid(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

	return (WS_Printf(ctx->bo->bereq->ws, "%u",
	    ctx->bo->vsl->wid & VSL_IDENTMASK));
}

/*--------------------------------------------------------------------*/

#define REQ_BOOL(hash_var)					\
void								\
VRT_l_req_##hash_var(const struct vrt_ctx *ctx, unsigned val)	\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);			\
	ctx->req->hash_var = val ? 1 : 0;			\
}								\
								\
unsigned							\
VRT_r_req_##hash_var(const struct vrt_ctx *ctx)			\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);			\
	return(ctx->req->hash_var);				\
}

REQ_BOOL(hash_ignore_busy)
REQ_BOOL(hash_always_miss)

/*--------------------------------------------------------------------*/

VCL_IP
VRT_r_client_ip(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->sp, SESS_MAGIC);
	return (sess_remote_addr(ctx->req->sp));
}

VCL_IP
VRT_r_server_ip(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->sp, SESS_MAGIC);
	return (sess_local_addr(ctx->req->sp));
}

const char*
VRT_r_server_identity(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (heritage.identity[0] != '\0')
		return (heritage.identity);
	else
		return (heritage.name);
}

const char*
VRT_r_server_hostname(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (vrt_hostname[0] == '\0')
		AZ(gethostname(vrt_hostname, sizeof(vrt_hostname)));
	return (vrt_hostname);
}

/*--------------------------------------------------------------------*/

long
VRT_r_obj_hits(const struct vrt_ctx *ctx)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->obj->objcore, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->obj->objcore->objhead, OBJHEAD_MAGIC);
	return (ctx->req->obj->objcore->objhead->hits);
}

unsigned
VRT_r_obj_uncacheable(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->obj, OBJECT_MAGIC);
	return (ctx->req->obj->objcore->flags & OC_F_PASS ? 1 : 0);
}
