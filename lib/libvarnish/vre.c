/*-
 * Copyright (c) 2006-2009 Varnish Software AS
 * All rights reserved.
 *
 * Author: Tollef Fog Heen <tfheen@redpill-linpro.com>
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

#include <pcre.h>

#include "libvarnish.h"
#include "miniobj.h"
#include "vre.h"

struct vre {
	unsigned		magic;
#define VRE_MAGIC		0xe83097dc
	pcre *re;
};

vre_t *VRE_compile(const char *pattern, int options,
		    const char **errptr, int *erroffset) {
	vre_t *v;
	*errptr = NULL; *erroffset = 0;

	ALLOC_OBJ(v, VRE_MAGIC);
	AN(v);
	v->re = pcre_compile(pattern, options, errptr, erroffset, NULL);
	if (v->re == NULL) {
		VRE_free(&v);
		return NULL;
	}
	return v;
}

int VRE_exec(const vre_t *code, const char *subject, int length,
	     int startoffset, int options, int *ovector, int ovecsize) {
	CHECK_OBJ_NOTNULL(code, VRE_MAGIC);
	int ov[30];
	if (ovector == NULL) {
		ovector = ov;
		ovecsize = sizeof(ov)/sizeof(ov[0]);
	}

	return pcre_exec(code->re, NULL, subject, length,
			 startoffset, options, ovector, ovecsize);
}

void VRE_free(vre_t **vv) {

	vre_t *v = *vv;

	*vv = NULL;
	CHECK_OBJ(v, VRE_MAGIC);
	pcre_free(v->re);
	v->magic = 0;
	FREE_OBJ(v);
}
