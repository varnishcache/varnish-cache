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
#include "cache/cache_filter.h"

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

/**********************************************************************/

static enum vfp_status v_matchproto_(vfp_pull_f)
xyzzy_rot13_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	enum vfp_status vp;
	char *q;
	ssize_t l;

	(void)vfe;
	vp = VFP_Suck(vc, p, lp);
	if (vp == VFP_ERROR)
		return (vp);
	q = p;
	for (l = 0; l < *lp; l++, q++) {
		if (*q >= 'A' && *q <= 'Z')
			*q = (((*q - 'A') + 13) % 26) + 'A';
		if (*q >= 'a' && *q <= 'z')
			*q = (((*q - 'a') + 13) % 26) + 'a';
	}
	return (vp);
}

static const struct vfp xyzzy_rot13 = {
	.name = "rot13",
	.pull = xyzzy_rot13_pull,
};

/**********************************************************************/

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

VCL_STRING v_matchproto_(td_debug_test_priv_task)
xyzzy_test_priv_task(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
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

	/*
	 * NB: This is a proof of concept, until we decide what the real
	 * API should look like, do NOT do this anywhere else.
	 */
	VRT_AddVFP(ctx, &xyzzy_rot13);
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
		VRT_RemoveVFP(ctx, &xyzzy_rot13);
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
