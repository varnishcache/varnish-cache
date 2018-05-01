/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
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
 */

#include "config.h"

#include <errno.h>
#include <dlfcn.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache_varnishd.h"
#include "common/heritage.h"

#include "vcl.h"

#include "cache_director.h"
#include "cache_vcl.h"
#include "vcli_serve.h"

const char * const VCL_TEMP_INIT = "init";
const char * const VCL_TEMP_COLD = "cold";
const char * const VCL_TEMP_WARM = "warm";
const char * const VCL_TEMP_BUSY = "busy";
const char * const VCL_TEMP_COOLING = "cooling";
const char * const VCL_TEMP_LABEL = "label";

/*
 * XXX: Presently all modifications to this list happen from the
 * CLI event-engine, so no locking is necessary
 */
static VTAILQ_HEAD(, vcl)	vcl_head =
    VTAILQ_HEAD_INITIALIZER(vcl_head);

struct lock		vcl_mtx;
struct vcl		*vcl_active; /* protected by vcl_mtx */

static struct vrt_ctx ctx_cli;
static unsigned handling_cli;
static struct ws ws_cli;
static uintptr_t ws_snapshot_cli;

/*--------------------------------------------------------------------*/

void
VCL_Bo2Ctx(struct vrt_ctx *ctx, struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	ctx->vcl = bo->vcl;
	ctx->vsl = bo->vsl;
	ctx->http_bereq = bo->bereq;
	ctx->http_beresp = bo->beresp;
	ctx->bo = bo;
	ctx->sp = bo->sp;
	ctx->now = bo->t_prev;
	ctx->ws = bo->ws;
}

void
VCL_Req2Ctx(struct vrt_ctx *ctx, struct req *req)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	ctx->vcl = req->vcl;
	ctx->vsl = req->vsl;
	ctx->http_req = req->http;
	ctx->http_req_top = req->top->http;
	ctx->http_resp = req->resp;
	ctx->req = req;
	ctx->sp = req->sp;
	ctx->now = req->t_prev;
	ctx->ws = req->ws;
}

/*--------------------------------------------------------------------*/

static struct vrt_ctx *
vcl_get_ctx(unsigned method, int msg)
{

	ASSERT_CLI();
	AZ(ctx_cli.handling);
	INIT_OBJ(&ctx_cli, VRT_CTX_MAGIC);
	handling_cli = 0;
	ctx_cli.handling = &handling_cli;
	ctx_cli.method = method;
	if (msg) {
		ctx_cli.msg = VSB_new_auto();
		AN(ctx_cli.msg);
	}
	ctx_cli.ws = &ws_cli;
	WS_Assert(ctx_cli.ws);
	VRTPRIV_init(cli_task_privs);
	return (&ctx_cli);
}

static void
vcl_rel_ctx(struct vrt_ctx **ctx)
{

	ASSERT_CLI();
	assert(*ctx == &ctx_cli);
	AN((*ctx)->handling);
	if (ctx_cli.msg)
		VSB_destroy(&ctx_cli.msg);
	WS_Assert(ctx_cli.ws);
	WS_Reset(&ws_cli, ws_snapshot_cli);
	INIT_OBJ(*ctx, VRT_CTX_MAGIC);
	*ctx = NULL;
	VRTPRIV_dynamic_kill(cli_task_privs, (uintptr_t)cli_task_privs);
}

/*--------------------------------------------------------------------*/

static int
vcl_send_event(VRT_CTX, enum vcl_event_e ev)
{
	int r;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl->conf, VCL_CONF_MAGIC);
	assert(ev == VCL_EVENT_LOAD ||
	       ev == VCL_EVENT_WARM ||
	       ev == VCL_EVENT_COLD ||
	       ev == VCL_EVENT_DISCARD);
	AN(ctx->handling);
	*ctx->handling = 0;
	AN(ctx->ws);

	if (ev == VCL_EVENT_LOAD || ev == VCL_EVENT_WARM)
		AN(ctx->msg);

	r = ctx->vcl->conf->event_vcl(ctx, ev);

	if (r && (ev == VCL_EVENT_COLD || ev == VCL_EVENT_DISCARD))
		WRONG("A VMOD cannot fail COLD or DISCARD events");

	return (r);
}

/*--------------------------------------------------------------------*/

struct vcl *
vcl_find(const char *name)
{
	struct vcl *vcl;

	ASSERT_CLI();
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl->discard)
			continue;
		if (!strcmp(vcl->loaded_name, name))
			return (vcl);
	}
	return (NULL);
}

