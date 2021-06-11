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

static void
Tadd(char **b, char *e, const char *p, int l)
{
	assert((*b) <= e);

	if (l <= 0) {
	} if ((*b) + l < e) {
		memcpy((*b), p, l);
		(*b) += l;
	} else {
		(*b) = e;
	}
}

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
	int ovector[30];
	int i, l;
	char *res_b;
	char *res_e;
	char *b0;
	const char *s;
	unsigned u, x;
	int options = 0;
	int offset = 0;
	size_t len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(re);
	if (str == NULL)
		str = "";
	if (sub == NULL)
		sub = "";
	memset(ovector, 0, sizeof(ovector));
	len = strlen(str);
	i = VRE_exec(re, str, len, 0, options, ovector, 30,
	    &cache_param->vre_limits);

	/* If it didn't match, we can return the original string */
	if (i == VRE_ERROR_NOMATCH)
		return (str);
	if (i < VRE_ERROR_NOMATCH ) {
		VRT_fail(ctx, "Regexp matching returned %d", i);
		return (str);
	}

	u = WS_ReserveAll(ctx->ws);
	res_e = res_b = b0 = WS_Reservation(ctx->ws);
	res_e += u;

	do {
		/* Copy prefix to match */
		Tadd(&res_b, res_e, str + offset, ovector[0] - offset);
		for (s = sub ; *s != '\0'; s++ ) {
			if (*s != '\\' || s[1] == '\0') {
				if (res_b < res_e)
					*res_b++ = *s;
				continue;
			}
			s++;
			if (isdigit(*s)) {
				x = *s - '0';
				l = ovector[2*x+1] - ovector[2*x];
				Tadd(&res_b, res_e, str + ovector[2*x], l);
				continue;
			} else {
				if (res_b < res_e)
					*res_b++ = *s;
			}
		}
		offset = ovector[1];
		if (!all)
			break;
		memset(ovector, 0, sizeof(ovector));
		options |= VRE_NOTEMPTY;
		i = VRE_exec(re, str, len, offset, options, ovector, 30,
		    &cache_param->vre_limits);
		if (i < VRE_ERROR_NOMATCH ) {
			WS_Release(ctx->ws, 0);
			VRT_fail(ctx, "Regexp matching returned %d", i);
			return (str);
		}
	} while (i != VRE_ERROR_NOMATCH);

	/* Copy suffix to match */
	Tadd(&res_b, res_e, str + offset, 1 + len - offset);
	if (res_b >= res_e) {
		WS_MarkOverflow(ctx->ws);
		WS_Release(ctx->ws, 0);
		return (str);
	}
	assert(res_b <= res_e);
	WS_ReleaseP(ctx->ws, res_b);
	return (b0);
}
