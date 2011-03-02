/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>

#include "vrt.h"
#include "vrt_obj.h"
#include "cache.h"
#include "cache_backend.h"
#include "hash_slinger.h"

static char vrt_hostname[255] = "";

/*--------------------------------------------------------------------*/

static void
vrt_do_string(struct worker *w, int fd, const struct http *hp, int fld,
    const char *err, const char *p, va_list ap)
{
	char *b;

	// AN(p);
	AN(hp);
	b = VRT_String(hp->ws, NULL, p, ap);
	if (b == NULL || *b == '\0') {
		WSL(w, SLT_LostHeader, fd, err);
	} else {
		http_SetH(hp, fld, b);
	}
	va_end(ap);
}

#define VRT_DO_HDR(obj, hdr, http, fld)				\
void								\
VRT_l_##obj##_##hdr(const struct sess *sp, const char *p, ...)	\
{								\
	va_list ap;						\
								\
	va_start(ap, p);					\
	vrt_do_string(sp->wrk, sp->fd,				\
	    http, fld, #obj "." #hdr, p, ap);			\
	va_end(ap);						\
}								\
								\
const char *							\
VRT_r_##obj##_##hdr(const struct sess *sp)			\
{								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
	CHECK_OBJ_NOTNULL(http, HTTP_MAGIC);			\
	return (http->hd[fld].b);				\
}

VRT_DO_HDR(req,   request,	sp->http,		HTTP_HDR_REQ)
VRT_DO_HDR(req,   url,		sp->http,		HTTP_HDR_URL)
VRT_DO_HDR(req,   proto,	sp->http,		HTTP_HDR_PROTO)
VRT_DO_HDR(bereq, request,	sp->wrk->bereq,		HTTP_HDR_REQ)
VRT_DO_HDR(bereq, url,		sp->wrk->bereq,		HTTP_HDR_URL)
VRT_DO_HDR(bereq, proto,	sp->wrk->bereq,		HTTP_HDR_PROTO)
VRT_DO_HDR(obj,   proto,	sp->obj->http,		HTTP_HDR_PROTO)
VRT_DO_HDR(obj,   response,	sp->obj->http,		HTTP_HDR_RESPONSE)
VRT_DO_HDR(resp,  proto,	sp->wrk->resp,		HTTP_HDR_PROTO)
VRT_DO_HDR(resp,  response,	sp->wrk->resp,		HTTP_HDR_RESPONSE)
VRT_DO_HDR(beresp,  proto,	sp->wrk->beresp,	HTTP_HDR_PROTO)
VRT_DO_HDR(beresp,  response,	sp->wrk->beresp,	HTTP_HDR_RESPONSE)

/*--------------------------------------------------------------------*/

#define VRT_DO_STATUS(obj, http)				\
void								\
VRT_l_##obj##_status(const struct sess *sp, int num)		\
{								\
								\
	assert(num >= 100 && num <= 999);			\
	http->status = num;					\
}								\
								\
int								\
VRT_r_##obj##_status(const struct sess *sp)			\
{								\
								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
	return(http->status);					\
}

VRT_DO_STATUS(obj, sp->obj->http)
VRT_DO_STATUS(beresp, sp->wrk->beresp)
VRT_DO_STATUS(resp, sp->wrk->resp)

/*--------------------------------------------------------------------*/

/* XXX: review this */
/* Add an objecthead to the saintmode list for the (hopefully) relevant
 * backend. Some double-up asserting here to avoid assert-errors when there
 * is no object.
 */
void
VRT_l_beresp_saintmode(const struct sess *sp, double a)
{
	struct trouble *new;
	struct trouble *tr;
	struct trouble *tr2;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (!sp->vbc)
		return;
	CHECK_OBJ_NOTNULL(sp->vbc, VBC_MAGIC);
	if (!sp->vbc->backend)
		return;
	CHECK_OBJ_NOTNULL(sp->vbc->backend, BACKEND_MAGIC);
	if (!sp->objcore)
		return;
	CHECK_OBJ_NOTNULL(sp->objcore, OBJCORE_MAGIC);

	/* Setting a negative holdoff period is a mistake. Detecting this
	 * when compiling the VCL would be better.
	 */
	assert(a > 0);

	ALLOC_OBJ(new, TROUBLE_MAGIC);
	AN(new);
	new->target = (uintptr_t)(sp->objcore->objhead);
	new->timeout = sp->t_req + a;

	/* Insert the new item on the list before the first item with a
	 * timeout at a later date (ie: sort by which entry will time out
	 * from the list
	 */
	Lck_Lock(&sp->vbc->backend->mtx);
	VTAILQ_FOREACH_SAFE(tr, &sp->vbc->backend->troublelist, list, tr2) {
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
		VTAILQ_INSERT_TAIL(&sp->vbc->backend->troublelist, new, list);

	Lck_Unlock(&sp->vbc->backend->mtx);
}

/*--------------------------------------------------------------------*/

#define VBERESP(dir, type, onm, field)					\
void									\
VRT_l_##dir##_##onm(const struct sess *sp, type a)			\
{									\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);				\
	sp->wrk->field = a;						\
}									\
									\
type									\
VRT_r_##dir##_##onm(const struct sess *sp)				\
{									\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);				\
	return (sp->wrk->field);					\
}

