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
#include "vcc_debug_if.h"
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
	int			tmpf;
};


static pthread_mutex_t vsc_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct vsc_seg *vsc_seg = NULL;
static struct VSC_debug *vsc = NULL;
static int loads;
static const int store_ip_token;
static const int fail_task_fini_token;
extern void mylog(struct vsl_log *vsl, enum VSL_tag_e tag,
    const char *fmt, ...) v_printflike_(3,4);

/**********************************************************************/

static enum vfp_status v_matchproto_(vfp_pull_f)
xyzzy_vfp_rot13_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
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

static const struct vfp xyzzy_vfp_rot13 = {
	.name = "rot13",
	.pull = xyzzy_vfp_rot13_pull,
};

/**********************************************************************/

// deliberately fragmenting the stream to make testing more interesting
#define ROT13_BUFSZ 8

static int v_matchproto_(vdp_init_f)
xyzzy_vfp_rot13_init(VRT_CTX, struct vdp_ctx *vdc, void **priv, struct objcore *oc)
{
	(void)vdc;
	(void)oc;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	*priv = malloc(ROT13_BUFSZ);
	if (*priv == NULL)
		return (-1);
	return (0);
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_vfp_rot13_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	char *q;
	const char *pp;
	int i, j, retval = 0;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	AN(priv);
	AN(*priv);
	if (len <= 0)
		return (VDP_bytes(vdc, act, ptr, len));
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
		if (i == ROT13_BUFSZ - 1 && j < len - 1) {
			retval = VDP_bytes(vdc, VDP_FLUSH, q, ROT13_BUFSZ);
			if (retval != 0)
				return (retval);
			i = -1;
		}
	}
	if (i >= 0)
		retval = VDP_bytes(vdc, act, q, i);
	return (retval);
}

static int v_matchproto_(vdp_fini_f)
xyzzy_vfp_rot13_fini(struct vdp_ctx *vdc, void **priv)
{
	(void)vdc;
	AN(priv);
	free(*priv);
	*priv = NULL;
	return (0);
}

static const struct vdp xyzzy_vdp_rot13 = {
	.name  = "rot13",
	.init  = xyzzy_vfp_rot13_init,
	.bytes = xyzzy_vfp_rot13_bytes,
	.fini  = xyzzy_vfp_rot13_fini,
};

/**********************************************************************
 * pedantic tests of the VDP API:
 * - assert that we see a VDP_END
 * - assert that _fini gets called before the task ends
 *
 * note:
 * we could lookup our own vdpe in _fini and check for vdpe->end == VDP_END
 * yet that would cross the API
 */

enum vdp_state_e {
	VDPS_NULL = 0,
	VDPS_INIT,	// _init called
	VDPS_BYTES,	// _bytes called act != VDP_END
	VDPS_END,	// _bytes called act == VDP_END
	VDPS_FINI	// _fini called
};

struct vdp_state_s {
	unsigned		magic;
#define VDP_STATE_MAGIC	0x57c8d309
	enum vdp_state_e	state;
};

static void v_matchproto_(vmod_priv_fini_f)
priv_pedantic_fini(VRT_CTX, void *priv)
{
	struct vdp_state_s *vdps;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ_NOTNULL(vdps, priv, VDP_STATE_MAGIC);

	assert(vdps->state == VDPS_FINI);
}

static const struct vmod_priv_methods priv_pedantic_methods[1] = {{
	.magic = VMOD_PRIV_METHODS_MAGIC,
	.type = "debug_vdp_pedantic",
	.fini = priv_pedantic_fini
}};

static int v_matchproto_(vdp_init_f)
xyzzy_pedantic_init(VRT_CTX, struct vdp_ctx *vdc, void **priv,
    struct objcore *oc)
{
	struct vdp_state_s *vdps;
	struct vmod_priv *p;

	(void)oc;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	WS_TASK_ALLOC_OBJ(ctx, vdps, VDP_STATE_MAGIC);
	if (vdps == NULL)
		return (-1);
	assert(vdps->state == VDPS_NULL);

	p = VRT_priv_task(ctx, (void *)vdc);
	if (p == NULL)
		return (-1);
	p->priv = vdps;
	p->methods = priv_pedantic_methods;

	AN(priv);
	*priv = vdps;

	vdps->state = VDPS_INIT;

	return (0);
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_pedantic_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct vdp_state_s *vdps;

	CAST_OBJ_NOTNULL(vdps, *priv, VDP_STATE_MAGIC);
	assert(vdps->state >= VDPS_INIT);
	assert(vdps->state < VDPS_END);

