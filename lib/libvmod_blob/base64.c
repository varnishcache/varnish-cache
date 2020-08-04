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

#include "venc.h"
#include "vsb.h"

static const struct b64_alphabet {
	const char b64[64];
	const int padding;
} b64_alphabet[] = {
	[BASE64] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789+/",
		'='
	},
	[BASE64URL] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789-_",
		'='
	},
	[BASE64URLNOPAD] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789-_",
		0
	},
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
	int i;
	struct vsb vsb;

	AN(buf);
	AN(alpha);
	AN(strings);

	if (buflen < 2) {
		errno = ENOMEM;
		return (-1);
	}
	AN(VSB_new(&vsb, buf, buflen + 1, 0));
	if (dec == BASE64) {
		i = VENC_Decode_Base64_Strands(&vsb, strings, inlen);
	} else if (dec == BASE64URL) {
		i = VENC_Decode_Base64URL_Strands(&vsb, strings, inlen, 1);
	} else if (dec == BASE64URLNOPAD) {
		i = VENC_Decode_Base64URL_Strands(&vsb, strings, inlen, 0);
	} else {
		WRONG("wrong turn in base64_decode");
	}
	if (i) {
		errno = EINVAL;
		return (-1);
	}
	if (VSB_finish(&vsb)) {
		errno = ENOMEM;
		return (-1);
	}
	return(VSB_len(&vsb));
}
