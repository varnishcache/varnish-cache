/*-
 * Copyright (c) 2012-2015 Varnish Software AS
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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "cache/cache.h"

#include "vcl.h"
#include "vrt.h"
#include "vsa.h"
#include "vsb.h"
#include "vtcp.h"
#include "vtim.h"
#include "vcc_if.h"

struct priv_vcl {
	unsigned		magic;
#define PRIV_VCL_MAGIC		0x8E62FA9D
	char			*foo;
	uintptr_t		obj_cb;
	struct vcl		*vcl;
	struct vclref		*vclref;
};

static VCL_DURATION vcl_release_delay = 0.0;

VCL_VOID __match_proto__(td_debug_panic)
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

VCL_STRING __match_proto__(td_debug_author)
vmod_author(VRT_CTX, VCL_ENUM person, VCL_ENUM someone)
{
	(void)someone;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (!strcmp(person, "phk"))
		return ("Poul-Henning");
	if (!strcmp(person, "des"))
		return ("Dag-Erling");
	if (!strcmp(person, "kristian"))
		return ("Kristian");
	if (!strcmp(person, "mithrandir"))
		return ("Tollef");
	WRONG("Illegal VMOD enum");
}

VCL_VOID __match_proto__(td_debug_test_priv_call)
vmod_test_priv_call(VRT_CTX, struct vmod_priv *priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup("BAR");
		priv->free = free;
	} else {
		assert(!strcmp(priv->priv, "BAR"));
	}
}

VCL_STRING __match_proto__(td_debug_test_priv_task)
vmod_test_priv_task(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (s == NULL || *s == '\0') {
		return priv->priv;
	} else if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->free = free;
	} else {
		char *n = realloc(priv->priv,
		    strlen(priv->priv) + strlen(s) + 2);
		if (n == NULL)
			return NULL;
		strcat(n, " ");
		strcat(n, s);
		priv->priv = n;
	}
	return (priv->priv);
}

VCL_STRING __match_proto__(td_debug_test_priv_top)
vmod_test_priv_top(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->free = free;
	}
	return (priv->priv);
}

VCL_VOID __match_proto__(td_debug_test_priv_vcl)
vmod_test_priv_vcl(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->foo);
	assert(!strcmp(priv_vcl->foo, "FOO"));
}

VCL_BLOB
vmod_str2blob(VRT_CTX, VCL_STRING s)
{
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	p = (void*)WS_Alloc(ctx->ws, sizeof *p);
	AN(p);
	memset(p, 0, sizeof *p);
	p->len = strlen(s);
	p->priv = WS_Copy(ctx->ws, s, -1);
	return (p);
}

VCL_STRING
vmod_blob2hex(VRT_CTX, VCL_BLOB b)
{
	char *s, *p;
	uint8_t *q;
	int i;

	s = WS_Alloc(ctx->ws, b->len * 2 + 2);
	AN(s);
	p = s;
	q = b->priv;
	for (i = 0; i < b->len; i++) {
		assert(snprintf(p, 3, "%02x", *q) == 2);
		p += 2;
		q += 1;
	}
	VRT_priv_fini(b);
	return (s);
}

VCL_BACKEND
vmod_no_backend(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

VCL_STEVEDORE __match_proto__(td_debug_no_stevedore)
vmod_no_stevedore(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

VCL_VOID __match_proto__(td_debug_rot52)
vmod_rot52(VRT_CTX, VCL_HTTP hp)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	http_PrintfHeader(hp, "Encrypted: ROT52");
}

VCL_STRING
vmod_argtest(VRT_CTX, VCL_STRING one, VCL_REAL two, VCL_STRING three,
    VCL_STRING comma, VCL_INT four)
{
	char buf[100];

	bprintf(buf, "%s %g %s %s %ld", one, two, three, comma, four);
	return (WS_Copy(ctx->ws, buf, -1));
}

VCL_INT
vmod_vre_limit(VRT_CTX)
{
	(void)ctx;
	return (cache_param->vre_limits.match);
}

static void __match_proto__(obj_event_f)
obj_cb(struct worker *wrk, void *priv, struct objcore *oc, unsigned event)
{
	const struct priv_vcl *priv_vcl;
	const char *what;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(priv_vcl, priv, PRIV_VCL_MAGIC);
	switch (event) {
	case OEV_INSERT: what = "insert"; break;
	case OEV_EXPIRE: what = "expire"; break;
	default: WRONG("Wrong object event");
	}

	/* We cannot trust %p to be 0x... format as expected by m00021.vtc */
	VSL(SLT_Debug, 0, "Object Event: %s 0x%jx", what,
	    (intmax_t)(uintptr_t)oc);
}

