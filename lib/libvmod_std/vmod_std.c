/*-
 * Copyright (c) 2010-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>

#include "vrt.h"
#include "vtcp.h"

#include "cache/cache.h"

#include "vcc_if.h"

void __match_proto__(td_std_set_ip_tos)
vmod_set_ip_tos(struct req *req, long tos)
{
	int itos = tos;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	VTCP_Assert(setsockopt(req->sp->fd,
	    IPPROTO_IP, IP_TOS, &itos, sizeof(itos)));
}

static const char *
vmod_updown(struct req *req, int up, const char *s, va_list ap)
{
	unsigned u;
	char *b, *e;
	const char *p;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	u = WS_Reserve(req->ws, 0);
	e = b = req->ws->f;
	e += u;
	p = s;
	while (p != vrt_magic_string_end && b < e) {
		if (p != NULL) {
			for (; b < e && *p != '\0'; p++)
				if (up)
					*b++ = (char)toupper(*p);
				else
					*b++ = (char)tolower(*p);
		}
		p = va_arg(ap, const char *);
	}
	if (b < e)
		*b = '\0';
	b++;
	if (b > e) {
		WS_Release(req->ws, 0);
		return (NULL);
	} else {
		e = b;
		b = req->ws->f;
		WS_Release(req->ws, e - b);
		return (b);
	}
}

const char * __match_proto__(td_std_toupper)
vmod_toupper(struct req *req, const char *s, ...)
{
	const char *p;
	va_list ap;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	va_start(ap, s);
	p = vmod_updown(req, 1, s, ap);
	va_end(ap);
	return (p);
}

const char * __match_proto__(td_std_tolower)
vmod_tolower(struct req *req, const char *s, ...)
{
	const char *p;
	va_list ap;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	va_start(ap, s);
	p = vmod_updown(req, 0, s, ap);
	va_end(ap);
	return (p);
}

double __match_proto__(td_std_random)
vmod_random(struct req *req, double lo, double hi)
{
	double a;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	a = drand48();
	a *= hi - lo;
	a += lo;
	return (a);
}

void __match_proto__(td_std_log)
vmod_log(struct req *req, const char *fmt, ...)
{
	unsigned u;
	va_list ap;
	txt t;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	u = WS_Reserve(req->ws, 0);
	t.b = req->ws->f;
	va_start(ap, fmt);
	t.e = VRT_StringList(t.b, u, fmt, ap);
	va_end(ap);
	if (t.e != NULL) {
		assert(t.e > t.b);
		t.e--;
		VSLbt(req->vsl, SLT_VCL_Log, t);
	}
	WS_Release(req->ws, 0);
}

void __match_proto__(td_std_syslog)
vmod_syslog(struct req *req, long fac, const char *fmt, ...)
{
	char *p;
	unsigned u;
	va_list ap;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	u = WS_Reserve(req->ws, 0);
	p = req->ws->f;
	va_start(ap, fmt);
	p = VRT_StringList(p, u, fmt, ap);
	va_end(ap);
	if (p != NULL)
		syslog((int)fac, "%s", p);
	WS_Release(req->ws, 0);
}

void __match_proto__(td_std_collect)
vmod_collect(struct req *req, enum gethdr_e e, const char *h)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (e == HDR_REQ)
		http_CollectHdr(req->http, h);
	else if (e == HDR_BERESP && req->busyobj != NULL)
		http_CollectHdr(req->busyobj->beresp, h);
}
