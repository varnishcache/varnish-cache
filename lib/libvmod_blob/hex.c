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

#include "vdef.h"
#include "vrt.h"
#include "vas.h"

#include "vmod_blob.h"

#include "hex.h"

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

static size_t
hex_encode_l(size_t l)
{
	return (((l) << 1) + 1);
}

static size_t
hex_decode_l(size_t l)
{
	return (((l) + 1) >> 1);
}

static inline char
hex2byte(const unsigned char hi, const unsigned char lo)
{
	return ((nibble[hi - '0'] << 4) | nibble[lo - '0']);
}

static ssize_t
hex_encode(BLOB_CODEC, const enum case_e kase, char *restrict const buf,
    const size_t buflen, const char *restrict const in, const size_t inlen)
{
	char *p = buf;
	const char *alphabet = hex_alphabet[0];
	int i;

	AN(buf);
	AN(codec);
	CHECK_BLOB_CODEC(codec, HEX);
	if (in == NULL || inlen == 0)
		return (0);
	if (buflen < hex_encode_l(inlen))
		return (-1);

	if (kase == UPPER)
		alphabet = hex_alphabet[1];

	for (i = 0; i < inlen; i++) {
		*p++ = alphabet[(in[i] & 0xf0) >> 4];
		*p++ = alphabet[in[i] & 0x0f];
	}

	/* XXX: the length arithmetics here are a bit complicated for such
	 * a simple problem. It looks like the estimated encoded length
	 * makes room for a null character, which makes sense for string
	 * encoding. But null characters are set by the caller as documented.
	 *
	 * The result is a confusing length computation where:
	 *
	 *     hex_decode_l(hex_encode_l(len)) != len
	 *
	 * Needs a proper review of "half byte" decoding. Should it be
	 * supported in the first place?
	 */

	return (p - buf);
}

static ssize_t
hex_decode(BLOB_CODEC, char *restrict const buf, const size_t buflen, ssize_t n,
    VCL_STRANDS strings)
{
	char *dest = buf;
	const char *b, *s;
	unsigned char extranib = '\0';
	size_t len = 0;
	int i;

	AN(buf);
	AN(strings);
	CHECK_BLOB_CODEC(codec, HEX);

	for (i = 0; i < strings->n; i++) {
		s = strings->p[i];

		if (s == NULL)
			continue;
		b = s;
		while (*s) {
			if (!isxdigit(*s++)) {
				errno = EINVAL;
				return (-1);
			}
		}
		len += s - b;
	}

	if (len == 0)
		return (0);
	if (n != -1 && len > n)
		len = n;

	if (hex_decode_l(len) > buflen) {
		errno = ENOMEM;
		return (-1);
	}

	/* XXX: prepended extra nibble, not appended! */
	if (len & 1) {
		extranib = '0';
		len++;
	}

	for (i = 0; len > 0 && i < strings->n; i++) {
		s = strings->p[i];

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

	/* XXX: no extra nible check for the last string? */

	assert(dest <= buf + buflen);
	return (dest - buf);
}

const struct vmod_blob_codec blob_codec_hex = {
	.decode_l	= hex_decode_l,
	.decode		= hex_decode,
	.encode_l	= hex_encode_l,
	.encode		= hex_encode,
	.name		= &VENUM(HEX)
};