VCL_VOID __match_proto__()
vmod_register_obj_events(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AZ(priv_vcl->obj_cb);
	priv_vcl->obj_cb = ObjSubscribeEvents(obj_cb, priv_vcl,
		OEV_INSERT|OEV_EXPIRE);
	VSL(SLT_Debug, 0, "Subscribed to Object Events");
}

VCL_VOID __match_proto__()
vmod_fail(VRT_CTX)
{

	VRT_fail(ctx, "Forced failure");
}

static void __match_proto__(vmod_priv_free_f)
priv_vcl_free(void *priv)
{
	struct priv_vcl *priv_vcl;

	CAST_OBJ_NOTNULL(priv_vcl, priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->foo);
	free(priv_vcl->foo);
	if (priv_vcl->obj_cb != 0) {
		ObjUnsubscribeEvents(&priv_vcl->obj_cb);
		VSL(SLT_Debug, 0, "Unsubscribed from Object Events");
	}
	AZ(priv_vcl->vcl);
	AZ(priv_vcl->vclref);
	FREE_OBJ(priv_vcl);
	AZ(priv_vcl);
}

static int
event_load(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	AN(ctx->msg);
	if (cache_param->nuke_limit == 42) {
		VSB_printf(ctx->msg, "nuke_limit is not the answer.");
		return (-1);
	}

	ALLOC_OBJ(priv_vcl, PRIV_VCL_MAGIC);
	AN(priv_vcl);
	priv_vcl->foo = strdup("FOO");
	AN(priv_vcl->foo);
	priv->priv = priv_vcl;
	priv->free = priv_vcl_free;
	return (0);
}

static int
event_warm(VRT_CTX, const struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;
	char buf[32];

	VSL(SLT_Debug, 0, "%s: VCL_EVENT_WARM", VCL_Name(ctx->vcl));

	AN(ctx->msg);
	if (cache_param->max_esi_depth == 42) {
		VSB_printf(ctx->msg, "max_esi_depth is not the answer.");
		return (-1);
	}

	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AZ(priv_vcl->vcl);
	AZ(priv_vcl->vclref);

	bprintf(buf, "vmod-debug ref on %s", VCL_Name(ctx->vcl));
	priv_vcl->vcl = ctx->vcl;
	priv_vcl->vclref = VRT_ref_vcl(ctx, buf);
	return (0);
}

static void*
cooldown_thread(void *priv)
{
	struct vrt_ctx ctx;
	struct priv_vcl *priv_vcl;

	CAST_OBJ_NOTNULL(priv_vcl, priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->vcl);
	AN(priv_vcl->vclref);

	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.vcl = priv_vcl->vcl;

	VTIM_sleep(vcl_release_delay);
	VRT_rel_vcl(&ctx, &priv_vcl->vclref);
	priv_vcl->vcl = NULL;
	return (NULL);
}

static int
event_cold(VRT_CTX, const struct vmod_priv *priv)
{
	pthread_t thread;
	struct priv_vcl *priv_vcl;

	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->vcl);
	AN(priv_vcl->vclref);

	VSL(SLT_Debug, 0, "%s: VCL_EVENT_COLD", VCL_Name(ctx->vcl));

	if (vcl_release_delay == 0.0) {
		VRT_rel_vcl(ctx, &priv_vcl->vclref);
		priv_vcl->vcl = NULL;
		return (0);
	}

	AZ(pthread_create(&thread, NULL, cooldown_thread, priv_vcl));
	AZ(pthread_detach(thread));
	return (0);
}

int __match_proto__(vmod_event_f)
event_function(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{

	switch (e) {
	case VCL_EVENT_LOAD: return (event_load(ctx, priv));
	case VCL_EVENT_WARM: return (event_warm(ctx, priv));
	case VCL_EVENT_COLD: return (event_cold(ctx, priv));
	default: return (0);
	}
}

VCL_VOID __match_proto__(td_debug_sleep)
vmod_sleep(VRT_CTX, VCL_DURATION t)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	VTIM_sleep(t);
}

