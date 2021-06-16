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
	const char *error;
	int erroroffset;

	/* This was already check-compiled by the VCL compiler */
	t = VRE_compile(re, 0, &error, &erroroffset);
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
	i = VRE_exec(re, s, strlen(s), 0, 0, NULL, 0, &cache_param->vre_limits);
	if (i >= 0)
		return (1);
	if (i < VRE_ERROR_NOMATCH )
		VRT_fail(ctx, "Regexp matching returned %d", i);
	return (0);
}

VCL_STRING
VRT_regsub(VRT_CTX, int all, VCL_STRING str, VCL_REGEX re,
    VCL_STRING sub)
{
	char *res;
	size_t res_len;
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(re);
	if (str == NULL)
		str = "";
	if (sub == NULL)
		sub = "";

	res_len = WS_ReserveAll(ctx->ws);
	if (res_len == 0) {
		WS_MarkOverflow(ctx->ws);
		WS_Release(ctx->ws, 0);
		return (str);
	}

	res_len--; /* for the null character */
	res = WS_Reservation(ctx->ws);
	i = VRE_sub(re, str, sub, res, &res_len,
	    &cache_param->vre_limits, all);

	if (i > 0) {
		res[res_len] = '\0';
		WS_Release(ctx->ws, res_len + 1);
		return (res);
	}

	if (res_len > WS_ReservationSize(ctx->ws))
		WS_MarkOverflow(ctx->ws);
	WS_Release(ctx->ws, 0);

	if (i < VRE_ERROR_NOMATCH)
	    VRT_fail(ctx, "Regexp matching returned %d", i);
	return (str);
}
