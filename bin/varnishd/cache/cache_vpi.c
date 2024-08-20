/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2019 Varnish Software AS
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

#include "cache_varnishd.h"

#include "vcl.h"
#include "vbm.h"

#include "vcc_interface.h"

#include "cache_vcl.h"

/*--------------------------------------------------------------------
 * Private & exclusive interfaces between VCC and varnishd
 */

const size_t vpi_wrk_len = sizeof(struct wrk_vpi);

void
VPI_wrk_init(struct worker *wrk, void *p, size_t spc)
{
	struct wrk_vpi *vpi = p;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(vpi);
	assert(spc >= sizeof *vpi);
	INIT_OBJ(vpi, WRK_VPI_MAGIC);
	wrk->vpi = vpi;
}

void
VPI_trace(VRT_CTX, unsigned u)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl->conf, VCL_CONF_MAGIC);
	assert(u < ctx->vcl->conf->nref);
	if (ctx->vsl != NULL)
		VSLb(ctx->vsl, SLT_VCL_trace, "%s %u %u.%u.%u",
		    ctx->vcl->loaded_name, u, ctx->vcl->conf->ref[u].source,
		    ctx->vcl->conf->ref[u].line, ctx->vcl->conf->ref[u].pos);
	else
		VSL(SLT_VCL_trace, NO_VXID, "%s %u %u.%u.%u",
		    ctx->vcl->loaded_name, u, ctx->vcl->conf->ref[u].source,
		    ctx->vcl->conf->ref[u].line, ctx->vcl->conf->ref[u].pos);
}

static void
vpi_ref_panic(struct vsb *vsb, unsigned n, const struct vcl *vcl)
{
	const struct VCL_conf *conf = NULL;
	const struct vpi_ref *ref;
	const char *p, *src = NULL;
	const int lim = 40;
	const char *abbstr = "[...]";
	v_vla_(char, buf, lim + sizeof(abbstr));
	int w = 0;

	AN(vsb);

	if (vcl != NULL)
		conf = vcl->conf;
	if (conf != NULL && conf->magic != VCL_CONF_MAGIC)
		conf = NULL;

	if (conf == NULL) {
		VSB_printf(vsb, "ref = %u, nref = ?,\n", n);
		return;
	}
	if (n >= conf->nref) {
		VSB_printf(vsb, "ref = %u *invalid*, nref = %u\n",
		    n, conf->nref);
		return;
	}

	VSB_printf(vsb, "ref = %u,\n", n);

	ref = &conf->ref[n];
	if (PAN_dump_struct(vsb, ref, VPI_REF_MAGIC, "vpi_ref"))
		return;

	if (ref->source < conf->nsrc) {
		VSB_printf(vsb, "source = %u (\"%s\"),\n", ref->source,
		    conf->srcname[ref->source]);
		src = conf->srcbody[ref->source];
	} else {
		VSB_printf(vsb, "source = %u *invalid*,\n", ref->source);
	}

	if (src != NULL) {
		w = strlen(src);
		assert(w > 0);
		if (ref->offset >= (unsigned)w)
			src = NULL;
	}
	if (src != NULL) {
		src += ref->offset;
		p = strchr(src, '\n');
		if (p != NULL)
			w = p - src;
		else
			w -= ref->offset;
		if (w > lim) {
			w = snprintf(buf, sizeof buf, "%.*s%s",
			    lim, src, abbstr);
			src = buf;
		}
	}

	VSB_printf(vsb, "offset = %u,\n", ref->offset);
	VSB_printf(vsb, "line = %u,\n", ref->line);
	VSB_printf(vsb, "pos = %u,\n", ref->pos);
	if (src != NULL) {
		VSB_cat(vsb, "src = ");
		VSB_quote(vsb, src, w, VSB_QUOTE_CSTR);
		VSB_putc(vsb, '\n');
	} else {
		VSB_printf(vsb, "token = \"%s\"\n", ref->token);
	}
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");

}
void
VPI_Panic(struct vsb *vsb, const struct wrk_vpi *vpi, const struct vcl *vcl)
{
	const char *hand;

	AN(vsb);
	if (PAN_dump_struct(vsb, vpi, WRK_VPI_MAGIC, "vpi"))
		return;

	hand = VCL_Return_Name(vpi->handling);
	if (vpi->handling == 0)
		hand = "none";
	else if (hand == NULL)
		hand = "*invalid*";

	VSB_printf(vsb, "handling (VCL::return) = 0x%x (%s),\n",
	    vpi->handling, hand);

	vpi_ref_panic(vsb, vpi->ref, vcl);

	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*
 * After vcl_fini {} == VGC_function_vcl_fini() is called from VGC_Discard(),
 * handling must either be OK from VCL "return (ok)" or FAIL from VRT_fail().
 *
 * replace OK with 0 for _fini callbacks because that handling has meaning only
 * when returning from VCL subs
 */

void
VPI_vcl_fini(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(ctx->vpi);

	if (ctx->vpi->handling == VCL_RET_FAIL)
		return;
	assert(ctx->vpi->handling == VCL_RET_OK);
	ctx->vpi->handling = 0;
}

VCL_VCL
VPI_vcl_get(VRT_CTX, const char *name)
{
	VCL_VCL vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	vcl = vcl_find(name);
	AN(vcl);
	Lck_Lock(&vcl_mtx);
	vcl->nrefs++;
	Lck_Unlock(&vcl_mtx);
	return (vcl);
}

void
VPI_vcl_rel(VRT_CTX, VCL_VCL vcl)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(vcl);
	Lck_Lock(&vcl_mtx);
	vcl->nrefs--;
	Lck_Unlock(&vcl_mtx);
}

void
VPI_vcl_select(VRT_CTX, VCL_VCL vcl)
{
	struct req *req = ctx->req;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->top, REQTOP_MAGIC);

	if ((IS_TOPREQ(req) && req->top->vcl0 != NULL) || req->restarts > 0)
		return;		// Illegal, req-FSM will fail this later.

	if (! IS_TOPREQ(req))
		assert(req->vcl == req->top->vcl0);

	Req_Rollback(ctx);

	if (IS_TOPREQ(req)) {
		AN(req->top);
		AZ(req->top->vcl0);
		req->top->vcl0 = req->vcl;
		req->vcl = NULL;
	}
	VCL_Update(&req->vcl, vcl);
	VSLb(ctx->req->vsl, SLT_VCL_use, "%s via %s",
	    req->vcl->loaded_name, vcl->loaded_name);
}

void v_noreturn_
VPI_Fail(const char *func, const char *file, int line,
    const char *cond)
{
	VAS_Fail(func, file, line, cond, VAS_ASSERT);
}

enum vcl_func_fail_e
VPI_Call_Check(VRT_CTX, const struct VCL_conf *conf,
    unsigned methods, unsigned n)
{
	struct vbitmap *vbm;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);

	assert(conf == ctx->vcl->conf);

	vbm = ctx->called;
	AN(vbm);

	if ((methods & ctx->method) == 0)
		return (VSUB_E_METHOD);

	if (vbit_test(vbm, n))
		return (VSUB_E_RECURSE);

	return (VSUB_E_OK);
}

void
VPI_Call_Begin(VRT_CTX, unsigned n)
{
	struct vbitmap *vbm;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	vbm = ctx->called;
	AN(vbm);
	vbit_set(vbm, n);
}

void
VPI_Call_End(VRT_CTX, unsigned n)
{
	struct vbitmap *vbm;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	vbm = ctx->called;
	AN(vbm);
	vbit_clr(vbm, n);
}
