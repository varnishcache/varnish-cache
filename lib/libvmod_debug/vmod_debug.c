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
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cache/cache_varnishd.h"

#include "vsa.h"
#include "vtim.h"
#include "vcc_if.h"
#include "VSC_debug.h"

struct priv_vcl {
	unsigned		magic;
#define PRIV_VCL_MAGIC		0x8E62FA9D
	char			*foo;
	uintptr_t		obj_cb;
	struct vcl		*vcl;
	struct vclref		*vclref;
};

static VCL_DURATION vcl_release_delay = 0.0;

static pthread_mutex_t vsc_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct vsc_seg *vsc_seg = NULL;
static struct VSC_debug *vsc = NULL;

VCL_STRING v_matchproto_(td_debug_author)
xyzzy_author(VRT_CTX, VCL_ENUM person, VCL_ENUM someone)
{
	(void)someone;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (person == xyzzy_enum_phk)
		return ("Poul-Henning");
	assert(strcmp(person, "phk"));
	if (person == xyzzy_enum_des)
		return ("Dag-Erling");
	assert(strcmp(person, "des"));
	if (person == xyzzy_enum_kristian)
		return ("Kristian");
	assert(strcmp(person, "kristian"));
	if (person == xyzzy_enum_mithrandir)
		return ("Tollef");
	assert(strcmp(person, "mithrandir"));
	WRONG("Illegal VMOD enum");
}

VCL_VOID v_matchproto_(td_debug_test_priv_call)
xyzzy_test_priv_call(VRT_CTX, struct vmod_priv *priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup("BAR");
		priv->free = free;
	} else {
		assert(!strcmp(priv->priv, "BAR"));
	}
}

static void
priv_task_free(void *ptr)
{
	AN(ptr);
	VSL(SLT_Debug, 0, "priv_task_free(%p)", ptr);
	free(ptr);
}

VCL_STRING v_matchproto_(td_debug_test_priv_task)
xyzzy_test_priv_task(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (s == NULL || *s == '\0') {
		VSL(SLT_Debug, 0, "test_priv_task(%p) = %p (exists)",
		    priv, priv->priv);
	} else if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->free = priv_task_free;
		VSL(SLT_Debug, 0, "test_priv_task(%p) = %p (new)",
		    priv, priv->priv);
	} else {
		char *n = realloc(priv->priv,
		    strlen(priv->priv) + strlen(s) + 2);
		if (n == NULL)
			return NULL;
		strcat(n, " ");
		strcat(n, s);
		priv->priv = n;
		VSL(SLT_Debug, 0, "test_priv_task(%p) = %p (update)",
		    priv, priv->priv);
	}
	if (priv->priv != NULL)
		assert(priv->free == priv_task_free);
	return (priv->priv);
}

VCL_STRING v_matchproto_(td_debug_test_priv_top)
xyzzy_test_priv_top(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->free = free;
	}
	return (priv->priv);
}

VCL_VOID v_matchproto_(td_debug_test_priv_vcl)
xyzzy_test_priv_vcl(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->foo);
	assert(!strcmp(priv_vcl->foo, "FOO"));
}

VCL_VOID v_matchproto_(td_debug_rot52)
xyzzy_rot52(VRT_CTX, VCL_HTTP hp)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	http_PrintfHeader(hp, "Encrypted: ROT52");
}

VCL_STRING v_matchproto_(td_debug_argtest)
xyzzy_argtest(VRT_CTX, struct xyzzy_argtest_arg *arg)
{
	char buf[100];

	AN(arg);
	bprintf(buf, "%s %g %s %s %jd %d %s",
	    arg->one, arg->two, arg->three, arg->comma, (intmax_t)arg->four,
	    arg->valid_opt, arg->valid_opt ? arg->opt : "<undef>");
	return (WS_Copy(ctx->ws, buf, -1));
}

VCL_INT v_matchproto_(td_debug_vre_limit)
xyzzy_vre_limit(VRT_CTX)
{
	(void)ctx;
	return (cache_param->vre_limits.match);
}

static void v_matchproto_(obj_event_f)
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

VCL_VOID v_matchproto_(td_debug_register_obj_events)
xyzzy_register_obj_events(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AZ(priv_vcl->obj_cb);
	priv_vcl->obj_cb = ObjSubscribeEvents(obj_cb, priv_vcl,
		OEV_INSERT|OEV_EXPIRE);
	VSL(SLT_Debug, 0, "Subscribed to Object Events");
}

VCL_VOID v_matchproto_(td_debug_fail)
xyzzy_fail(VRT_CTX)
{

	VRT_fail(ctx, "Forced failure");
}

VCL_BOOL v_matchproto_(td_debug_fail2)
xyzzy_fail2(VRT_CTX)
{

	VRT_fail(ctx, "Forced failure");
	return (1);
}

static void v_matchproto_(vmod_priv_free_f)
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

int v_matchproto_(vmod_event_f)
event_function(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{

	switch (e) {
	case VCL_EVENT_LOAD: return (event_load(ctx, priv));
	case VCL_EVENT_WARM: return (event_warm(ctx, priv));
	case VCL_EVENT_COLD: return (event_cold(ctx, priv));
	case VCL_EVENT_DISCARD:
		if (vsc)
			VSC_debug_Destroy(&vsc_seg);
		return (0);
	default: return (0);
	}
}

VCL_VOID v_matchproto_(td_debug_vcl_release_delay)
xyzzy_vcl_release_delay(VRT_CTX, VCL_DURATION delay)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(delay > 0.0);
	vcl_release_delay = delay;
}

