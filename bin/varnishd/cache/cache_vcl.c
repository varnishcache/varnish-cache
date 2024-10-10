/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 *
 */

#include "config.h"

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
#include "vte.h"
#include "vtim.h"
#include "vcc_interface.h"

const struct vcltemp VCL_TEMP_INIT[1] = {{ .name = "init", .is_cold = 1 }};
const struct vcltemp VCL_TEMP_COLD[1] = {{ .name = "cold", .is_cold = 1 }};
const struct vcltemp VCL_TEMP_WARM[1] = {{ .name = "warm", .is_warm = 1 }};
const struct vcltemp VCL_TEMP_BUSY[1] = {{ .name = "busy", .is_warm = 1 }};
const struct vcltemp VCL_TEMP_COOLING[1] = {{ .name = "cooling" }};

// not really a temperature
static const struct vcltemp VCL_TEMP_LABEL[1] = {{ .name = "label" }};

/*
 * XXX: Presently all modifications to this list happen from the
 * CLI event-engine, so no locking is necessary
 */
static VTAILQ_HEAD(, vcl)	vcl_head =
    VTAILQ_HEAD_INITIALIZER(vcl_head);

struct lock		vcl_mtx;
struct vcl		*vcl_active; /* protected by vcl_mtx */

static struct vrt_ctx ctx_cli;
static struct wrk_vpi wrk_vpi_cli;
static struct ws ws_cli;
static uintptr_t ws_snapshot_cli;
static struct vsl_log vsl_cli;

/*--------------------------------------------------------------------*/

void
VCL_Bo2Ctx(struct vrt_ctx *ctx, struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->wrk, WORKER_MAGIC);
	ctx->vcl = bo->vcl;
	ctx->syntax = ctx->vcl->conf->syntax;
	ctx->vsl = bo->vsl;
	ctx->http_bereq = bo->bereq;
	ctx->http_beresp = bo->beresp;
	ctx->bo = bo;
	ctx->sp = bo->sp;
	ctx->now = bo->t_prev;
	ctx->ws = bo->ws;
	ctx->vpi = bo->wrk->vpi;
	ctx->vpi->handling = 0;
	ctx->vpi->trace = bo->trace;
}

void
VCL_Req2Ctx(struct vrt_ctx *ctx, struct req *req)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->doclose, STREAM_CLOSE_MAGIC);

	ctx->vcl = req->vcl;
	ctx->syntax = ctx->vcl->conf->syntax;
	ctx->vsl = req->vsl;
	ctx->http_req = req->http;
	CHECK_OBJ_NOTNULL(req->top, REQTOP_MAGIC);
	ctx->http_req_top = req->top->topreq->http;
	ctx->http_resp = req->resp;
	ctx->req = req;
	ctx->sp = req->sp;
	ctx->now = req->t_prev;
	ctx->ws = req->ws;
	ctx->vpi = req->wrk->vpi;
	ctx->vpi->handling = 0;
	ctx->vpi->trace = req->trace;
}

/*--------------------------------------------------------------------*/

struct vrt_ctx *
VCL_Get_CliCtx(int msg)
{

	ASSERT_CLI();
	INIT_OBJ(&ctx_cli, VRT_CTX_MAGIC);
	INIT_OBJ(&wrk_vpi_cli, WRK_VPI_MAGIC);
	ctx_cli.vpi = &wrk_vpi_cli;
	wrk_vpi_cli.trace = FEATURE(FEATURE_TRACE);
	ctx_cli.now = VTIM_real();
	if (msg) {
		ctx_cli.msg = VSB_new_auto();
		AN(ctx_cli.msg);
	} else {
		ctx_cli.vsl = &vsl_cli;
	}
	ctx_cli.ws = &ws_cli;
	WS_Assert(ctx_cli.ws);
	return (&ctx_cli);
}

/*
 * releases CLI ctx
 *
 * returns finished error msg vsb if VCL_Get_CliCtx(1) was called
 *
 * caller needs to VSB_destroy a non-NULL return value
 *
 */
