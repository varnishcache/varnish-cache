/*-
 * Copyright (c) 2006-2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * ctype(3) like functions, according to RFC2616
 */

#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "vdef.h"

#include "vas.h"
#include "vct.h"

/* NB: VCT always operate in ASCII, don't replace 0x0d with \r etc. */

#define VCT_UPALPHA	(VCT_ALPHA | VCT_UPPER | VCT_BASE64)
#define VCT_LOALPHA	(VCT_ALPHA | VCT_LOWER | VCT_BASE64)

const uint16_t vct_typtab[256] = {
	[0x00]	=	VCT_CTL,
	[0x01]	=	VCT_CTL,
	[0x02]	=	VCT_CTL,
	[0x03]	=	VCT_CTL,
	[0x04]	=	VCT_CTL,
	[0x05]	=	VCT_CTL,
	[0x06]	=	VCT_CTL,
	[0x07]	=	VCT_CTL,
	[0x08]	=	VCT_CTL,
	[0x09]	=	VCT_CTL | VCT_OWS,
	[0x0a]	=	VCT_CTL | VCT_CRLF,
	[0x0b]	=	VCT_CTL | VCT_VT,
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
	[0x20]	=	VCT_OWS,
	[0x21]	=	VCT_TCHAR,
	[0x22]	=	VCT_SEPARATOR,
	[0x23]	=	VCT_TCHAR,
	[0x24]	=	VCT_TCHAR,
	[0x25]	=	VCT_TCHAR,
	[0x26]	=	VCT_TCHAR,
	[0x27]	=	VCT_TCHAR,
	[0x28]	=	VCT_SEPARATOR,
	[0x29]	=	VCT_SEPARATOR,
	[0x2a]	=	VCT_TCHAR,
	[0x2b]	=	VCT_TCHAR | VCT_BASE64,
	[0x2c]	=	VCT_SEPARATOR,
	[0x2d]	=	VCT_XMLNAME | VCT_TCHAR | VCT_ID,
	[0x2e]	=	VCT_XMLNAME | VCT_TCHAR,
	[0x2f]	=	VCT_SEPARATOR | VCT_BASE64,
	[0x30]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x31]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x32]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x33]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x34]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x35]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x36]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x37]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x38]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x39]	=	VCT_DIGIT | VCT_HEX | VCT_XMLNAME | VCT_BASE64,
	[0x3a]	=	VCT_SEPARATOR | VCT_XMLNAMESTART,
	[0x3b]	=	VCT_SEPARATOR,
	[0x3c]	=	VCT_SEPARATOR,
	[0x3d]	=	VCT_SEPARATOR | VCT_BASE64,
	[0x3e]	=	VCT_SEPARATOR,
	[0x3f]	=	VCT_SEPARATOR,
	[0x40]	=	VCT_SEPARATOR,
	[0x41]	=	VCT_UPALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x42]	=	VCT_UPALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x43]	=	VCT_UPALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x44]	=	VCT_UPALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x45]	=	VCT_UPALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x46]	=	VCT_UPALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x47]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x48]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x49]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x4a]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x4b]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x4c]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x4d]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x4e]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x4f]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x50]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x51]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x52]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x53]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x54]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x55]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x56]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x57]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x58]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x59]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x5a]	=	VCT_UPALPHA | VCT_XMLNAMESTART,
	[0x5b]	=	VCT_SEPARATOR,
	[0x5c]	=	VCT_SEPARATOR,
	[0x5d]	=	VCT_SEPARATOR,
	[0x5e]	=	VCT_TCHAR,
	[0x5f]	=	VCT_XMLNAMESTART | VCT_TCHAR | VCT_ID,
	[0x60]	=	VCT_TCHAR,
	[0x61]	=	VCT_LOALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x62]	=	VCT_LOALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x63]	=	VCT_LOALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x64]	=	VCT_LOALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x65]	=	VCT_LOALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x66]	=	VCT_LOALPHA | VCT_HEX | VCT_XMLNAMESTART,
	[0x67]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x68]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x69]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x6a]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x6b]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x6c]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x6d]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x6e]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x6f]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x70]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x71]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x72]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x73]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x74]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x75]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x76]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x77]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x78]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x79]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x7a]	=	VCT_LOALPHA | VCT_XMLNAMESTART,
	[0x7b]	=	VCT_SEPARATOR,
	[0x7c]	=	VCT_TCHAR,
	[0x7d]	=	VCT_SEPARATOR,
	[0x7e]	=	VCT_TCHAR,
	[0x7f]	=	VCT_CTL,
	[0xb7]	=	VCT_XMLNAME,
	[0xc0]	=	VCT_XMLNAMESTART,
	[0xc1]	=	VCT_XMLNAMESTART,
	[0xc2]	=	VCT_XMLNAMESTART,
	[0xc3]	=	VCT_XMLNAMESTART,
	[0xc4]	=	VCT_XMLNAMESTART,
	[0xc5]	=	VCT_XMLNAMESTART,
	[0xc6]	=	VCT_XMLNAMESTART,
	[0xc7]	=	VCT_XMLNAMESTART,
	[0xc8]	=	VCT_XMLNAMESTART,
	[0xc9]	=	VCT_XMLNAMESTART,
	[0xca]	=	VCT_XMLNAMESTART,
	[0xcb]	=	VCT_XMLNAMESTART,
	[0xcc]	=	VCT_XMLNAMESTART,
	[0xcd]	=	VCT_XMLNAMESTART,
	[0xce]	=	VCT_XMLNAMESTART,
	[0xcf]	=	VCT_XMLNAMESTART,
	[0xd0]	=	VCT_XMLNAMESTART,
	[0xd1]	=	VCT_XMLNAMESTART,
	[0xd2]	=	VCT_XMLNAMESTART,
	[0xd3]	=	VCT_XMLNAMESTART,
	[0xd4]	=	VCT_XMLNAMESTART,
	[0xd5]	=	VCT_XMLNAMESTART,
	[0xd6]	=	VCT_XMLNAMESTART,
	[0xd8]	=	VCT_XMLNAMESTART,
	[0xd9]	=	VCT_XMLNAMESTART,
	[0xda]	=	VCT_XMLNAMESTART,
	[0xdb]	=	VCT_XMLNAMESTART,
	[0xdc]	=	VCT_XMLNAMESTART,
	[0xdd]	=	VCT_XMLNAMESTART,
	[0xde]	=	VCT_XMLNAMESTART,
	[0xdf]	=	VCT_XMLNAMESTART,
	[0xe0]	=	VCT_XMLNAMESTART,
	[0xe1]	=	VCT_XMLNAMESTART,
	[0xe2]	=	VCT_XMLNAMESTART,
	[0xe3]	=	VCT_XMLNAMESTART,
	[0xe4]	=	VCT_XMLNAMESTART,
	[0xe5]	=	VCT_XMLNAMESTART,
	[0xe6]	=	VCT_XMLNAMESTART,
	[0xe7]	=	VCT_XMLNAMESTART,
	[0xe8]	=	VCT_XMLNAMESTART,
	[0xe9]	=	VCT_XMLNAMESTART,
	[0xea]	=	VCT_XMLNAMESTART,
	[0xeb]	=	VCT_XMLNAMESTART,
	[0xec]	=	VCT_XMLNAMESTART,
	[0xed]	=	VCT_XMLNAMESTART,
	[0xee]	=	VCT_XMLNAMESTART,
	[0xef]	=	VCT_XMLNAMESTART,
	[0xf0]	=	VCT_XMLNAMESTART,
	[0xf1]	=	VCT_XMLNAMESTART,
	[0xf2]	=	VCT_XMLNAMESTART,
	[0xf3]	=	VCT_XMLNAMESTART,
	[0xf4]	=	VCT_XMLNAMESTART,
	[0xf5]	=	VCT_XMLNAMESTART,
	[0xf6]	=	VCT_XMLNAMESTART,
	[0xf8]	=	VCT_XMLNAMESTART,
	[0xf9]	=	VCT_XMLNAMESTART,
	[0xfa]	=	VCT_XMLNAMESTART,
	[0xfb]	=	VCT_XMLNAMESTART,
	[0xfc]	=	VCT_XMLNAMESTART,
	[0xfd]	=	VCT_XMLNAMESTART,
	[0xfe]	=	VCT_XMLNAMESTART,
	[0xff]	=	VCT_XMLNAMESTART,
};