VCL_BOOL v_matchproto_(td_debug_match_acl)
xyzzy_match_acl(VRT_CTX, VCL_ACL acl, VCL_IP ip)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_ORNULL(acl, VRT_ACL_MAGIC);
	assert(VSA_Sane(ip));

	return (VRT_acl_match(ctx, acl, ip));
}

VCL_VOID v_matchproto_(td_debug_test_probe)
xyzzy_test_probe(VRT_CTX, VCL_PROBE probe, VCL_PROBE same)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(probe, VRT_BACKEND_PROBE_MAGIC);
	CHECK_OBJ_ORNULL(same, VRT_BACKEND_PROBE_MAGIC);
	AZ(same == NULL || probe == same);
}

VCL_VOID
xyzzy_vsc_new(VRT_CTX)
{
	(void)ctx;
	AZ(pthread_mutex_lock(&vsc_mtx));
	if (vsc == NULL) {
		AZ(vsc_seg);
		vsc = VSC_debug_New(NULL, &vsc_seg, "");
	}
	AN(vsc);
	AN(vsc_seg);
	AZ(pthread_mutex_unlock(&vsc_mtx));
}

VCL_VOID
xyzzy_vsc_count(VRT_CTX, VCL_INT cnt)
{
	(void)ctx;
	AN(vsc);
	vsc->count += cnt;
}

VCL_VOID
xyzzy_vsc_destroy(VRT_CTX)
{
	(void)ctx;
	AZ(pthread_mutex_lock(&vsc_mtx));
	if (vsc != NULL) {
		AN(vsc_seg);
		VSC_debug_Destroy(&vsc_seg);
	}
	AZ(vsc_seg);
	vsc = NULL;
	AZ(pthread_mutex_unlock(&vsc_mtx));
}

struct xyzzy_debug_concat {
	unsigned	magic;
#define CONCAT_MAGIC 0x6b746493
	VCL_STRING	s;
};

VCL_VOID
xyzzy_concat__init(VRT_CTX, struct xyzzy_debug_concat **concatp,
		   const char *vcl_name, VCL_STRANDS s)
{
	struct xyzzy_debug_concat *concat;
	size_t sz = 0;
	char *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(concatp);
	AZ(*concatp);
	AN(vcl_name);

	ALLOC_OBJ(concat, CONCAT_MAGIC);
	AN(concat);
	*concatp = concat;

	for (int i = 0; i < s->n; i++)
		if (s->p[i] != NULL)
			sz += strlen(s->p[i]);
	p = malloc(sz + 1);
	AN(p);
	(void)VRT_Strands(p, sz + 1, s);
	concat->s = p;
}

VCL_VOID
xyzzy_concat__fini(struct xyzzy_debug_concat **concatp)
{
	struct xyzzy_debug_concat *concat;
	void *p;

	if (concatp == NULL || *concatp == NULL)
		return;
	CHECK_OBJ(*concatp, CONCAT_MAGIC);
	concat = *concatp;
	*concatp = NULL;
	p = TRUST_ME(concat->s);
	free(p);
	FREE_OBJ(concat);
}

VCL_STRING
xyzzy_concat_get(VRT_CTX, struct xyzzy_debug_concat *concat)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(concat, CONCAT_MAGIC);
	return (concat->s);
}

VCL_STRING
xyzzy_concatenate(VRT_CTX, VCL_STRANDS s)
{
	VCL_STRING r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	r = VRT_StrandsWS(ctx->ws, NULL, s);
	if (r != NULL && *r != '\0')
		WS_Assert_Allocated(ctx->ws, r, strlen(r) + 1);
	return (r);
}

VCL_STRING
xyzzy_collect(VRT_CTX, VCL_STRANDS s)
{
	VCL_STRING r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	r = VRT_CollectStrands(ctx, s);
	if (r != NULL && *r != '\0')
		WS_Assert_Allocated(ctx->ws, r, strlen(r) + 1);
	return (r);
}

/* cf. VRT_SetHdr() */
VCL_VOID
xyzzy_sethdr(VRT_CTX, VCL_HEADER hs, VCL_STRANDS s)
{
	struct http *hp;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(hs);
	AN(hs->what);
	hp = VRT_selecthttp(ctx, hs->where);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (s->n == 0) {
		http_Unset(hp, hs->what);
	} else {
		b = VRT_StrandsWS(hp->ws, hs->what + 1, s);
		if (b == NULL) {
			VSLb(ctx->vsl, SLT_LostHeader, "%s", hs->what + 1);
		} else {
			if (*b != '\0')
				WS_Assert_Allocated(hp->ws, b, strlen(b) + 1);
			http_Unset(hp, hs->what);
			http_SetHeader(hp, b);
		}
	}
}

VCL_VOID
xyzzy_store_ip(VRT_CTX, struct vmod_priv *priv, VCL_IP ip)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	AZ(priv->free);
	assert(VSA_Sane(ip));

	priv->priv = TRUST_ME(ip);
}

VCL_IP
xyzzy_get_ip(VRT_CTX, struct vmod_priv *priv)
{
	VCL_IP ip;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	AZ(priv->free);

	ip = priv->priv;
	assert(VSA_Sane(ip));

	return (ip);
}

VCL_STRANDS
xyzzy_return_strands(VRT_CTX, VCL_STRANDS strand)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (strand);
}