struct vsb *
VCL_Rel_CliCtx(struct vrt_ctx **ctx)
{
	struct vsb *r = NULL;

	ASSERT_CLI();
	assert(*ctx == &ctx_cli);
	AN((*ctx)->vpi);
	if (ctx_cli.msg) {
		TAKE_OBJ_NOTNULL(r, &ctx_cli.msg, VSB_MAGIC);
		AZ(VSB_finish(r));
	}
	if (ctx_cli.vsl)
		VSL_Flush(ctx_cli.vsl, 0);
	WS_Assert(ctx_cli.ws);
	WS_Rollback(&ws_cli, ws_snapshot_cli);
	INIT_OBJ(*ctx, VRT_CTX_MAGIC);
	*ctx = NULL;

	return (r);
}

/*--------------------------------------------------------------------*/

/* VRT_fail() can be called
 * - from the vcl sub via a vmod
 * - via a PRIV_TASK .fini callback
 *
 * if this happens during init, we fail it
 * if during fini, we ignore, because otherwise VMOD authors would be forced to
 * handle VCL_MET_FINI specifically everywhere.
 */

static int
vcl_event_handling(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (ctx->vpi->handling == 0)
		return (0);

	assert(ctx->vpi->handling == VCL_RET_FAIL);

	if (ctx->method == VCL_MET_INIT)
		return (1);

	/*
	 * EVENT_WARM / EVENT_COLD: method == 0
	 * must not set handling
	 */
	assert(ctx->method == VCL_MET_FINI);

	ctx->vpi->handling = 0;
	VRT_fail(ctx, "VRT_fail() from vcl_fini{} has no effect");
	return (0);
}

static int
vcl_send_event(struct vcl *vcl, enum vcl_event_e ev, struct vsb **msg)
{
	int r, havemsg;
	unsigned method = 0;
	struct vrt_ctx *ctx;

	ASSERT_CLI();
	ASSERT_VCL_ACTIVE();

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);
	AN(msg);
	AZ(*msg);

	switch (ev) {
	case VCL_EVENT_LOAD:
		method = VCL_MET_INIT;
		/* FALLTHROUGH */
	case VCL_EVENT_WARM:
		havemsg = 1;
		break;
	case VCL_EVENT_DISCARD:
		method = VCL_MET_FINI;
		/* FALLTHROUGH */
	case VCL_EVENT_COLD:
		havemsg = 0;
		break;
	default:
		WRONG("vcl_event");
	}

	ctx = VCL_Get_CliCtx(havemsg);

	AN(ctx->vpi);
	AZ(ctx->vpi->handling);
	AN(ctx->ws);

	ctx->vcl = vcl;
	ctx->syntax = ctx->vcl->conf->syntax;
	ctx->method = method;

	VCL_TaskEnter(cli_task_privs);
	r = ctx->vcl->conf->event_vcl(ctx, ev);
	VCL_TaskLeave(ctx, cli_task_privs);
	r |= vcl_event_handling(ctx);

	*msg = VCL_Rel_CliCtx(&ctx);

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

