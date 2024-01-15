/*-
 * Copyright (c) 2012-2017 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cache/cache.h"

#include "vsb.h"
#include "vtcp.h"
#include "vtim.h"

#include "vcc_vtc_if.h"

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

	if (ctx->vsl != NULL)
		VSLb(ctx->vsl, SLT_Debug, "barrier_sync(\"%s\")", addr);
	else
		VSL(SLT_Debug, NO_VXID, "barrier_sync(\"%s\")", addr);

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

VCL_IP v_matchproto_(td_vtc_no_ip)
vmod_no_ip(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

/*--------------------------------------------------------------------*/

VCL_VOID v_noreturn_ v_matchproto_(td_vtc_panic)
vmod_panic(VRT_CTX, VCL_STRANDS str)
{
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	b = VRT_StrandsWS(ctx->ws, "PANIC:", str);
	VAS_Fail("VCL", "", 0, b, VAS_VCL);
}

/*--------------------------------------------------------------------*/

VCL_VOID v_matchproto_(td_vtc_sleep)
vmod_sleep(VRT_CTX, VCL_DURATION t)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	VTIM_sleep(t);
}

/*--------------------------------------------------------------------*/

// XXX this really should be PRIV_TASK state
static uintptr_t vtc_ws_snapshot;

static struct ws *
vtc_ws_find(VRT_CTX, VCL_ENUM which)
{

	if (which == VENUM(client))
		return (ctx->ws);
	if (which == VENUM(backend))
		return (ctx->bo->ws);
	if (which == VENUM(session))
		return (ctx->req->sp->ws);
	if (which == VENUM(thread))
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
		size += WS_ReserveAll(ws);
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

VCL_BYTES v_matchproto_(td_vtc_workspace_reserve)
vmod_workspace_reserve(VRT_CTX, VCL_ENUM which, VCL_INT size)
{
	struct ws *ws;
	unsigned r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = vtc_ws_find(ctx, which);
	if (ws == NULL)
		return (0);
	WS_Assert(ws);

	if (size < 0) {
		size += WS_ReserveAll(ws);
		WS_Release(ws, 0);
	}
	if (size <= 0) {
		VRT_fail(ctx, "Attempted negative WS reservation");
		return (0);
	}
	r = WS_ReserveSize(ws, size);
	if (r == 0)
		return (0);
	memset(WS_Reservation(ws), 0, r);
	WS_Release(ws, 0);
	return (r);
}

VCL_INT v_matchproto_(td_vtc_workspace_free)
vmod_workspace_free(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	unsigned u;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = vtc_ws_find(ctx, which);
	if (ws == NULL)
		return (-1);
	WS_Assert(ws);

	u = WS_ReserveAll(ws);
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

VCL_BLOB v_matchproto_(td_vtc_workspace_dump)
vmod_workspace_dump(VRT_CTX, VCL_ENUM which, VCL_ENUM where,
    VCL_BYTES off, VCL_BYTES len)
{
	struct ws *ws;
	VCL_BYTES l, maxlen = 1024;
	v_vla_(unsigned char, buf, maxlen);
	const char *p, *err;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(where);

	ws = vtc_ws_find(ctx, which);
	if (ws == NULL)
		return (NULL);
	WS_Assert(ws);

	if (len > maxlen) {
		VRT_fail(ctx, "workspace_dump: max length is %jd",
		    (intmax_t)maxlen);
		return (NULL);
	}

	l = WS_Dump(ws, *where, off, buf, len);

	if (l == 0) {
		switch (errno) {
		case EINVAL: WRONG(where); break;
		case EAGAIN: err = "NULL"; break;
		case EFAULT: err = "off limit"; break;
		default: err = "unknown error"; break;
		}
		VRT_fail(ctx, "workspace_dump: %s", err);
		return (NULL);
	}

	assert(l < maxlen);
	p = WS_Copy(ctx->ws, buf, l);
	if (p == NULL) {
		VRT_fail(ctx, "workspace_dump: copy failed");
		return (NULL);
	}
	return (VRT_blob(ctx, "workspace_dump", p, l, 0xd000d000));
}

/*--------------------------------------------------------------------*/

VCL_INT v_matchproto_(td_vtc_typesize)
vmod_typesize(VRT_CTX, VCL_STRING s)
{
	size_t i = 0, l, a, p = 0;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(s);
	AN(*s);

	for (; *s; s++) {
		switch (*s) {
#define VTC_TYPESIZE(c, t) case c: l = sizeof(t); break;
		VTC_TYPESIZE('c', char)
		VTC_TYPESIZE('d', double)
		VTC_TYPESIZE('f', float)
		VTC_TYPESIZE('i', int)
		VTC_TYPESIZE('j', intmax_t)
		VTC_TYPESIZE('l', long)
		VTC_TYPESIZE('o', off_t)
		VTC_TYPESIZE('p', void *)
		VTC_TYPESIZE('s', short)
		VTC_TYPESIZE('u', unsigned)
		VTC_TYPESIZE('z', size_t)
#undef VTC_TYPESIZE
		default:	return (-1);
		}
		if (l > p)
			p = l;
		a = i % l;
		if (a != 0)
			i += (l - a); /* align */
		i += l;
	}
	AN(p);
	a = i % p;
	if (a != 0)
		i += (p - a); /* pad */
	return ((VCL_INT)i);
}

/*--------------------------------------------------------------------*/

#define BLOB_VMOD_PROXY_HEADER_TYPE	0xc8f34f78

VCL_BLOB v_matchproto_(td_vtc_proxy_header)
vmod_proxy_header(VRT_CTX, VCL_ENUM venum, VCL_IP client, VCL_IP server,
    VCL_STRING authority)
{
	struct vsb *vsb;
	const void *h;
	int version;
	size_t l;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (venum == VENUM(v1))
		version = 1;
	else if (venum == VENUM(v2))
		version = 2;
	else
		WRONG(venum);

	vsb = VSB_new_auto();
	AN(vsb);
	VRT_Format_Proxy(vsb, version, client, server, authority);
	l = VSB_len(vsb);
	h = WS_Copy(ctx->ws, VSB_data(vsb), l);
	VSB_destroy(&vsb);

	if (h == NULL) {
		VRT_fail(ctx, "proxy_header: out of workspace");
		return (NULL);
	}

	return (VRT_blob(ctx, "proxy_header", h, l,
	    BLOB_VMOD_PROXY_HEADER_TYPE));
}

// ref vsl.c
struct vsl_tag2enum {
	const char	*string;
	enum VSL_tag_e	tag;
};

static struct vsl_tag2enum vsl_tag2enum[SLT__MAX] = {
#define SLTM(name,flags,sdesc,ldesc) [SLT_ ## name] = {	\
		.string = #name,				\
		.tag = SLT_ ## name				\
	},
#include "tbl/vsl_tags.h"
};

static int
vsl_tagcmp(const void *aa, const void *bb)
{
	const struct vsl_tag2enum *a = aa, *b = bb;

	// ref vsl2rst.c ptag_cmp
	if (a->string == NULL && b->string != NULL)
		return (1);
	else if (a->string != NULL && b->string == NULL)
		return (-1);
	else if (a->string == NULL && b->string == NULL)
		return (0);
	return (strcmp(a->string, b->string));
}

/*lint -esym(528, init_vsl_tag2enum) */
static void __attribute__((constructor))
init_vsl_tag2enum(void)
{
	qsort(vsl_tag2enum, SLT__MAX, sizeof *vsl_tag2enum, vsl_tagcmp);
}


VCL_VOID
vmod_vsl(VRT_CTX, VCL_INT id, VCL_STRING tag_s, VCL_ENUM side, VCL_STRANDS s)
{
	struct vsl_tag2enum *te, key;
	vxid_t vxid;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	key.string = tag_s;
	te = bsearch(&key, vsl_tag2enum, SLT__MAX,
	    sizeof *vsl_tag2enum, vsl_tagcmp);

	if (te == NULL) {
		VRT_fail(ctx, "No such tag: %s", tag_s);
		return;
	}

	if (id < 0 || id > VRT_INTEGER_MAX) {
		VRT_fail(ctx, "id out of bounds");
		return;
	}

	vxid.vxid = id & VSL_IDENTMASK;
	if (side == VENUM(c))
		vxid.vxid |= VSL_CLIENTMARKER;
	else if (side == VENUM(b))
		vxid.vxid |= VSL_BACKENDMARKER;
	else
		WRONG("side");

	VSLs(te->tag, vxid, s);
}

static void
vsl_line(VRT_CTX, char *str)
{
	VCL_INT id;
	VCL_ENUM side;
	VCL_STRANDS s;
	const char *tag, *delim = " \t\r\n";
	char *e, *save;

	if (*str == '*') {
		// varnishtest
		str = strstr(str, "vsl|");
		if (str == NULL)
			return;
		str += 4;
	}
	if ((str = strtok_r(str, delim, &save)) == NULL)
		return;
	id = strtoll(str, &e, 10);
	if (e == str)
		return;

	if ((str = strtok_r(NULL, delim, &save)) == NULL)
		return;
	tag = str;

	if ((str = strtok_r(NULL, delim, &save)) == NULL)
		return;
	if (*str == 'c')
		side = VENUM(c);
	else if (*str == 'b')
		side = VENUM(b);
	else
		return;

	str = strtok_r(NULL, "\r\n", &save);
	s = TOSTRAND(str);
	if (str == NULL)
		s = vrt_null_strands;

	vmod_vsl(ctx, id, tag, side, s);
}

VCL_VOID
vmod_vsl_replay(VRT_CTX, VCL_STRANDS s)
{
	struct vsb cp[1];
	const char *p, *pp;
	size_t l;
	int i, err = 0;

	if (s == NULL || s->n == 0)
		return;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	WS_VSB_new(cp, ctx->ws);

	for (i = 0; i < s->n; i++) {
		p = s->p[i];
		if (p == NULL || *p == '\0')
			continue;
		pp = strpbrk(p, "\r\n");
		while (pp != NULL) {
			l = pp - p;
			if (VSB_bcat(cp, p, l) || VSB_finish(cp)) {
				err = 1;
				break;
			}
			vsl_line(ctx, VSB_data(cp));
			VSB_clear(cp);
			p = pp + 1;
			pp = strpbrk(p, "\r\n");
		}
		if (err || VSB_cat(cp, p)) {
			err = 1;
			break;
		}
	}
	if (err || VSB_finish(cp)) {
		AZ(WS_VSB_finish(cp, ctx->ws, NULL));
		VRT_fail(ctx, "out of workspace");
		return;
	}
	vsl_line(ctx, VSB_data(cp));
	VSB_clear(cp);
	AN(WS_VSB_finish(cp, ctx->ws, NULL));
}
