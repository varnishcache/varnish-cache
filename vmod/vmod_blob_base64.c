/*-
 * Copyright 2015-2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Nils Goroll <nils.goroll@uplex.de>
 *          Geoffrey Simmons <geoffrey.simmons@uplex.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "vdef.h"
#include "vrt.h"
#include "vas.h"

#include "vmod_blob.h"

static const struct b64_alphabet {
	const char b64[64];
	const int8_t i64[256];
	const int padding;
} b64_alphabet[] = {
	[BASE64] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789+/",
		{
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL,  62, ILL, ILL, ILL,  63, /* +, /    */
			 52,  53,  54,  55,  56,  57,  58,  59, /* 0 - 7   */
			 60,  61, ILL, ILL, ILL, PAD, ILL, ILL, /* 8, 9, = */
			ILL,   0,   1,   2,   3,   4,   5,   6, /* A - G   */
			  7,   8,   9,  10,  11,  12,  13,  14, /* H - O   */
			 15,  16,  17,  18,  19,  20,  21,  22, /* P - W   */
			 23,  24,  25, ILL, ILL, ILL, ILL, ILL, /* X, Y, Z */
			ILL,  26,  27,  28,  29,  30,  31,  32, /* a - g   */
			 33,  34,  35,  36,  37,  38,  39,  40, /* h - o   */
			 41,  42,  43,  44,  45,  46,  47,  48, /* p - w   */
			 49,  50,  51, ILL, ILL, ILL, ILL, ILL, /* x, y, z */
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
		},
		'='
	},
	[BASE64URL] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789-_",
		{
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL,  62, ILL, ILL, /* -       */
			 52,  53,  54,  55,  56,  57,  58,  59, /* 0 - 7   */
			 60,  61, ILL, ILL, ILL, PAD, ILL, ILL, /* 8, 9, = */
			ILL,   0,   1,   2,   3,   4,   5,   6, /* A - G   */
			  7,   8,   9,  10,  11,  12,  13,  14, /* H - O   */
			 15,  16,  17,  18,  19,  20,  21,  22, /* P - W   */
			 23,  24,  25, ILL, ILL, ILL, ILL,  63, /* X-Z, _  */
			ILL,  26,  27,  28,  29,  30,  31,  32, /* a - g   */
			 33,  34,  35,  36,  37,  38,  39,  40, /* h - o   */
			 41,  42,  43,  44,  45,  46,  47,  48, /* p - w   */
			 49,  50,  51, ILL, ILL, ILL, ILL, ILL, /* x, y, z */
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
		},
		'='
	},
	[BASE64URLNOPAD] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789-_",
		{
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL,  62, ILL, ILL, /* -       */
			 52,  53,  54,  55,  56,  57,  58,  59, /* 0 - 7   */
			 60,  61, ILL, ILL, ILL, ILL, ILL, ILL, /* 8, 9    */
			ILL,   0,   1,   2,   3,   4,   5,   6, /* A - G   */
			  7,   8,   9,  10,  11,  12,  13,  14, /* H - O   */
			 15,  16,  17,  18,  19,  20,  21,  22, /* P - W   */
			 23,  24,  25, ILL, ILL, ILL, ILL,  63, /* X-Z, _  */
			ILL,  26,  27,  28,  29,  30,  31,  32, /* a - g   */
			 33,  34,  35,  36,  37,  38,  39,  40, /* h - o   */
			 41,  42,  43,  44,  45,  46,  47,  48, /* p - w   */
			 49,  50,  51, ILL, ILL, ILL, ILL, ILL, /* x, y, z */
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
		},
		0
	},
	[BASE64CF] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789-~",
		{
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL,  62, ILL, ILL, /* -          */
			 52,  53,  54,  55,  56,  57,  58,  59, /* 0 - 7      */
			 60,  61, ILL, ILL, ILL, ILL, ILL, ILL, /* 8, 9       */
			ILL,   0,   1,   2,   3,   4,   5,   6, /* A - G      */
			  7,   8,   9,  10,  11,  12,  13,  14, /* H - O      */
			 15,  16,  17,  18,  19,  20,  21,  22, /* P - W      */
			 23,  24,  25, ILL, ILL, ILL, ILL, PAD, /* X, Y, Z, _ */
			ILL,  26,  27,  28,  29,  30,  31,  32, /* a - g      */
			 33,  34,  35,  36,  37,  38,  39,  40, /* h - o      */
			 41,  42,  43,  44,  45,  46,  47,  48, /* p - w      */
			 49,  50,  51, ILL, ILL, ILL,  63, ILL, /* x, y, z,  ~*/
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
		},
		'_'
	}
};
#define base64_l(l)		(((l) << 2) / 3)

