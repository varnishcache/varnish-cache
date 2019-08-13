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
 */

/* from libvarnish/vct.c */

#include "vas.h"

#define VCT_SP			(1<<0)
#define VCT_CRLF		(1<<1)
#define VCT_LWS			(VCT_CRLF | VCT_SP)
#define VCT_CTL			(1<<2)
#define VCT_ALPHA		(1<<3)
#define VCT_SEPARATOR		(1<<4)
#define VCT_DIGIT		(1<<5)
#define VCT_HEX			(1<<6)
#define VCT_XMLNAMESTART	(1<<7)
#define VCT_XMLNAME		(1<<8)
#define VCT_TCHAR		(1<<9)
#define VCT_ID			(1<<10)
#define VCT_IDENT		(VCT_ALPHA | VCT_DIGIT | VCT_ID)
#define VCT_VAR			(1<<11)
#define VCT_VT			(1<<12)
#define VCT_SPACE		(VCT_LWS | VCT_VT)

extern const uint16_t vct_typtab[256];

const char *VCT_invalid_name(const char *b, const char *e);

static inline int
vct_is(int x, uint16_t y)
{

	x &= 0xff;
	return (vct_typtab[x] & (y));
}

#define vct_issp(x) vct_is(x, VCT_SP)
#define vct_ishex(x) vct_is(x, VCT_HEX)
#define vct_islws(x) vct_is(x, VCT_LWS)
#define vct_isctl(x) vct_is(x, VCT_CTL)
#define vct_isspace(x) vct_is(x, VCT_SPACE)
#define vct_isdigit(x) vct_is(x, VCT_DIGIT)
#define vct_isalpha(x) vct_is(x, VCT_ALPHA)
#define vct_isalnum(x) vct_is(x, VCT_ALPHA | VCT_DIGIT)
#define vct_issep(x) vct_is(x, VCT_SEPARATOR)
#define vct_issepctl(x) vct_is(x, VCT_SEPARATOR | VCT_CTL)
#define vct_isident1(x) vct_isalpha(x)
#define vct_isident(x) vct_is(x, VCT_IDENT)
#define vct_isvar(x) vct_is(x, VCT_IDENT | VCT_VAR)
#define vct_isxmlnamestart(x) vct_is(x, VCT_XMLNAMESTART)
#define vct_isxmlname(x) vct_is(x, VCT_XMLNAMESTART | VCT_XMLNAME)
#define vct_istchar(x) vct_is(x, VCT_ALPHA | VCT_DIGIT | VCT_TCHAR)

static inline int
vct_iscrlf(const char* p, const char* end)
{
	assert(p <= end);
	if (p == end)
		return (0);
	if ((p[0] == 0x0d && (p+1 < end) && p[1] == 0x0a)) // CR LF
		return (2);
	if (p[0] == 0x0a) // LF
		return (1);
	return (0);
}

/* NB: VCT always operate in ASCII, don't replace 0x0d with \r etc. */
static inline char*
vct_skipcrlf(char* p, const char* end)
{
	return (p + vct_iscrlf(p, end));
}
