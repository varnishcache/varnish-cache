/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
#include "vrt_obj.h"
#include "vtcp.h"
#include "vtim.h"

static char vrt_hostname[255] = "";

/*--------------------------------------------------------------------*/

static void
vrt_do_string(const struct http *hp, int fld,
    const char *err, const char *p, va_list ap)
{
	char *b;

	AN(hp);
	b = VRT_String(hp->ws, NULL, p, ap);
	if (b == NULL || *b == '\0') {
		VSLb(hp->vsl, SLT_LostHeader, "%s", err);
	} else {
		http_SetH(hp, fld, b);
	}
	va_end(ap);
}

#define VRT_DO_HDR(obj, hdr, http, fld)				\
void								\
VRT_l_##obj##_##hdr(const struct CPAR *px, const char *p, ...)	\
{								\
	va_list ap;						\
								\
	CHECK_OBJ_NOTNULL(px, CMAGIC);				\
	va_start(ap, p);					\
	vrt_do_string(http, fld, #obj "." #hdr, p, ap);		\
	va_end(ap);						\
}								\
								\
const char *							\
VRT_r_##obj##_##hdr(const struct CPAR *px)			\
{								\
	CHECK_OBJ_NOTNULL(px, CMAGIC);				\
	CHECK_OBJ_NOTNULL(http, HTTP_MAGIC);			\
	return (http->hd[fld].b);				\
}

#define VRT_DO_STATUS(obj, http)				\
void								\
VRT_l_##obj##_status(const struct CPAR *px, long num)		\
{								\
								\
	CHECK_OBJ_NOTNULL(px, CMAGIC);				\
	assert(num >= 100 && num <= 999);			\
	http->status = (uint16_t)num;				\
}								\
								\
long								\
VRT_r_##obj##_status(const struct CPAR *px)			\
{								\
								\
	CHECK_OBJ_NOTNULL(px, CMAGIC);				\
	return(http->status);					\
}

#define CPAR req
#define CMAGIC REQ_MAGIC
VRT_DO_HDR(req,    method,	px->http,	HTTP_HDR_METHOD)
VRT_DO_HDR(req,    request,	px->http,	HTTP_HDR_METHOD)
VRT_DO_HDR(req,    url,		px->http,	HTTP_HDR_URL)
VRT_DO_HDR(req,    proto,	px->http,	HTTP_HDR_PROTO)
VRT_DO_HDR(obj,    proto,	px->obj->http,	HTTP_HDR_PROTO)
VRT_DO_HDR(obj,    response,	px->obj->http,	HTTP_HDR_RESPONSE)
VRT_DO_STATUS(obj,		px->obj->http)
VRT_DO_HDR(resp,   proto,	px->resp,	HTTP_HDR_PROTO)
VRT_DO_HDR(resp,   response,	px->resp,	HTTP_HDR_RESPONSE)
VRT_DO_STATUS(resp,		px->resp)
#undef CPAR
#undef CMAGIC

#define CPAR busyobj
#define CMAGIC BUSYOBJ_MAGIC
VRT_DO_HDR(bereq,  method,	px->bereq,	HTTP_HDR_METHOD)
VRT_DO_HDR(bereq,  request,	px->bereq,	HTTP_HDR_METHOD)
VRT_DO_HDR(bereq,  url,		px->bereq,	HTTP_HDR_URL)
VRT_DO_HDR(bereq,  proto,	px->bereq,	HTTP_HDR_PROTO)
VRT_DO_HDR(beresp, proto,	px->beresp,	HTTP_HDR_PROTO)
VRT_DO_HDR(beresp, response,	px->beresp,	HTTP_HDR_RESPONSE)
VRT_DO_STATUS(beresp,		px->beresp)
#undef CPAR
#undef CMAGIC

/*--------------------------------------------------------------------*/

#define VBERESP(dir, type, onm, field)					\
void									\
VRT_l_##dir##_##onm(struct busyobj *bo, type a)				\
{									\
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);				\
	bo->field = a;							\
}									\
									\
type									\
VRT_r_##dir##_##onm(const struct busyobj *bo)				\
{									\
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);				\
	return (bo->field);						\
}

VBERESP(beresp, unsigned, do_esi,	do_esi)
VBERESP(beresp, unsigned, do_gzip,	do_gzip)
VBERESP(beresp, unsigned, do_gunzip,	do_gunzip)
VBERESP(beresp, unsigned, do_stream,	do_stream)
VBERESP(beresp, unsigned, do_pass,	do_pass)
VBERESP(beresp, unsigned, uncacheable,	do_pass)

/*--------------------------------------------------------------------*/