/*--------------------------------------------------------------------*/

void
VCL_Panic(struct vsb *vsb, const struct vcl *vcl)
{
	int i;

	AN(vsb);
	if (vcl == NULL)
		return;
	VSB_printf(vsb, "vcl = {\n");
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, vcl, VCL_MAGIC);
	VSB_printf(vsb, "name = \"%s\",\n", vcl->loaded_name);
	VSB_printf(vsb, "busy = %u,\n", vcl->busy);
	VSB_printf(vsb, "discard = %u,\n", vcl->discard);
	VSB_printf(vsb, "state = %s,\n", vcl->state);
	VSB_printf(vsb, "temp = %s,\n", (const volatile char *)vcl->temp);
	VSB_printf(vsb, "conf = {\n");
	VSB_indent(vsb, 2);
	if (vcl->conf == NULL) {
		VSB_printf(vsb, "conf = NULL\n");
	} else {
		PAN_CheckMagic(vsb, vcl->conf, VCL_CONF_MAGIC);
		VSB_printf(vsb, "srcname = {\n");
		VSB_indent(vsb, 2);
		for (i = 0; i < vcl->conf->nsrc; ++i)
			VSB_printf(vsb, "\"%s\",\n", vcl->conf->srcname[i]);
		VSB_indent(vsb, -2);
		VSB_printf(vsb, "},\n");
	}
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

void
vcl_get(struct vcl **vcc, struct vcl *vcl)
{
	AN(vcc);

	Lck_Lock(&vcl_mtx);
	if (vcl == NULL)
		vcl = vcl_active; /* Sample vcl_active under lock to avoid
				   * race */
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	if (vcl->label == NULL) {
		AN(strcmp(vcl->state, VCL_TEMP_LABEL));
		*vcc = vcl;
	} else {
		AZ(strcmp(vcl->state, VCL_TEMP_LABEL));
		*vcc = vcl->label;
	}
	CHECK_OBJ_NOTNULL(*vcc, VCL_MAGIC);
	AZ((*vcc)->discard);
	(*vcc)->busy++;
	Lck_Unlock(&vcl_mtx);
	AZ(errno=pthread_rwlock_rdlock(&(*vcc)->temp_rwl));
	assert(VCL_WARM(*vcc));
	AZ(errno=pthread_rwlock_unlock(&(*vcc)->temp_rwl));
}

/*--------------------------------------------------------------------*/

static int
vcl_iterdir(struct cli *cli, const char *pat, const struct vcl *vcl,
    vcl_be_func *func, void *priv)
{
	int i, found = 0;
	struct vcldir *vdir;

	VTAILQ_FOREACH(vdir, &vcl->director_list, list) {
		if (fnmatch(pat, vdir->cli_name, 0))
			continue;
		found++;
		i = func(cli, vdir->dir, priv);
		if (i < 0)
			return (i);
		found += i;
	}
	return (found);
}

int
VCL_IterDirector(struct cli *cli, const char *pat,
    vcl_be_func *func, void *priv)
{
	int i, found = 0;
	struct vsb *vsb;
	struct vcl *vcl;

	ASSERT_CLI();
	vsb = VSB_new_auto();
	AN(vsb);
	if (pat == NULL || *pat == '\0' || !strcmp(pat, "*")) {
		// all backends in active VCL
		VSB_printf(vsb, "%s.*", VCL_Name(vcl_active));
		vcl = vcl_active;
	} else if (strchr(pat, '.') == NULL) {
		// pattern applies to active vcl
		VSB_printf(vsb, "%s.%s", VCL_Name(vcl_active), pat);
		vcl = vcl_active;
	} else {
		// use pattern as is
		VSB_cat(vsb, pat);
		vcl = NULL;
	}
	AZ(VSB_finish(vsb));
	Lck_Lock(&vcl_mtx);
	if (vcl != NULL) {
		found = vcl_iterdir(cli, VSB_data(vsb), vcl, func, priv);
	} else {
		VTAILQ_FOREACH(vcl, &vcl_head, list) {
			i = vcl_iterdir(cli, VSB_data(vsb), vcl, func, priv);
			if (i < 0) {
				found = i;
				break;
			} else {
				found += i;
			}
		}
	}
	Lck_Unlock(&vcl_mtx);
	VSB_destroy(&vsb);
	return (found);
}