	if (act == VDP_END)
		vdps->state = VDPS_END;
	else
		vdps->state = VDPS_BYTES;

	return (VDP_bytes(vdc, act, ptr, len));
}

static int v_matchproto_(vdp_fini_f)
xyzzy_pedantic_fini(struct vdp_ctx *vdc, void **priv)
{
	struct vdp_state_s *vdps;

	(void) vdc;
	AN(priv);
	if (*priv == NULL)
		return (0);
	CAST_OBJ_NOTNULL(vdps, *priv, VDP_STATE_MAGIC);
	assert(vdps->state == VDPS_INIT || vdps->state == VDPS_END);
	vdps->state = VDPS_FINI;

	*priv = NULL;
	return (0);
}

static const struct vdp xyzzy_vdp_pedantic = {
	.name  = "debug.pedantic",
	.init  = xyzzy_pedantic_init,
	.bytes = xyzzy_pedantic_bytes,
	.fini  = xyzzy_pedantic_fini,
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

#define AN0(x) (void) 0
#define AN1(x) AN(x)
#define PRIV_FINI(name, assert)						\
static void v_matchproto_(vmod_priv_fini_f)				\
priv_ ## name ## _fini(VRT_CTX, void *ptr)				\
{									\
	const char * const fmt = "priv_" #name "_fini(%p)";		\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	AN ## assert (ptr);						\
	mylog(ctx->vsl, SLT_Debug, fmt, (char *)ptr);			\
	free(ptr);							\
}									\
									\
static const struct vmod_priv_methods					\
xyzzy_test_priv_ ## name ## _methods[1] = {{				\
	.magic = VMOD_PRIV_METHODS_MAGIC,				\
	.type = "debug_test_priv_" #name,				\
	.fini = priv_ ## name ## _fini					\
}};
PRIV_FINI(call, 0)
PRIV_FINI(task, 1)
PRIV_FINI(top, 1)
#undef PRIV_FINI
#undef AN0
#undef AN1

VCL_VOID v_matchproto_(td_debug_test_priv_call)
xyzzy_test_priv_call(VRT_CTX, struct vmod_priv *priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup("BAR");
		priv->methods = xyzzy_test_priv_call_methods;
	} else {
		assert(priv->methods == xyzzy_test_priv_call_methods);
		assert(!strcmp(priv->priv, "BAR"));
	}
}

VCL_VOID v_matchproto_(td_debug_test_priv_task_get)
xyzzy_test_priv_task_get(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AZ(VRT_priv_task_get(ctx, NULL));
}

VCL_STRING v_matchproto_(td_debug_test_priv_task)
xyzzy_test_priv_task(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (s == NULL || *s == '\0') {
		mylog(ctx->vsl, SLT_Debug, "test_priv_task(%p) = %p (exists)",
		    priv, priv->priv);
	} else if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->methods = xyzzy_test_priv_task_methods;
		mylog(ctx->vsl, SLT_Debug, "test_priv_task(%p) = %p (new)",
		    priv, priv->priv);
	} else {
		char *n = realloc(priv->priv,
		    strlen(priv->priv) + strlen(s) + 2);
		if (n == NULL)
			return (NULL);
		strcat(n, " ");
		strcat(n, s);
		priv->priv = n;
		mylog(ctx->vsl, SLT_Debug, "test_priv_task(%p) = %p (update)",
		    priv, priv->priv);
	}
	if (priv->priv != NULL)
		assert(priv->methods == xyzzy_test_priv_task_methods);
	return (priv->priv);
}

VCL_STRING v_matchproto_(td_debug_test_priv_top)
xyzzy_test_priv_top(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->methods = xyzzy_test_priv_top_methods;
	} else {
		assert(priv->methods == xyzzy_test_priv_top_methods);
	}
	return (priv->priv);
}

VCL_VOID v_matchproto_(td_debug_test_priv_vcl)
xyzzy_test_priv_vcl(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;
	char t[PATH_MAX];
	ssize_t l;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);

	l = pread(priv_vcl->tmpf, t, sizeof t, 0);
	assert(l > 0);

	AN(priv_vcl->foo);
	assert(!strncmp(priv_vcl->foo, t, l));
}