static void
vcl_panic_conf(struct vsb *vsb, const struct VCL_conf *conf)
{
	unsigned u;
	const struct vpi_ii *ii;

	if (PAN_dump_struct(vsb, conf, VCL_CONF_MAGIC, "conf"))
		return;
	VSB_printf(vsb, "syntax = \"%u\",\n", conf->syntax);
	VSB_cat(vsb, "srcname = {\n");
	VSB_indent(vsb, 2);
	for (u = 0; u < conf->nsrc; ++u)
		VSB_printf(vsb, "[%u] = \"%s\",\n", u, conf->srcname[u]);
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
	VSB_cat(vsb, "instances = {\n");
	VSB_indent(vsb, 2);
	ii = conf->instance_info;
	while (ii != NULL && ii->p != NULL) {
		VSB_printf(vsb, "\"%s\" = %p,\n", ii->name,
		    (const void *)*(const uintptr_t *)ii->p);
		ii++;
	}
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

void
VCL_Panic(struct vsb *vsb, const char *nm, const struct vcl *vcl)
{

	AN(vsb);
	if (PAN_dump_struct(vsb, vcl, VCL_MAGIC, "vcl[%s]", nm))
		return;
	VSB_printf(vsb, "name = \"%s\",\n", vcl->loaded_name);
	VSB_printf(vsb, "busy = %u,\n", vcl->busy);
	VSB_printf(vsb, "discard = %u,\n", vcl->discard);
	VSB_printf(vsb, "state = %s,\n", vcl->state);
	VSB_printf(vsb, "temp = %s,\n", vcl->temp ? vcl->temp->name : "(null)");
	vcl_panic_conf(vsb, vcl->conf);
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

void
VCL_Update(struct vcl **vcc, struct vcl *vcl)
{
	struct vcl *old;

	AN(vcc);

	old = *vcc;
	*vcc = NULL;

	CHECK_OBJ_ORNULL(old, VCL_MAGIC);
	ASSERT_VCL_ACTIVE();

	Lck_Lock(&vcl_mtx);
	if (old != NULL) {
		assert(old->busy > 0);
		old->busy--;
	}

	if (vcl == NULL)
		vcl = vcl_active; /* Sample vcl_active under lock to avoid
				   * race */
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	if (vcl->label == NULL) {
		AN(strcmp(vcl->state, VCL_TEMP_LABEL->name));
		*vcc = vcl;
	} else {
		AZ(strcmp(vcl->state, VCL_TEMP_LABEL->name));
		*vcc = vcl->label;
	}
	CHECK_OBJ_NOTNULL(*vcc, VCL_MAGIC);
	AZ((*vcc)->discard);
	(*vcc)->busy++;
	Lck_Unlock(&vcl_mtx);
	assert((*vcc)->temp->is_warm);
}

/*--------------------------------------------------------------------
 * vdire: Vcl DIrector REsignation Management (born out of a dire situation)
 * iterators over the director list need to register.
 * while iterating, directors can not retire immediately,
 * they get put on a list of resigning directors. The
 * last iterator executes the retirement.
 */

static struct vdire *
vdire_new(struct lock *mtx, const struct vcltemp **tempp)
{
	struct vdire *vdire;

	ALLOC_OBJ(vdire, VDIRE_MAGIC);
	AN(vdire);
	AN(mtx);
	VTAILQ_INIT(&vdire->directors);
	VTAILQ_INIT(&vdire->resigning);
	vdire->mtx = mtx;
	vdire->tempp = tempp;
	return (vdire);
}

/* starting an interation prevents removals from the directors list */
void
vdire_start_iter(struct vdire *vdire)
{

	CHECK_OBJ_NOTNULL(vdire, VDIRE_MAGIC);

	Lck_Lock(vdire->mtx);
	vdire->iterating++;
	Lck_Unlock(vdire->mtx);
}

void
vdire_end_iter(struct vdire *vdire)
{
	struct vcldir_head resigning = VTAILQ_HEAD_INITIALIZER(resigning);
	const struct vcltemp *temp = NULL;
	struct vcldir *vdir;
	unsigned n;

	CHECK_OBJ_NOTNULL(vdire, VDIRE_MAGIC);

	Lck_Lock(vdire->mtx);
	AN(vdire->iterating);
	n = --vdire->iterating;

	if (n == 0) {
		VTAILQ_SWAP(&vdire->resigning, &resigning, vcldir, resigning_list);
		VTAILQ_FOREACH(vdir, &resigning, resigning_list)
			VTAILQ_REMOVE(&vdire->directors, vdir, directors_list);
		temp = *vdire->tempp;
	}
	Lck_Unlock(vdire->mtx);

	VTAILQ_FOREACH(vdir, &resigning, resigning_list)
		vcldir_retire(vdir, temp);
}

// if there are no iterators, remove from directors and retire
// otherwise but on resigning list to work when iterators end
void
vdire_resign(struct vdire *vdire, struct vcldir *vdir)
{
	const struct vcltemp *temp = NULL;

	CHECK_OBJ_NOTNULL(vdire, VDIRE_MAGIC);
	AN(vdir);

	Lck_Lock(vdire->mtx);
	if (vdire->iterating != 0) {
		VTAILQ_INSERT_TAIL(&vdire->resigning, vdir, resigning_list);
		vdir = NULL;
	} else {
		VTAILQ_REMOVE(&vdire->directors, vdir, directors_list);
		temp = *vdire->tempp;
	}
	Lck_Unlock(vdire->mtx);

	if (vdir != NULL)
		vcldir_retire(vdir, temp);
}

// unlocked version of vcl_iterdir
// pat can also be NULL (to iterate all)
static int
vdire_iter(struct cli *cli, const char *pat, const struct vcl *vcl,
    vcl_be_func *func, void *priv)
{
	int i, found = 0;
	struct vcldir *vdir;
	struct vdire *vdire = vcl->vdire;

	vdire_start_iter(vdire);
	VTAILQ_FOREACH(vdir, &vdire->directors, directors_list) {
		if (pat != NULL && fnmatch(pat, vdir->cli_name, 0))
			continue;
		found++;
		i = func(cli, vdir->dir, priv);
		if (i < 0) {
			found = i;
			break;
		}
		found += i;
	}
	vdire_end_iter(vdire);
	return (found);
}


/*--------------------------------------------------------------------*/

// XXX locked case across VCLs - should do without
static int
vcl_iterdir(struct cli *cli, const char *pat, const struct vcl *vcl,
    vcl_be_func *func, void *priv)
{
	int i, found = 0;
	struct vcldir *vdir;

	Lck_AssertHeld(&vcl_mtx);
	VTAILQ_FOREACH(vdir, &vcl->vdire->directors, directors_list) {
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
	ASSERT_VCL_ACTIVE();
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
	if (vcl != NULL) {
		found = vdire_iter(cli, VSB_data(vsb), vcl, func, priv);
	} else {
		Lck_Lock(&vcl_mtx);
		VTAILQ_FOREACH(vcl, &vcl_head, list) {
			i = vcl_iterdir(cli, VSB_data(vsb), vcl, func, priv);
			if (i < 0) {
				found = i;
				break;
			} else {
				found += i;
			}
		}
		Lck_Unlock(&vcl_mtx);
	}
	VSB_destroy(&vsb);
	return (found);
}

static void
vcl_BackendEvent(const struct vcl *vcl, enum vcl_event_e e)
{
	struct vcldir *vdir;
	struct vdire *vdire;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	AZ(vcl->busy);
	vdire = vcl->vdire;

	vdire_start_iter(vdire);
	VTAILQ_FOREACH(vdir, &vdire->directors, directors_list)
		VDI_Event(vdir->dir, e);
	vdire_end_iter(vdire);
}

static void
vcl_KillBackends(const struct vcl *vcl)
{
	struct vcldir *vdir, *vdir2;
	struct vdire *vdire;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	assert(vcl->temp == VCL_TEMP_COLD || vcl->temp == VCL_TEMP_INIT);
	vdire = vcl->vdire;
	CHECK_OBJ_NOTNULL(vdire, VDIRE_MAGIC);

	/*
	 * Unlocked and sidelining vdire because no further directors can be added, and the
	 * remaining ones need to be able to remove themselves.
	 */
	VTAILQ_FOREACH_SAFE(vdir, &vdire->directors, directors_list, vdir2)
		VDI_Event(vdir->dir, VCL_EVENT_DISCARD);
	assert(VTAILQ_EMPTY(&vdire->directors));
}

/*--------------------------------------------------------------------*/

static struct vcl *
VCL_Open(const char *fn, struct vsb *msg)
{
	struct vcl *vcl;
	void *dlh;
	struct VCL_conf const *cnf;
	const char *dlerr;
	int err;

	AN(fn);
	AN(msg);

#ifdef RTLD_NOLOAD
	/* Detect bogus caching by dlopen(3) */
	dlh = dlopen(fn, RTLD_NOW | RTLD_NOLOAD);
	AZ(dlh);
#endif
	dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (dlh == NULL) {
		err = errno;
		dlerr = dlerror();
		VSB_cat(msg, "Could not load compiled VCL.\n");
		if (dlerr != NULL)
			VSB_printf(msg, "\tdlopen() = %s\n", dlerr);
		if (err) {
			VSB_printf(msg, "\terror = %s (%d)\n",
			    strerror(err), err);
		}
		VSB_cat(msg, "\thint: check for \"noexec\" mount\n");
		VSB_cat(msg, "\thint: check \"vmod_path\" parameter\n");
		return (NULL);
	}
	cnf = dlsym(dlh, "VCL_conf");
	if (cnf == NULL) {
		VSB_cat(msg, "Compiled VCL lacks metadata.\n");
		(void)dlclose(dlh);
		return (NULL);
	}
	if (cnf->magic != VCL_CONF_MAGIC) {
		VSB_cat(msg, "Compiled VCL has mangled metadata.\n");
		(void)dlclose(dlh);
		return (NULL);
	}
	if (cnf->syntax < heritage.min_vcl_version ||
	    cnf->syntax > heritage.max_vcl_version) {
		VSB_printf(msg, "Compiled VCL version (%.1f) not supported.\n",
		    .1 * cnf->syntax);
		(void)dlclose(dlh);
		return (NULL);
	}
	ALLOC_OBJ(vcl, VCL_MAGIC);
	AN(vcl);
	vcl->dlh = dlh;
	vcl->conf = cnf;
	vcl->vdire = vdire_new(&vcl_mtx, &vcl->temp);
	return (vcl);
}

static void
VCL_Close(struct vcl **vclp)
{
	struct vcl *vcl;

	TAKE_OBJ_NOTNULL(vcl, vclp, VCL_MAGIC);
	assert(VTAILQ_EMPTY(&vcl->filters));
	AZ(dlclose(vcl->dlh));
	FREE_OBJ(vcl->vdire);
	FREE_OBJ(vcl);
}

/*--------------------------------------------------------------------
 * NB: This function is called in/from the test-load subprocess.
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

static struct vsb *
vcl_print_refs(const struct vcl *vcl)
{
	struct vsb *msg;
	struct vclref *ref;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	msg = VSB_new_auto();
	AN(msg);

	VSB_printf(msg, "VCL %s is waiting for:", vcl->loaded_name);
	Lck_Lock(&vcl_mtx);
	VTAILQ_FOREACH(ref, &vcl->ref_list, list)
		VSB_printf(msg, "\n\t- %s", ref->desc);
	Lck_Unlock(&vcl_mtx);
	AZ(VSB_finish(msg));
	return (msg);
}

static int
vcl_set_state(struct vcl *vcl, const char *state, struct vsb **msg)
{
	struct vsb *nomsg = NULL;
	int i = 0;

	ASSERT_CLI();

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	AN(state);
	AN(msg);
	AZ(*msg);

	AN(vcl->temp);

	switch (state[0]) {
	case '0':
		if (vcl->temp == VCL_TEMP_COLD)
			break;
		if (vcl->busy == 0 && vcl->temp->is_warm) {
			Lck_Lock(&vcl_mtx);
			vcl->temp = VTAILQ_EMPTY(&vcl->ref_list) ?
			    VCL_TEMP_COLD : VCL_TEMP_COOLING;
			Lck_Unlock(&vcl_mtx);
			vcl_BackendEvent(vcl, VCL_EVENT_COLD);
			AZ(vcl_send_event(vcl, VCL_EVENT_COLD, msg));
			AZ(*msg);
		}
		else if (vcl->busy)
			vcl->temp = VCL_TEMP_BUSY;
		else if (VTAILQ_EMPTY(&vcl->ref_list))
			vcl->temp = VCL_TEMP_COLD;
		else
			vcl->temp = VCL_TEMP_COOLING;
		break;
	case '1':
		if (vcl->temp == VCL_TEMP_WARM)
			break;
		/* The warm VCL hasn't seen a cold event yet */
		if (vcl->temp == VCL_TEMP_BUSY)
			vcl->temp = VCL_TEMP_WARM;
		/* The VCL must first reach a stable cold state */
		else if (vcl->temp == VCL_TEMP_COOLING) {
			*msg = vcl_print_refs(vcl);
			i = -1;
		}
		else {
			Lck_Lock(&vcl_mtx);
			vcl->temp = VCL_TEMP_WARM;
			Lck_Unlock(&vcl_mtx);
			i = vcl_send_event(vcl, VCL_EVENT_WARM, msg);
			if (i == 0) {
				vcl_BackendEvent(vcl, VCL_EVENT_WARM);
				break;
			}
			AZ(vcl_send_event(vcl, VCL_EVENT_COLD, &nomsg));
			AZ(nomsg);
			vcl->temp = VCL_TEMP_COLD;
		}
		break;
	default:
		WRONG("Wrong enum state");
	}
	if (i == 0 && state[1])
		bprintf(vcl->state, "%s", state + 1);

	return (i);
}

static void
vcl_cancel_load(struct vcl *vcl, struct cli *cli, struct vsb *msg,
    const char *name, const char *step)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);

	VCLI_SetResult(cli, CLIS_CANT);
	VCLI_Out(cli, "VCL \"%s\" Failed %s", name, step);
	if (VSB_len(msg))
		VCLI_Out(cli, "\nMessage:\n\t%s", VSB_data(msg));
	VSB_destroy(&msg);

	AZ(vcl_send_event(vcl, VCL_EVENT_DISCARD, &msg));
	AZ(msg);

	vcl_KillBackends(vcl);
	free(vcl->loaded_name);
	VCL_Close(&vcl);
}