VBERESP(beresp, unsigned, do_esi, do_esi)
VBERESP(beresp, unsigned, do_gzip, do_gzip)
VBERESP(beresp, unsigned, do_gunzip, do_gunzip)

/*--------------------------------------------------------------------*/

const char * __match_proto__()
VRT_r_client_identity(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->client_identity != NULL)
		return (sp->client_identity);
	else
		return (sp->addr);
}

void
VRT_l_client_identity(struct sess *sp, const char *str, ...)
{
	va_list ap;
	char *b;

	va_start(ap, str);
	b = VRT_String(sp->http->ws, NULL, str, ap);
	va_end(ap);
	sp->client_identity = b;
}

/*--------------------------------------------------------------------
 * XXX: Working relative to t_req is maybe not the right thing, we could
 * XXX: have spent a long time talking to the backend since then.
 * XXX: It might make sense to cache a timestamp as "current time"
 * XXX: before vcl_recv (== t_req) and vcl_fetch.
 * XXX: On the other hand, that might lead to inconsistent behaviour
 * XXX: where an object expires while we are running VCL code, and
 * XXX: and that may not be a good idea either.
 * XXX: See also related t_req use in cache_hash.c
 */

void
VRT_l_beresp_ttl(const struct sess *sp, double a)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	WSP(sp, SLT_TTL, "%u VCL %.0f %.0f", sp->xid, a, sp->t_req);
	/*
	 * If people set obj.ttl = 0s, they don't expect it to be cacheable
	 * any longer, but it will still be for up to 1s - epsilon because
	 * of the rounding to seconds.
	 * We special case and make sure that rounding does not surprise.
	 */
	if (a <= 0) {
		sp->wrk->exp.ttl = sp->t_req - 1;
		sp->wrk->exp.grace = 0.;
	} else
		sp->wrk->exp.ttl = sp->t_req + a;
}

double
VRT_r_beresp_ttl(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->wrk->exp.ttl - sp->t_req);
}

/*--------------------------------------------------------------------*/

#define BEREQ_TIMEOUT(which)					\
void __match_proto__()						\
VRT_l_bereq_##which(struct sess *sp, double num)		\
{								\
								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
	sp->wrk->which = (num > 0.0 ? num : 0.0);		\
}								\
								\
double __match_proto__()					\
VRT_r_bereq_##which(struct sess *sp)				\
{								\
								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
	return(sp->wrk->which);					\
}

BEREQ_TIMEOUT(connect_timeout)
BEREQ_TIMEOUT(first_byte_timeout)
BEREQ_TIMEOUT(between_bytes_timeout)

/*--------------------------------------------------------------------*/

const char *
VRT_r_beresp_backend_name(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbc, VBC_MAGIC);
	return(sp->vbc->backend->vcl_name);
}

struct sockaddr_storage *
VRT_r_beresp_backend_ip(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbc, VBC_MAGIC);
	return(sp->vbc->addr);
}

int
VRT_r_beresp_backend_port(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbc, VBC_MAGIC);
	return (TCP_port(sp->vbc->addr));
}

const char * __match_proto__()
VRT_r_beresp_storage(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->wrk->storage_hint != NULL)
		return (sp->wrk->storage_hint);
	else
		return (NULL);
}

void __match_proto__()
VRT_l_beresp_storage(struct sess *sp, const char *str, ...)
{
	va_list ap;
	char *b;

	va_start(ap, str);
	b = VRT_String(sp->wrk->ws, NULL, str, ap);
	va_end(ap);
	sp->wrk->storage_hint = b;
}

void
VRT_l_obj_ttl(const struct sess *sp, double a)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	if (sp->obj->objcore == NULL)
		return;
	WSP(sp, SLT_TTL, "%u VCL %.0f %.0f",
	    sp->obj->xid, a, sp->t_req);
	/*
	 * If people set obj.ttl = 0s, they don't expect it to be cacheable
	 * any longer, but it will still be for up to 1s - epsilon because
	 * of the rounding to seconds.
	 * We special case and make sure that rounding does not surprise.
	 */
	if (a <= 0) {
		sp->obj->exp.ttl = sp->t_req - 1;
		sp->obj->exp.grace = 0;
	} else
		sp->obj->exp.ttl = sp->t_req + a;
	EXP_Rearm(sp->obj);
}

