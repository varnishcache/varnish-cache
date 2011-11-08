/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Varnish Software AS
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
 * Runtime support for compiled VCL programs, regexps
 */

#include "config.h"

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "vrt.h"
#include "vre.h"
#include "vcl.h"
#include "cache.h"

void
VRT_re_init(void **rep, const char *re)
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
VRT_re_fini(void *rep)
{
	vre_t *vv;

	vv = rep;
	if (rep != NULL)
		VRE_free(&vv);
}

int
VRT_re_match(const struct sess *sp, const char *s, void *re)
{
	vre_t *t;
	int i;

	if (s == NULL)
		s = "";
	AN(re);
	t = re;
	i = VRE_exec(t, s, strlen(s), 0, 0, NULL, 0, &params->vre_limits);
	if (i >= 0)
		return (1);
	if (i < VRE_ERROR_NOMATCH )
		WSP(sp, SLT_VCL_error, "Regexp matching returned %d", i);
	return (0);
}

const char *
VRT_regsub(const struct sess *sp, int all, const char *str, void *re,
    const char *sub)
{
	int ovector[30];
	vre_t *t;
	int i, l;
	txt res;
	char *b0;
	const char *s;
	unsigned u, x;
	int options = 0;
	size_t len;

	AN(re);
	if (str == NULL)
		str = "";
	t = re;
	memset(ovector, 0, sizeof(ovector));
	len = strlen(str);
	i = VRE_exec(t, str, len, 0, options, ovector, 30,
	    &params->vre_limits);

	/* If it didn't match, we can return the original string */
	if (i == VRE_ERROR_NOMATCH)
		return(str);
	if (i < VRE_ERROR_NOMATCH ) {
		WSP(sp, SLT_VCL_error, "Regexp matching returned %d", i);
		return(str);
	}

	u = WS_Reserve(sp->http->ws, 0);
	res.e = res.b = b0 = sp->http->ws->f;
	res.e += u;

	do {
		/* Copy prefix to match */
		Tadd(&res, str, ovector[0]);
		for (s = sub ; *s != '\0'; s++ ) {
			if (*s != '\\' || s[1] == '\0') {
				if (res.b < res.e)
					*res.b++ = *s;
				continue;
			}
			s++;
			if (isdigit(*s)) {
				x = *s - '0';
				l = ovector[2*x+1] - ovector[2*x];
				Tadd(&res, str + ovector[2*x], l);
				continue;
			} else {
				if (res.b < res.e)
					*res.b++ = *s;
			}
		}
		str += ovector[1];
		len -= ovector[1];
		if (!all)
			break;
		memset(&ovector, 0, sizeof(ovector));
		options |= VRE_NOTEMPTY_ATSTART;
		i = VRE_exec(t, str, len, 0, options, ovector, 30,
		    &params->vre_limits);
		if (i < VRE_ERROR_NOMATCH ) {
			WS_Release(sp->http->ws, 0);
			WSP(sp, SLT_VCL_error,
			    "Regexp matching returned %d", i);
			return(str);
		}
	} while (i != VRE_ERROR_NOMATCH);

	/* Copy suffix to match */
	Tadd(&res, str, len+1);
	if (res.b >= res.e) {
		WS_Release(sp->http->ws, 0);
		return (str);
	}
	Tcheck(res);
	WS_ReleaseP(sp->http->ws, res.b);
	return (b0);
}