static void
vcl_BackendEvent(const struct vcl *vcl, enum vcl_event_e e)
{
	struct vcldir *vdir;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	AZ(vcl->busy);

	VTAILQ_FOREACH(vdir, &vcl->director_list, list)
		VDI_Event(vdir->dir, e);
}

static void
vcl_KillBackends(struct vcl *vcl)
{
	struct vcldir *vdir;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	AZ(vcl->busy);
	assert(VTAILQ_EMPTY(&vcl->ref_list));
	while (1) {
		vdir = VTAILQ_FIRST(&vcl->director_list);
		if (vdir == NULL)
			break;
		VTAILQ_REMOVE(&vcl->director_list, vdir, list);
		REPLACE(vdir->cli_name, NULL);
		AN(vdir->methods->destroy);
		vdir->methods->destroy(vdir->dir);
		FREE_OBJ(vdir);
	}
}

/*--------------------------------------------------------------------*/

static struct vcl *
VCL_Open(const char *fn, struct vsb *msg)
{
	struct vcl *vcl;
	void *dlh;
	struct VCL_conf const *cnf;

	AN(fn);
	AN(msg);

#ifdef RTLD_NOLOAD
	/* Detect bogus caching by dlopen(3) */
	dlh = dlopen(fn, RTLD_NOW | RTLD_NOLOAD);
	AZ(dlh);
#endif
	dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (dlh == NULL) {
		VSB_printf(msg, "Could not load compiled VCL.\n");
		VSB_printf(msg, "\tdlopen() = %s\n", dlerror());
		return (NULL);
	}
	cnf = dlsym(dlh, "VCL_conf");
	if (cnf == NULL) {
		VSB_printf(msg, "Compiled VCL lacks metadata.\n");
		(void)dlclose(dlh);
		return (NULL);
	}
	if (cnf->magic != VCL_CONF_MAGIC) {
		VSB_printf(msg, "Compiled VCL has mangled metadata.\n");
		(void)dlclose(dlh);
		return (NULL);
	}
	if (cnf->syntax < heritage.min_vcl || cnf->syntax > heritage.max_vcl) {
		VSB_printf(msg, "Compiled VCL version (%.1f) not supported.\n",
		    .1 * cnf->syntax);
		(void)dlclose(dlh);
		return (NULL);
	}
	ALLOC_OBJ(vcl, VCL_MAGIC);
	AN(vcl);
	AZ(errno=pthread_rwlock_init(&vcl->temp_rwl, NULL));
	vcl->dlh = dlh;
	vcl->conf = cnf;
	return (vcl);
}

static void
VCL_Close(struct vcl **vclp)
{
	struct vcl *vcl;

	CHECK_OBJ_NOTNULL(*vclp, VCL_MAGIC);
	vcl = *vclp;
	*vclp = NULL;
	AZ(dlclose(vcl->dlh));
	AZ(errno=pthread_rwlock_destroy(&vcl->temp_rwl));
	FREE_OBJ(vcl);
}

/*--------------------------------------------------------------------
 * NB: This function is called from the test-load subprocess.
 */

int
VCL_TestLoad(const char *fn)
{
	struct vsb *vsb;
	struct vcl *vcl;
	int retval = 0;

	AN(fn);
	vsb = VSB_new_auto();
	AN(vsb);
	vcl = VCL_Open(fn, vsb);
	if (vcl == NULL) {
		AZ(VSB_finish(vsb));
		fprintf(stderr, "%s", VSB_data(vsb));
		retval = -1;
	} else
		VCL_Close(&vcl);
	VSB_destroy(&vsb);
	return (retval);
}

/*--------------------------------------------------------------------*/

static void
vcl_print_refs(VRT_CTX)
{
	struct vcl *vcl;
	struct vclref *ref;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	AN(ctx->msg);
	vcl = ctx->vcl;
	VSB_printf(ctx->msg, "VCL %s is waiting for:", vcl->loaded_name);
	Lck_Lock(&vcl_mtx);
	VTAILQ_FOREACH(ref, &ctx->vcl->ref_list, list)
		VSB_printf(ctx->msg, "\n\t- %s", ref->desc);
	Lck_Unlock(&vcl_mtx);
}

