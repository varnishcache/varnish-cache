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

#include <stdio.h>
#include <stdlib.h>

#include "cache_varnishd.h"

#include "vcl.h"
#include "vtim.h"
#include "vbm.h"

#include "cache_director.h"
#include "cache_transport.h"
#include "cache_vcl.h"
#include "vcc_interface.h"

/*--------------------------------------------------------------------*/

const char *
VCL_Return_Name(unsigned r)
{

	switch (r) {
#define VCL_RET_MAC(l, U, B)	\
	case VCL_RET_##U:	\
		return(#l);
#include "tbl/vcl_returns.h"
	default:
		return (NULL);
	}
}

const char *
VCL_Method_Name(unsigned m)
{

	switch (m) {
#define VCL_MET_MAC(func, upper, typ, bitmap)	\
	case VCL_MET_##upper:			\
		return (#upper);
#include "tbl/vcl_returns.h"
	default:
		return (NULL);
	}
}

/*--------------------------------------------------------------------*/

void
VCL_Refresh(struct vcl **vcc)
{

	while (vcl_active == NULL)
		(void)usleep(100000);

	ASSERT_VCL_ACTIVE();
	if (*vcc == vcl_active)
		return;

	VCL_Update(vcc, NULL);
}

void
VCL_Recache(const struct worker *wrk, struct vcl **vclp)
{

	AN(wrk);
	AN(vclp);
	CHECK_OBJ_NOTNULL(*vclp, VCL_MAGIC);
	ASSERT_VCL_ACTIVE();

	if (*vclp != vcl_active || wrk->wpriv->vcl == vcl_active) {
		VCL_Rel(vclp);
		return;
	}
	if (wrk->wpriv->vcl != NULL)
		VCL_Rel(&wrk->wpriv->vcl);
	wrk->wpriv->vcl = *vclp;
	*vclp = NULL;
}

void
VCL_Ref(struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	assert(!vcl->temp->is_cold);
	Lck_Lock(&vcl_mtx);
	assert(vcl->busy > 0);
	vcl->busy++;
	Lck_Unlock(&vcl_mtx);
}

void
VCL_Rel(struct vcl **vcc)
{
	struct vcl *vcl;

	TAKE_OBJ_NOTNULL(vcl, vcc, VCL_MAGIC);
	Lck_Lock(&vcl_mtx);
	assert(vcl->busy > 0);
	vcl->busy--;
	/*
	 * We do not garbage collect discarded VCL's here, that happens
	 * in VCL_Poll() which is called from the CLI thread.
	 */
	Lck_Unlock(&vcl_mtx);
}

/*--------------------------------------------------------------------*/

static void
vcldir_free(struct vcldir *vdir)
{

	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);
	CHECK_OBJ_NOTNULL(vdir->dir, DIRECTOR_MAGIC);
	AZ(vdir->refcnt);
	Lck_Delete(&vdir->dlck);
	free(vdir->cli_name);
	FREE_OBJ(vdir->dir);
	FREE_OBJ(vdir);
}

static VCL_BACKEND
vcldir_surplus(struct vcldir *vdir)
{

	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);
	assert(vdir->refcnt == 1);
	vdir->refcnt = 0;
	vcldir_free(vdir);
	return (NULL);
}