static void
vcl_load(struct cli *cli,
    const char *name, const char *fn, const char *state)
{
	struct vcl *vcl;
	struct vsb *msg;

	ASSERT_CLI();
	ASSERT_VCL_ACTIVE();

	vcl = vcl_find(name);
	AZ(vcl);

	msg = VSB_new_auto();
	AN(msg);
	vcl = VCL_Open(fn, msg);
	AZ(VSB_finish(msg));
	if (vcl == NULL) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "%s", VSB_data(msg));
		VSB_destroy(&msg);
		return;
	}

	VSB_destroy(&msg);

	vcl->loaded_name = strdup(name);
	XXXAN(vcl->loaded_name);
	VTAILQ_INIT(&vcl->ref_list);
	VTAILQ_INIT(&vcl->filters);

	vcl->temp = VCL_TEMP_INIT;

	if (vcl_send_event(vcl, VCL_EVENT_LOAD, &msg)) {
		vcl_cancel_load(vcl, cli, msg, name, "initialization");
		return;
	}
	VSB_destroy(&msg);

	if (vcl_set_state(vcl, state, &msg)) {
		assert(*state == '1');
		vcl_cancel_load(vcl, cli, msg, name, "warmup");
		return;
	}
	if (msg)
		VSB_destroy(&msg);

	VCLI_Out(cli, "Loaded \"%s\" as \"%s\"", fn , name);
	VTAILQ_INSERT_TAIL(&vcl_head, vcl, list);
	VSC_C_main->n_vcl++;
	VSC_C_main->n_vcl_avail++;
}

