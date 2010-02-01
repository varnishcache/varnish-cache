/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "shmlog.h"
#include "vrt.h"
#include "vrt_obj.h"
#include "vcl.h"
#include "cache.h"
#include "hash_slinger.h"
#include "cache_backend.h"

/*XXX: sort of a hack, improve the Tcl code in the compiler to avoid */
/*lint -save -esym(818,sp) */

const void * const vrt_magic_string_end = &vrt_magic_string_end;
static char vrt_hostname[255] = "";

/*--------------------------------------------------------------------*/

void
VRT_error(struct sess *sp, unsigned code, const char *reason)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	WSL(sp->wrk, SLT_Debug, 0, "VCL_error(%u, %s)", code, reason ?
	    reason : "(null)");
	sp->err_code = code ? code : 503;
	sp->err_reason = reason ? reason : http_StatusMessage(sp->err_code);
}

/*--------------------------------------------------------------------*/

void
VRT_count(const struct sess *sp, unsigned u)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (params->vcl_trace)
		WSP(sp, SLT_VCL_trace, "%u %d.%d", u,
		    sp->vcl->ref[u].line, sp->vcl->ref[u].pos);
}

/*--------------------------------------------------------------------*/

void
VRT_acl_log(const struct sess *sp, const char *msg)
{
	WSL(sp->wrk, SLT_VCL_acl, sp->fd, msg);
}

/*--------------------------------------------------------------------*/

static struct http *
vrt_selecthttp(const struct sess *sp, enum gethdr_e where)
{
	struct http *hp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	switch (where) {
	case HDR_REQ:
		hp = sp->http;
		break;
	case HDR_BEREQ:
		hp = sp->wrk->bereq;
		break;
	case HDR_BERESP:
		hp = sp->wrk->beresp;
		break;
	case HDR_RESP:
		hp = sp->wrk->resp;
		break;
	case HDR_OBJ:
		hp = sp->obj->http;
		break;
	default:
		INCOMPL();
	}
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	return (hp);
}

char *
VRT_GetHdr(const struct sess *sp, enum gethdr_e where, const char *n)
{
	char *p;
	struct http *hp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	hp = vrt_selecthttp(sp, where);
	if (!http_GetHdr(hp, n, &p))
		return (NULL);
	return (p);
}

/*--------------------------------------------------------------------
 * XXX: Optimize the single element case ?
 */

/*lint -e{818} ap,hp could be const */
static char *
vrt_assemble_string(struct http *hp, const char *h, const char *p, va_list ap)
{
	char *b, *e;
	unsigned u, x;

	u = WS_Reserve(hp->ws, 0);
	e = b = hp->ws->f;
	e += u;
	if (h != NULL) {
		x = strlen(h);
		if (b + x < e)
			memcpy(b, h, x);
		b += x;
		if (b + 1 < e)
			*b++ = ' ';
	}
	while (p != vrt_magic_string_end) {
		if (p == NULL)
			p = "(null)";
		x = strlen(p);
		if (b + x < e)
			memcpy(b, p, x);
		b += x;
		p = va_arg(ap, const char *);
	}
	if (b + 1 < e)
		*b++ = '\0';
	if (b > e) {
		WS_Release(hp->ws, 0);
		return (NULL);
	} else {
		e = b;
		b = hp->ws->f;
		WS_Release(hp->ws, 1 + e - b);
		return (b);
	}
}

/*--------------------------------------------------------------------*/

void
VRT_SetHdr(const struct sess *sp , enum gethdr_e where, const char *hdr,
    const char *p, ...)
{
	struct http *hp;
	va_list ap;
	char *b;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	hp = vrt_selecthttp(sp, where);
	va_start(ap, p);
	if (p == NULL) {
		http_Unset(hp, hdr);
	} else {
		b = vrt_assemble_string(hp, hdr + 1, p, ap);
		if (b == NULL) {
			WSP(sp, SLT_LostHeader, "%s", hdr + 1);
		} else {
			http_Unset(hp, hdr);
			http_SetHeader(sp->wrk, sp->fd, hp, b);
		}
	}
	va_end(ap);
}

/*--------------------------------------------------------------------*/

