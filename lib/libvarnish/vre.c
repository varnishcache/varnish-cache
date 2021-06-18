/*-
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Tollef Fog Heen <tfheen@redpill-linpro.com>
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
 */

#include "config.h"

#include <pcre.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"

#include "vas.h"	// XXX Flexelint "not used" - but req'ed for assert()
#include "vsb.h"
#include "miniobj.h"

#include "vre.h"

#if defined(USE_PCRE_JIT)
#define VRE_STUDY_JIT_COMPILE PCRE_STUDY_JIT_COMPILE
#else
#define VRE_STUDY_JIT_COMPILE 0
#endif

const unsigned VRE_has_jit = VRE_STUDY_JIT_COMPILE;

#if PCRE_MAJOR < 8 || (PCRE_MAJOR == 8 && PCRE_MINOR < 20)
#  define pcre_free_study pcre_free
#endif

struct vre {
	unsigned		magic;
#define VRE_MAGIC		0xe83097dc
	pcre			*re;
	pcre_extra		*re_extra;
	int			my_extra;
};

/*
 * We don't want to spread or even expose the majority of PCRE options
 * so we establish our own options and implement hard linkage to PCRE
 * here.
 */
const unsigned VRE_CASELESS = PCRE_CASELESS;
const unsigned VRE_NOTEMPTY = PCRE_NOTEMPTY;

/*
 * Even though we only have one for each case so far, keep track of masks
 * to differentiate between compile and exec options and enfore the hard
 * VRE linkage.
 */
#define VRE_MASK_COMPILE	PCRE_CASELESS
#define VRE_MASK_EXEC		PCRE_NOTEMPTY

vre_t *
VRE_compile(const char *pattern, unsigned options,
    const char **errptr, int *erroffset)
{
	vre_t *v;
	*errptr = NULL; *erroffset = 0;

	ALLOC_OBJ(v, VRE_MAGIC);
	if (v == NULL) {
		*errptr = "Out of memory for VRE";
		return (NULL);
	}
	AZ(options & (~VRE_MASK_COMPILE));
	v->re = pcre_compile(pattern, options, errptr, erroffset, NULL);
	if (v->re == NULL) {
		VRE_free(&v);
		return (NULL);
	}
	v->re_extra = pcre_study(v->re, VRE_STUDY_JIT_COMPILE, errptr);
	if (*errptr != NULL) {
		VRE_free(&v);
		return (NULL);
	}
	if (v->re_extra == NULL) {
		/* allocate our own */
		v->re_extra = calloc(1, sizeof(pcre_extra));
		v->my_extra = 1;
		if (v->re_extra == NULL) {
			*errptr = "Out of memory for pcre_extra";
			VRE_free(&v);
			return (NULL);
		}
	}
	return (v);
}

int
VRE_exec(const vre_t *code, const char *subject, int length,
    int startoffset, int options, int *ovector, int ovecsize,
    const volatile struct vre_limits *lim)
{
	CHECK_OBJ_NOTNULL(code, VRE_MAGIC);
	int ov[30];

	if (ovector == NULL) {
		ovector = ov;
		ovecsize = sizeof(ov)/sizeof(ov[0]);
	}

	if (lim != NULL) {
		/* XXX: not reentrant */
		code->re_extra->match_limit = lim->match;
		code->re_extra->flags |= PCRE_EXTRA_MATCH_LIMIT;
		code->re_extra->match_limit_recursion = lim->match_recursion;
		code->re_extra->flags |= PCRE_EXTRA_MATCH_LIMIT_RECURSION;
	} else {
		code->re_extra->flags &= ~PCRE_EXTRA_MATCH_LIMIT;
		code->re_extra->flags &= ~PCRE_EXTRA_MATCH_LIMIT_RECURSION;
	}

	AZ(options & (~VRE_MASK_EXEC));
	return (pcre_exec(code->re, code->re_extra, subject, length,
	    startoffset, options, ovector, ovecsize));
}

int
VRE_sub(const vre_t *code, const char *subject, const char *replacement,
    struct vsb *vsb, const volatile struct vre_limits *lim, int all)
{
	int ovector[30];
	int i, l;
	const char *s;
	unsigned x;
	int options = 0;
	int offset = 0;
	size_t len;

	CHECK_OBJ_NOTNULL(code, VRE_MAGIC);
	CHECK_OBJ_NOTNULL(vsb, VSB_MAGIC);
	AN(subject);
	AN(replacement);

	memset(ovector, 0, sizeof(ovector));
	len = strlen(subject);
	i = VRE_exec(code, subject, len, 0, options, ovector, 30, lim);

	if (i <= VRE_ERROR_NOMATCH)
		return (i);

	do {
		/* Copy prefix to match */
		VSB_bcat(vsb, subject + offset, ovector[0] - offset);
		for (s = replacement; *s != '\0'; s++ ) {
			if (*s != '\\' || s[1] == '\0') {
				VSB_putc(vsb, *s);
				continue;
			}
			s++;
			if (isdigit(*s)) {
				x = *s - '0';
				l = ovector[2*x+1] - ovector[2*x];
				VSB_bcat(vsb, subject + ovector[2*x], l);
				continue;
			}
			VSB_putc(vsb, *s);
		}
		offset = ovector[1];
		if (!all)
			break;
		memset(ovector, 0, sizeof(ovector));
		options |= VRE_NOTEMPTY;
		i = VRE_exec(code, subject, len, offset, options, ovector, 30,
		    lim);
		if (i < VRE_ERROR_NOMATCH )
			return (i);
	} while (i != VRE_ERROR_NOMATCH);

	/* Copy suffix to match */
	VSB_bcat(vsb, subject + offset, 1 + len - offset);
	return (1);
}

void
VRE_free(vre_t **vv)
{
	vre_t *v = *vv;

	*vv = NULL;
	CHECK_OBJ(v, VRE_MAGIC);
	if (v->re_extra != NULL) {
		if (v->my_extra)
			free(v->re_extra);
		else
			pcre_free_study(v->re_extra);
	}
	if (v->re != NULL)
		pcre_free(v->re);
	FREE_OBJ(v);
}

void
VRE_quote(struct vsb *vsb, const char *src)
{
	const char *b, *e;

	CHECK_OBJ_NOTNULL(vsb, VSB_MAGIC);
	if (src == NULL)
		return;
	for (b = src; (e = strstr(b, "\\E")) != NULL; b = e + 2)
		VSB_printf(vsb, "\\Q%.*s\\\\EE", (int)(e - b), b);
	if (*b != '\0')
		VSB_printf(vsb, "\\Q%s\\E", b);
}