static int
vcl_set_state(VRT_CTX, const char *state)
{
	struct vcl *vcl;
	int i = 0;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl->conf, VCL_CONF_MAGIC);
	AN(ctx->handling);
	AN(ctx->vcl);
	AN(state);
	assert(ctx->msg != NULL || *state == '0');

	vcl = ctx->vcl;
	AZ(errno=pthread_rwlock_wrlock(&vcl->temp_rwl));
	AN(vcl->temp);

	switch (state[0]) {
	case '0':
		assert(vcl->temp != VCL_TEMP_COLD);
		if (vcl->busy == 0 && VCL_WARM(vcl)) {

			vcl->temp = VTAILQ_EMPTY(&vcl->ref_list) ?
			    VCL_TEMP_COLD : VCL_TEMP_COOLING;
			AZ(vcl_send_event(ctx, VCL_EVENT_COLD));
			vcl_BackendEvent(vcl, VCL_EVENT_COLD);
		}
		else if (vcl->busy)
			vcl->temp = VCL_TEMP_BUSY;
		else if (VTAILQ_EMPTY(&vcl->ref_list))
			vcl->temp = VCL_TEMP_COLD;
		else
			vcl->temp = VCL_TEMP_COOLING;
		break;
	case '1':
		assert(vcl->temp != VCL_TEMP_WARM);
		/* The warm VCL hasn't seen a cold event yet */
		if (vcl->temp == VCL_TEMP_BUSY)
			vcl->temp = VCL_TEMP_WARM;
		/* The VCL must first reach a stable cold state */
		else if (vcl->temp == VCL_TEMP_COOLING) {
			vcl_print_refs(ctx);
			i = -1;
		}
		else {
			vcl->temp = VCL_TEMP_WARM;
			i = vcl_send_event(ctx, VCL_EVENT_WARM);
			if (i == 0)
				vcl_BackendEvent(vcl, VCL_EVENT_WARM);
			else
				AZ(vcl->conf->event_vcl(ctx, VCL_EVENT_COLD));
		}
		break;
	default:
		WRONG("Wrong enum state");
	}
	AZ(errno=pthread_rwlock_unlock(&vcl->temp_rwl));

	return (i);
}

static void
vcl_cancel_load(VRT_CTX, struct cli *cli, const char *name, const char *step)
{
	struct vcl *vcl = ctx->vcl;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);

	AZ(VSB_finish(ctx->msg));
	VCLI_SetResult(cli, CLIS_CANT);
	VCLI_Out(cli, "VCL \"%s\" Failed %s", name, step);
	if (VSB_len(ctx->msg))
		VCLI_Out(cli, "\nMessage:\n\t%s", VSB_data(ctx->msg));
	*ctx->handling = 0;
	AZ(vcl->conf->event_vcl(ctx, VCL_EVENT_DISCARD));
	vcl_KillBackends(vcl);
	free(vcl->loaded_name);
	VCL_Close(&vcl);
}

static void
vcl_load(struct cli *cli, struct vrt_ctx *ctx,
    const char *name, const char *fn, const char *state)
{
	struct vcl *vcl;
	int i;

	ASSERT_CLI();

	vcl = vcl_find(name);
	AZ(vcl);

	vcl = VCL_Open(fn, ctx->msg);
	if (vcl == NULL) {
		AZ(VSB_finish(ctx->msg));
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "%s", VSB_data(ctx->msg));
		return;
	}

	vcl->loaded_name = strdup(name);
	XXXAN(vcl->loaded_name);
	VTAILQ_INIT(&vcl->director_list);
	VTAILQ_INIT(&vcl->ref_list);
	VTAILQ_INIT(&vcl->vfps);

	vcl->temp = VCL_TEMP_INIT;

	ctx->vcl = vcl;

	VSB_clear(ctx->msg);
	i = vcl_send_event(ctx, VCL_EVENT_LOAD);
	if (i || *ctx->handling != VCL_RET_OK) {
		vcl_cancel_load(ctx, cli, name, "initialization");
		return;
	}
	assert(*ctx->handling == VCL_RET_OK);
	VSB_clear(ctx->msg);
	i = vcl_set_state(ctx, state);
	if (i) {
		assert(*state == '1');
		vcl_cancel_load(ctx, cli, name, "warmup");
		return;
	}
	bprintf(vcl->state, "%s", state + 1);
	VCLI_Out(cli, "Loaded \"%s\" as \"%s\"", fn , name);
	VTAILQ_INSERT_TAIL(&vcl_head, vcl, list);
	Lck_Lock(&vcl_mtx);
	if (vcl_active == NULL)
		vcl_active = vcl;
	Lck_Unlock(&vcl_mtx);
	VSC_C_main->n_vcl++;
	VSC_C_main->n_vcl_avail++;
}

