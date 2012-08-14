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
VRT_l_##obj##_##hdr(const struct req *req, const char *p, ...)	\
{								\
	va_list ap;						\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	va_start(ap, p);					\
	vrt_do_string(http, fld, #obj "." #hdr, p, ap);		\
	va_end(ap);						\
}								\
								\
const char *							\
VRT_r_##obj##_##hdr(const struct req *req)			\
{								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	CHECK_OBJ_NOTNULL(http, HTTP_MAGIC);			\
	return (http->hd[fld].b);				\
}

VRT_DO_HDR(req,   request,	req->http,		HTTP_HDR_REQ)
VRT_DO_HDR(req,   url,		req->http,		HTTP_HDR_URL)
VRT_DO_HDR(req,   proto,	req->http,		HTTP_HDR_PROTO)
VRT_DO_HDR(bereq, request,	req->busyobj->bereq,	HTTP_HDR_REQ)
VRT_DO_HDR(bereq, url,		req->busyobj->bereq,	HTTP_HDR_URL)
VRT_DO_HDR(bereq, proto,	req->busyobj->bereq,	HTTP_HDR_PROTO)
VRT_DO_HDR(obj,   proto,	req->obj->http,		HTTP_HDR_PROTO)
VRT_DO_HDR(obj,   response,	req->obj->http,		HTTP_HDR_RESPONSE)
VRT_DO_HDR(resp,  proto,	req->resp,		HTTP_HDR_PROTO)
VRT_DO_HDR(resp,  response,	req->resp,		HTTP_HDR_RESPONSE)
VRT_DO_HDR(beresp,  proto,	req->busyobj->beresp,	HTTP_HDR_PROTO)
VRT_DO_HDR(beresp,  response,	req->busyobj->beresp,	HTTP_HDR_RESPONSE)

/*--------------------------------------------------------------------*/

#define VRT_DO_STATUS(obj, http)				\
void								\
VRT_l_##obj##_status(const struct req *req, int num)		\
{								\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	assert(num >= 100 && num <= 999);			\
	http->status = (uint16_t)num;				\
}								\
								\
int								\
VRT_r_##obj##_status(const struct req *req)			\
{								\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	return(http->status);					\
}

VRT_DO_STATUS(obj, req->obj->http)
VRT_DO_STATUS(beresp, req->busyobj->beresp)
VRT_DO_STATUS(resp, req->resp)

/*--------------------------------------------------------------------*/

/* XXX: review this */
/* Add an objecthead to the saintmode list for the (hopefully) relevant
 * backend. Some double-up asserting here to avoid assert-errors when there
 * is no object.
 */
void
VRT_l_beresp_saintmode(const struct req *req, double a)
{
	struct trouble *new;
	struct trouble *tr;
	struct trouble *tr2;
	struct vbc *vbc;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->busyobj, BUSYOBJ_MAGIC);
	vbc = req->busyobj->vbc;
	if (!vbc)
		return;
	CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);
	if (!vbc->backend)
		return;
	CHECK_OBJ_NOTNULL(vbc->backend, BACKEND_MAGIC);
	if (!req->objcore->objhead)
		return;
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	/* Setting a negative holdoff period is a mistake. Detecting this
	 * when compiling the VCL would be better.
	 */
	assert(a > 0);

	ALLOC_OBJ(new, TROUBLE_MAGIC);
	AN(new);
	memcpy(new->digest, req->digest, sizeof new->digest);
	new->timeout = req->t_req + a;

	/* Insert the new item on the list before the first item with a
	 * timeout at a later date (ie: sort by which entry will time out
	 * from the list
	 */
	Lck_Lock(&vbc->backend->mtx);
	VTAILQ_FOREACH_SAFE(tr, &vbc->backend->troublelist, list, tr2) {
		if (tr->timeout < new->timeout) {
			VTAILQ_INSERT_BEFORE(tr, new, list);
			new = NULL;
			break;
		}
	}

	/* Insert the item at the end if the list is empty or all other
	 * items have a longer timeout.
	 */
	if (new)
		VTAILQ_INSERT_TAIL(&vbc->backend->troublelist, new, list);

	Lck_Unlock(&vbc->backend->mtx);
}

/*--------------------------------------------------------------------*/

#define VBERESP(dir, type, onm, field)					\
void									\
VRT_l_##dir##_##onm(const struct req *req, type a)			\
{									\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);				\
	req->field = a;							\
}									\
									\
type									\
VRT_r_##dir##_##onm(const struct req *req)				\
{									\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);				\
	return (req->field);						\
}