const char *
VRT_r_client_identity(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (req->client_identity != NULL)
		return (req->client_identity);
	else
		return (req->sp->addr);
}

void
VRT_l_client_identity(struct req *req, const char *str, ...)
{
	va_list ap;
	char *b;

	va_start(ap, str);
	b = VRT_String(req->http->ws, NULL, str, ap);
	va_end(ap);
	req->client_identity = b;
}

/*--------------------------------------------------------------------*/

#define BEREQ_TIMEOUT(which)					\
void								\
VRT_l_bereq_##which(struct busyobj *bo, double num)		\
{								\
								\
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);			\
	bo->which = (num > 0.0 ? num : 0.0);			\
}								\
								\
double								\
VRT_r_bereq_##which(const struct busyobj *bo)			\
{								\
								\
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);			\
	return (bo->which);					\
}

BEREQ_TIMEOUT(connect_timeout)
BEREQ_TIMEOUT(first_byte_timeout)
BEREQ_TIMEOUT(between_bytes_timeout)

/*--------------------------------------------------------------------*/

const char *
VRT_r_beresp_backend_name(const struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->vbc, VBC_MAGIC);
	return(bo->vbc->backend->vcl_name);
}

struct sockaddr_storage *
VRT_r_beresp_backend_ip(const struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->vbc, VBC_MAGIC);
	return(bo->vbc->addr);
}

long
VRT_r_beresp_backend_port(const struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->vbc, VBC_MAGIC);
	return (VTCP_port(bo->vbc->addr));
}

const char *
VRT_r_beresp_storage(const struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (bo->storage_hint != NULL)
		return (bo->storage_hint);
	else
		return (NULL);
}

void
VRT_l_beresp_storage(struct busyobj *bo, const char *str, ...)
{
	va_list ap;
	char *b;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	va_start(ap, str);
	b = VRT_String(bo->ws, NULL, str, ap);
	va_end(ap);
	bo->storage_hint = b;
}

/*--------------------------------------------------------------------*/

void
VRT_l_req_backend(struct req *req, struct director *be)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	req->director = be;
}

struct director *
VRT_r_req_backend(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return (req->director);
}

unsigned
VRT_r_req_backend_healthy(const struct req *req)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	/*
	 * XXX: Not optimal, but we do not have a backend in vcl_deliver
	 * XXX: and we have to return something.
	 */
	if (req->director == NULL)
		return (0);
	CHECK_OBJ_NOTNULL(req->director, DIRECTOR_MAGIC);
	return (VDI_Healthy(req->director, req->digest));
}

/*--------------------------------------------------------------------*/

void
VRT_l_bereq_backend(struct busyobj *bo, struct director *be)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AN(bo->director);
	bo->director = be;
}

struct director *
VRT_r_bereq_backend(const struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AN(bo->director);
	return (bo->director);
}

unsigned
VRT_r_bereq_backend_healthy(const struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->director, DIRECTOR_MAGIC);
	return (VDI_Healthy(bo->director, bo->digest));
}

/*--------------------------------------------------------------------*/

void
VRT_l_req_esi(struct req *req, unsigned process_esi)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	/*
	 * Only allow you to turn of esi in the main request
	 * else everything gets confused
	 */
	if(req->esi_level == 0)
		req->disable_esi = !process_esi;
}

unsigned
VRT_r_req_esi(const struct req *req)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return (!req->disable_esi);
}

long
VRT_r_req_esi_level(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return(req->esi_level);
}

/*--------------------------------------------------------------------*/

unsigned
VRT_r_req_can_gzip(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return (RFC2616_Req_Gzip(req->http));
}


/*--------------------------------------------------------------------*/

long
VRT_r_req_restarts(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return (req->restarts);
}

/*--------------------------------------------------------------------
 * NB: TTL is relative to when object was created, whereas grace and
 * keep are relative to ttl.
 */

#define VRT_DO_EXP(which, exp, fld, offset, extra)		\
								\
void								\
VRT_l_##which##_##fld(struct CPAR *px, double a)		\
{								\
								\
	CHECK_OBJ_NOTNULL(px, CMAGIC);				\
	if (a > 0.)						\
		a += offset;					\
	EXP_Set_##fld(&exp, a);					\
	extra;							\
}								\
								\