VCL_VOID v_matchproto_(td_debug_rot104)
xyzzy_rot104(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	// This should fail
	AN(VRT_AddFilter(ctx, &xyzzy_vfp_rot13, &xyzzy_vdp_rot13));
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
	VSL(SLT_Debug, NO_VXID, "Object Event: %s 0x%jx", what,
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
	VSL(SLT_Debug, NO_VXID, "Subscribed to Object Events");
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

static void v_matchproto_(vmod_priv_fini_f)
priv_vcl_fini(VRT_CTX, void *priv)
{
	struct priv_vcl *priv_vcl;

	CAST_OBJ_NOTNULL(priv_vcl, priv, PRIV_VCL_MAGIC);
	AZ(close(priv_vcl->tmpf));
	AN(priv_vcl->foo);
	AZ(unlink(priv_vcl->foo));
	free(priv_vcl->foo);
	if (priv_vcl->obj_cb != 0) {
		ObjUnsubscribeEvents(&priv_vcl->obj_cb);
		VSLb(ctx->vsl, SLT_Debug, "Unsubscribed from Object Events");
	}
	AZ(priv_vcl->vclref_discard);
	AZ(priv_vcl->vclref_cold);
	FREE_OBJ(priv_vcl);
}

static const struct vmod_priv_methods priv_vcl_methods[1] = {{
	.magic = VMOD_PRIV_METHODS_MAGIC,
	.type = "debug_priv_vcl_fini",
	.fini = priv_vcl_fini
}};

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
	priv_vcl->foo = strdup("worker_tmpdir/vmod_debug.XXXXXX");
	AN(priv_vcl->foo);
	priv_vcl->tmpf = mkstemp(priv_vcl->foo);
	assert(priv_vcl->tmpf >= 0);
	AN(write(priv_vcl->tmpf, priv_vcl->foo, strlen(priv_vcl->foo)));
	priv->priv = priv_vcl;
	priv->methods = priv_vcl_methods;

	AZ(VRT_AddFilter(ctx, &xyzzy_vfp_rot13, &xyzzy_vdp_rot13));
	AZ(VRT_AddFilter(ctx, NULL, &xyzzy_vdp_pedantic));
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
	const char *vcl_name = VCL_Name(ctx->vcl);

	// Using VSLs for coverage
	VSLs(SLT_Debug, NO_VXID, TOSTRANDS(2, vcl_name, ": VCL_EVENT_WARM"));

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
	struct vrt_endpoint vep[1];
	struct vrt_backend be[1];

	INIT_OBJ(vep, VRT_ENDPOINT_MAGIC);
	vep->uds_path = "/";
	INIT_OBJ(be, VRT_BACKEND_MAGIC);
	be->endpoint = vep;
	be->vcl_name = "doomed";
	return (VRT_new_backend(ctx, be, NULL));
}

static int
event_cold(VRT_CTX, const struct vmod_priv *priv)
{
	pthread_t thread;
	struct priv_vcl *priv_vcl;

	AZ(ctx->msg);

	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);

	VSL(SLT_Debug, NO_VXID, "%s: VCL_EVENT_COLD", VCL_Name(ctx->vcl));

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

	PTOK(pthread_create(&thread, NULL, cooldown_thread, priv_vcl));
	PTOK(pthread_detach(thread));
	return (0);
}

static int
event_discard(VRT_CTX, void *priv)
{

	(void)priv;

	AZ(ctx->msg);

	VRT_RemoveFilter(ctx, &xyzzy_vfp_rot13, &xyzzy_vdp_rot13);
	VRT_RemoveFilter(ctx, NULL, &xyzzy_vdp_pedantic);

	if (--loads)
		return (0);

	/*
	 * The vsc and vsc_seg variables are not per-VCL, they are
	 * the same in all VCL's which import the same binary version
	 * of this VMOD, so we should only carry out cleanup on the
	 * last discard event.
	 */
	PTOK(pthread_mutex_lock(&vsc_mtx));
	if (vsc != NULL) {
		VSC_debug_Destroy(&vsc_seg);
		vsc = NULL;
	}
	PTOK(pthread_mutex_unlock(&vsc_mtx));

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
	PTOK(pthread_mutex_lock(&vsc_mtx));
	if (vsc == NULL) {
		AZ(vsc_seg);
		vsc = VSC_debug_New(NULL, &vsc_seg, "");
	}
	AN(vsc);
	AN(vsc_seg);
	PTOK(pthread_mutex_unlock(&vsc_mtx));
}

VCL_VOID
xyzzy_vsc_count(VRT_CTX, VCL_INT cnt)
{
	(void)ctx;
	PTOK(pthread_mutex_lock(&vsc_mtx));
	AN(vsc);
	vsc->count += cnt;
	PTOK(pthread_mutex_unlock(&vsc_mtx));
}

