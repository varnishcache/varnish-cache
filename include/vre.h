/*-
 * Copyright (c) 2009 Varnish Software AS
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
 *
 * Regular expression support
 *
 * We wrap PCRE2 in VRE to make to make it feasible to use something else
 * without hunting down stuff through out the Varnish source code.
 *
 */

#ifndef VRE_H_INCLUDED
#define VRE_H_INCLUDED

#define VRE_ERROR_LEN	128

struct vre;
struct vsb;

struct vre_limits {
	unsigned	match;
	unsigned	depth;
};

typedef struct vre vre_t;

/* This maps to PCRE2 error codes */
extern const int VRE_ERROR_NOMATCH;

/* And those to PCRE2 compilation options */
extern const unsigned VRE_CASELESS;

/* we enforce a hard limit on the number of groups in VRE_compile() */
#define VRE_MAX_CAPTURES	9

vre_t *VRE_compile(const char *, unsigned, int *, int *, unsigned);
vre_t *VRE_export(const vre_t *, size_t *);
int VRE_error(struct vsb *, int err);
int VRE_match(const vre_t *code, const char *subject, size_t length,
    int options, const volatile struct vre_limits *lim);
int VRE_capture(const vre_t *code, const char *subject, size_t length,
    int options, txt *groups, size_t count,
    const volatile struct vre_limits *lim);
int VRE_sub(const vre_t *code, const char *subject, const char *replacement,
    struct vsb *vsb, const volatile struct vre_limits *lim, int all);
void VRE_free(vre_t **);
void VRE_quote(struct vsb *, const char *);

#endif /* VRE_H_INCLUDED */