VCL_BACKEND
VRT_AddDirector(VRT_CTX, const struct vdi_methods *m, void *priv,
    const char *fmt, ...)
{
	struct vsb *vsb;
	struct vcl *vcl;
	struct vcldir *vdir;
	const struct vcltemp *temp;
	va_list ap;
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(m, VDI_METHODS_MAGIC);
	AN(fmt);
	vcl = ctx->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);

	// opportunistic, re-checked again under lock
	if (vcl->temp == VCL_TEMP_COOLING && !DO_DEBUG(DBG_VTC_MODE))
		return (NULL);

	ALLOC_OBJ(vdir, VCLDIR_MAGIC);
	AN(vdir);
	ALLOC_OBJ(vdir->dir, DIRECTOR_MAGIC);
	AN(vdir->dir);
	vdir->dir->vdir = vdir;

	vdir->methods = m;
	vdir->dir->priv = priv;
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s.", VCL_Name(vcl));
	i = VSB_len(vsb);
	va_start(ap, fmt);
	VSB_vprintf(vsb, fmt, ap);
	va_end(ap);
	AZ(VSB_finish(vsb));
	REPLACE(vdir->cli_name, VSB_data(vsb));
	VSB_destroy(&vsb);
	vdir->dir->vcl_name = vdir->cli_name + i;

	vdir->vcl = vcl;
	vdir->admin_health = VDI_AH_AUTO;
	vdir->health_changed = VTIM_real();

	vdir->refcnt++;
	Lck_New(&vdir->dlck, lck_director);
	vdir->dir->mtx = &vdir->dlck;

	/* NB: at this point we look at the VCL temperature after getting
	 * through the trouble of creating the director even though it might
	 * not be legal to do so. Because we change the VCL temperature before
	 * sending COLD events we have to tolerate and undo attempts for the
	 * COOLING case.
	 *
	 * To avoid deadlocks during vcl_BackendEvent, we only wait for vcl_mtx
	 * if the vcl is busy (ref vcl_set_state())
	 */

	while (1) {
		temp = vcl->temp;
		if (temp == VCL_TEMP_COOLING)
			return (vcldir_surplus(vdir));
		if (vcl->busy == 0 && vcl->temp->is_warm) {
			if (! Lck_Trylock(&vcl_mtx))
				break;
			usleep(10 * 1000);
			continue;
		}
		Lck_Lock(&vcl_mtx);
		break;
	}
	Lck_AssertHeld(&vcl_mtx);
	temp = vcl->temp;
	if (temp != VCL_TEMP_COOLING)
		VTAILQ_INSERT_TAIL(&vcl->vdire->directors, vdir, directors_list);
	if (temp->is_warm)
		VDI_Event(vdir->dir, VCL_EVENT_WARM);
	Lck_Unlock(&vcl_mtx);

	if (temp == VCL_TEMP_COOLING)
		return (vcldir_surplus(vdir));

	if (!temp->is_warm && temp != VCL_TEMP_INIT)
		WRONG("Dynamic Backends can only be added to warm VCLs");

	return (vdir->dir);
}

void
VRT_StaticDirector(VCL_BACKEND b)
{
	struct vcldir *vdir;

	CHECK_OBJ_NOTNULL(b, DIRECTOR_MAGIC);
	vdir = b->vdir;
	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);
	assert(vdir->refcnt == 1);
	AZ(vdir->flags & VDIR_FLG_NOREFCNT);
	vdir->flags |= VDIR_FLG_NOREFCNT;
}

// vcldir is already removed from the directors list
// to be called only from vdire_*
void
vcldir_retire(struct vcldir *vdir, const struct vcltemp *temp)
{

	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);
	assert(vdir->refcnt == 0);
	AN(temp);

	if (temp->is_warm)
		VDI_Event(vdir->dir, VCL_EVENT_COLD);
	if (vdir->methods->destroy != NULL)
		vdir->methods->destroy(vdir->dir);
	vcldir_free(vdir);
}

static int
vcldir_deref(struct vcldir *vdir)
{
	int busy;

	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);
	AZ(vdir->flags & VDIR_FLG_NOREFCNT);

	Lck_Lock(&vdir->dlck);
	assert(vdir->refcnt > 0);
	busy = --vdir->refcnt;
	Lck_Unlock(&vdir->dlck);

	if (!busy)
		vdire_resign(vdir->vcl->vdire, vdir);
	return (busy);
}