/*--------------------------------------------------------------------*/

void
VCL_Poll(void)
{
	struct vsb *nomsg = NULL;
	struct vcl *vcl, *vcl2;

	ASSERT_CLI();
	ASSERT_VCL_ACTIVE();
	VTAILQ_FOREACH_SAFE(vcl, &vcl_head, list, vcl2) {
		if (vcl->temp == VCL_TEMP_BUSY ||
		    vcl->temp == VCL_TEMP_COOLING)
			AZ(vcl_set_state(vcl, "0", &nomsg));
		AZ(nomsg);
		if (vcl->discard && vcl->temp == VCL_TEMP_COLD) {
			AZ(vcl->busy);
			assert(vcl != vcl_active);
			assert(VTAILQ_EMPTY(&vcl->ref_list));
			VTAILQ_REMOVE(&vcl_head, vcl, list);
			AZ(vcl_send_event(vcl, VCL_EVENT_DISCARD, &nomsg));
			AZ(nomsg);
			vcl_KillBackends(vcl);
			free(vcl->loaded_name);
			VCL_Close(&vcl);
			VSC_C_main->n_vcl--;
			VSC_C_main->n_vcl_discard--;
		}
	}
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
vcl_cli_list(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;
	const char *flg;
	struct vte *vte;

	/* NB: Shall generate same output as mcf_vcl_list() */

	(void)av;
	(void)priv;
	ASSERT_CLI();
	ASSERT_VCL_ACTIVE();
	vte = VTE_new(7, 80);
	AN(vte);
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl == vcl_active) {
			flg = "active";
		} else if (vcl->discard) {
			flg = "discarded";
		} else
			flg = "available";
		VTE_printf(vte, "%s\t%s\t%s\t\v%u\t%s", flg, vcl->state,
		    vcl->temp->name, vcl->busy, vcl->loaded_name);
		if (vcl->label != NULL) {
			VTE_printf(vte, "\t->\t%s", vcl->label->loaded_name);
			if (vcl->nrefs)
				VTE_printf(vte, " (%d return(vcl)%s)",
				    vcl->nrefs, vcl->nrefs > 1 ? "'s" : "");
		} else if (vcl->nlabels > 0) {
			VTE_printf(vte, "\t<-\t(%d label%s)",
			    vcl->nlabels, vcl->nlabels > 1 ? "s" : "");
		}
		VTE_cat(vte, "\n");
	}
	AZ(VTE_finish(vte));
	AZ(VTE_format(vte, VCLI_VTE_format, cli));
	VTE_destroy(&vte);
}

