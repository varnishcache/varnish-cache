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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "cache_backend.h"
#include "hash/hash_slinger.h"
#include "vav.h"
#include "vcl.h"
#include "vrt.h"
#include "vrt_obj.h"
#include "vtim.h"

const void * const vrt_magic_string_end = &vrt_magic_string_end;
const void * const vrt_magic_string_unset = &vrt_magic_string_unset;

/*--------------------------------------------------------------------*/

const struct gethdr_s *
VRT_MkGethdr(struct req *req, enum gethdr_e where, const char *what)
{
	struct gethdr_s *retval;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	retval = (void*)WS_Alloc(req->wrk->aws, sizeof *retval);
	AN(retval);
	retval->where = where;
	retval->what = what;
	return (retval);
}


/*--------------------------------------------------------------------*/

void
VRT_error(struct req *req, unsigned code, const char *reason)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	VSLb(req->vsl, SLT_Debug, "VCL_error(%u, %s)", code,
	    reason ?  reason : "(null)");
	if (code < 100 || code > 999)
		code = 503;
	req->err_code = (uint16_t)code;
	req->err_reason = reason ? reason : http_StatusMessage(req->err_code);
}

/*--------------------------------------------------------------------*/

void
VRT_count(struct req *req, unsigned u)
{

	if (req == NULL)
		return;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	VSLb(req->vsl, SLT_VCL_trace, "%u %u.%u", u,
	    req->vcl->ref[u].line, req->vcl->ref[u].pos);
}

/*--------------------------------------------------------------------*/

void
VRT_acl_log(struct req *req, const char *msg)
{

	VSLb(req->vsl, SLT_VCL_acl, "%s", msg);
}

/*--------------------------------------------------------------------*/

static struct http *
vrt_selecthttp(const struct req *req, enum gethdr_e where)
{
	struct http *hp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	switch (where) {
	case HDR_REQ:
		hp = req->http;
		break;
	case HDR_BEREQ:
		hp = req->busyobj->bereq;
		break;
	case HDR_BERESP:
		hp = req->busyobj->beresp;
		break;
	case HDR_RESP:
		hp = req->resp;
		break;
	case HDR_OBJ:
		CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
		hp = req->obj->http;
		break;
	default:
		INCOMPL();
	}
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	return (hp);
}

char *
VRT_GetHdr(const struct req *req, const struct gethdr_s *hs)
{
	char *p;
	struct http *hp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	hp = vrt_selecthttp(req, hs->where);
	if (!http_GetHdr(hp, hs->what, &p))
		return (NULL);
	return (p);
}

/*--------------------------------------------------------------------
 * XXX: Optimize the single element case ?
 */

char *
VRT_StringList(char *d, unsigned dl, const char *p, va_list ap)
{
	char *b, *e;
	unsigned x;

	b = d;
	e = b + dl;
	while (p != vrt_magic_string_end && b < e) {
		if (p != NULL) {
			x = strlen(p);
			if (b + x < e)
				memcpy(b, p, x);
			b += x;
		}
		p = va_arg(ap, const char *);
	}
	if (b >= e)
		return (NULL);
	*b++ = '\0';
	return (b);
}

/*--------------------------------------------------------------------
 * XXX: Optimize the single element case ?
 */

char *
VRT_String(struct ws *ws, const char *h, const char *p, va_list ap)
{
	char *b, *e;
	unsigned u, x;

	u = WS_Reserve(ws, 0);
	e = b = ws->f;
	e += u;
	if (h != NULL) {
		x = strlen(h);
		if (b + x < e)
			memcpy(b, h, x);
		b += x;
		if (b < e)
			*b = ' ';
		b++;
	}
	b = VRT_StringList(b, e > b ? e - b : 0, p, ap);
	if (b == NULL || b == e) {
		WS_Release(ws, 0);
		return (NULL);
	}
	e = b;
	b = ws->f;
	WS_Release(ws, e - b);
	return (b);
}

/*--------------------------------------------------------------------
 * Build a string on the request workspace
 */

const char *
VRT_ReqString(struct req *req, const char *p, ...)
{
	va_list ap;
	char *b;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	va_start(ap, p);
	b = VRT_String(req->ws, NULL, p, ap);
	va_end(ap);
	return (b);
}

/*--------------------------------------------------------------------*/

