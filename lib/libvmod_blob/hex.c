/*-
 * Copyright 2016 UPLEX - Nils Goroll Systemoptimierung
 * Copyright 2019 Varnish Software
 * All rights reserved.
 *
 * Authors: Nils Goroll <nils.goroll@uplex.de>
 *          Geoffrey Simmons <geoffrey.simmons@uplex.de>
 *          Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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

#include "vmod_blob.h"

const char hex_alphabet[][16] = {
	"0123456789abcdef",
	"0123456789ABCDEF"
};

/*
 * Shift the ASCII table over so that it begins at '0', and replace the
 * hex digits with their binary values. This fits all of the hex digits
 * into 55 bytes (cacheline friendly).
 */
const uint8_t hex_nibble[] = {
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9,
	ILL, ILL, ILL, ILL, ILL, ILL, ILL, 0xa, 0xb, 0xc,
	0xd, 0xe, 0xf, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
	ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
	ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, 0xa,
	0xb, 0xc, 0xd, 0xe, 0xf
};

static size_t v_matchproto_(blob_len_f)
hex_encode_len(size_t l)
{
	return ((l << 1) + 1);
}

static size_t v_matchproto_(blob_len_f)
hex_decode_len(size_t l)
{
	return ((l + 1) >> 1);
}

static inline char
hex2byte(unsigned char hi, unsigned char lo)
{
	return ((hex_nibble[hi - '0'] << 4) | hex_nibble[lo - '0']);
}

static ssize_t v_matchproto_(blob_encode_f)
hex_encode(BLOB_CODEC, blob_case_t kase, blob_dest_t dest, size_t destlen,
    blob_src_t src, size_t srclen)
{
	blob_dest_t p = dest;
	const char *alphabet;
	size_t i;

	CHECK_BLOB_CODEC(codec, HEX);
	AN(dest);
	AN(kase);

	if (src == NULL || srclen == 0)
		return (0);
	if (destlen < hex_encode_len(srclen))
		return (-1);

	alphabet = hex_alphabet[kase->upper];

	for (i = 0; i < srclen; i++) {
		*p++ = alphabet[(src[i] & 0xf0) >> 4];
		*p++ = alphabet[src[i] & 0x0f];
	}

	return (p - dest);
}

static ssize_t v_matchproto_(blob_decode_f)
hex_decode(BLOB_CODEC, blob_dest_t dest, size_t destlen, ssize_t slen,
    VCL_STRANDS strings)
{
	blob_dest_t p = dest;
	const char *b, *s;
	unsigned char extranib = '\0';
	size_t len = 0;
	int i;

	CHECK_BLOB_CODEC(codec, HEX);
	AN(dest);
	AN(strings);

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
	if (slen != -1 && len > slen)
		len = slen;

	if (hex_decode_len(len) > destlen) {
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
			*p++ = hex2byte(extranib, *s++);
			len -= 2;
		}
		while (len >= 2 && *s && *(s+1)) {
			*p++ = hex2byte(*s, *(s+1));
			s += 2;
			len -= 2;
		}
		extranib = *s;
	}

	/* XXX: no extra nible check after the last string? */

	assert(p <= dest + destlen);
	return (p - dest);
}

const struct blob_codec blob_codec_hex = {
	.decode_len	= hex_decode_len,
	.decode		= hex_decode,
	.encode_len	= hex_encode_len,
	.encode		= hex_encode,
	.name		= &VENUM(HEX)
};
