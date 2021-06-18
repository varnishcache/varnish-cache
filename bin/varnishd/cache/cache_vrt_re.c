/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Runtime support for compiled VCL programs, regexps
 */

#include "config.h"

#include <ctype.h>

#include "cache_varnishd.h"
#include "vcc_interface.h"

void
VPI_re_init(vre_t **rep, const char *re)
{
	vre_t *t;
	int error, erroroffset;

	/* This was already check-compiled by the VCL compiler */
	t = VRE_compile(re, 0, &error, &erroroffset,
	    cache_param->pcre_jit_compilation);
	AN(t);
	*rep = t;
}

void
VPI_re_fini(vre_t *rep)
{
	vre_t *vv;

	vv = rep;
	if (rep != NULL)
		VRE_free(&vv);
}

VCL_BOOL
VRT_re_match(VRT_CTX, const char *s, VCL_REGEX re)
{
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (s == NULL)
		s = "";
	AN(re);
	i = VRE_match(re, s, 0, 0, &cache_param->vre_limits);
	if (i >= 0)
		return (1);
	if (i < VRE_ERROR_NOMATCH )
		VRT_fail(ctx, "Regexp matching returned %d", i);
	return (0);
}

VCL_STRING
VRT_regsub(VRT_CTX, int all, VCL_STRING str, VCL_REGEX re, VCL_STRING sub)
{
	struct vsb vsb[1];
	const char *res;
	uintptr_t snap;
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(re);
	if (str == NULL)
		str = "";
	if (sub == NULL)
		sub = "";

	snap = WS_Snapshot(ctx->ws);
	WS_VSB_new(vsb, ctx->ws);
	i = VRE_sub(re, str, sub, vsb, &cache_param->vre_limits, all);
	res = WS_VSB_finish(vsb, ctx->ws, NULL);

	if (i < VRE_ERROR_NOMATCH)
		VRT_fail(ctx, "regsub: Regexp matching returned %d", i);
	else if (res == NULL)
		VRT_fail(ctx, "regsub: Out of workspace");
	else if (i > 0)
		return (res);
	WS_Reset(ctx->ws, snap);
	return (str);
}
