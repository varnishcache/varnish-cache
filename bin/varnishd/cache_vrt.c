/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "vrt.h"
#include "vrt_obj.h"
#include "vcl.h"
#include "cache.h"
#include "hash_slinger.h"
#include "cache_backend.h"

const void * const vrt_magic_string_end = &vrt_magic_string_end;

/*--------------------------------------------------------------------*/

void
VRT_error(struct sess *sp, unsigned code, const char *reason)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	WSL(sp->wrk, SLT_Debug, 0, "VCL_error(%u, %s)", code, reason ?
	    reason : "(null)");
	if (code < 100 || code > 999)
		code = 503;
	sp->err_code = (uint16_t)code;
	sp->err_reason = reason ? reason : http_StatusMessage(sp->err_code);
}

/*--------------------------------------------------------------------*/

void
VRT_count(const struct sess *sp, unsigned u)
{

	if (sp == NULL)
		return;
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
		CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
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
 * Build a string on the worker threads workspace
 */

const char *
VRT_WrkString(const struct sess *sp, const char *p, ...)
{
	va_list ap;
	char *b;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	va_start(ap, p);
	b = VRT_String(sp->wrk->ws, NULL, p, ap);
	va_end(ap);
	return (b);
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
		b = VRT_String(hp->ws, hdr + 1, p, ap);
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

void
VRT_handling(struct sess *sp, unsigned hand)
{

	if (sp == NULL) {
		assert(hand == VCL_RET_OK);
		return;
	}
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(hand < VCL_RET_MAX);
	sp->handling = hand;
}

/*--------------------------------------------------------------------
 * Add an element to the array/list of hash bits.
 */

void
VRT_hashdata(const struct sess *sp, const char *str, ...)
{
	va_list ap;
	const char *p;

	HSH_AddString(sp, str);
	va_start(ap, str);
	while (1) {
		p = va_arg(ap, const char *);
		if (p == vrt_magic_string_end)
			break;
		HSH_AddString(sp, p);
	}
}

/*--------------------------------------------------------------------*/

double
VRT_r_now(const struct sess *sp)
{

	(void)sp;
	return (TIM_real());
}

/*--------------------------------------------------------------------*/

char *
VRT_IP_string(const struct sess *sp, const struct sockaddr_storage *sa)
{
	char *p;
	const struct sockaddr_in *si4;
	const struct sockaddr_in6 *si6;
	const void *addr;
	int len;

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
	AN(p = WS_Alloc(sp->http->ws, len));
	AN(inet_ntop(sa->ss_family, addr, p, len));
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
VRT_backend_string(const struct sess *sp, const struct director *d)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (d == NULL)
		d = sp->director;
	if (d == NULL)
		return (NULL);
	return (d->vcl_name);
}

const char *
VRT_bool_string(const struct sess *sp, unsigned val)
{

	(void)sp;
	return (val ? "true" : "false");
}

/*--------------------------------------------------------------------*/

void
VRT_Rollback(struct sess *sp)
{

	HTTP_Copy(sp->http, sp->http0);
	WS_Reset(sp->ws, sp->ws_req);
}

/*--------------------------------------------------------------------*/

void
VRT_panic(const struct sess *sp, const char *str, ...)
{
	va_list ap;
	char *b;

	va_start(ap, str);
	b = VRT_String(sp->http->ws, "PANIC: ", str, ap);
	va_end(ap);
	VAS_Fail("VCL", "", 0, b, 0, 2);
}

/*--------------------------------------------------------------------*/

void
VRT_synth_page(const struct sess *sp, unsigned flags, const char *str, ...)
{
	va_list ap;
	const char *p;
	struct vsb *vsb;

	(void)flags;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	vsb = SMS_Makesynth(sp->obj);
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
	SMS_Finish(sp->obj);
	http_Unset(sp->obj->http, H_Content_Length);
	http_PrintfHeader(sp->wrk, sp->fd, sp->obj->http,
	    "Content-Length: %d", sp->obj->len);
}

/*--------------------------------------------------------------------*/

void
VRT_ban(struct sess *sp, char *cmds, ...)
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
VRT_ban_string(struct sess *sp, const char *str)
{
	char *a1, *a2, *a3;
	char **av;
	struct ban *b;
	int good;
	int i;

	(void)sp;
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
		/* XXX: report error how ? */
		BAN_Free(b);
	else
		BAN_Insert(b);
	VAV_Free(av);
}

/*--------------------------------------------------------------------
 * "real" purges
 */

void
VRT_purge(const struct sess *sp, double ttl, double grace)
{
	if (sp->cur_method == VCL_MET_HIT)
		HSH_Purge(sp, sp->obj->objcore->objhead, ttl, grace);
	else if (sp->cur_method == VCL_MET_MISS)
		HSH_Purge(sp, sp->objcore->objhead, ttl, grace);
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