/*--------------------------------------------------------------------*/

void
VCL_Poll(void)
{
	struct vrt_ctx *ctx;
	struct vcl *vcl, *vcl2;

	ASSERT_CLI();
	ctx = vcl_get_ctx(0, 0);
	VTAILQ_FOREACH_SAFE(vcl, &vcl_head, list, vcl2) {
		if (vcl->temp == VCL_TEMP_BUSY ||
		    vcl->temp == VCL_TEMP_COOLING) {
			ctx->vcl = vcl;
			ctx->syntax = ctx->vcl->conf->syntax;
			ctx->method = 0;
			(void)vcl_set_state(ctx, "0");
		}
		if (vcl->discard && vcl->temp == VCL_TEMP_COLD) {
			AZ(vcl->busy);
			assert(vcl != vcl_active);
			assert(VTAILQ_EMPTY(&vcl->ref_list));
			VTAILQ_REMOVE(&vcl_head, vcl, list);
			ctx->method = VCL_MET_FINI;
			ctx->vcl = vcl;
			ctx->syntax = ctx->vcl->conf->syntax;
			AZ(vcl_send_event(ctx, VCL_EVENT_DISCARD));
			vcl_KillBackends(vcl);
			free(vcl->loaded_name);
			VCL_Close(&vcl);
			VSC_C_main->n_vcl--;
			VSC_C_main->n_vcl_discard--;
		}
	}
	vcl_rel_ctx(&ctx);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
vcl_cli_list(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;
	const char *flg;

	/* NB: Shall generate same output as mcf_vcl_list() */

	(void)av;
	(void)priv;
	ASSERT_CLI();
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl == vcl_active) {
			flg = "active";
		} else if (vcl->discard) {
			flg = "discarded";
		} else
			flg = "available";
		VCLI_Out(cli, "%-10s %5s/%-8s %6u %s",
		    flg, vcl->state, vcl->temp, vcl->busy, vcl->loaded_name);
		if (vcl->label != NULL) {
			VCLI_Out(cli, " -> %s", vcl->label->loaded_name);
			if (vcl->nrefs)
				VCLI_Out(cli, " (%d return(vcl)%s)",
				    vcl->nrefs, vcl->nrefs > 1 ? "'s" : "");
		} else if (vcl->nlabels > 0) {
			VCLI_Out(cli, " (%d label%s)",
			    vcl->nlabels, vcl->nlabels > 1 ? "s" : "");
		}
		VCLI_Out(cli, "\n");
	}
}

static void v_matchproto_(cli_func_t)
vcl_cli_load(struct cli *cli, const char * const *av, void *priv)
{
	struct vrt_ctx *ctx;

	AZ(priv);
	ASSERT_CLI();
	ctx = vcl_get_ctx(VCL_MET_INIT, 1);
	vcl_load(cli, ctx, av[2], av[3], av[4]);
	vcl_rel_ctx(&ctx);
}

static void v_matchproto_(cli_func_t)
vcl_cli_state(struct cli *cli, const char * const *av, void *priv)
{
	struct vrt_ctx *ctx;

	AZ(priv);
	ASSERT_CLI();
	AN(av[2]);
	AN(av[3]);
	ctx = vcl_get_ctx(0, 1);
	ctx->vcl = vcl_find(av[2]);
	AN(ctx->vcl);			// MGT ensures this
	if (vcl_set_state(ctx, av[3]) == 0) {
		bprintf(ctx->vcl->state, "%s", av[3] + 1);
	} else {
		AZ(VSB_finish(ctx->msg));
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "Failed <vcl.state %s %s>", ctx->vcl->loaded_name,
		    av[3] + 1);
		if (VSB_len(ctx->msg))
			VCLI_Out(cli, "\nMessage:\n\t%s", VSB_data(ctx->msg));
	}
	vcl_rel_ctx(&ctx);
}