size_t
base64nopad_encode_l(size_t l)
{
	return (base64_l(l) + 4);
}

size_t
base64_encode_l(size_t l)
{
	return ((((base64_l(l)) + 3) & ~3) + 1);
}

size_t
base64_decode_l(size_t l)
{
	return ((l * 3) >> 2);
}

static inline int
decode(char *restrict *restrict dest, blob_src_t buf,
    blob_len_t buflen, unsigned u, const int n)
{
	char *d;
	int i;

	if (n <= 1) {
		errno = EINVAL;
		return (-1);
	}
	d = *dest;
	for (i = 0; i < n - 1; i++) {
		if (d == buf + buflen) {
			errno = ENOMEM;
			return (-1);
		}
		*d++ = (u >> 16) & 0xff;
		u <<= 8;
	}
	*dest += d - *dest;
	return (1);
}

ssize_t
base64_encode(const enum encoding enc, const enum case_e kase,
    blob_dest_t buf, blob_len_t buflen,
    blob_src_t inbuf, blob_len_t inlength)
{
	const struct b64_alphabet *alpha = &b64_alphabet[enc];
	char *p = buf;
	const uint8_t *in = (const uint8_t *)inbuf;
	const uint8_t * const end = in + inlength;

	(void)kase;
	AN(buf);
	AN(alpha);
	if (in == NULL || inlength == 0)
		return (0);

	if ((enc == BASE64URLNOPAD &&
	    buflen < base64nopad_encode_l(inlength)) ||
	    (enc != BASE64URLNOPAD && buflen < base64_encode_l(inlength))) {
		errno = ENOMEM;
		return (-1);
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
	assert(p >= buf);
	assert(p <= buf + buflen);
	return (p - buf);
}

ssize_t
base64_decode(const enum encoding dec, blob_dest_t buf,
    blob_len_t buflen, ssize_t inlen, VCL_STRANDS strings)
{
	const struct b64_alphabet *alpha = &b64_alphabet[dec];
	const char *s;
	char *dest = buf;
	unsigned u = 0, term = 0;
	size_t len = SIZE_MAX;
	int n = 0, i;
	char b;

	AN(buf);
	AN(alpha);
	AN(strings);

	if (inlen >= 0)
		len = inlen;

	for (i = 0; len > 0 && i < strings->n; i++) {
		s = strings->p[i];

		if (s == NULL)
			continue;
		if (*s && term) {
			errno = EINVAL;
			return (-1);
		}
		while (*s && len) {
			b = alpha->i64[(uint8_t)*s];
			s++;
			len--;
			u <<= 6;
			if (b == ILL) {
				errno = EINVAL;
				return (-1);
			}
			n++;
			if (b == PAD) {
				term++;
				continue;
			}
			u |= (uint8_t)b;
			if (n == 4) {
				if (decode(&dest, buf, buflen, u, n-term) < 0)
					return (-1);
				n = 0;
			}
		}
	}
	if (n) {
		if (n - term != 0)
			u <<= (6 * (4 - n));
		if (decode(&dest, buf, buflen, u, n-term) < 0)
			return (-1);
	}

	return (dest - buf);
}