double
VRT_r_obj_ttl(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	if (sp->obj->objcore == NULL)
		return (0.0);
	return (sp->obj->exp.ttl - sp->t_req);
}

/*--------------------------------------------------------------------*/

void
VRT_l_req_backend(struct sess *sp, struct director *be)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sp->director = be;
}

struct director * __match_proto__()
VRT_r_req_backend(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->director);
}

/*--------------------------------------------------------------------*/

void
VRT_l_req_esi(struct sess *sp, unsigned process_esi)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	/*
	 * Only allow you to turn of esi in the main request
	 * else everything gets confused
	 */
	if(sp->esi_level == 0)
		sp->disable_esi = !process_esi;
}

unsigned __match_proto__()
VRT_r_req_esi(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (!sp->disable_esi);
}

int
VRT_r_req_esi_level(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return(sp->esi_level);
}

/*--------------------------------------------------------------------*/

unsigned __match_proto__()
VRT_r_req_can_gzip(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (RFC2616_Req_Gzip(sp));
}


/*--------------------------------------------------------------------*/

int
VRT_r_req_restarts(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->restarts);
}

/*--------------------------------------------------------------------*/

#define VRT_DO_GRACE(which, fld, extra)				\
void __match_proto__()						\
VRT_l_##which##_grace(struct sess *sp, double a)		\
{								\
								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
	fld = a >= 0.0 ? a : NAN;				\
	extra;							\
}								\
								\
double __match_proto__()					\
VRT_r_##which##_grace(struct sess *sp)				\
{								\
								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
	return(EXP_Grace(fld));					\
}

VRT_DO_GRACE(req, sp->exp.grace, )
VRT_DO_GRACE(obj, sp->obj->exp.grace, EXP_Rearm(sp->obj))
VRT_DO_GRACE(beresp, sp->wrk->exp.grace, )

/*--------------------------------------------------------------------
 * req.xid
 */

const char * __match_proto__()
VRT_r_req_xid(struct sess *sp)
{
	char *p;
	int size;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	size = snprintf(NULL, 0, "%u", sp->xid) + 1;
	AN(p = WS_Alloc(sp->http->ws, size));
	assert(snprintf(p, size, "%u", sp->xid) < size);
	return (p);
}

/*--------------------------------------------------------------------*/

#define REQ_BOOL(which)						\
void __match_proto__()						\
VRT_l_req_##which(struct sess *sp, unsigned val)		\
{								\
								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
	sp->which = val ? 1 : 0;				\
}								\
								\
unsigned __match_proto__()					\
VRT_r_req_##which(struct sess *sp)				\
{								\
								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
	return(sp->which);					\
}

REQ_BOOL(hash_ignore_busy)
REQ_BOOL(hash_always_miss)

/*--------------------------------------------------------------------*/

struct sockaddr_storage *
VRT_r_client_ip(const struct sess *sp)
{

	return (sp->sockaddr);
}

struct sockaddr_storage *
VRT_r_server_ip(struct sess *sp)
{
	int i;

	if (sp->mysockaddr->ss_family == AF_UNSPEC) {
		i = getsockname(sp->fd, (void*)sp->mysockaddr, &sp->mysockaddrlen);
		assert(TCP_Check(i));
	}

	return (sp->mysockaddr);
}

const char*
VRT_r_server_identity(struct sess *sp)
{
	(void)sp;

	if (heritage.identity[0] != '\0')
		return heritage.identity;
	else
		return heritage.name;
}


const char*
VRT_r_server_hostname(struct sess *sp)
{
	(void)sp;

	if (vrt_hostname[0] == '\0')
		AZ(gethostname(vrt_hostname, sizeof(vrt_hostname)));

	return (vrt_hostname);
}

/*--------------------------------------------------------------------
 * XXX: This is pessimistically silly
 */

int
VRT_r_server_port(struct sess *sp)
{

	if (sp->mysockaddr->ss_family == AF_UNSPEC)
		AZ(getsockname(sp->fd, (void*)sp->mysockaddr, &sp->mysockaddrlen));
	return (TCP_port(sp->mysockaddr));
}

/*--------------------------------------------------------------------*/

int
VRT_r_obj_hits(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	return (sp->obj->hits);
}

double
VRT_r_obj_lastuse(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	return (TIM_real() - sp->obj->last_use);
}

unsigned
VRT_r_req_backend_healthy(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	return (VDI_Healthy(sp->director, sp));
}

