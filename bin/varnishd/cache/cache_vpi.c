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

#include "cache_varnishd.h"

#include "vcl.h"

#include "vcc_interface.h"

#include "cache_vcl.h"

/*--------------------------------------------------------------------
 * Private & exclusive interfaces between VCC and varnishd
 */

void
VPI_count(VRT_CTX, unsigned u)
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
		VSL(SLT_VCL_trace, 0, "%s %u %u.%u.%u",
		    ctx->vcl->loaded_name, u, ctx->vcl->conf->ref[u].source,
		    ctx->vcl->conf->ref[u].line, ctx->vcl->conf->ref[u].pos);
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