static struct ws *
wsfind(VRT_CTX, VCL_ENUM which)
{
	if (!strcmp(which, "client"))
		return (ctx->ws);
	else if (!strcmp(which, "backend"))
		return (ctx->bo->ws);
	else if (!strcmp(which, "session"))
		return (ctx->req->sp->ws);
	else if (!strcmp(which, "thread"))
		return (ctx->req->wrk->aws);
	else
		WRONG("No such workspace.");
}

void
vmod_workspace_allocate(VRT_CTX, VCL_ENUM which, VCL_INT size)
{
	struct ws *ws;
	char *s;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);

	WS_Assert(ws);
	AZ(ws->r);

	if (size < 0) {
		size += WS_Reserve(ws, 0);
		WS_Release(ws, 0);
	}
	s = WS_Alloc(ws, size);
	if (!s)
		return;
	memset(s, '\0', size);
}

VCL_INT
vmod_workspace_free(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	unsigned u;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);

	WS_Assert(ws);
	u = WS_Reserve(ws, 0);
	WS_Release(ws, 0);

	return (u);
}

VCL_BOOL
vmod_workspace_overflowed(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);
	WS_Assert(ws);

	return (WS_Overflowed(ws));
}

static uintptr_t debug_ws_snap;

void
vmod_workspace_snap(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);
	WS_Assert(ws);

	debug_ws_snap = WS_Snapshot(ws);
}

void
vmod_workspace_reset(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);
	WS_Assert(ws);

	WS_Reset(ws, debug_ws_snap);
}

void
vmod_workspace_overflow(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);
	WS_Assert(ws);

	WS_MarkOverflow(ws);
}

void
vmod_vcl_release_delay(VRT_CTX, VCL_DURATION delay)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(delay > 0.0);
	vcl_release_delay = delay;
}

VCL_BOOL
vmod_match_acl(VRT_CTX, VCL_ACL acl, VCL_IP ip)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_ORNULL(acl, VRT_ACL_MAGIC);
	assert(VSA_Sane(ip));

	return (VRT_acl_match(ctx, acl, ip));
}

VCL_BOOL
vmod_barrier_sync(VRT_CTX, VCL_STRING addr)
{
	const char *err;
	char buf[32];
	int sock, i;
	ssize_t sz;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(addr);
	AN(*addr);

	VSLb(ctx->vsl, SLT_Debug, "barrier_sync(\"%s\")", addr);
	sock = VTCP_open(addr, NULL, 0., &err);
	if (sock < 0) {
		VSLb(ctx->vsl, SLT_Error, "Barrier connection failed: %s", err);
		return (0);
	}

	sz = read(sock, buf, sizeof buf);
	i = errno;
	closefd(&sock);
	if (sz == 0)
		return (1);
	if (sz < 0)
		VSLb(ctx->vsl, SLT_Error,
		    "Barrier read failed: %s (errno=%d)", strerror(i), i);
	if (sz > 0)
		VSLb(ctx->vsl, SLT_Error, "Barrier unexpected data (%zdB)", sz);
	return (0);
}

VCL_VOID
vmod_test_probe(VRT_CTX, VCL_PROBE probe, VCL_PROBE same)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(probe, VRT_BACKEND_PROBE_MAGIC);
	CHECK_OBJ_ORNULL(same, VRT_BACKEND_PROBE_MAGIC);
	AZ(same == NULL || probe == same);
}

VCL_INT
vmod_typesize(VRT_CTX, VCL_STRING s)
{
	size_t i = 0;
	const char *p;

	(void)ctx;
	for (p = s; *p; p++) {
		switch (*p) {
		case 'p':	i += sizeof(void *); break;
		case 'i':	i += sizeof(int); break;
		case 'd':	i += sizeof(double); break;
		case 'f':	i += sizeof(float); break;
		case 'l':	i += sizeof(long); break;
		case 's':	i += sizeof(short); break;
		case 'z':	i += sizeof(size_t); break;
		case 'o':	i += sizeof(off_t); break;
		case 'j':	i += sizeof(intmax_t); break;
		default:	return(-1);
		}
	}
	return ((VCL_INT)i);
}

VCL_VOID
vmod_purge(VRT_CTX, double ttl, double grace, double keep)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	VSLb(ctx->vsl, SLT_VCL_Log, "PURGE");
	VRT_purge(ctx, ttl, grace, keep);
}