void
VRT_DelDirector(VCL_BACKEND *dirp)
{
	VCL_BACKEND dir;
	struct vcldir *vdir;

	TAKE_OBJ_NOTNULL(dir, dirp, DIRECTOR_MAGIC);

	vdir = dir->vdir;
	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);

	if (vdir->methods->release != NULL)
		vdir->methods->release(vdir->dir);

	if (vdir->flags & VDIR_FLG_NOREFCNT) {
		vdir->flags &= ~VDIR_FLG_NOREFCNT;
		AZ(vcldir_deref(vdir));
	} else {
		(void) vcldir_deref(vdir);
	}
}

void
VRT_Assign_Backend(VCL_BACKEND *dst, VCL_BACKEND src)
{
	struct vcldir *vdir;

	AN(dst);
	CHECK_OBJ_ORNULL((*dst), DIRECTOR_MAGIC);
	CHECK_OBJ_ORNULL(src, DIRECTOR_MAGIC);
	if (*dst != NULL) {
		vdir = (*dst)->vdir;
		CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);
		if (!(vdir->flags & VDIR_FLG_NOREFCNT))
			(void)vcldir_deref(vdir);
	}
	if (src != NULL) {
		vdir = src->vdir;
		CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);
		if (!(vdir->flags & VDIR_FLG_NOREFCNT)) {
			Lck_Lock(&vdir->dlck);
			assert(vdir->refcnt > 0);
			vdir->refcnt++;
			Lck_Unlock(&vdir->dlck);
		}
	}
	*dst = src;
}

void
VRT_DisableDirector(VCL_BACKEND d)
{
	struct vcldir *vdir;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	vdir = d->vdir;
	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);

	vdir->admin_health = VDI_AH_DELETED;
	vdir->health_changed = VTIM_real();
}

VCL_BACKEND
VRT_LookupDirector(VRT_CTX, VCL_STRING name)
{
	struct vcl *vcl;
	struct vcldir *vdir;
	VCL_BACKEND dd, d = NULL;
	struct vdire *vdire;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(name);

	assert(ctx->method & VCL_MET_TASK_H);
	ASSERT_CLI();

	vcl = ctx->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);

	vdire = vcl->vdire;

	vdire_start_iter(vdire);
	VTAILQ_FOREACH(vdir, &vdire->directors, directors_list) {
		dd = vdir->dir;
		if (strcmp(dd->vcl_name, name))
			continue;
		d = dd;
		break;
	}
	vdire_end_iter(vdire);

	return (d);
}

/*--------------------------------------------------------------------*/

VCL_BACKEND
VCL_DefaultDirector(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);
	return (*vcl->conf->default_director);
}

const char *
VCL_Name(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	return (vcl->loaded_name);
}

VCL_PROBE
VCL_DefaultProbe(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);
	return (vcl->conf->default_probe);
}

/*--------------------------------------------------------------------*/

void
VRT_CTX_Assert(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (ctx->msg != NULL)
		CHECK_OBJ(ctx->msg, VSB_MAGIC);
	else
		AN(ctx->vsl);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	WS_Assert(ctx->ws);

	CHECK_OBJ_ORNULL(ctx->sp, SESS_MAGIC);

	CHECK_OBJ_ORNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(ctx->http_req, HTTP_MAGIC);
	CHECK_OBJ_ORNULL(ctx->http_req_top, HTTP_MAGIC);
	CHECK_OBJ_ORNULL(ctx->http_resp, HTTP_MAGIC);

	CHECK_OBJ_ORNULL(ctx->bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_ORNULL(ctx->http_bereq, HTTP_MAGIC);
	CHECK_OBJ_ORNULL(ctx->http_beresp, HTTP_MAGIC);
}

struct vclref *
VRT_VCL_Prevent_Cold(VRT_CTX, const char *desc)
{
	struct vclref* ref;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);

	ALLOC_OBJ(ref, VCLREF_MAGIC);
	AN(ref);
	ref->vcl = ctx->vcl;
	REPLACE(ref->desc, desc);

	VCL_Ref(ctx->vcl);

	Lck_Lock(&vcl_mtx);
	VTAILQ_INSERT_TAIL(&ctx->vcl->ref_list, ref, list);
	Lck_Unlock(&vcl_mtx);

	return (ref);
}

