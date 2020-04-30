/*-
 * Copyright (c) 2012-2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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
	struct vclref		*vclref_discard;
	struct vclref		*vclref_cold;
	VCL_DURATION		vcl_discard_delay;
	VCL_BACKEND		be;
	unsigned		cold_be;
	unsigned		cooling_be;
};


static pthread_mutex_t vsc_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct vsc_seg *vsc_seg = NULL;
static struct VSC_debug *vsc = NULL;
static int loads;
static const int store_ip_token;
static const int fail_rollback_token;

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

#define ROT13_BUFSZ (1 << 13)

static int v_matchproto_(vdp_init_f)
xyzzy_rot13_init(struct req *req, void **priv)
{
	(void)req;
	AN(priv);
	*priv = malloc(ROT13_BUFSZ);
	if (*priv == NULL)
		return (-1);
	return (0);
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_rot13_bytes(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	char *q;
	const char *pp;
	int i, j, retval = 0;

	AN(priv);
	AN(*priv);
	if (len <= 0)
		return (VDP_bytes(req, act, ptr, len));
	AN(ptr);
	if (act != VDP_END)
		act = VDP_FLUSH;
	q = *priv;
	pp = ptr;

	for (i = 0, j = 0; j < len; i++, j++) {
		if (pp[j] >= 'A' && pp[j] <= 'Z')
			q[i] = (((pp[j] - 'A') + 13) % 26) + 'A';
		else if (pp[j] >= 'a' && pp[j] <= 'z')
			q[i] = (((pp[j] - 'a') + 13) % 26) + 'a';
		else
			q[i] = pp[j];
		if (i == ROT13_BUFSZ - 1) {
			retval = VDP_bytes(req, act, q, ROT13_BUFSZ);
			if (retval != 0)
				return (retval);
			i = -1;
		}
	}
	if (i >= 0)
		retval = VDP_bytes(req, act, q, i + 1L);
	return (retval);
}

static int v_matchproto_(vdp_fini_f)
xyzzy_rot13_fini(struct req *req, void **priv)
{
	(void)req;
	AN(priv);
	free(*priv);
	*priv = NULL;
	return (0);
}

static const struct vdp xyzzy_vdp_rot13 = {
	.name  = "rot13",
	.init  = xyzzy_rot13_init,
	.bytes = xyzzy_rot13_bytes,
	.fini  = xyzzy_rot13_fini,
};

/**********************************************************************/

VCL_STRING v_matchproto_(td_debug_author)
xyzzy_author(VRT_CTX, VCL_ENUM person, VCL_ENUM someone)
{
	(void)someone;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (person == VENUM(phk))
		return ("Poul-Henning");
	assert(strcmp(person, "phk"));
	if (person == VENUM(des))
		return ("Dag-Erling");
	assert(strcmp(person, "des"));
	if (person == VENUM(kristian))
		return ("Kristian");
	assert(strcmp(person, "kristian"));
	if (person == VENUM(mithrandir))
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
			return (NULL);
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
xyzzy_argtest(VRT_CTX, struct VARGS(argtest) *arg)
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
	AZ(priv_vcl->vclref_discard);
	AZ(priv_vcl->vclref_cold);
	FREE_OBJ(priv_vcl);
	AZ(priv_vcl);
}

static int
event_load(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	AN(ctx->msg);

	loads++;

	if (cache_param->nuke_limit == 42) {
		VSB_cat(ctx->msg, "nuke_limit is not the answer.");
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

	VRT_AddVDP(ctx, &xyzzy_vdp_rot13);
	return (0);
}

VCL_VOID
xyzzy_vcl_prevent_cold(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;
	char buf[32];

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AZ(priv_vcl->vclref_cold);

	bprintf(buf, "vmod-debug ref on %s", VCL_Name(ctx->vcl));
	priv_vcl->vclref_cold = VRT_VCL_Prevent_Cold(ctx, buf);
}

VCL_VOID
xyzzy_vcl_allow_cold(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->vclref_cold);
	VRT_VCL_Allow_Cold(&priv_vcl->vclref_cold);
}

