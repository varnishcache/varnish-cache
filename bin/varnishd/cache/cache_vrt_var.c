/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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
#include <stdlib.h>

#include "cache.h"
#include "common/heritage.h"

#include "cache_backend.h"
#include "vrt.h"
#include "vrt_obj.h"
#include "vsa.h"
#include "vtcp.h"

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

#define VRT_DO_HDR(obj, hdr, fld)					\
void									\
VRT_l_##obj##_##hdr(const struct vrt_ctx *ctx, const char *p, ...)	\
{									\
	va_list ap;							\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	va_start(ap, p);						\
	vrt_do_string(ctx->http_##obj, fld, #obj "." #hdr, p, ap);	\
	va_end(ap);							\
}									\
									\
const char *								\
VRT_r_##obj##_##hdr(const struct vrt_ctx *ctx)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	return (ctx->http_##obj->hd[fld].b);				\
}

#define VRT_DO_STATUS(obj)						\
void									\
VRT_l_##obj##_status(const struct vrt_ctx *ctx, long num)		\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	assert(num >= 100 && num <= 999);				\
	ctx->http_##obj->status = (uint16_t)num;			\
}									\
									\
long									\
VRT_r_##obj##_status(const struct vrt_ctx *ctx)				\
{									\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->http_##obj, HTTP_MAGIC);			\
	return(ctx->http_##obj->status);				\
}

VRT_DO_HDR(req,    method,	HTTP_HDR_METHOD)
VRT_DO_HDR(req,    request,	HTTP_HDR_METHOD)
VRT_DO_HDR(req,    url,		HTTP_HDR_URL)
VRT_DO_HDR(req,    proto,	HTTP_HDR_PROTO)
VRT_DO_HDR(obj,    proto,	HTTP_HDR_PROTO)
VRT_DO_HDR(obj,    response,	HTTP_HDR_RESPONSE)
VRT_DO_STATUS(obj)
VRT_DO_HDR(resp,   proto,	HTTP_HDR_PROTO)
VRT_DO_HDR(resp,   response,	HTTP_HDR_RESPONSE)
VRT_DO_STATUS(resp)

VRT_DO_HDR(bereq,  method,	HTTP_HDR_METHOD)
VRT_DO_HDR(bereq,  request,	HTTP_HDR_METHOD)
VRT_DO_HDR(bereq,  url,		HTTP_HDR_URL)
VRT_DO_HDR(bereq,  proto,	HTTP_HDR_PROTO)
VRT_DO_HDR(beresp, proto,	HTTP_HDR_PROTO)
VRT_DO_HDR(beresp, response,	HTTP_HDR_RESPONSE)
VRT_DO_STATUS(beresp)

/*--------------------------------------------------------------------*/

#define VBERESP(dir, type, onm, field)					\
void									\
VRT_l_##dir##_##onm(const struct vrt_ctx *ctx, type a)			\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	ctx->bo->field = a;						\
}									\
									\
type									\
VRT_r_##dir##_##onm(const struct vrt_ctx *ctx)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);			\
	return (ctx->bo->field);					\
}

VBERESP(beresp, unsigned, do_esi,	do_esi)
VBERESP(beresp, unsigned, do_gzip,	do_gzip)
VBERESP(beresp, unsigned, do_gunzip,	do_gunzip)
VBERESP(beresp, unsigned, do_stream,	do_stream)

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
		ctx->bo->do_pass = a;
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
		ctx->bo->uncacheable = a;
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
		return (ctx->req->sp->addr);
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
	CHECK_OBJ_NOTNULL(ctx->bo->vbc, VBC_MAGIC);
	return(ctx->bo->vbc->backend->vcl_name);
}

const void *
VRT_r_beresp_backend_ip(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo->vbc, VBC_MAGIC);
	return(ctx->bo->vbc->addr);
}

long
VRT_r_beresp_backend_port(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo->vbc, VBC_MAGIC);
	return (VSA_Port(ctx->bo->vbc->addr));
}

const char *
VRT_r_beresp_storage(const struct vrt_ctx *ctx)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->storage_hint != NULL)
		return (ctx->bo->storage_hint);
	else
		return (NULL);
}

void
VRT_l_beresp_storage(const struct vrt_ctx *ctx, const char *str, ...)
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

void
VRT_l_req_backend(const struct vrt_ctx *ctx, struct director *be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	ctx->req->director = be;
}

struct director *
VRT_r_req_backend(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (ctx->req->director);
}

unsigned
VRT_r_req_backend_healthy(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	/*
	 * XXX: Not optimal, but we do not have a backend in vcl_deliver
	 * XXX: and we have to return something.
	 */
	if (ctx->req->director == NULL)
		return (0);
	CHECK_OBJ_NOTNULL(ctx->req->director, DIRECTOR_MAGIC);
	return (VDI_Healthy(ctx->req->director));
}

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

unsigned
VRT_r_bereq_backend_healthy(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->bo->director, DIRECTOR_MAGIC);
	return (VDI_Healthy(ctx->bo->director));
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