double								\
VRT_r_##which##_##fld(const struct CPAR *px)			\
{								\
								\
	CHECK_OBJ_NOTNULL(px, CMAGIC);				\
	return(EXP_Get_##fld(&exp) - offset);			\
}

static void
vrt_wsp_exp(struct vsl_log *vsl, unsigned xid, double now, const struct exp *e)
{
	double dt;

	dt = now - e->entered;
	VSLb(vsl, SLT_TTL, "%u VCL %.0f %.0f %.0f %.0f %.0f",
	    xid, e->ttl - dt, e->grace, e->keep, now, e->age + dt);
}

#define CPAR req
#define CMAGIC REQ_MAGIC
VRT_DO_EXP(req, px->exp, ttl, 0, )
VRT_DO_EXP(req, px->exp, grace, 0, )
VRT_DO_EXP(req, px->exp, keep, 0, )

VRT_DO_EXP(obj, px->obj->exp, grace, 0,
   EXP_Rearm(px->obj);
   vrt_wsp_exp(px->vsl, px->obj->vxid, px->t_req, &px->obj->exp);)
VRT_DO_EXP(obj, px->obj->exp, ttl,
   (px->t_req - px->obj->exp.entered),
   EXP_Rearm(px->obj);
   vrt_wsp_exp(px->vsl, px->obj->vxid, px->t_req, &px->obj->exp);)
VRT_DO_EXP(obj, px->obj->exp, keep, 0,
   EXP_Rearm(px->obj);
   vrt_wsp_exp(px->vsl, px->obj->vxid, px->t_req, &px->obj->exp);)
#undef CPAR
#undef CMAGIC

#define CPAR busyobj
#define CMAGIC BUSYOBJ_MAGIC
VRT_DO_EXP(beresp, px->exp, grace, 0,
   vrt_wsp_exp(px->vsl, px->vsl->wid & VSL_IDENTMASK,
	px->exp.entered, &px->exp);)
VRT_DO_EXP(beresp, px->exp, ttl, 0,
   vrt_wsp_exp(px->vsl, px->vsl->wid & VSL_IDENTMASK,
	px->exp.entered, &px->exp);)
VRT_DO_EXP(beresp, px->exp, keep, 0,
   vrt_wsp_exp(px->vsl, px->vsl->wid & VSL_IDENTMASK,
	px->exp.entered, &px->exp);)
#undef CPAR
#undef CMAGIC

/*--------------------------------------------------------------------
 * req.xid
 */

const char *
VRT_r_req_xid(const struct req *req)
{
	char *p;
	int size;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	size = snprintf(NULL, 0, "%u", req->vsl->wid & VSL_IDENTMASK) + 1;
	AN(p = WS_Alloc(req->http->ws, size));
	assert(snprintf(p, size, "%u", req->vsl->wid & VSL_IDENTMASK) < size);
	return (p);
}

/*--------------------------------------------------------------------*/

#define REQ_BOOL(hash_var)					\
void								\
VRT_l_req_##hash_var(struct req *req, unsigned val)		\
{								\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	req->hash_var = val ? 1 : 0;				\
}								\
								\
unsigned							\
VRT_r_req_##hash_var(const struct req *req)			\
{								\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	return(req->hash_var);					\
}

REQ_BOOL(hash_ignore_busy)
REQ_BOOL(hash_always_miss)

/*--------------------------------------------------------------------*/

struct sockaddr_storage *
VRT_r_client_ip(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return (&req->sp->sockaddr);
}

struct sockaddr_storage *
VRT_r_server_ip(const struct req *req)
{
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (req->sp->mysockaddr.ss_family == AF_UNSPEC) {
		i = getsockname(req->sp->fd,
		    (void*)&req->sp->mysockaddr, &req->sp->mysockaddrlen);
		assert(VTCP_Check(i));
	}

	return (&req->sp->mysockaddr);
}

const char*
VRT_r_server_identity(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (heritage.identity[0] != '\0')
		return (heritage.identity);
	else
		return (heritage.name);
}

const char*
VRT_r_server_hostname(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (vrt_hostname[0] == '\0')
		AZ(gethostname(vrt_hostname, sizeof(vrt_hostname)));
	return (vrt_hostname);
}

/*--------------------------------------------------------------------
 * XXX: This is pessimistically silly
 */

long
VRT_r_server_port(const struct req *req)
{
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (req->sp->mysockaddr.ss_family == AF_UNSPEC) {
		i = getsockname(req->sp->fd,
		    (void*)&req->sp->mysockaddr, &req->sp->mysockaddrlen);
		assert(VTCP_Check(i));
	}
	return (VTCP_port(&req->sp->mysockaddr));
}

/*--------------------------------------------------------------------*/

long
VRT_r_obj_hits(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);	/* XXX */
	return (req->obj->hits);
}

double
VRT_r_obj_lastuse(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);	/* XXX */
	return (VTIM_real() - req->obj->last_use);
}

unsigned
VRT_r_obj_uncacheable(const struct req *req)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	return (req->obj->objcore->flags & OC_F_PASS ? 1 : 0);
}
