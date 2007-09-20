/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 * $Id$
 *
 * Runtime support for compiled VCL programs, regexps
 */

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <regex.h>

#include "shmlog.h"
#include "vrt.h"
#include "vsb.h"
#include "vcl.h"
#include "cache.h"

void
VRT_re_init(void **rep, const char *re, int sub)
{
	regex_t	*t;

	t = calloc(sizeof *t, 1);
	XXXAN(t);
	/* This was already check-compiled by the VCL compiler */
	AZ(regcomp(t, re, REG_EXTENDED | REG_ICASE | (sub ? 0 : REG_NOSUB)));
	*rep = t;
}

void
VRT_re_fini(void *rep)
{

	if (rep != NULL)
		regfree(rep);
}

int
VRT_re_match(const char *s, void *re)
{
	regex_t	*t;
	int i;

	if (s == NULL)
		return (0);
	AN(re);
	t = re;
	i = regexec(t, s, 0, NULL, 0);
	if (i == 0)
		return (1);
	assert(i == REG_NOMATCH);
	return (0);
}

int
VRT_re_test(struct vsb *sb, const char *re, int sub)
{
	int i;
	regex_t	t;
	char buf[BUFSIZ];

	memset(&t, 0, sizeof t);
	i = regcomp(&t, re, REG_EXTENDED | (sub ? 0 : REG_NOSUB));
	if (i == 0) {
		regfree(&t);
		return (0);
	}
	(void)regerror(i, &t, buf, sizeof buf);
	vsb_printf(sb, "Regexp compilation error:\n\n%s\n\n", buf);
	regfree(&t);
	return (1);
}

const char *
VRT_regsub(struct sess *sp, const char *str, void *re, const char *sub)
{
	regmatch_t pm[10];
	regex_t *t;
	int i, l;
	char *b, *p, *e;
	unsigned u, x;

	AN(re);
	t = re;
	i = regexec(t, str, 10, pm, 0);

	/* If it didn't match, we can return the original string */
	if (i == REG_NOMATCH)
		return(str);

	u = WS_Reserve(sp->http->ws, 0);
	e = p = b = sp->http->ws->f;
	e += u;

	/* Copy prefix to match */
	if (pm[0].rm_so > 0) {
		if (p + pm[0].rm_so < e)
			memcpy(p, str, pm[0].rm_so);
		p += pm[0].rm_so;
	}

	for ( ; *sub != '\0'; sub++ ) {
		if (*sub == '&') {
			l = pm[0].rm_eo - pm[0].rm_so;
			if (l > 0) {
				if (p + l < e)
					memcpy(p, str + pm[0].rm_so, l);
				p += l;
			}
		} else if (*sub == '$' && isdigit(sub[1])) {
			x = sub[1] - '0';
			sub++;
			l = pm[x].rm_eo - pm[x].rm_so;
			if (l > 0) {
				if (p + l < e)
					memcpy(p, str + pm[x].rm_so, l);
				p += l;
			}
		} else {
			if (p + 1 < e)
				*p = *sub;
			p++;
		}
	}

	/* Copy suffix to match */
	l = strlen(str + pm[0].rm_eo);
	if (l > 0) {
		if (p + l < e)
			memcpy(p, str + pm[0].rm_eo, l);
		p += l;
	}
	if (p + 1 < e)
		*p++ = '\0';
	xxxassert(p <= e);
	if (p > e) {
		WS_Release(sp->http->ws, 0);
		return (str);
	} 
	WS_Release(sp->http->ws, p - b);
	return (b);
}
