/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * Initial implementation by Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * $Id$
 *
 * Runtime support for compiled VCL programs, regexps
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>

#include "shmlog.h"
#include "vrt.h"
#include "vsb.h"
#include "vcl.h"
#include "cache.h"

void
VRT_re_init(void **rep, const char *re)
{
	regex_t	*t;

	t = calloc(sizeof *t, 1);
	XXXAN(t);
	/* This was already check-compiled by the VCL compiler */
	AZ(regcomp(t, re, REG_EXTENDED | REG_NOSUB));
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
VRT_re_test(struct vsb *sb, const char *re)
{
	int i;
	regex_t	t;	
	char buf[BUFSIZ];

	memset(&t, 0, sizeof t);
	i = regcomp(&t, re, REG_EXTENDED | REG_NOSUB);
	if (i == 0) {
		regfree(&t);
		return (0);
	}
	(void)regerror(i, &t, buf, sizeof buf);
	vsb_printf(sb, "Regexp compilation error:\n\n%s\n\n", buf);
	regfree(&t);
	return (1);
}
