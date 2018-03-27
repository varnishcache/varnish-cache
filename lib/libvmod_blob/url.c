/*-
 * Copyright 2015-2016 UPLEX - Nils Goroll Systemoptimierung
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
#include <errno.h>

#include "vmod_blob.h"
#include "hex.h"

#include "vdef.h"
#include "vrt.h"
#include "vas.h"

/* Decoder states */
enum state_e {
	NORMAL,
	PERCENT,  /* just read '%' */
	FIRSTNIB, /* just read the first nibble after '%' */
};

size_t
url_encode_l(size_t l)
{
	return (l * 3) + 1;
}

size_t
url_decode_l(size_t l)
{
	return l;
}

/*
 * Bitmap of unreserved characters according to RFC 3986 section 2.3
 * (locale-independent and cacheline friendly)
 */
static const uint8_t unreserved[] = {
	0x0,  0x0,  0x0,  0x0,  0x0,  0x60, 0xff, 0x3,
	0xfe, 0xff, 0xff, 0x87, 0xfe, 0xff, 0xff, 0x47,
	0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
	0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0
};

static inline int
isunreserved(const uint8_t c)
{
	return (unreserved[c >> 3] & (1 << (c & 7)));
}

static inline int
isoutofrange(const uint8_t c)
{
	return (c < '0' || c > 'f');
}

ssize_t
url_encode(const enum encoding enc, const enum case_e kase,
	   char *restrict const buf, const size_t buflen,
	   const char *restrict const in, const size_t inlen)
{
	char *p = buf;
	const char * const end = buf + buflen;
	const char *alphabet = hex_alphabet[0];

	AN(buf);
	assert(enc == URL);
	if (in == NULL || inlen == 0)
		return 0;

	if (kase == UPPER)
		alphabet = hex_alphabet[1];

	for (int i = 0; i < inlen; i++) {
		if (isunreserved(in[i])) {
			if (p == end)
				return -1;
			*p++ = in[i];
		}
		else {
			if (p + 3 > end)
				return -1;
			*p++ = '%';
			*p++ = alphabet[(in[i] & 0xf0) >> 4];
			*p++ = alphabet[in[i] & 0x0f];
		}
	}

	return p - buf;
}

ssize_t
url_decode(const enum encoding dec, char *restrict const buf,
	   const size_t buflen, ssize_t n, const char *restrict const p,
	   va_list ap)
{
	char *dest = buf;
	const char * const end = buf + buflen;
	size_t len = SIZE_MAX;
	uint8_t nib = 0;
	enum state_e state = NORMAL;

	AN(buf);
	assert(dec == URL);

	if (n >= 0 && (size_t)n < len)
		len = n;

	for (const char *s = p; len > 0 && s != vrt_magic_string_end;
	     s = va_arg(ap, const char *)) {
		if (s == NULL || *s == '\0')
			continue;
		while (*s && len) {
			uint8_t nib2;

			switch (state) {
			case NORMAL:
				if (*s == '%')
					state = PERCENT;
				else {
					if (dest == end) {
						errno = ENOMEM;
						return -1;
					}
					*dest++ = *s;
				}
				break;
			case PERCENT:
				if (isoutofrange(*s) ||
				    (nib = nibble[*s - '0']) == ILL) {
					errno = EINVAL;
					return -1;
				}
				state = FIRSTNIB;
				break;
			case FIRSTNIB:
				if (dest == end) {
					errno = ENOMEM;
					return -1;
				}
				if (isoutofrange(*s) ||
				    (nib2 = nibble[*s - '0']) == ILL) {
					errno = EINVAL;
					return -1;
				}
				*dest++ = (nib << 4) | nib2;
				nib = 0;
				state = NORMAL;
				break;
			default:
				WRONG("illegal URL decode state");
			}
			s++;
			len--;
		}
	}
	if (state != NORMAL) {
		errno = EINVAL;
		return -1;
	}
	assert(dest <= end);
	return dest - buf;
}