static void
vrt_do_string(struct worker *w, int fd, struct http *hp, int fld,
    const char *err, const char *p, va_list ap)
{
	char *b;

	AN(p);
	AN(hp);
	b = vrt_assemble_string(hp, NULL, p, ap);
	if (b == NULL) {
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
	AN(p);							\
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

/* XXX: review this */

void
VRT_l_obj_status(const struct sess *sp, int num)
{
	char *p;

	assert(num >= 100 && num <= 999);
	p = WS_Alloc(sp->obj->http->ws, 4);
	if (p == NULL)
		WSP(sp, SLT_LostHeader, "%s", "obj.status");
	else
		sprintf(p, "%d", num);
	http_SetH(sp->obj->http, HTTP_HDR_STATUS, p);
}

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
	if (!sp->vbe)
		return;
	CHECK_OBJ_NOTNULL(sp->vbe, VBE_CONN_MAGIC);
	if (!sp->vbe->backend)
		return;
	CHECK_OBJ_NOTNULL(sp->vbe->backend, BACKEND_MAGIC);
	if (!sp->objhead)
		return;
	CHECK_OBJ_NOTNULL(sp->objhead, OBJHEAD_MAGIC);

	/* Setting a negative holdoff period is a mistake. Detecting this
	 * when compiling the VCL would be better.
	 */
	assert(a > 0);

	ALLOC_OBJ(new, TROUBLE_MAGIC);
	AN(new);
	new->target = (uintptr_t)sp->objhead;
	new->timeout = sp->t_req + a;

	/* Insert the new item on the list before the first item with a
	 * timeout at a later date (ie: sort by which entry will time out
	 * from the list
	 */
	Lck_Lock(&sp->vbe->backend->mtx);
	VTAILQ_FOREACH_SAFE(tr, &sp->vbe->backend->troublelist, list, tr2) {
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
		VTAILQ_INSERT_TAIL(&sp->vbe->backend->troublelist, new, list);

	Lck_Unlock(&sp->vbe->backend->mtx);
}

int
VRT_r_obj_status(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	/* XXX: use http_GetStatus() */
	if (sp->obj->http->status)
		return (sp->obj->http->status);
	AN(sp->obj->http->hd[HTTP_HDR_STATUS].b);
	return (atoi(sp->obj->http->hd[HTTP_HDR_STATUS].b));
}

void
VRT_l_resp_status(const struct sess *sp, int num)
{
	char *p;

	assert(num >= 100 && num <= 999);
	p = WS_Alloc(sp->wrk->ws, 4);
	if (p == NULL)
		WSP(sp, SLT_LostHeader, "%s", "resp.status");
	else
		sprintf(p, "%d", num);
	http_SetH(sp->wrk->resp, HTTP_HDR_STATUS, p);
}

int
VRT_r_resp_status(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk->resp, HTTP_MAGIC);
	return (atoi(sp->wrk->resp->hd[HTTP_HDR_STATUS].b));
}

/*--------------------------------------------------------------------*/

#define VBEREQ(dir, type,onm,field)					\
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

VBEREQ(beresp, unsigned, cacheable, cacheable)
VBEREQ(beresp, double, grace, grace)

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
	if (a <= 0)
		sp->wrk->ttl = sp->t_req - 1;
	else
		sp->wrk->ttl = sp->t_req + a;
}

double
VRT_r_beresp_ttl(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->wrk->ttl - sp->t_req);
}

void
VRT_l_beresp_status(const struct sess *sp, int num)
{
	char *p;

	assert(num >= 100 && num <= 999);
	p = WS_Alloc(sp->wrk->beresp->ws, 4);
	if (p == NULL)
		WSP(sp, SLT_LostHeader, "%s", "obj.status");
	else
		sprintf(p, "%d", num);
	http_SetH(sp->wrk->beresp, HTTP_HDR_STATUS, p);
}

int
VRT_r_beresp_status(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	/* XXX: use http_GetStatus() */
	if (sp->wrk->beresp->status)
		return (sp->wrk->beresp->status);
	AN(sp->wrk->beresp->hd[HTTP_HDR_STATUS].b);
	return (atoi(sp->wrk->beresp->hd[HTTP_HDR_STATUS].b));
}


void
VRT_l_bereq_connect_timeout(struct sess *sp, double num)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sp->wrk->connect_timeout = (num > 0 ? num : 0);
}

double
VRT_r_bereq_connect_timeout(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->wrk->connect_timeout);
}

void
VRT_l_bereq_first_byte_timeout(struct sess *sp, double num)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sp->wrk->first_byte_timeout = (num > 0 ? num : 0);
}

double
VRT_r_bereq_first_byte_timeout(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->wrk->first_byte_timeout);
}

void
VRT_l_bereq_between_bytes_timeout(struct sess *sp, double num)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sp->wrk->between_bytes_timeout = (num > 0 ? num : 0);
}

double
VRT_r_bereq_between_bytes_timeout(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->wrk->between_bytes_timeout);
}

/*--------------------------------------------------------------------*/

void
VRT_handling(struct sess *sp, unsigned hand)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(hand < VCL_RET_MAX);
	sp->handling = hand;
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
	if (a <= 0)
		sp->obj->ttl = sp->t_req - 1;
	else
		sp->obj->ttl = sp->t_req + a;
	EXP_Rearm(sp->obj);
}

