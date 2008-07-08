/*-
 * Copyright (c) 2006-2008 Linpro AS
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
 * ctype(3) like functions, according to RFC2616
 */

#include <vct.h>

/* NB: VCT always operate in ASCII, don't replace 0x0d with \r etc. */

#define VCT_UPALPHA	VCT_ALPHA
#define VCT_LOALPHA	VCT_ALPHA

unsigned char vct_typtab[256] = {
	[0x00]	=	VCT_CTL,
	[0x01]	=	VCT_CTL,
	[0x02]	=	VCT_CTL,
	[0x03]	=	VCT_CTL,
	[0x04]	=	VCT_CTL,
	[0x05]	=	VCT_CTL,
	[0x06]	=	VCT_CTL,
	[0x07]	=	VCT_CTL,
	[0x08]	=	VCT_CTL,
	[0x09]	=	VCT_CTL | VCT_SP | VCT_SEPARATOR,
	[0x0a]	=	VCT_CTL | VCT_CRLF,
	[0x0b]	=	VCT_CTL,
	[0x0c]	=	VCT_CTL,
	[0x0d]	=	VCT_CTL | VCT_CRLF,
	[0x0e]	=	VCT_CTL,
	[0x0f]	=	VCT_CTL,
	[0x10]	=	VCT_CTL,
	[0x11]	=	VCT_CTL,
	[0x12]	=	VCT_CTL,
	[0x13]	=	VCT_CTL,
	[0x14]	=	VCT_CTL,
	[0x15]	=	VCT_CTL,
	[0x16]	=	VCT_CTL,
	[0x17]	=	VCT_CTL,
	[0x18]	=	VCT_CTL,
	[0x19]	=	VCT_CTL,
	[0x1a]	=	VCT_CTL,
	[0x1b]	=	VCT_CTL,
	[0x1c]	=	VCT_CTL,
	[0x1d]	=	VCT_CTL,
	[0x1e]	=	VCT_CTL,
	[0x1f]	=	VCT_CTL,
        [0x20]  =	VCT_SP | VCT_SEPARATOR,
        [0x22]  =	VCT_SEPARATOR,
        [0x28]  =	VCT_SEPARATOR,
        [0x29]  =	VCT_SEPARATOR,
        [0x2c]  =	VCT_SEPARATOR,
        [0x2f]  =	VCT_SEPARATOR,
	[0x30]	=	VCT_DIGIT | VCT_HEX,
	[0x31]	=	VCT_DIGIT | VCT_HEX,
	[0x32]	=	VCT_DIGIT | VCT_HEX,
	[0x33]	=	VCT_DIGIT | VCT_HEX,
	[0x34]	=	VCT_DIGIT | VCT_HEX,
	[0x35]	=	VCT_DIGIT | VCT_HEX,
	[0x36]	=	VCT_DIGIT | VCT_HEX,
	[0x37]	=	VCT_DIGIT | VCT_HEX,
	[0x38]	=	VCT_DIGIT | VCT_HEX,
	[0x39]	=	VCT_DIGIT | VCT_HEX,
        [0x3a]  =	VCT_SEPARATOR,
        [0x3b]  =	VCT_SEPARATOR,
        [0x3c]  =	VCT_SEPARATOR,
        [0x3d]  =	VCT_SEPARATOR,
        [0x3e]  =	VCT_SEPARATOR,
        [0x3f]  =	VCT_SEPARATOR,
        [0x40]  =	VCT_SEPARATOR,
	[0x41]	=	VCT_UPALPHA | VCT_HEX,
	[0x42]	=	VCT_UPALPHA | VCT_HEX,
	[0x43]	=	VCT_UPALPHA | VCT_HEX,
	[0x44]	=	VCT_UPALPHA | VCT_HEX,
	[0x45]	=	VCT_UPALPHA | VCT_HEX,
	[0x46]	=	VCT_UPALPHA | VCT_HEX,
	[0x47]	=	VCT_UPALPHA,
	[0x48]	=	VCT_UPALPHA,
	[0x49]	=	VCT_UPALPHA,
	[0x4a]	=	VCT_UPALPHA,
	[0x4b]	=	VCT_UPALPHA,
	[0x4c]	=	VCT_UPALPHA,
	[0x4d]	=	VCT_UPALPHA,
	[0x4e]	=	VCT_UPALPHA,
	[0x4f]	=	VCT_UPALPHA,
	[0x50]	=	VCT_UPALPHA,
	[0x51]	=	VCT_UPALPHA,
	[0x52]	=	VCT_UPALPHA,
	[0x53]	=	VCT_UPALPHA,
	[0x54]	=	VCT_UPALPHA,
	[0x55]	=	VCT_UPALPHA,
	[0x56]	=	VCT_UPALPHA,
	[0x57]	=	VCT_UPALPHA,
	[0x58]	=	VCT_UPALPHA,
	[0x59]	=	VCT_UPALPHA,
	[0x5a]	=	VCT_UPALPHA,
        [0x5b]  =	VCT_SEPARATOR,
        [0x5c]  =	VCT_SEPARATOR,
        [0x5d]  =	VCT_SEPARATOR,
	[0x61]	=	VCT_LOALPHA | VCT_HEX,
	[0x62]	=	VCT_LOALPHA | VCT_HEX,
	[0x63]	=	VCT_LOALPHA | VCT_HEX,
	[0x64]	=	VCT_LOALPHA | VCT_HEX,
	[0x65]	=	VCT_LOALPHA | VCT_HEX,
	[0x66]	=	VCT_LOALPHA | VCT_HEX,
	[0x67]	=	VCT_LOALPHA,
	[0x68]	=	VCT_LOALPHA,
	[0x69]	=	VCT_LOALPHA,
	[0x6a]	=	VCT_LOALPHA,
	[0x6b]	=	VCT_LOALPHA,
	[0x6c]	=	VCT_LOALPHA,
	[0x6d]	=	VCT_LOALPHA,
	[0x6e]	=	VCT_LOALPHA,
	[0x6f]	=	VCT_LOALPHA,
	[0x70]	=	VCT_LOALPHA,
	[0x71]	=	VCT_LOALPHA,
	[0x72]	=	VCT_LOALPHA,
	[0x73]	=	VCT_LOALPHA,
	[0x74]	=	VCT_LOALPHA,
	[0x75]	=	VCT_LOALPHA,
	[0x76]	=	VCT_LOALPHA,
	[0x77]	=	VCT_LOALPHA,
	[0x78]	=	VCT_LOALPHA,
	[0x79]	=	VCT_LOALPHA,
	[0x7a]	=	VCT_LOALPHA,
        [0x7b]  =	VCT_SEPARATOR,
        [0x7d]  =	VCT_SEPARATOR,
	[0x7f]	=	VCT_CTL,
};