VCL_VOID
xyzzy_vsc_destroy(VRT_CTX)
{
	(void)ctx;
	PTOK(pthread_mutex_lock(&vsc_mtx));
	if (vsc != NULL) {
		AN(vsc_seg);
		VSC_debug_Destroy(&vsc_seg);
	}
	AZ(vsc_seg);
	vsc = NULL;
	PTOK(pthread_mutex_unlock(&vsc_mtx));
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


	TAKE_OBJ_NOTNULL(concat, concatp, CONCAT_MAGIC);
	free(TRUST_ME(concat->s));
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
		AN(WS_Allocated(ctx->ws, r, strlen(r) + 1));
	return (r);
}

VCL_STRING
xyzzy_collect(VRT_CTX, VCL_STRANDS s)
{
	VCL_STRING r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	r = VRT_STRANDS_string(ctx, s);
	if (r != NULL && *r != '\0')
		AN(WS_Allocated(ctx->ws, r, strlen(r) + 1));
	return (r);
}

/* cf. VRT_SetHdr() */
VCL_VOID
xyzzy_sethdr(VRT_CTX, VCL_HEADER hdr, VCL_STRANDS s)
{
	struct http *hp;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (hdr == NULL) {
		VRT_fail(ctx, "debug.sethdr(): header argument is NULL");
		return;
	}
	hp = VRT_selecthttp(ctx, hdr->where);
	if (hp == NULL) {
		VRT_fail(ctx, "debug.sethdr(): header argument "
		    "cannot be used here");
		return;
	}
	AN(hdr->what);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (s->n == 0) {
		http_Unset(hp, hdr->what);
	} else {
		b = VRT_StrandsWS(hp->ws, hdr->what + 1, s);
		if (b == NULL) {
			VSLbs(ctx->vsl, SLT_LostHeader,
			    TOSTRAND(hdr->what + 1));
		} else {
			if (*b != '\0')
				AN(WS_Allocated(hp->ws, b, strlen(b) + 1));
			http_Unset(hp, hdr->what);
			http_SetHeader(hp, b);
		}
	}
}

void
mylog(struct vsl_log *vsl, enum VSL_tag_e tag,  const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (vsl != NULL)
		VSLbv(vsl, tag, fmt, ap);
	else
		VSLv(tag, NO_VXID, fmt, ap);
	va_end(ap);
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
			p = VRT_priv_task_get(ctx, (void *)(uintptr_t)s);
			AN(p);
			check += (uintptr_t)p->priv;
			p->priv = (void *)(uintptr_t)(s * rounds + r);
		}
	}
	t1 = VTIM_mono();

	d = (t1 - t0) * 1e9 / ((double)size * (double)rounds);

	mylog(ctx->vsl, SLT_Debug,
	     "perf size %jd rounds %jd time %.1fns check %jd",
	     (intmax_t)size, (intmax_t)rounds, d, (uintmax_t)check);

	return (d);
}

VCL_STRANDS
xyzzy_return_strands(VRT_CTX, VCL_STRANDS strand)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	VSLbs(ctx->vsl, SLT_Debug, strand);
	return (strand);
}

VCL_VOID
xyzzy_vsl_flush(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	VSL_Flush(ctx->vsl, 0);
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
	WS_TASK_ALLOC_OBJ(ctx, req->vcf, VCF_MAGIC);
	if (req->vcf == NULL)
		return;
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

	AZ(priv->methods);
	assert(VSA_Sane(ip));
	priv->priv = TRUST_ME(ip);
}

VCL_IP
xyzzy_get_ip(VRT_CTX)
{
	struct vmod_priv *priv;
	VCL_IP ip;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	priv = VRT_priv_task_get(ctx, &store_ip_token);
	AN(priv);
	AZ(priv->methods);

	ip = priv->priv;
	assert(VSA_Sane(ip));
	return (ip);
}

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

static void * fail_magic = &fail_magic;

static void
fail_f(VRT_CTX, void *priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(priv == fail_magic);

	VRT_fail(ctx, "thou shalt not fini");
}

static const struct vmod_priv_methods xyzzy_fail_task_fini_methods[1] = {{
	.magic = VMOD_PRIV_METHODS_MAGIC,
	.type = "debug_fail_task_fini",
	.fini = fail_f
}};

VCL_VOID v_matchproto_(td_xyzzy_debug_fail_task_fini)
xyzzy_fail_task_fini(VRT_CTX)
{
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	p = VRT_priv_task(ctx, &fail_task_fini_token);
	if (p == NULL) {
		VRT_fail(ctx, "no priv task - out of ws?");
		return;
	}

	if (p->priv != NULL) {
		assert(p->priv == fail_magic);
		assert(p->methods == xyzzy_fail_task_fini_methods);
		return;
	}

	p->priv = fail_magic;
	p->methods = xyzzy_fail_task_fini_methods;
}

