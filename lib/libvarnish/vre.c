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
	PCRE2_UCHAR		err_buf[128];
};

/*
 * We don't want to spread or even expose the majority of PCRE2 options
 * so we establish our own options and implement hard linkage to PCRE2
 * here.
 */
const unsigned VRE_CASELESS = PCRE2_CASELESS;
const unsigned VRE_NOTEMPTY = PCRE2_NOTEMPTY;

/*
 * Even though we only have one for each case so far, keep track of masks
 * to differentiate between compile and exec options and enfore the hard
 * VRE linkage.
 */
#define VRE_MASK_COMPILE	PCRE2_CASELESS
#define VRE_MASK_EXEC		PCRE2_NOTEMPTY

vre_t *
VRE_compile(const char *pattern, unsigned options,
    const char **errptr, int *erroffset)
{
	PCRE2_SIZE erroff;
	int errcode = 0;
	vre_t *v;
	*errptr = NULL; *erroffset = 0;

	ALLOC_OBJ(v, VRE_MAGIC);
	if (v == NULL) {
		*errptr = "Out of memory for VRE";
		return (NULL);
	}
	AZ(options & (~VRE_MASK_COMPILE));
	v->re = pcre2_compile((PCRE2_SPTR8)pattern, PCRE2_ZERO_TERMINATED,
	    options, &errcode, &erroff, NULL);
	if (errcode) {
		*erroffset = (int)erroff;
		(void)pcre2_get_error_message(errcode, v->err_buf,
		    sizeof(v->err_buf));
		*errptr = (char *)v->err_buf;
	}
	if (v->re == NULL) {
		VRE_free(&v);
		return (NULL);
	}
#if USE_PCRE2_JIT
	(void)pcre2_jit_compile(v->re, 0);
#endif
	v->re_ctx = pcre2_match_context_create(NULL);
	if (v->re_ctx == NULL) {
		*errptr = "Out of memory for pcre2_match_context";
		VRE_free(&v);
		return (NULL);
	}
	return (v);
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
VRE_exec(const vre_t *code, const char *subject, int length,
    int startoffset, int options, int *ovector, int ovecsize,
    const volatile struct vre_limits *lim)
{
	pcre2_match_data *data;
	PCRE2_SIZE *rov;
	int ov[30], rv, rsize;

	CHECK_OBJ_NOTNULL(code, VRE_MAGIC);

	if (ovector == NULL) {
		ovector = ov;
		ovecsize = sizeof(ov)/sizeof(ov[0]);
	}

	/* XXX: how to allocate on the stack? */
	data = pcre2_match_data_create(ovecsize, NULL);
	XXXAN(data);

	vre_limit(code, lim);

	AZ(options & (~VRE_MASK_EXEC));
	rv = pcre2_match(code->re, (PCRE2_SPTR)subject, length,
	    startoffset, options, data, NULL);

	rov = pcre2_get_ovector_pointer(data);
	rsize = pcre2_get_ovector_count(data);
	AN(rov);

	while (ovecsize > 0) {
		if (rsize > 0) {
			*ovector = (int)*rov;
			rov++;
			rsize--;
		} else {
			*ov = 0;
		}
		ovector++;
		ovecsize--;
	}

	pcre2_match_data_free(data);
	return (rv);
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
