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

#if USE_PCRE2_JIT
#  include <pthread.h>
#endif
#include <string.h>
#include <unistd.h>

#include <pcre2.h>

#include "vdef.h"

#include "vas.h"	// XXX Flexelint "not used" - but req'ed for assert()
#include "vsb.h"
#include "miniobj.h"

#include "vre.h"

#if !HAVE_PCRE2_SET_DEPTH_LIMIT
#  define pcre2_set_depth_limit(r, d) pcre2_set_recursion_limit(r, d)
#endif

struct vre {
	unsigned		magic;
#define VRE_MAGIC		0xe83097dc
	pcre2_code		*re;
	pcre2_match_context	*re_ctx;
};

/*
 * We don't want to spread or even expose the majority of PCRE2 options
 * and errors so we establish our own symbols and implement hard linkage
 * to PCRE2 here.
 */
const int VRE_ERROR_NOMATCH = PCRE2_ERROR_NOMATCH;

const unsigned VRE_CASELESS = PCRE2_CASELESS;

/*
 * Even though we only have one for each case so far, keep track of masks
 * to differentiate between compile and match options and enfore the hard
 * VRE linkage.
 */
#define VRE_MASK_COMPILE	PCRE2_CASELESS
#define VRE_MASK_MATCH		0

vre_t *
VRE_compile(const char *pattern, unsigned options,
    int *errptr, int *erroffset, unsigned jit)
{
	PCRE2_SIZE erroff;
	vre_t *v;

	AN(pattern);
	AZ(options & (~VRE_MASK_COMPILE));
	AN(errptr);
	AN(erroffset);

	*errptr = 0;
	*erroffset = -1;

	ALLOC_OBJ(v, VRE_MAGIC);
	if (v == NULL) {
		*errptr = PCRE2_ERROR_NOMEMORY;
		return (NULL);
	}
	v->re = pcre2_compile((PCRE2_SPTR8)pattern, PCRE2_ZERO_TERMINATED,
	    options, errptr, &erroff, NULL);
	*erroffset = erroff;
	if (v->re == NULL) {
		VRE_free(&v);
		return (NULL);
	}
	v->re_ctx = pcre2_match_context_create(NULL);
	if (v->re_ctx == NULL) {
		*errptr = PCRE2_ERROR_NOMEMORY;
		VRE_free(&v);
		return (NULL);
	}
#if USE_PCRE2_JIT
	if (jit)
		(void)pcre2_jit_compile(v->re, 0);
#else
	(void)jit;
#endif
	return (v);
}

int
VRE_error(int err, char *buf)
{
	int i;

	i = pcre2_get_error_message(err, (PCRE2_UCHAR *)buf, VRE_ERROR_LEN);
	return (i == PCRE2_ERROR_BADDATA ? -1 : 0);
}

static void
vre_limit(const vre_t *code, const volatile struct vre_limits *lim)
{

	CHECK_OBJ_NOTNULL(code, VRE_MAGIC);

	if (lim == NULL)
		return;

	/* XXX: not reentrant */
	pcre2_set_match_limit(code->re_ctx, lim->match);
	pcre2_set_depth_limit(code->re_ctx, lim->depth);
}

int
VRE_match(const vre_t *code, const char *subject, size_t length,
    int options, const volatile struct vre_limits *lim)
{
	pcre2_match_data *data;
	int matches;

	CHECK_OBJ_NOTNULL(code, VRE_MAGIC);
	AN(subject);
	AZ(options & (~VRE_MASK_MATCH));

	if (length == 0)
		length = PCRE2_ZERO_TERMINATED;

	vre_limit(code, lim);

	/* XXX: keep a dummy match_data around */
	data = pcre2_match_data_create_from_pattern(code->re, NULL);
	AN(data);

	matches =  pcre2_match(code->re, (PCRE2_SPTR)subject, length, 0,
	    options, data, NULL);

	pcre2_match_data_free(data);
	return (matches);
}

int
VRE_sub(const vre_t *code, const char *subject, const char *replacement,
    void *buf, size_t *buf_len, const volatile struct vre_limits *lim, int all)
{
	uint32_t options;
	int matches;

	CHECK_OBJ(code, VRE_MAGIC);
	AN(subject);
	AN(replacement);
	AN(buf);
	AN(buf_len);
	assert(*buf_len > 0);

	vre_limit(code, lim);

	options = PCRE2_SUBSTITUTE_EXTENDED|PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
	if (all)
		options |= PCRE2_SUBSTITUTE_GLOBAL;

	matches = pcre2_substitute(code->re,
	    (PCRE2_SPTR)subject, PCRE2_ZERO_TERMINATED, 0,
	    options, NULL, code->re_ctx,
	    (PCRE2_SPTR)replacement, PCRE2_ZERO_TERMINATED,
	    buf, buf_len);
	return (matches);
}

void
VRE_free(vre_t **vv)
{
	vre_t *v = *vv;

	*vv = NULL;
	CHECK_OBJ(v, VRE_MAGIC);
	if (v->re_ctx != NULL)
		pcre2_match_context_free(v->re_ctx);
	if (v->re != NULL)
		pcre2_code_free(v->re);
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
