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
#include <stdarg.h>
#include <stdlib.h>

#include "vrt.h"
#include "vrt_obj.h"
#include "cache.h"
#include "cache_backend.h"
#include "hash_slinger.h"

#define ILLEGAL_R(sess, obj, field)                                         \
WSP(sess, SLT_VCL_error, "%s does not exist (reading field %s)", obj, field)

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

#define VRT_DO_HDR_l(obj, hdr, cont, http, fld)			\
void								\
VRT_l_##obj##_##hdr(const struct sess *sp, const char *p, ...)	\
{								\
	va_list ap;						\
								\
	va_start(ap, p);					\
	vrt_do_string(sp->wrk, sp->fd,				\
	    cont->http, fld, #obj "." #hdr, p, ap);		\
	va_end(ap);						\
}

#define VRT_DO_HDR_r(obj, hdr, cont, http, fld, nullable)	\
const char *							\
VRT_r_##obj##_##hdr(const struct sess *sp)			\
{								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
        if (!nullable || cont != NULL) {			\
            CHECK_OBJ_NOTNULL(cont->http, HTTP_MAGIC);		\
            return (cont->http->hd[fld].b);			\
        }							\
        ILLEGAL_R(sp, #obj, #hdr);				\
        return(NULL);                                           \
}                                                               \

#define VRT_DO_HDR(obj, hdr, cont, http, fld, nullable)		\
VRT_DO_HDR_l(obj, hdr, cont, http, fld)				\
VRT_DO_HDR_r(obj, hdr, cont, http, fld, nullable)		\

VRT_DO_HDR(req,         request,    sp,             http,	HTTP_HDR_REQ,	    0)
VRT_DO_HDR(req,         url,        sp,             http,	HTTP_HDR_URL,	    0)
VRT_DO_HDR(req,         proto,      sp,             http,	HTTP_HDR_PROTO,	    0)
VRT_DO_HDR(bereq,       request,    sp->wrk,        bereq,	HTTP_HDR_REQ,	    0)
VRT_DO_HDR(bereq,       url,        sp->wrk,        bereq,	HTTP_HDR_URL,	    0)
VRT_DO_HDR(bereq,       proto,      sp->wrk,        bereq,	HTTP_HDR_PROTO,	    0)
VRT_DO_HDR(obj,         proto,      sp->obj,        http,	HTTP_HDR_PROTO,	    0)
VRT_DO_HDR(obj,         response,   sp->obj,        http,	HTTP_HDR_RESPONSE,  0)
VRT_DO_HDR(resp,        proto,      sp->wrk,        resp,	HTTP_HDR_PROTO,	    0)
VRT_DO_HDR(resp,        response,   sp->wrk,        resp,	HTTP_HDR_RESPONSE,  0)
VRT_DO_HDR(beresp,      proto,      sp->wrk,        beresp,	HTTP_HDR_PROTO,	    0)
VRT_DO_HDR(beresp,      response,   sp->wrk,        beresp,	HTTP_HDR_RESPONSE,  0)
VRT_DO_HDR_r(stale_obj,   proto,      sp->stale_obj,  http,	HTTP_HDR_PROTO,	    1)
VRT_DO_HDR_r(stale_obj,   response,   sp->stale_obj,  http,	HTTP_HDR_RESPONSE,  1)

/*--------------------------------------------------------------------*/

#define VRT_DO_STATUS_l(obj, cont, http)			\
void								\
VRT_l_##obj##_status(const struct sess *sp, int num)		\
{								\
								\
	assert(num >= 100 && num <= 999);			\
	cont->http->status = num;				\
}

#define VRT_DO_STATUS_r(obj, cont, http, nullable)		\
int								\
VRT_r_##obj##_status(const struct sess *sp)			\
{								\
								\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			\
        if (nullable && cont == NULL) {				\
            ILLEGAL_R(sp, #obj, "status");			\
            return (503);                                       \
        }                                                       \
	return(cont->http->status);				\
}

#define VRT_DO_STATUS(obj, cont, http, nullable)		\
VRT_DO_STATUS_l(obj, cont, http)				\
VRT_DO_STATUS_r(obj, cont, http, nullable)			\

VRT_DO_STATUS(obj,          sp->obj,        http,	0)
VRT_DO_STATUS(beresp,       sp->wrk,        beresp,	0)
VRT_DO_STATUS(resp,	    sp->wrk,        resp,	0)
VRT_DO_STATUS_r(stale_obj,  sp->stale_obj,  http,	1)

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
VBERESP(beresp, unsigned, do_stream, do_stream)

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
	return (VTCP_port(sp->vbc->addr));
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

#define VRT_DO_EXP_l(which, cont, fld, extra)			    \
void __match_proto__()						    \
VRT_l_##which##_##fld(struct sess *sp, double a)		    \
{								    \
								    \
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			    \
	EXP_Set_##fld(&cont->exp, a);				    \
	extra;							    \
}

#define VRT_DO_EXP_r(which, cont, fld, nullable)		    \
double __match_proto__()					    \
VRT_r_##which##_##fld(struct sess *sp)				    \
{								    \
								    \
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);			    \
	if (nullable && cont == NULL) {				    \
	    ILLEGAL_R(sp, #which, #fld);			    \
	    return (-1);					    \
	}							    \
	return(EXP_Get_##fld(&cont->exp));			    \
}

#define VRT_DO_EXP(which, cont, fld, nullable, extra)		    \
VRT_DO_EXP_l(which, cont, fld, extra)				    \
VRT_DO_EXP_r(which, cont, fld, nullable)			    \

VRT_DO_EXP(req, sp, ttl, 0, )
VRT_DO_EXP(req, sp, grace, 0, )
VRT_DO_EXP(req, sp, keep, 0, )
VRT_DO_EXP(obj, sp->obj, grace, 0, EXP_Rearm(sp->obj))
VRT_DO_EXP(obj, sp->obj, ttl, 0, EXP_Rearm(sp->obj))
VRT_DO_EXP(obj, sp->obj, keep, 0, EXP_Rearm(sp->obj))
VRT_DO_EXP(beresp, sp->wrk, grace, 0, )
VRT_DO_EXP(beresp, sp->wrk, ttl, 0, )
VRT_DO_EXP(beresp, sp->wrk, keep, 0, )
VRT_DO_EXP_r(stale_obj, sp->stale_obj, grace, 1)
VRT_DO_EXP_r(stale_obj, sp->stale_obj, ttl, 1)
VRT_DO_EXP_r(stale_obj, sp->stale_obj, keep, 1)

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
		assert(VTCP_Check(i));
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
	return (VTCP_port(sp->mysockaddr));
}

/*--------------------------------------------------------------------*/

/* XXX: uplex/GS: a nice macro would eliminate the repetition here ... */

int
VRT_r_obj_hits(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	return (sp->obj->hits);
}

int
VRT_r_stale_obj_hits(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
        if (sp->stale_obj == NULL) {
            ILLEGAL_R(sp, "stale_obj", "hits");
            return (0);
        }
	CHECK_OBJ(sp->stale_obj, OBJECT_MAGIC);	/* XXX */
	return (sp->stale_obj->hits);
}

double
VRT_r_obj_lastuse(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	return (TIM_real() - sp->obj->last_use);
}

double
VRT_r_stale_obj_lastuse(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
        if (sp->stale_obj == NULL) {
            ILLEGAL_R(sp, "stale_obj", "lastuse");
            return (0);
        }
	CHECK_OBJ(sp->stale_obj, OBJECT_MAGIC);	/* XXX */
	return (TIM_real() - sp->stale_obj->last_use);
}

unsigned
VRT_r_req_backend_healthy(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	return (VDI_Healthy(sp->director, sp));
}

unsigned
VRT_r_stale_obj(const struct sess *sp)
{
        CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
        return (sp->stale_obj != NULL);
}
