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
 * $Id$
 */

/* from libvarnish/vct.c */

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

extern const uint16_t vct_typtab[256];

static inline int
vct_is(unsigned char x, unsigned char y)
{

	return (vct_typtab[x] & (y));
}

#define vct_issp(x) vct_is(x, VCT_SP)
#define vct_iscrlf(x) vct_is(x, VCT_CRLF)
#define vct_islws(x) vct_is(x, VCT_LWS)
#define vct_isctl(x) vct_is(x, VCT_CTL)
#define vct_isalpha(x) vct_is(x, VCT_ALPHA)
#define vct_issep(x) vct_is(x, VCT_SEPARATOR)
#define vct_issepctl(x) vct_is(x, VCT_SEPARATOR | VCT_CTL)
#define vct_isxmlnamestart(x) vct_is(x, VCT_XMLNAMES)
#define vct_isxmlname(x) vct_is(x, VCT_XMLNAMESTART | VCT_XMLNM)

/* NB: VCT always operate in ASCII, don't replace 0x0d with \r etc. */
#define vct_skipcrlf(p) (p[0] == 0x0d && p[1] == 0x0a ? 2 : 1)