#define VRT_DO_EXP(which, exp, fld, offset, extra)		\
								\
void								\
VRT_l_##which##_##fld(const struct vrt_ctx *ctx, double a)	\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	if (a > 0.)						\
		a += offset;					\
	EXP_Set_##fld(&exp, a);					\
	extra;							\
}								\
								\
double								\
VRT_r_##which##_##fld(const struct vrt_ctx *ctx)		\
{								\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	return(EXP_Get_##fld(&exp) - offset);			\
}

static void
vrt_wsp_exp(struct vsl_log *vsl, double now, const struct exp *e)
{
	double dt;

	dt = now - e->t_origin;
	VSLb(vsl, SLT_TTL, "VCL %.0f %.0f %.0f %.0f %.0f",
	    e->ttl - dt, e->grace, e->keep, now, dt);
}

VRT_DO_EXP(req, ctx->req->exp, ttl, 0, )

VRT_DO_EXP(obj, ctx->req->obj->exp, grace, 0,
   EXP_Rearm(ctx->req->obj);
   vrt_wsp_exp(ctx->vsl, ctx->req->t_req, &ctx->req->obj->exp);)
VRT_DO_EXP(obj, ctx->req->obj->exp, ttl,
   (ctx->req->t_req - ctx->req->obj->exp.t_origin),
   EXP_Rearm(ctx->req->obj);
   vrt_wsp_exp(ctx->vsl, ctx->req->t_req, &ctx->req->obj->exp);)
VRT_DO_EXP(obj, ctx->req->obj->exp, keep, 0,
   EXP_Rearm(ctx->req->obj);
   vrt_wsp_exp(ctx->vsl, ctx->req->t_req, &ctx->req->obj->exp);)

VRT_DO_EXP(beresp, ctx->bo->exp, grace, 0,
   vrt_wsp_exp(ctx->vsl, ctx->bo->exp.t_origin, &ctx->bo->exp);)
VRT_DO_EXP(beresp, ctx->bo->exp, ttl, 0,
   vrt_wsp_exp(ctx->vsl, ctx->bo->exp.t_origin, &ctx->bo->exp);)
VRT_DO_EXP(beresp, ctx->bo->exp, keep, 0,
   vrt_wsp_exp(ctx->vsl, ctx->bo->exp.t_origin, &ctx->bo->exp);)

/*--------------------------------------------------------------------
 * req.xid
 */

const char *
VRT_r_req_xid(const struct vrt_ctx *ctx)
{
	char *p;
	int size;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);

	// XXX ?
	size = snprintf(NULL, 0, "%u", ctx->req->vsl->wid & VSL_IDENTMASK) + 1;
	AN(p = WS_Alloc(ctx->req->http->ws, size));
	assert(snprintf(p, size, "%u", ctx->req->vsl->wid & VSL_IDENTMASK) < size);
	return (p);
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

const void *
VRT_r_client_ip(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	return (&ctx->req->sp->sockaddr);
}

const void *
VRT_r_server_ip(const struct vrt_ctx *ctx)
{
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (ctx->req->sp->mysockaddr.ss_family == AF_UNSPEC) {
		i = getsockname(ctx->req->sp->fd,
		    (void*)&ctx->req->sp->mysockaddr,
		    &ctx->req->sp->mysockaddrlen);
		assert(VTCP_Check(i));
	}

	return (&ctx->req->sp->mysockaddr);
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

/*--------------------------------------------------------------------
 * XXX: This is pessimistically silly
 */

long
VRT_r_server_port(const struct vrt_ctx *ctx)
{
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (ctx->req->sp->mysockaddr.ss_family == AF_UNSPEC) {
		i = getsockname(ctx->req->sp->fd,
		    (void*)&ctx->req->sp->mysockaddr,
		    &ctx->req->sp->mysockaddrlen);
		assert(VTCP_Check(i));
	}
	return (VSA_Port(&ctx->req->sp->mysockaddr));
}

/*--------------------------------------------------------------------*/

#define VOBJ_L(type, field)						\
void									\
VRT_l_obj_##field(const struct vrt_ctx *ctx, type a)			\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req->obj, OBJECT_MAGIC);			\
	ctx->req->obj->field = a;					\
}

#define VOBJ_R(type, field)						\
type									\
VRT_r_obj_##field(const struct vrt_ctx *ctx)				\
{									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);				\
	CHECK_OBJ_NOTNULL(ctx->req->obj, OBJECT_MAGIC);			\
	return (ctx->req->obj->field);					\
}

VOBJ_L(long, hits)
VOBJ_R(long, hits)
VOBJ_L(double, last_use)
VOBJ_R(double, last_use)

unsigned
VRT_r_obj_uncacheable(const struct vrt_ctx *ctx)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->obj, OBJECT_MAGIC);
	return (ctx->req->obj->objcore->flags & OC_F_PASS ? 1 : 0);
}
