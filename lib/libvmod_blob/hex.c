/*-
 * Copyright 2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Nils Goroll <nils.goroll@uplex.de>
 *          Geoffrey Simmons <geoffrey.simmons@uplex.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"
#include <ctype.h>
#include <errno.h>

#include "vmod_blob.h"
#include "hex.h"

#include "vdef.h"
#include "vrt.h"
#include "vas.h"

const char hex_alphabet[][16] = {
	"0123456789abcdef",
	"0123456789ABCDEF"
};

/*
 * Shift the ASCII table over so that it begins at '0', and replace the
 * hex digits with their binary values. This fits all of the hex digits
 * into 55 bytes (cacheline friendly).
 */
const uint8_t nibble[] = {
	   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
	   ILL, ILL, ILL, ILL, ILL, ILL, ILL, 10,  11,  12,
	   13,  14,  15,  ILL, ILL, ILL, ILL, ILL, ILL, ILL,
	   ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
	   ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, 10,
	   11,  12,  13,  14,  15
};

size_t
hex_encode_l(size_t l)
{
	return ((l) << 1) + 1;
}

size_t
hex_decode_l(size_t l)
{
	return ((l) + 1) >> 1;
}

static inline char
hex2byte(const unsigned char hi, const unsigned char lo)
{
	return (nibble[hi - '0'] << 4) | nibble[lo - '0'];
}

ssize_t
hex_encode(const enum encoding enc, const enum case_e kase,
	   char *restrict const buf, const size_t buflen,
	   const char *restrict const in, const size_t inlen)
{
	char *p = buf;
	const char *alphabet = hex_alphabet[0];

	AN(buf);
	assert(enc == HEX);
	if (in == NULL || inlen == 0)
		return 0;
	if (buflen < hex_encode_l(inlen))
		return -1;

	if (kase == UPPER)
		alphabet = hex_alphabet[1];

	for (int i = 0; i < inlen; i++) {
		*p++ = alphabet[(in[i] & 0xf0) >> 4];
		*p++ = alphabet[in[i] & 0x0f];
	}

	return p - buf;
}

ssize_t
hex_decode(const enum encoding dec, char *restrict const buf,
	   const size_t buflen, ssize_t n,
	   const char *restrict const p, va_list ap)
{
	char *dest = buf;
	const char *b;
	unsigned char extranib = 0;
	ssize_t len = 0;
	va_list ap2;

	AN(buf);
	assert(dec == HEX);

	va_copy(ap2, ap);
	for (const char *s = p; s != vrt_magic_string_end;
	     s = va_arg(ap2, const char *)) {
		if (s == NULL)
			continue;
		b = s;
		while (*s) {
			if (!isxdigit(*s++)) {
				len = -1;
				break;
			}
		}
		if (len == -1)
			break;
		len += s - b;
	}
	va_end(ap2);

	if (len == 0)
		return 0;
	if (len == -1) {
		errno = EINVAL;
		return -1;
	}
	if (n != -1 && len > n)
		len = n;

	if ((len+1) >> 1 > buflen) {
		errno = ENOMEM;
		return -1;
	}
	if (len & 1) {
		extranib = '0';
		len++;
	}

	for (const char *s = p; len > 0 && s != vrt_magic_string_end;
	     s = va_arg(ap, const char *)) {
		if (s == NULL || *s == '\0')
			continue;
		if (extranib) {
			*dest++ = hex2byte(extranib, *s++);
			len -= 2;
		}
		while (len >= 2 && *s && *(s+1)) {
			*dest++ = hex2byte(*s, *(s+1));
			s += 2;
			len -= 2;
		}
		extranib = *s;
	}
	assert(dest <= buf + buflen);
	return dest - buf;
}