static void v_matchproto_(cli_func_t)
vcl_cli_list_json(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;

	(void)priv;
	ASSERT_CLI();
	ASSERT_VCL_ACTIVE();
	VCLI_JSON_begin(cli, 2, av);
	VCLI_Out(cli, ",\n");
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		VCLI_Out(cli, "{\n");
		VSB_indent(cli->sb, 2);
		VCLI_Out(cli, "\"status\": ");
		if (vcl == vcl_active) {
			VCLI_Out(cli, "\"active\",\n");
		} else if (vcl->discard) {
			VCLI_Out(cli, "\"discarded\",\n");
		} else
			VCLI_Out(cli, "\"available\",\n");
		VCLI_Out(cli, "\"state\": \"%s\",\n", vcl->state);
		VCLI_Out(cli, "\"temperature\": \"%s\",\n", vcl->temp->name);
		VCLI_Out(cli, "\"busy\": %u,\n", vcl->busy);
		VCLI_Out(cli, "\"name\": \"%s\"", vcl->loaded_name);
		if (vcl->label != NULL) {
			VCLI_Out(cli, ",\n");
			VCLI_Out(cli, "\"label\": {\n");
			VSB_indent(cli->sb, 2);
				VCLI_Out(cli, "\"name\": \"%s\"",
					 vcl->label->loaded_name);
			if (vcl->nrefs)
				VCLI_Out(cli, ",\n\"refs\": %d", vcl->nrefs);
			VCLI_Out(cli, "\n");
			VCLI_Out(cli, "}");
			VSB_indent(cli->sb, -2);
		} else if (vcl->nlabels > 0) {
			VCLI_Out(cli, ",\n");
			VCLI_Out(cli, "\"labels\": %d", vcl->nlabels);
		}
		VSB_indent(cli->sb, -2);
		VCLI_Out(cli, "\n}");
		if (VTAILQ_NEXT(vcl, list) != NULL)
			VCLI_Out(cli, ",\n");
	}
	VCLI_JSON_end(cli);
}

