/*-
 * Copyright (c) 2010-2014 Varnish Software AS
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
#include "vsa.h"
#include "vtim.h"

#include "cache/cache.h"
#include "cache/cache_backend.h"

#include "vcc_if.h"

VCL_VOID __match_proto__(td_std_set_ip_tos)
vmod_set_ip_tos(const struct vrt_ctx *ctx, VCL_INT tos)
{
	int itos = tos;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	VTCP_Assert(setsockopt(ctx->req->sp->fd,
	    IPPROTO_IP, IP_TOS, &itos, sizeof(itos)));
}

static const char *
vmod_updown(const struct vrt_ctx *ctx, int up, const char *s, va_list ap)
{
	unsigned u;
	char *b, *e;
	const char *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	u = WS_Reserve(ctx->ws, 0);
	e = b = ctx->ws->f;
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
		WS_Release(ctx->ws, 0);
		return (NULL);
	} else {
		e = b;
		b = ctx->ws->f;
		WS_Release(ctx->ws, e - b);
		return (b);
	}
}

VCL_STRING __match_proto__(td_std_toupper)
vmod_toupper(const struct vrt_ctx *ctx, const char *s, ...)
{
	const char *p;
	va_list ap;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	va_start(ap, s);
	p = vmod_updown(ctx, 1, s, ap);
	va_end(ap);
	return (p);
}

VCL_STRING __match_proto__(td_std_tolower)
vmod_tolower(const struct vrt_ctx *ctx, const char *s, ...)
{
	const char *p;
	va_list ap;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	va_start(ap, s);
	p = vmod_updown(ctx, 0, s, ap);
	va_end(ap);
	return (p);
}

VCL_REAL __match_proto__(td_std_random)
vmod_random(const struct vrt_ctx *ctx, VCL_REAL lo, VCL_REAL hi)
{
	double a;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	a = drand48();
	a *= hi - lo;
	a += lo;
	return (a);
}

VCL_VOID __match_proto__(td_std_log)
vmod_log(const struct vrt_ctx *ctx, const char *fmt, ...)
{
	unsigned u;
	va_list ap;
	txt t;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	u = WS_Reserve(ctx->ws, 0);
	t.b = ctx->ws->f;
	va_start(ap, fmt);
	t.e = VRT_StringList(t.b, u, fmt, ap);
	va_end(ap);
	if (t.e != NULL) {
		assert(t.e > t.b);
		t.e--;
		VSLbt(ctx->vsl, SLT_VCL_Log, t);
	}
	WS_Release(ctx->ws, 0);
}

VCL_VOID __match_proto__(td_std_syslog)
vmod_syslog(const struct vrt_ctx *ctx, VCL_INT fac, const char *fmt, ...)
{
	char *p;
	unsigned u;
	va_list ap;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	u = WS_Reserve(ctx->ws, 0);
	p = ctx->ws->f;
	va_start(ap, fmt);
	p = VRT_StringList(p, u, fmt, ap);
	va_end(ap);
	if (p != NULL)
		syslog((int)fac, "%s", p);
	WS_Release(ctx->ws, 0);
}

VCL_VOID __match_proto__(td_std_collect)
vmod_collect(const struct vrt_ctx *ctx, VCL_HEADER hdr)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (hdr->where == HDR_REQ)
		http_CollectHdr(ctx->http_req, hdr->what);
	else if (hdr->where == HDR_BEREQ) {
		http_CollectHdr(ctx->http_bereq, hdr->what);
	} else if (hdr->where == HDR_BERESP) {
		http_CollectHdr(ctx->http_beresp, hdr->what);
	} else if (hdr->where == HDR_RESP) {
		http_CollectHdr(ctx->http_resp, hdr->what);
	}
}

VCL_BOOL __match_proto__(td_std_healthy)
vmod_healthy(const struct vrt_ctx *ctx, VCL_BACKEND be)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (be == NULL)
		return (0);
	CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
	return (VDI_Healthy(be));
}

VCL_INT __match_proto__(td_std_port)
vmod_port(const struct vrt_ctx *ctx, VCL_IP ip)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ip == NULL)
		return (0);
	return (VSA_Port(ip));
}

VCL_VOID __match_proto__(td_std_timestamp)
vmod_timestamp(const struct vrt_ctx *ctx, VCL_STRING label)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (label == NULL)
		return;
	if (*label == '\0')
		return;
	if (ctx->bo != NULL && ctx->req == NULL) {
		/* Called from backend vcl methods */
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		VSLb_ts_busyobj(ctx->bo, label, VTIM_real());
	} else if (ctx->req != NULL) {
		/* Called from request vcl methdos */
		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
		VSLb_ts_req(ctx->req, label, VTIM_real());
	}
}