const uint8_t vct_lowertab[256] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,	// 0x00
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,	// 0x08
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,	// 0x10
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,	// 0x18
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,	// 0x20
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,	// 0x28
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,	// 0x30
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,	// 0x38
	0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,	// 0x40
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,	// 0x48
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,	// 0x50
	0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,	// 0x58
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,	// 0x60
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,	// 0x68
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,	// 0x70
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,	// 0x78
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,	// 0x80
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,	// 0x88
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,	// 0x90
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,	// 0x98
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,	// 0xa0
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,	// 0xa8
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,	// 0xb0
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,	// 0xb8
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,	// 0xc0
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,	// 0xc8
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,	// 0xd0
	0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,	// 0xd8
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,	// 0xe0
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,	// 0xe8
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,	// 0xf0
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,	// 0xf8
};

const char *
VCT_invalid_name(const char *b, const char *e)
{

	AN(b);
	if (e == NULL)
		e = strchr(b, '\0');
	assert(b < e);

	if (!vct_isident1(*b))
		return (b);

	for (; b < e; b++)
		if (!vct_isident(*b))
			return (b);

	return (NULL);
}

#ifdef TEST_DRIVER

#include <ctype.h>
#include <locale.h>

int
main(int argc, char **argv)
{
	int i;
	const char *p;

	(void)argc;
	(void)argv;

	AN(setlocale(LC_ALL, "C"));

	p = "";
	assert(vct_iscrlf(p, p) == 0);

	for (i = 0x20; i < 0x7f; i++)
		assert(vct_lowertab[i] == tolower(i));

	assert(vct_casecmp("AZaz", "azAZ") == 0);
	assert(vct_casecmp("AZaz", "azAY") > 0);
	assert(vct_casecmp("AZay", "azAZ") < 0);
	assert(vct_casecmp("AZaz_", "azAZ") > 0);
	assert(vct_casecmp("AZaz", "azAZ_") < 0);
	assert(vct_casecmp("", "") == 0);

	assert(vct_caselencmp("AZaz_", "azAZ", 4) == 0);
	assert(vct_caselencmp("AZaz", "azAZ_", 4) == 0);

	assert(vct_caselencmp("AZaz1", "azAZ1", 4) == 0);
	assert(vct_caselencmp("AZaz1", "azAY1", 4) > 0);
	assert(vct_caselencmp("AZay1", "azAZ1", 4) < 0);

	assert(vct_caselencmp("AZaz", "azAZ", 5) == 0);
	assert(vct_caselencmp("AZaz ", "azAY", 5) > 0);
	assert(vct_caselencmp("AZay ", "azAZ", 5) < 0);

	assert(vct_caselencmp("AZaz", "azAZ1", 5) < 0);
	assert(vct_caselencmp("AZaz1", "azAZ", 5) > 0);


	assert(vct_caselencmp("A", "B", 0) == 0);

	return (0);
}

#endif /* TEST_DRIVER */
