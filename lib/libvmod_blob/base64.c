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

#include "base64.h"

#include "vdef.h"
#include "vrt.h"
#include "vas.h"

#define base64_l(l)		(((l) << 2) / 3)

size_t
base64nopad_encode_l(size_t l)
{
	return base64_l(l) + 4;
}

size_t
base64_encode_l(size_t l)
{
	return (((base64_l(l)) + 3) & ~3) + 1;
}

size_t
base64_decode_l(size_t l)
{
	return ((l) * 3) >> 2;
}

static inline int
decode(char *restrict *restrict dest, const char *restrict const buf,
       const size_t buflen, unsigned u, const int n)
{
	char *d;

	if (n <= 1) {
		errno = EINVAL;
		return -1;
	}
	d = *dest;
	for (int i = 0; i < n - 1; i++) {
		if (d == buf + buflen) {
			errno = ENOMEM;
			return -1;
		}
		*d++ = (u >> 16) & 0xff;
		u <<= 8;
	}
	*dest += d - *dest;
	return 1;
}

ssize_t
base64_encode(const enum encoding enc, const enum case_e kase,
	      char *restrict const buf, const size_t buflen,
	      const char *restrict const inbuf, const size_t inlength)
{
	const struct b64_alphabet *alpha = &b64_alphabet[enc];
	char *p = buf;
	const uint8_t *in = (const uint8_t *)inbuf;
	const uint8_t * const end = in + inlength;

	(void) kase;
	AN(buf);
	AN(alpha);
	if (in == NULL || inlength == 0)
		return 0;

	if ((enc == BASE64URLNOPAD &&
	     buflen < base64nopad_encode_l(inlength)) ||
	    (enc != BASE64URLNOPAD &&
	     buflen < base64_encode_l(inlength))) {
		errno = ENOMEM;
		return -1;
	}

	while (end - in >= 3) {
		*p++ = alpha->b64[(in[0] >> 2) & 0x3f];
		*p++ = alpha->b64[((in[0] << 4) | (in[1] >> 4)) & 0x3f];
		*p++ = alpha->b64[((in[1] << 2) | (in[2] >> 6)) & 0x3f];
		*p++ = alpha->b64[in[2] & 0x3f];
		in += 3;
	}
	if (end - in > 0) {
		*p++ = alpha->b64[(in[0] >> 2) & 0x3f];
		if (end - in == 1) {
			*p++ = alpha->b64[(in[0] << 4) & 0x3f];
			if (alpha->padding) {
				*p++ = alpha->padding;
				*p++ = alpha->padding;
			}
		}
		else {
			*p++ = alpha->b64[((in[0] << 4) | (in[1] >> 4)) & 0x3f];
			*p++ = alpha->b64[(in[1] << 2) & 0x3f];
			if (alpha->padding) {
				*p++ = alpha->padding;
			}
		}
	}
	assert(p >= buf && p - buf <= buflen);
	return p - buf;
}

ssize_t
base64_decode(const enum encoding dec, char *restrict const buf,
	      const size_t buflen, ssize_t inlen,
	      const char *const p, va_list ap)
{
	const struct b64_alphabet *alpha = &b64_alphabet[dec];
	char *dest = buf;
	unsigned u = 0, term = 0;
	int n = 0;
	size_t len = SIZE_MAX;

	AN(buf);
	AN(alpha);

	if (inlen >= 0)
		len = inlen;

	for (const char *s = p; len > 0 && s != vrt_magic_string_end;
	     s = va_arg(ap, const char *)) {
		if (s == NULL)
			continue;
		if (*s && term) {
			errno = EINVAL;
			return -1;
		}
		while (*s && len) {
			while (n < 4) {
				char b = alpha->i64[(unsigned) *s++];
				u <<= 6;
				if (b == ILL) {
					errno = EINVAL;
					return -1;
				}
				n++;
				if (b == PAD) {
					term++;
					continue;
				}
				u |= (unsigned) b;
				if (--len == 0)
					break;
				if (!*s)
					break;
			}
			if (n == 4) {
				if (decode(&dest, buf, buflen, u, n-term) < 0)
					return -1;
				n = 0;
			}
		}
	}
	if (n) {
		if (!alpha->padding)
			u <<= (6 * (4 - n));
		if (decode(&dest, buf, buflen, u, n-term) < 0)
			return -1;
	}

	return dest - buf;
}