VCL_VOID v_matchproto_(td_xyzzy_debug_ok_task_fini)
xyzzy_ok_task_fini(VRT_CTX)
{
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	p = VRT_priv_task(ctx, &fail_task_fini_token);
	if (p == NULL) {
		VRT_fail(ctx, "no priv task - out of ws?");
		return;
	}

	p->priv = NULL;
	p->methods = NULL;
}

VCL_STRING v_matchproto_(td_xyzzy_debug_re_quote)
xyzzy_re_quote(VRT_CTX, VCL_STRING s)
{
	struct vsb vsb[1];
	char *q;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	WS_VSB_new(vsb, ctx->ws);
	VRE_quote(vsb, s);
	q = WS_VSB_finish(vsb, ctx->ws, NULL);
	if (q == NULL)
		WS_MarkOverflow(ctx->ws);
	return (q);
}

VCL_STRING v_matchproto_(td_xyzzy_priv_task_with_option)
xyzzy_priv_task_with_option(VRT_CTX, struct VARGS(priv_task_with_option) *args)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(args->priv);
	if (args->priv->priv == NULL && args->valid_opt)
		args->priv->priv = WS_Copy(ctx->ws, args->opt, -1);
	return (args->priv->priv);
}

VCL_BOOL v_matchproto_(td_xyzzy_validhdr)
xyzzy_validhdr(VRT_CTX, VCL_STRANDS s)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (VRT_ValidHdr(ctx, s));
}

VCL_REGEX v_matchproto_(td_xyzzy_regex)
xyzzy_just_return_regex(VRT_CTX, VCL_REGEX r)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(r);
	return (r);
}

/*---------------------------------------------------------------------*/

VCL_VOID v_matchproto_(td_xyzzy_call)
xyzzy_call(VRT_CTX, VCL_SUB sub)
{
	VRT_call(ctx, sub);
}

VCL_STRING v_matchproto_(td_xyzzy_check_call)
xyzzy_check_call(VRT_CTX, VCL_SUB sub)
{
	return (VRT_check_call(ctx, sub));
}

/* the next two are to test WRONG vmod behavior:
 * holding a VCL_SUB reference across vcls
 */

static VCL_SUB wrong = NULL;

VCL_VOID v_matchproto_(td_xyzzy_bad_memory)
xyzzy_bad_memory(VRT_CTX, VCL_SUB sub)
{
	(void) ctx;

	wrong = sub;
}

VCL_SUB v_matchproto_(td_xyzzy_total_recall)
xyzzy_total_recall(VRT_CTX)
{
	(void) ctx;

	return (wrong);
}

/*---------------------------------------------------------------------*/

struct VPFX(debug_caller) {
       unsigned        magic;
#define DEBUG_CALLER_MAGIC 0xb47f3449
       VCL_SUB         sub;
};

VCL_VOID v_matchproto_(td_xyzzy_debug_caller__init)
xyzzy_caller__init(VRT_CTX, struct VPFX(debug_caller) **callerp,
    const char *name, VCL_SUB sub)
{
	struct VPFX(debug_caller) *caller;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(callerp);
	AZ(*callerp);
	AN(name);
	AN(sub);

	ALLOC_OBJ(caller, DEBUG_CALLER_MAGIC);
	AN(caller);
	*callerp = caller;
	caller->sub = sub;
}

VCL_VOID v_matchproto_(td_xyzzy_debug_caller__fini)
xyzzy_caller__fini(struct VPFX(debug_caller) **callerp)
{
	struct VPFX(debug_caller) *caller;

	if (callerp == NULL || *callerp == NULL)
		return;
	TAKE_OBJ_NOTNULL(caller, callerp, DEBUG_CALLER_MAGIC);
	FREE_OBJ(caller);
}

VCL_VOID v_matchproto_(td_xyzzy_debug_caller_call)
xyzzy_caller_call(VRT_CTX, struct VPFX(debug_caller) *caller)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(caller, DEBUG_CALLER_MAGIC);
	AN(caller->sub);

	VRT_call(ctx, caller->sub);
}

VCL_SUB v_matchproto_(td_xyzzy_debug_caller_sub)
xyzzy_caller_xsub(VRT_CTX, struct VPFX(debug_caller) *caller)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(caller, DEBUG_CALLER_MAGIC);
	AN(caller->sub);

	return (caller->sub);
}