VCL_VOID
xyzzy_cold_backend(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	priv_vcl->cold_be = 1;
}

VCL_VOID
xyzzy_cooling_backend(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	priv_vcl->cooling_be = 1;
}

static const struct vdi_methods empty_methods[1] = {{
	.magic =	VDI_METHODS_MAGIC,
	.type =	"debug.dummy"
}};

static int
event_warm(VRT_CTX, const struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;
	char buf[32];

	VSL(SLT_Debug, 0, "%s: VCL_EVENT_WARM", VCL_Name(ctx->vcl));

	AN(ctx->msg);
	if (cache_param->max_esi_depth == 42) {
		VSB_cat(ctx->msg, "max_esi_depth is not the answer.");
		return (-1);
	}

	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AZ(priv_vcl->vclref_discard);

	if (!priv_vcl->cold_be) {
		/* NB: set up a COOLING step unless we want a COLD backend. */
		bprintf(buf, "vmod-debug ref on %s", VCL_Name(ctx->vcl));
		priv_vcl->vclref_discard = VRT_VCL_Prevent_Discard(ctx, buf);
	}

	AZ(priv_vcl->be);
	priv_vcl->be = VRT_AddDirector(ctx, empty_methods,
	    NULL, "%s", "dir_warmcold");

	return (0);
}

static void*
cooldown_thread(void *priv)
{
	struct priv_vcl *priv_vcl;

	CAST_OBJ_NOTNULL(priv_vcl, priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->vclref_discard);

	VTIM_sleep(priv_vcl->vcl_discard_delay);
	VRT_VCL_Allow_Discard(&priv_vcl->vclref_discard);
	return (NULL);
}

static VCL_BACKEND
create_cold_backend(VRT_CTX)
{
	struct vrt_backend be[1];

	INIT_OBJ(be, VRT_BACKEND_MAGIC);
	be->path = "/";
	be->vcl_name = "doomed";
	return (VRT_new_backend(ctx, be));
}

static int
event_cold(VRT_CTX, const struct vmod_priv *priv)
{
	pthread_t thread;
	struct priv_vcl *priv_vcl;

	AZ(ctx->msg);

	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);

	VSL(SLT_Debug, 0, "%s: VCL_EVENT_COLD", VCL_Name(ctx->vcl));

	VRT_DelDirector(&priv_vcl->be);

	if (priv_vcl->cold_be) {
		AZ(priv_vcl->vclref_discard);
		priv_vcl->be = create_cold_backend(ctx);
		WRONG("unreachable");
	}

	if (priv_vcl->cooling_be) {
		AN(priv_vcl->vclref_discard);
		priv_vcl->be = create_cold_backend(ctx);
		AZ(priv_vcl->be);
	}

	if (priv_vcl->vcl_discard_delay == 0.0) {
		AN(priv_vcl->vclref_discard);
		VRT_VCL_Allow_Discard(&priv_vcl->vclref_discard);
		return (0);
	}

	AZ(pthread_create(&thread, NULL, cooldown_thread, priv_vcl));
	AZ(pthread_detach(thread));
	return (0);
}

static int
event_discard(VRT_CTX, void *priv)
{

	(void)priv;

	AZ(ctx->msg);

	VRT_RemoveVFP(ctx, &xyzzy_rot13);
	VRT_RemoveVDP(ctx, &xyzzy_vdp_rot13);

	if (--loads)
		return (0);

	/*
	 * The vsc and vsc_seg variables are not per-VCL, they are
	 * the same in all VCL's which import the same binary version
	 * of this VMOD, so we should only carry out cleanup on the
	 * last discard event.
	 */
	if (vsc)
		VSC_debug_Destroy(&vsc_seg);

	return (0);
}

int v_matchproto_(vmod_event_f)
xyzzy_event_function(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{

	switch (e) {
	case VCL_EVENT_LOAD:	return (event_load(ctx, priv));
	case VCL_EVENT_WARM:	return (event_warm(ctx, priv));
	case VCL_EVENT_COLD:	return (event_cold(ctx, priv));
	case VCL_EVENT_DISCARD:	return (event_discard(ctx, priv));
	default: WRONG("we should test all possible events");
	}
}

VCL_VOID v_matchproto_(td_debug_vcl_discard_delay)
xyzzy_vcl_discard_delay(VRT_CTX, struct vmod_priv *priv, VCL_DURATION delay)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	assert(delay > 0.0);
	priv_vcl->vcl_discard_delay = delay;
}