void
VRT_VCL_Allow_Cold(struct vclref **refp)
{
	struct vcl *vcl;
	struct vclref *ref;

	TAKE_OBJ_NOTNULL(ref, refp, VCLREF_MAGIC);
	vcl = ref->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);

	Lck_Lock(&vcl_mtx);
	assert(!VTAILQ_EMPTY(&vcl->ref_list));
	VTAILQ_REMOVE(&vcl->ref_list, ref, list);
	Lck_Unlock(&vcl_mtx);

	VCL_Rel(&vcl);

	REPLACE(ref->desc, NULL);
	FREE_OBJ(ref);
}

struct vclref *
VRT_VCL_Prevent_Discard(VRT_CTX, const char *desc)
{
	struct vcl *vcl;
	struct vclref* ref;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(desc);
	AN(*desc);

	vcl = ctx->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	assert(vcl->temp->is_warm);

	ALLOC_OBJ(ref, VCLREF_MAGIC);
	AN(ref);
	ref->vcl = vcl;
	REPLACE(ref->desc, desc);

	Lck_Lock(&vcl_mtx);
	VTAILQ_INSERT_TAIL(&vcl->ref_list, ref, list);
	vcl->nrefs++;
	Lck_Unlock(&vcl_mtx);

	return (ref);
}

void
VRT_VCL_Allow_Discard(struct vclref **refp)
{
	struct vcl *vcl;
	struct vclref *ref;

	TAKE_OBJ_NOTNULL(ref, refp, VCLREF_MAGIC);
	vcl = ref->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);

	/* NB: A VCL may be released by a VMOD at any time, but it must happen
	 * after a warmup and before the end of a cooldown. The release may or
	 * may not happen while the same thread holds the temperature lock, so
	 * instead we check that all references are gone in VCL_Nuke.
	 */

	Lck_Lock(&vcl_mtx);
	assert(!VTAILQ_EMPTY(&vcl->ref_list));
	VTAILQ_REMOVE(&vcl->ref_list, ref, list);
	vcl->nrefs--;
	/* No garbage collection here, for the same reasons as in VCL_Rel. */
	Lck_Unlock(&vcl_mtx);

	REPLACE(ref->desc, NULL);
	FREE_OBJ(ref);
}

/*--------------------------------------------------------------------
 */