static void v_matchproto_(cli_func_t)
vcl_cli_discard(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;

	ASSERT_CLI();
	(void)cli;
	AZ(priv);
	vcl = vcl_find(av[2]);
	AN(vcl);			// MGT ensures this
	Lck_Lock(&vcl_mtx);
	assert (vcl != vcl_active);	// MGT ensures this
	AZ(vcl->nlabels);		// MGT ensures this
	VSC_C_main->n_vcl_discard++;
	VSC_C_main->n_vcl_avail--;
	vcl->discard = 1;
	if (vcl->label != NULL) {
		AZ(strcmp(vcl->state, VCL_TEMP_LABEL));
		vcl->label->nlabels--;
		vcl->label= NULL;
	}
	Lck_Unlock(&vcl_mtx);

	if (!strcmp(vcl->state, VCL_TEMP_LABEL)) {
		VTAILQ_REMOVE(&vcl_head, vcl, list);
		free(vcl->loaded_name);
		AZ(errno=pthread_rwlock_destroy(&vcl->temp_rwl));
		FREE_OBJ(vcl);
	} else if (vcl->temp == VCL_TEMP_COLD) {
		VCL_Poll();
	}
}

static void v_matchproto_(cli_func_t)
vcl_cli_label(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *lbl;
	struct vcl *vcl;

	ASSERT_CLI();
	(void)cli;
	(void)priv;
	vcl = vcl_find(av[3]);
	AN(vcl);				// MGT ensures this
	lbl = vcl_find(av[2]);
	if (lbl == NULL) {
		ALLOC_OBJ(lbl, VCL_MAGIC);
		AN(lbl);
		bprintf(lbl->state, "%s", VCL_TEMP_LABEL);
		lbl->temp = VCL_TEMP_WARM;
		REPLACE(lbl->loaded_name, av[2]);
		AZ(errno=pthread_rwlock_init(&lbl->temp_rwl, NULL));
		VTAILQ_INSERT_TAIL(&vcl_head, lbl, list);
	}
	if (lbl->label != NULL)
		lbl->label->nlabels--;
	lbl->label = vcl;
	vcl->nlabels++;
}

static void v_matchproto_(cli_func_t)
vcl_cli_use(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;

	ASSERT_CLI();
	AN(cli);
	AZ(priv);
	vcl = vcl_find(av[2]);
	AN(vcl);				// MGT ensures this
	assert(vcl->temp == VCL_TEMP_WARM);	// MGT ensures this
	Lck_Lock(&vcl_mtx);
	vcl_active = vcl;
	Lck_Unlock(&vcl_mtx);
}

static void v_matchproto_(cli_func_t)
vcl_cli_show(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;
	int verbose = 0;
	int i;

	ASSERT_CLI();
	AZ(priv);
	if (!strcmp(av[2], "-v") && av[3] == NULL) {
		VCLI_Out(cli, "Too few parameters");
		VCLI_SetResult(cli, CLIS_TOOFEW);
		return;
	} else if (strcmp(av[2], "-v") && av[3] != NULL) {
		VCLI_Out(cli, "Unknown options '%s'", av[2]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	} else if (av[3] != NULL) {
		verbose = 1;
		vcl = vcl_find(av[3]);
	} else
		vcl = vcl_find(av[2]);

	if (vcl == NULL) {
		VCLI_Out(cli, "No VCL named '%s'",
		    av[3] == NULL ? av[2] : av[3]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);
	if (verbose) {
		for (i = 0; i < vcl->conf->nsrc; i++)
			VCLI_Out(cli, "// VCL.SHOW %d %zd %s\n%s\n",
			    i, strlen(vcl->conf->srcbody[i]),
			    vcl->conf->srcname[i],
			    vcl->conf->srcbody[i]);
	} else {
		VCLI_Out(cli, "%s", vcl->conf->srcbody[0]);
	}
}

/*--------------------------------------------------------------------*/

static struct cli_proto vcl_cmds[] = {
	{ CLICMD_VCL_LOAD,		"", vcl_cli_load },
	{ CLICMD_VCL_LIST,		"", vcl_cli_list },
	{ CLICMD_VCL_STATE,		"", vcl_cli_state },
	{ CLICMD_VCL_DISCARD,		"", vcl_cli_discard },
	{ CLICMD_VCL_USE,		"", vcl_cli_use },
	{ CLICMD_VCL_SHOW,		"", vcl_cli_show },
	{ CLICMD_VCL_LABEL,		"", vcl_cli_label },
	{ NULL }
};

void
VCL_Init(void)
{

	assert(cache_param->workspace_client > 0);
	WS_Init(&ws_cli, "cli", malloc(cache_param->workspace_client),
	    cache_param->workspace_client);
	ws_snapshot_cli = WS_Snapshot(&ws_cli);
	CLI_AddFuncs(vcl_cmds);
	Lck_New(&vcl_mtx, lck_vcl);
}
