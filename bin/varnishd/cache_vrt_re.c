/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "shmlog.h"
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
	if (rep != NULL)
		VRE_free((vre_t**)&rep);
}

int
VRT_re_match(const char *s, void *re)
{
	vre_t *t;
	int i;

	if (s == NULL)
		return (0);
	AN(re);
	t = re;
	i = VRE_exec(t, s, strlen(s), 0, 0, NULL, 0);
	if (i >= 0)
		return (1);
	assert(i == VRE_ERROR_NOMATCH);
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

	AN(re);
	if (str == NULL)
		return ("");
	t = re;
	memset(&ovector, 0, sizeof(ovector));
	i = VRE_exec(t, str, strlen(str), 0, 0, ovector, 30);

	/* If it didn't match, we can return the original string */
	if (i == VRE_ERROR_NOMATCH)
		return(str);

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
		if (!all)
			break;
		memset(&ovector, 0, sizeof(ovector));
		i = VRE_exec(t, str, strlen(str), 0, 0, ovector, 30);
	} while (i != VRE_ERROR_NOMATCH);

	/* Copy suffix to match */
	l = strlen(str) + 1;
	Tadd(&res, str, l);
	if (res.b >= res.e) {
		WS_Release(sp->http->ws, 0);
		return (str);
	}
	Tcheck(res);
	WS_ReleaseP(sp->http->ws, res.b);
	return (b0);
}