static int
req_poll(struct worker *wrk, struct req *req)
{
	struct req *top;

	/* NB: Since a fail transition leads to vcl_synth, the request may be
	 * short-circuited twice.
	 */
	if (req->req_reset) {
		wrk->vpi->handling = VCL_RET_FAIL;
		return (-1);
	}

	top = req->top->topreq;
	CHECK_OBJ_NOTNULL(top, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(top->transport, TRANSPORT_MAGIC);

	if (!FEATURE(FEATURE_VCL_REQ_RESET))
		return (0);
	if (top->transport->poll == NULL)
		return (0);
	if (top->transport->poll(top) >= 0)
		return (0);

	VSLb_ts_req(req, "Reset", W_TIM_real(wrk));
	wrk->stats->req_reset++;
	wrk->vpi->handling = VCL_RET_FAIL;
	req->req_reset = 1;
	return (-1);
}

/*--------------------------------------------------------------------
 * Method functions to call into VCL programs.
 *
 * Either the request or busyobject must be specified, but not both.
 * The workspace argument is where random VCL stuff gets space from.
 */

static void
vcl_call_method(struct worker *wrk, struct req *req, struct busyobj *bo,
    void *specific, unsigned method, vcl_func_f *func, unsigned track_call)
{
	uintptr_t rws = 0, aws;
	struct vrt_ctx ctx;
	struct vbitmap *vbm;
	void *p;
	size_t sz;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	if (bo != NULL) {
		CHECK_OBJ(bo, BUSYOBJ_MAGIC);
		CHECK_OBJ_NOTNULL(bo->vcl, VCL_MAGIC);
		VCL_Bo2Ctx(&ctx, bo);
	}
	if (req != NULL) {
		if (bo != NULL)
			assert(method == VCL_MET_PIPE);
		CHECK_OBJ(req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);
		CHECK_OBJ_NOTNULL(req->vcl, VCL_MAGIC);
		CHECK_OBJ_NOTNULL(req->top, REQTOP_MAGIC);
		if (req_poll(wrk, req))
			return;
		VCL_Req2Ctx(&ctx, req);
	}
	assert(ctx.now != 0);
	ctx.specific = specific;
	ctx.method = method;
	if (track_call > 0) {
		rws = WS_Snapshot(wrk->aws);
		sz = VBITMAP_SZ(track_call);
		p = WS_Alloc(wrk->aws, sz);
		// No use to attempt graceful failure, all VCL calls will fail
		AN(p);
		vbm = vbit_init(p, sz);
		ctx.called = vbm;
	}
	aws = WS_Snapshot(wrk->aws);
	wrk->cur_method = method;
	wrk->seen_methods |= method;
	AN(ctx.vsl);
	VSLbs(ctx.vsl, SLT_VCL_call, TOSTRAND(VCL_Method_Name(method)));
	func(&ctx, VSUB_STATIC, NULL);
	VSLbs(ctx.vsl, SLT_VCL_return,
	    TOSTRAND(VCL_Return_Name(wrk->vpi->handling)));
	wrk->cur_method |= 1;		// Magic marker
	if (wrk->vpi->handling == VCL_RET_FAIL)
		wrk->stats->vcl_fail++;

	/*
	 * VCL/Vmods are not allowed to make permanent allocations from
	 * wrk->aws, but they can reserve and return from it.
	 */
	assert(aws == WS_Snapshot(wrk->aws));
	if (rws != 0)
		WS_Reset(wrk->aws, rws);
}

#define VCL_MET_MAC(func, upper, typ, bitmap)				\
void									\
VCL_##func##_method(struct vcl *vcl, struct worker *wrk,		\
     struct req *req, struct busyobj *bo, void *specific)		\
{									\
									\
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);				\
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);			\
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);				\
	vcl_call_method(wrk, req, bo, specific,				\
	    VCL_MET_ ## upper, vcl->conf->func##_func, vcl->conf->nsub);\
	AN((1U << wrk->vpi->handling) & bitmap);			\
}

#include "tbl/vcl_returns.h"

/*--------------------------------------------------------------------
 */

VCL_STRING
VRT_check_call(VRT_CTX, VCL_SUB sub)
{
	VCL_STRING err = NULL;
	enum vcl_func_fail_e fail;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(sub, VCL_SUB_MAGIC);

	AN(sub->func);
	sub->func(ctx, VSUB_CHECK, &fail);

	switch (fail) {
	case VSUB_E_OK:
		break;
	case VSUB_E_METHOD:
		err = WS_Printf(ctx->ws, "Dynamic call to \"sub %s{}\""
		    " not allowed from here", sub->name);
		if (err == NULL)
			err = "Dynamic call not allowed and workspace overflow";
		break;
	case VSUB_E_RECURSE:
		err = WS_Printf(ctx->ws, "Recursive dynamic call to"
		    " \"sub %s{}\"", sub->name);
		if (err == NULL)
			err = "Recursive dynamic call and workspace overflow";
		break;
	default:
		INCOMPL();
	}

	return (err);
}

VCL_VOID
VRT_call(VRT_CTX, VCL_SUB sub)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(sub, VCL_SUB_MAGIC);

	AZ(VRT_handled(ctx));
	AN(sub->func);
	sub->func(ctx, VSUB_DYNAMIC, NULL);
}