double
VRT_r_obj_ttl(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	if (sp->obj->objcore == NULL)
		return (0.0);
	return (sp->obj->ttl - sp->t_req);
}

/*--------------------------------------------------------------------
 * obj.grace is relative to obj.ttl, so no special magic is necessary.
 */

void
VRT_l_obj_grace(const struct sess *sp, double a)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	if (a < 0)
		a = 0;
	sp->obj->grace = a;
	EXP_Rearm(sp->obj);
}

double
VRT_r_obj_grace(const struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	if (isnan(sp->obj->grace))
		return ((double)params->default_grace);
	return (sp->obj->grace);
}

/*--------------------------------------------------------------------*/

#define VOBJ(type,onm,field)						\
void									\
VRT_l_obj_##onm(const struct sess *sp, type a)				\
{									\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);				\
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */	\
	sp->obj->field = a;						\
}									\
									\
type									\
VRT_r_obj_##onm(const struct sess *sp)					\
{									\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);				\
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */	\
	return (sp->obj->field);					\
}

VOBJ(unsigned, cacheable, cacheable)

/*--------------------------------------------------------------------*/

void
VRT_l_req_backend(struct sess *sp, struct director *be)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sp->director = be;
}

/*lint -e{818} sp could be const */
struct director *
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
	/* only allow you to turn of esi in the main request
	   else everything gets confused */
	if(sp->esis == 0)
	   sp->disable_esi = !process_esi;
}

unsigned
VRT_r_req_esi(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (!sp->disable_esi);
}

/*--------------------------------------------------------------------*/

int
VRT_r_req_restarts(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->restarts);
}

/*--------------------------------------------------------------------
 * req.grace
 */

void
VRT_l_req_grace(struct sess *sp, double a)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (a < 0)
		a = 0;
	sp->grace = a;
}

/*lint -e{818} sp could be const */
double
VRT_r_req_grace(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (isnan(sp->grace))
		return ((double)params->default_grace);
	return (sp->grace);
}

/*--------------------------------------------------------------------
 * req.xid
 */

/*lint -e{818} sp could be const */
const char *
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

struct sockaddr *
VRT_r_client_ip(const struct sess *sp)
{

	return (sp->sockaddr);
}

struct sockaddr *
VRT_r_server_ip(struct sess *sp)
{

	if (sp->mysockaddr->sa_family == AF_UNSPEC)
		assert(!getsockname(sp->fd, sp->mysockaddr, &sp->mysockaddrlen)
		    || errno == ECONNRESET);

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
		AZ(gethostname(vrt_hostname, 255));

	return (vrt_hostname);
}

int
VRT_r_server_port(struct sess *sp)
{
	char abuf[TCP_ADDRBUFSIZE];
	char pbuf[TCP_PORTBUFSIZE];

	if (sp->mysockaddr->sa_family == AF_UNSPEC)
		AZ(getsockname(sp->fd, sp->mysockaddr, &sp->mysockaddrlen));
	TCP_name(sp->mysockaddr, sp->mysockaddrlen,
	    abuf, sizeof abuf, pbuf, sizeof pbuf);

	return (atoi(pbuf));
}

/*--------------------------------------------------------------------
 * Add an element to the array/list of hash bits.
 */

void
VRT_l_req_hash(struct sess *sp, const char *str)
{

	HSH_AddString(sp, str);
}

/*--------------------------------------------------------------------*/

double
VRT_r_now(const struct sess *sp)
{

	(void)sp;
	return (TIM_real());
}

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
	return (VBE_Healthy_sp(sp, sp->director));
}

/*--------------------------------------------------------------------*/

char *
VRT_IP_string(const struct sess *sp, const struct sockaddr *sa)
{
	char *p;
	const struct sockaddr_in *si4;
	const struct sockaddr_in6 *si6;
	const void *addr;
	int len;

	switch (sa->sa_family) {
	case AF_INET:
		len = INET_ADDRSTRLEN;
		si4 = (const void *)sa;
		addr = &(si4->sin_addr);
		break;
	case AF_INET6:
		len = INET6_ADDRSTRLEN;
		si6 = (const void *)sa;
		addr = &(si6->sin6_addr);
		break;
	default:
		INCOMPL();
	}
	XXXAN(len);
	AN(p = WS_Alloc(sp->http->ws, len));
	AN(inet_ntop(sa->sa_family, addr, p, len));
	return (p);
}

char *
VRT_int_string(const struct sess *sp, int num)
{
	char *p;
	int size;

	size = snprintf(NULL, 0, "%d", num) + 1;
	AN(p = WS_Alloc(sp->http->ws, size));
	assert(snprintf(p, size, "%d", num) < size);
	return (p);
}

char *
VRT_double_string(const struct sess *sp, double num)
{
	char *p;
	int size;

	size = snprintf(NULL, 0, "%.3f", num) + 1;
	AN(p = WS_Alloc(sp->http->ws, size));
	assert(snprintf(p, size, "%.3f", num) < size);
	return (p);
}