void
VRT_SetHdr(struct req *req , const struct gethdr_s *hs, const char *p, ...)
{
	struct http *hp;
	va_list ap;
	char *b;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(hs);
	AN(hs->what);
	hp = vrt_selecthttp(req, hs->where);
	va_start(ap, p);
	if (p == vrt_magic_string_unset) {
		http_Unset(hp, hs->what);
	} else {
		b = VRT_String(hp->ws, hs->what + 1, p, ap);
		if (b == NULL) {
			VSLb(req->vsl, SLT_LostHeader, "%s", hs->what + 1);
		} else {
			http_Unset(hp, hs->what);
			http_SetHeader(hp, b);
		}
	}
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
VRT_handling(struct req *req, unsigned hand)
{

	if (req == NULL) {
		assert(hand == VCL_RET_OK);
		return;
	}
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	assert(hand < VCL_RET_MAX);
	req->handling = hand;
}

/*--------------------------------------------------------------------
 * Add an element to the array/list of hash bits.
 */

void
VRT_hashdata(struct req *req, const char *str, ...)
{
	va_list ap;
	const char *p;

	HSH_AddString(req, str);
	va_start(ap, str);
	while (1) {
		p = va_arg(ap, const char *);
		if (p == vrt_magic_string_end)
			break;
		HSH_AddString(req, p);
	}
}

/*--------------------------------------------------------------------*/

double
VRT_r_now(const struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return (VTIM_real());
}

/*--------------------------------------------------------------------*/

char *
VRT_IP_string(const struct req *req, const struct sockaddr_storage *sa)
{
	char *p;
	const struct sockaddr_in *si4;
	const struct sockaddr_in6 *si6;
	const void *addr;
	int len;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	switch (sa->ss_family) {
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
	AN(p = WS_Alloc(req->http->ws, len));
	AN(inet_ntop(sa->ss_family, addr, p, len));
	return (p);
}

char *
VRT_INT_string(const struct req *req, long num)
{
	char *p;
	int size;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	size = snprintf(NULL, 0, "%ld", num) + 1;
	AN(p = WS_Alloc(req->http->ws, size));
	assert(snprintf(p, size, "%ld", num) < size);
	return (p);
}

char *
VRT_REAL_string(const struct req *req, double num)
{
	char *p;
	int size;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	size = snprintf(NULL, 0, "%.3f", num) + 1;
	AN(p = WS_Alloc(req->http->ws, size));
	assert(snprintf(p, size, "%.3f", num) < size);
	return (p);
}

char *
VRT_TIME_string(const struct req *req, double t)
{
	char *p;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	p = WS_Alloc(req->http->ws, VTIM_FORMAT_SIZE);
	if (p != NULL)
		VTIM_format(t, p);
	return (p);
}

const char *
VRT_BACKEND_string(const struct req *req, const struct director *d)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (d == NULL)
		d = req->director;
	if (d == NULL)
		return (NULL);
	return (d->vcl_name);
}

const char *
VRT_BOOL_string(const struct req *req, unsigned val)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	return (val ? "true" : "false");
}

/*--------------------------------------------------------------------*/

void
VRT_Rollback(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	HTTP_Copy(req->http, req->http0);
	WS_Reset(req->ws, req->ws_req);
}

/*--------------------------------------------------------------------*/

void
VRT_synth_page(const struct req *req, unsigned flags, const char *str, ...)
{
	va_list ap;
	const char *p;
	struct vsb *vsb;

	(void)flags;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->obj, OBJECT_MAGIC);
	vsb = SMS_Makesynth(req->obj);
	AN(vsb);

	VSB_cat(vsb, str);
	va_start(ap, str);
	p = va_arg(ap, const char *);
	while (p != vrt_magic_string_end) {
		if (p == NULL)
			p = "(null)";
		VSB_cat(vsb, p);
		p = va_arg(ap, const char *);
	}
	va_end(ap);
	SMS_Finish(req->obj);
	http_Unset(req->obj->http, H_Content_Length);
	http_PrintfHeader(req->obj->http, "Content-Length: %zd", req->obj->len);
}

/*--------------------------------------------------------------------*/

void
VRT_ban(const struct req *req, char *cmds, ...)
{
	char *a1, *a2, *a3;
	va_list ap;
	struct ban *b;
	int good;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
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
		BAN_Free(b);		/* XXX: report error how ? */
	else
		(void)BAN_Insert(b);	/* XXX: report error how ? */
}

/*--------------------------------------------------------------------*/

void
VRT_ban_string(const struct req *req, const char *str)
{
	char *a1, *a2, *a3;
	char **av;
	struct ban *b;
	int good;
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	av = VAV_Parse(str, NULL, ARGV_NOESC);
	if (av[0] != NULL) {
		/* XXX: report error how ? */
		VAV_Free(av);
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
		BAN_Free(b);		/* XXX: report error how ? */
	else
		(void)BAN_Insert(b);	/* XXX: report error how ? */
	VAV_Free(av);
}

/*--------------------------------------------------------------------
 *
 */

int
VRT_CacheReqBody(struct req *req, long long maxsize)
{
	return (HTTP1_CacheReqBody(req, maxsize));
}

/*--------------------------------------------------------------------
 * "real" purges
 */

void
VRT_purge(struct req *req, double ttl, double grace)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (req->cur_method == VCL_MET_LOOKUP)
		HSH_Purge(req, req->obj->objcore->objhead, ttl, grace);
	else if (req->cur_method == VCL_MET_MISS)
		HSH_Purge(req, req->objcore->objhead, ttl, grace);
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