VCL_BOOL v_matchproto_(td_debug_match_acl)
xyzzy_match_acl(VRT_CTX, VCL_ACL acl, VCL_IP ip)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	AN(acl);
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

	TAKE_OBJ_NOTNULL(concat, concatp, CONCAT_MAGIC);
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

VCL_DURATION
xyzzy_priv_perf(VRT_CTX, VCL_INT size, VCL_INT rounds)
{
	vtim_mono t0, t1;
	vtim_dur d;
	struct vmod_priv *p;
	VCL_INT s, r;
	uintptr_t check = 0;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	for (s = 1; s <= size; s++) {
		p = VRT_priv_task(ctx, (void *)(uintptr_t)s);
		if (p == NULL) {
			VRT_fail(ctx, "no priv task - out of ws?");
			return (-1.0);
		}
		p->priv = NULL;
	}

	t0 = VTIM_mono();
	for (r = 0; r < rounds; r++) {
		for (s = 1; s <= size; s++) {
			p = VRT_priv_task(ctx, (void *)(uintptr_t)s);
			AN(p);
			check += (uintptr_t)p->priv;
			p->priv = (void *)(uintptr_t)(s * rounds + r);
		}
	}
	t1 = VTIM_mono();

	d = (t1 - t0) * 1e9 / ((double)size * (double)rounds);

	VSLb(ctx->vsl, SLT_Debug,
	     "perf size %jd rounds %jd time %.1fns check %jd",
	     (intmax_t)size, (intmax_t)rounds, d, (uintmax_t)check);

	return (d);
}

VCL_STRANDS
xyzzy_return_strands(VRT_CTX, VCL_STRANDS strand)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (strand);
}

/*---------------------------------------------------------------------*/

static const struct vcf_return * v_matchproto_(vcf_func_f)
xyzzy_catflap_simple(struct req *req, struct objcore **oc,
    struct objcore **oc_exp, int state)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcf, VCF_MAGIC);
	assert(req->vcf->func == xyzzy_catflap_simple);

	(void)oc;
	(void)oc_exp;
	if (state == 0) {
		if (req->vcf->priv == VENUM(first))
			return (VCF_HIT);
		if (req->vcf->priv == VENUM(miss))
			return (VCF_MISS);
		WRONG("Shouldn't get here");
	}
	return (VCF_DEFAULT);
}

static const struct vcf_return * v_matchproto_(vcf_func_f)
xyzzy_catflap_last(struct req *req, struct objcore **oc,
    struct objcore **oc_exp, int state)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->vcf, VCF_MAGIC);
	assert(req->vcf->func == xyzzy_catflap_last);

	(void)oc_exp;
	if (state == 0) {
		AN(oc);
		CHECK_OBJ_NOTNULL(*oc, OBJCORE_MAGIC);
		req->vcf->priv = *oc;
		return (VCF_CONTINUE);
	}
	if (state == 1) {
		AN(oc);
		if (req->vcf->priv != NULL)
			CAST_OBJ_NOTNULL(*oc, req->vcf->priv, OBJCORE_MAGIC);
		return (VCF_CONTINUE);
	}
	return (VCF_DEFAULT);
}

VCL_VOID
xyzzy_catflap(VRT_CTX, VCL_ENUM type)
{
	struct req *req;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	req = ctx->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	XXXAZ(req->vcf);
	req->vcf = WS_Alloc(req->ws, sizeof *req->vcf);
	if (req->vcf == NULL) {
		VRT_fail(ctx, "WS_Alloc failed in debug.catflap()");
		return;
	}
	INIT_OBJ(req->vcf, VCF_MAGIC);
	if (type == VENUM(first) || type == VENUM(miss)) {
		req->vcf->func = xyzzy_catflap_simple;
		req->vcf->priv = TRUST_ME(type);
	} else if (type == VENUM(last)) {
		req->vcf->func = xyzzy_catflap_last;
	} else {
		WRONG("Wrong VENUM");
	}
}