VBERESP(beresp, unsigned, do_esi, busyobj->do_esi)
VBERESP(beresp, unsigned, do_gzip, busyobj->do_gzip)
VBERESP(beresp, unsigned, do_gunzip, busyobj->do_gunzip)
VBERESP(beresp, unsigned, do_stream, busyobj->do_stream)
VBERESP(beresp, unsigned, do_pass, busyobj->do_pass)

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
void __match_proto__()						\
VRT_l_bereq_##which(struct req *req, double num)		\
{								\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	CHECK_OBJ_NOTNULL(req->busyobj, BUSYOBJ_MAGIC);		\
	req->busyobj->which = (num > 0.0 ? num : 0.0);		\
}								\
								\
double								\
VRT_r_bereq_##which(const struct req *req)			\
{								\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	CHECK_OBJ_NOTNULL(req->busyobj, BUSYOBJ_MAGIC);		\
	return(req->busyobj->which);				\
}

BEREQ_TIMEOUT(connect_timeout)
BEREQ_TIMEOUT(first_byte_timeout)
BEREQ_TIMEOUT(between_bytes_timeout)

/*--------------------------------------------------------------------*/

const char *
VRT_r_beresp_backend_name(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->busyobj->vbc, VBC_MAGIC);
	return(req->busyobj->vbc->backend->vcl_name);
}

struct sockaddr_storage *
VRT_r_beresp_backend_ip(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->busyobj->vbc, VBC_MAGIC);
	return(req->busyobj->vbc->addr);
}

int
VRT_r_beresp_backend_port(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->busyobj->vbc, VBC_MAGIC);
	return (VTCP_port(req->busyobj->vbc->addr));
}

const char *
VRT_r_beresp_storage(const struct req *req)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (req->storage_hint != NULL)
		return (req->storage_hint);
	else
		return (NULL);
}

void
VRT_l_beresp_storage(struct req *req, const char *str, ...)
{
	va_list ap;
	char *b;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	va_start(ap, str);
	b = VRT_String(req->busyobj->ws, NULL, str, ap);
	va_end(ap);
	req->storage_hint = b;
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

int
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

int
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
VRT_l_##which##_##fld(struct req *req, double a)		\
{								\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	if (a > 0.)						\
		a += offset;					\
	EXP_Set_##fld(&exp, a);					\
	extra;							\
}								\
								\
double								\
VRT_r_##which##_##fld(const struct req *req)			\
{								\
								\
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);			\
	return(EXP_Get_##fld(&exp) - offset);			\
}

static void
vrt_wsp_exp(struct req *req, unsigned xid, const struct exp *e)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	VSLb(req->vsl, SLT_TTL, "%u VCL %.0f %.0f %.0f %.0f %.0f",
	    xid, e->ttl - (req->t_req - e->entered), e->grace, e->keep,
	    req->t_req, e->age + (req->t_req - e->entered));
}

VRT_DO_EXP(req, req->exp, ttl, 0, )
VRT_DO_EXP(req, req->exp, grace, 0, )
VRT_DO_EXP(req, req->exp, keep, 0, )

VRT_DO_EXP(obj, req->obj->exp, grace, 0,
   EXP_Rearm(req->obj);
   vrt_wsp_exp(req, req->obj->vxid, &req->obj->exp);)
VRT_DO_EXP(obj, req->obj->exp, ttl,
   (req->t_req - req->obj->exp.entered),
   EXP_Rearm(req->obj);
   vrt_wsp_exp(req, req->obj->vxid, &req->obj->exp);)
VRT_DO_EXP(obj, req->obj->exp, keep, 0,
   EXP_Rearm(req->obj);
   vrt_wsp_exp(req, req->obj->vxid, &req->obj->exp);)

VRT_DO_EXP(beresp, req->busyobj->exp, grace, 0,
   vrt_wsp_exp(req, req->vsl->wid & VSL_IDENTMASK, &req->busyobj->exp);)
VRT_DO_EXP(beresp, req->busyobj->exp, ttl, 0,
   vrt_wsp_exp(req, req->vsl->wid & VSL_IDENTMASK, &req->busyobj->exp);)
VRT_DO_EXP(beresp, req->busyobj->exp, keep, 0,
   vrt_wsp_exp(req, req->vsl->wid & VSL_IDENTMASK, &req->busyobj->exp);)

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

int
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

int
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
VRT_r_req_backend_healthy(const struct req *req)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->director, DIRECTOR_MAGIC);
	return (VDI_Healthy(req->director, req));
}
