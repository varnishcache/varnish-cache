/*-
 * Copyright (c) 2012-2017 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cache/cache.h"

#include "vtcp.h"
#include "vtim.h"

#include "vcc_if.h"

VCL_VOID v_matchproto_(td_vtc_barrier_sync)
vmod_barrier_sync(VRT_CTX, VCL_STRING addr, VCL_DURATION tmo)
{
	const char *err;
	char buf[32];
	int sock, i;
	ssize_t sz;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(addr);
	AN(*addr);
	assert(tmo >= 0.0);

	VSLb(ctx->vsl, SLT_Debug, "barrier_sync(\"%s\")", addr);
	sock = VTCP_open(addr, NULL, 0., &err);
	if (sock < 0) {
		VRT_fail(ctx, "Barrier connection failed: %s", err);
		return;
	}

	sz = VTCP_read(sock, buf, sizeof buf, tmo);
	i = errno;
	closefd(&sock);
	if (sz < 0)
		VRT_fail(ctx, "Barrier read failed: %s (errno=%d)",
		    strerror(i), i);
	if (sz > 0)
		VRT_fail(ctx, "Barrier unexpected data (%zdB)", sz);
}

/*--------------------------------------------------------------------*/

VCL_BACKEND v_matchproto_(td_vtc_no_backend)
vmod_no_backend(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

VCL_STEVEDORE v_matchproto_(td_vtc_no_stevedore)
vmod_no_stevedore(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

/*--------------------------------------------------------------------*/

VCL_VOID v_matchproto_(td_vtc_panic)
vmod_panic(VRT_CTX, const char *str, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	va_start(ap, str);
	b = VRT_String(ctx->ws, "PANIC: ", str, ap);
	va_end(ap);
	VAS_Fail("VCL", "", 0, b, VAS_VCL);
}

/*--------------------------------------------------------------------*/

VCL_VOID v_matchproto_(td_vtc_sleep)
vmod_sleep(VRT_CTX, VCL_DURATION t)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	VTIM_sleep(t);
}

/*--------------------------------------------------------------------*/

static uintptr_t vtc_ws_snapshot;

static struct ws *
vtc_ws_find(VRT_CTX, VCL_ENUM which)
{

	if (!strcmp(which, "client"))
		return (ctx->ws);
	if (!strcmp(which, "backend"))
		return (ctx->bo->ws);
	if (!strcmp(which, "session"))
		return (ctx->req->sp->ws);
	if (!strcmp(which, "thread"))
		return (ctx->req->wrk->aws);
	WRONG("vtc_ws_find Illegal enum");
}

VCL_VOID v_matchproto_(td_vtc_workspace_alloc)
vmod_workspace_alloc(VRT_CTX, VCL_ENUM which, VCL_INT size)
{
	struct ws *ws;
	void *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = vtc_ws_find(ctx, which);
	if (ws == NULL)
		return;
	WS_Assert(ws);

	if (size < 0) {
		size += WS_Reserve(ws, 0);
		WS_Release(ws, 0);
	}
	if (size <= 0) {
		VRT_fail(ctx, "Attempted negative WS allocation");
		return;
	}
	p = WS_Alloc(ws, size);
	if (p == NULL)
		VRT_fail(ctx, "vtc.workspace_alloc");
	else
		memset(p, '\0', size);
}

VCL_INT v_matchproto_(td_vtc_workspace_free)
vmod_workspace_free(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	unsigned u;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = vtc_ws_find(ctx, which);
	if (ws == NULL)
		return(-1);
	WS_Assert(ws);

	u = WS_Reserve(ws, 0);
	WS_Release(ws, 0);
	return (u);
}

#define VTC_WS_OP(type, def, name, op)			\
VCL_##type v_matchproto_(td_vtc_workspace_##name)	\
vmod_workspace_##name(VRT_CTX, VCL_ENUM which)		\
{							\
	struct ws *ws;					\
							\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);		\
							\
	ws = vtc_ws_find(ctx, which);			\
	if (ws == NULL)					\
		return def ;				\
	WS_Assert(ws);					\
							\
	op;						\
}
VTC_WS_OP(VOID, , snapshot, (vtc_ws_snapshot = WS_Snapshot(ws)))
VTC_WS_OP(VOID, , reset, WS_Reset(ws, vtc_ws_snapshot))
VTC_WS_OP(VOID, , overflow, WS_MarkOverflow(ws))
VTC_WS_OP(BOOL, (0), overflowed, return (WS_Overflowed(ws)))
#undef VTC_WS_OP

/*--------------------------------------------------------------------*/

VCL_INT v_matchproto_(td_vtc_typesize)
vmod_typesize(VRT_CTX, VCL_STRING s)
{
	size_t i = 0;
	const char *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	for (p = s; *p; p++) {
		switch (*p) {
#define VTC_TYPESIZE(c, t) case c: i += sizeof(t); break;
		VTC_TYPESIZE('d', double)
		VTC_TYPESIZE('f', float)
		VTC_TYPESIZE('i', int)
		VTC_TYPESIZE('j', intmax_t)
		VTC_TYPESIZE('l', long)
		VTC_TYPESIZE('o', off_t)
		VTC_TYPESIZE('p', void *)
		VTC_TYPESIZE('s', short)
		VTC_TYPESIZE('z', size_t)
#undef VTC_TYPESIZE
		default:	return (-1);
		}
	}
	return ((VCL_INT)i);
}