VCL_BYTES
xyzzy_stk(VRT_CTX)
{
	const VCL_BYTES max = 100 * 1024 * 1024;
	const char *a, *b;
	VCL_BYTES r;

	a = TRUST_ME(&b);
	b = TRUST_ME(ctx->req->wrk);
	b += sizeof(*ctx->req->wrk);

	if (b > a && (r = b - a) < max)
		return (r);
	if (a > b && (r = a - b) < max)
		return (r);

	return (0);
}

VCL_VOID
xyzzy_sndbuf(VRT_CTX, VCL_BYTES arg)
{
	int fd = -1, oldbuf, newbuf, buflen;
	socklen_t intlen = sizeof(int);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (ctx->bo) {
		CHECK_OBJ(ctx->bo, BUSYOBJ_MAGIC);
		INCOMPL();
	}
	else if (ctx->req) {
		CHECK_OBJ(ctx->req, REQ_MAGIC);
		CHECK_OBJ(ctx->req->sp, SESS_MAGIC);
		fd = ctx->req->sp->fd;
	}
	else {
		VRT_fail(ctx, "debug.sndbuf() called outside a transaction.");
		return;
	}

	xxxassert(fd >= 0);

	if (arg > INT_MAX)
		buflen = INT_MAX;
	else if (arg <= 0)
		buflen = 0;
	else
		buflen = (int)arg;

	oldbuf = 0;
	AZ(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &oldbuf, &intlen));

	newbuf = buflen;
	AZ(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &newbuf, intlen));
	AZ(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &newbuf, &intlen));

	AN(ctx->vsl);
	VSLb(ctx->vsl, SLT_Debug, "SO_SNDBUF fd=%d old=%d new=%d actual=%d",
	    fd, oldbuf, buflen, newbuf);
}

VCL_VOID
xyzzy_store_ip(VRT_CTX, VCL_IP ip)
{
	struct vmod_priv *priv;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	priv = VRT_priv_task(ctx, &store_ip_token);
	if (priv == NULL) {
		VRT_fail(ctx, "no priv task - out of ws?");
		return;
	}

	AZ(priv->free);
	assert(VSA_Sane(ip));
	priv->priv = TRUST_ME(ip);
}

VCL_IP
xyzzy_get_ip(VRT_CTX)
{
	struct vmod_priv *priv;
	VCL_IP ip;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	priv = VRT_priv_task(ctx, &store_ip_token);
	AN(priv);
	AZ(priv->free);

	ip = priv->priv;
	assert(VSA_Sane(ip));
	return (ip);
}

/**********************************************************************
 * For testing import code on bad vmod files (m00003.vtc)
 */

//lint -save -e9075 external symbol '...' defined without a prior declaration

const struct vmod_data Vmod_wrong0_Data = {
	.vrt_major =    0,
	.vrt_minor =    0,
};

//lint -save -e835 A zero has been given as left argument to operatorp'+'
const struct vmod_data Vmod_wrong1_Data = {
	.vrt_major =    VRT_MAJOR_VERSION,
	.vrt_minor =    VRT_MINOR_VERSION + 1,
};
//lint -restore

static const struct foo {
	int bar;
} foo_struct[1];

const struct vmod_data Vmod_wrong2_Data = {
	.vrt_major =    VRT_MAJOR_VERSION,
	.vrt_minor =    VRT_MINOR_VERSION,
	.name =		"wrongN",
	.func =		foo_struct,
	.func_len =	sizeof foo_struct,
	.func_name =	"foo_struct",
	.proto =	"blablabla",
};

const struct vmod_data Vmod_wrong3_Data = {
	.vrt_major =    VRT_MAJOR_VERSION,
	.vrt_minor =    VRT_MINOR_VERSION,
	.name =		"wrongN",
	.func =		foo_struct,
	.func_len =	sizeof foo_struct,
	.func_name =	"foo_struct",
	.proto =	"blablabla",
	.abi =		"abiblabla",
};