static void v_matchproto_(cli_func_t)
vcl_cli_load(struct cli *cli, const char * const *av, void *priv)
{

	AZ(priv);
	ASSERT_CLI();
	// XXX move back code from vcl_load?
	vcl_load(cli, av[2], av[3], av[4]);
}

static void v_matchproto_(cli_func_t)
vcl_cli_state(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;
	struct vsb *msg = NULL;

	AZ(priv);
	ASSERT_CLI();
	ASSERT_VCL_ACTIVE();
	AN(av[2]);
	AN(av[3]);

	vcl = vcl_find(av[2]);
	AN(vcl);

	if (vcl_set_state(vcl, av[3], &msg)) {
		CHECK_OBJ_NOTNULL(msg, VSB_MAGIC);

		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "Failed <vcl.state %s %s>", vcl->loaded_name,
		    av[3] + 1);
		if (VSB_len(msg))
			VCLI_Out(cli, "\nMessage:\n\t%s", VSB_data(msg));
	}
	if (msg)
		VSB_destroy(&msg);
}

static void v_matchproto_(cli_func_t)
vcl_cli_discard(struct cli *cli, const char * const *av, void *priv)
{
	struct vcl *vcl;

	ASSERT_CLI();
	ASSERT_VCL_ACTIVE();
	(void)cli;
	AZ(priv);
	vcl = vcl_find(av[2]);
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);		// MGT ensures this
	Lck_Lock(&vcl_mtx);
	assert (vcl != vcl_active);	// MGT ensures this
	AZ(vcl->nlabels);		// MGT ensures this
	VSC_C_main->n_vcl_discard++;
	VSC_C_main->n_vcl_avail--;
	vcl->discard = 1;
	if (vcl->label != NULL) {
		AZ(strcmp(vcl->state, VCL_TEMP_LABEL->name));
		vcl->label->nlabels--;
		vcl->label= NULL;
	}
	Lck_Unlock(&vcl_mtx);

	if (!strcmp(vcl->state, VCL_TEMP_LABEL->name)) {
		VTAILQ_REMOVE(&vcl_head, vcl, list);
		free(vcl->loaded_name);
		AZ(vcl->vdire);
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
	ASSERT_VCL_ACTIVE();
	(void)cli;
	(void)priv;
	vcl = vcl_find(av[3]);
	AN(vcl);				// MGT ensures this
	lbl = vcl_find(av[2]);
	if (lbl == NULL) {
		ALLOC_OBJ(lbl, VCL_MAGIC);
		AN(lbl);
		bprintf(lbl->state, "%s", VCL_TEMP_LABEL->name);
		lbl->temp = VCL_TEMP_WARM;
		REPLACE(lbl->loaded_name, av[2]);
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
	ASSERT_VCL_ACTIVE();
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
	int i = 2;
	unsigned u;

	ASSERT_CLI();
	ASSERT_VCL_ACTIVE();
	AZ(priv);

	if (av[i] != NULL && !strcmp(av[i], "-v")) {
		verbose = 1;
		i++;
	}

	if (av[i] == NULL) {
		vcl = vcl_active;
		AN(vcl);
	} else {
		vcl = vcl_find(av[i]);
		i++;
	}

	if (av[i] != NULL) {
		VCLI_Out(cli, "Too many parameters: '%s'", av[i]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}

	if (vcl == NULL) {
		VCLI_Out(cli, "No VCL named '%s'", av[i - 1]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	if (vcl->label) {
		vcl = vcl->label;
		CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
		AZ(vcl->label);
	}
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);
	if (verbose) {
		for (u = 0; u < vcl->conf->nsrc; u++)
			VCLI_Out(cli, "// VCL.SHOW %u %zd %s\n%s\n",
			    u, strlen(vcl->conf->srcbody[u]),
			    vcl->conf->srcname[u],
			    vcl->conf->srcbody[u]);
	} else {
		VCLI_Out(cli, "%s", vcl->conf->srcbody[0]);
	}
}

/*--------------------------------------------------------------------*/

static struct cli_proto vcl_cmds[] = {
	{ CLICMD_VCL_LOAD,		"", vcl_cli_load },
	{ CLICMD_VCL_LIST,		"", vcl_cli_list, vcl_cli_list_json },
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
	VSL_Setup(&vsl_cli, NULL, 0);
}