char *
VRT_time_string(const struct sess *sp, double t)
{
	char *p;

	AN(p = WS_Alloc(sp->http->ws, TIM_FORMAT_SIZE));
	TIM_format(t, p);
	return p;
}

const char *
VRT_backend_string(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->director == NULL)
		return (NULL);
	return (sp->director->vcl_name);
}

/*--------------------------------------------------------------------*/

void
VRT_Rollback(struct sess *sp)
{

	*sp->http = *sp->http0;
	WS_Reset(sp->ws, sp->ws_req);
}

/*--------------------------------------------------------------------*/

void
VRT_ESI(struct sess *sp)
{

	if (sp->cur_method != VCL_MET_FETCH) {
		/* XXX: we should catch this at compile time */
		WSP(sp, SLT_VCL_error,
		    "esi can only be called from vcl_fetch");
		return;
	}

	sp->wrk->do_esi = 1;
}

/*--------------------------------------------------------------------*/

/*lint -e{818} sp could be const */
void
VRT_panic(struct sess *sp, const char *str, ...)
{
	va_list ap;
	char *b;

	va_start(ap, str);
	b = vrt_assemble_string(sp->http, "PANIC: ", str, ap);
	va_end(ap);
	lbv_assert("VCL", "", 0, b, 0, 2);
}

/*--------------------------------------------------------------------*/

/*lint -e{818} sp could be const */
void
VRT_synth_page(struct sess *sp, unsigned flags, const char *str, ...)
{
	va_list ap;
	const char *p;
	struct vsb *vsb;

	(void)flags;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	vsb = SMS_Makesynth(sp->obj);
	AN(vsb);

	vsb_cat(vsb, str);
	va_start(ap, str);
	p = va_arg(ap, const char *);
	while (p != vrt_magic_string_end) {
		if (p == NULL)
			p = "(null)";
		vsb_cat(vsb, p);
		p = va_arg(ap, const char *);
	}
	va_end(ap);
	SMS_Finish(sp->obj);
	http_Unset(sp->obj->http, H_Content_Length);
	http_PrintfHeader(sp->wrk, sp->fd, sp->obj->http,
	    "Content-Length: %d", sp->obj->len);
}

/*--------------------------------------------------------------------*/

void
VRT_purge(struct sess *sp, char *cmds, ...)
{
	char *a1, *a2, *a3;
	va_list ap;
	struct ban *b;
	int good;

	(void)sp;
	b = BAN_New();
	va_start(ap, cmds);
	a1 = cmds;
	good = 0;
	while (a1 != NULL) {
		good = 0;
		a2 = va_arg(ap, char *);
		if (a2 == NULL)
			break;
		a3 = va_arg(ap, char *);
		if (a3 == NULL)
			break;
		if (BAN_AddTest(NULL, b, a1, a2, a3))
			break;
		a1 = va_arg(ap, char *);
		good = 1;
	}
	if (!good)
		/* XXX: report error how ? */
		BAN_Free(b);
	else
		BAN_Insert(b);
}

/*--------------------------------------------------------------------*/

void
VRT_purge_string(struct sess *sp, const char *str, ...)
{
	char *p, *a1, *a2, *a3;
	char **av;
	va_list ap;
	struct ban *b;
	int good;
	int i;

	va_start(ap, str);
	p = vrt_assemble_string(sp->http, NULL, str, ap);
	if (p == NULL)
		/* XXX: report error how ? */
		return;

	av = ParseArgv(p, 0);
	if (av[0] != NULL) {
		/* XXX: report error how ? */
		FreeArgv(av);
		return;
	}
	b = BAN_New();
	good = 0;
	for (i = 1; ;) {
		a1 = av[i++];
		if (a1 == NULL)
			break;
		good = 0;
		a2 = av[i++];
		if (a2 == NULL)
			break;
		a3 = av[i++];
		if (a3 == NULL)
			break;
		if (BAN_AddTest(NULL, b, a1, a2, a3))
			break;
		good = 1;
		if (av[i] == NULL)
			break;
		good = 0;
		if (strcmp(av[i++], "&&"))
			break;
	}
	if (!good)
		/* XXX: report error how ? */
		BAN_Free(b);
	else
		BAN_Insert(b);
	FreeArgv(av);
}

/*--------------------------------------------------------------------
 * Simple stuff
 */

int
VRT_strcmp(const char *s1, const char *s2)
{
	if (s1 == NULL || s2 == NULL)
		return(1);
	return (strcmp(s1, s2));
}

void
VRT_memmove(void *dst, const void *src, unsigned len)
{

	(void)memmove(dst, src, len);
}

/*lint -restore */