//lint -restore

/*---------------------------------------------------------------------*/

struct VPFX(debug_director) {
	unsigned			magic;
#define VMOD_DEBUG_DIRECTOR_MAGIC	0x66b9ff3d
	VCL_BACKEND			dir;
};

/* XXX more callbacks ? */
static vdi_healthy_f vmod_debug_director_healthy;
static vdi_resolve_f vmod_debug_director_resolve;

static const struct vdi_methods vmod_debug_director_methods[1] = {{
	.magic =	VDI_METHODS_MAGIC,
	.type =		"debug.director",
	.resolve =	vmod_debug_director_resolve,
	.healthy =	vmod_debug_director_healthy
}};

VCL_VOID v_matchproto_(td_xyzzy_debug_director__init)
xyzzy_director__init(VRT_CTX, struct VPFX(debug_director) **dp,
    const char *vcl_name)
{
	struct VPFX(debug_director) *d;

	AN(dp);
	AZ(*dp);
	ALLOC_OBJ(d, VMOD_DEBUG_DIRECTOR_MAGIC);
	AN(d);

	*dp = d;
	d->dir = VRT_AddDirector(ctx, vmod_debug_director_methods, d,
	    "%s", vcl_name);
}

VCL_VOID v_matchproto_(td_xyzzy_debug_director__fini)
xyzzy_director__fini(struct VPFX(debug_director) **dp)
{
	struct VPFX(debug_director) *d;

	TAKE_OBJ_NOTNULL(d, dp, VMOD_DEBUG_DIRECTOR_MAGIC);
	VRT_DelDirector(&d->dir);
	FREE_OBJ(d);
}

VCL_BACKEND v_matchproto_(td_xyzzy_debug_director_fail)
xyzzy_director_fail(VRT_CTX, struct VPFX(debug_director) *d)
{
	CHECK_OBJ_NOTNULL(d, VMOD_DEBUG_DIRECTOR_MAGIC);
	(void) ctx;

	return (d->dir);
}

static VCL_BOOL v_matchproto_(vdi_healthy_f)
vmod_debug_director_healthy(VRT_CTX, VCL_BACKEND dir, VCL_TIME *changed)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	(void) dir;
	(void) changed;

	VRT_fail(ctx, "fail");
	return (1);
}

static VCL_BACKEND v_matchproto_(vdi_resolve_f)
vmod_debug_director_resolve(VRT_CTX, VCL_BACKEND dir)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	(void) dir;

	VRT_fail(ctx, "fail");
	return (NULL);
}

VCL_STRING v_matchproto_(td_xyzzy_debug_client_ip)
xyzzy_client_ip(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return (SES_Get_String_Attr(ctx->sp, SA_CLIENT_IP));
}

VCL_STRING v_matchproto_(td_xyzzy_debug_client_port)
xyzzy_client_port(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return (SES_Get_String_Attr(ctx->sp, SA_CLIENT_PORT));
}

static void
fail_f(void *priv)
{
	VRT_CTX;

	CAST_OBJ_NOTNULL(ctx, priv, VRT_CTX_MAGIC);
	VRT_fail(ctx, "thou shalt not rollet back");
}

VCL_VOID v_matchproto_(td_xyzzy_debug_fail_rollback)
xyzzy_fail_rollback(VRT_CTX)
{
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	p = VRT_priv_task(ctx, &fail_rollback_token);
	if (p == NULL) {
		VRT_fail(ctx, "no priv task - out of ws?");
		return;
	}

	if (p->priv != NULL) {
		assert(p->priv == ctx);
		assert(p->free == fail_f);
		return;
	}

	p->priv = TRUST_ME(ctx);
	p->free = fail_f;
}

VCL_VOID v_matchproto_(td_xyzzy_debug_ok_rollback)
xyzzy_ok_rollback(VRT_CTX)
{
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	p = VRT_priv_task(ctx, &fail_rollback_token);
	if (p == NULL) {
		VRT_fail(ctx, "no priv task - out of ws?");
		return;
	}

	p->priv = NULL;
	p->free = NULL;
}
