/*-
 * Copyright 2015-2016 UPLEX - Nils Goroll Systemoptimierung
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

static size_t v_matchproto_(blob_len_f)
id_encode_len(size_t l)
{
	return (l + 1);
}

static size_t v_matchproto_(blob_len_f)
id_decode_len(size_t l)
{
	return (l);
}

static ssize_t v_matchproto_(blob_encode_f)
id_encode(BLOB_CODEC, enum case_e kase, blob_dest_t dest, size_t destlen,
    blob_src_t src, size_t srclen)
{

	CHECK_BLOB_CODEC(codec, IDENTITY);
	(void)kase;
	AN(dest);

	if (destlen < srclen + 1)
		return (-1);
	if (src == NULL || srclen == 0)
		return (0);

	memcpy(dest, src, srclen);
	return (srclen);
}

static ssize_t v_matchproto_(blob_decode_f)
id_decode(BLOB_CODEC, blob_dest_t dest, size_t destlen, ssize_t slen,
    VCL_STRANDS strings)
{
	size_t len, outlen = 0, maxlen = SIZE_MAX;
	const char *s;
	int i;

	CHECK_BLOB_CODEC(codec, IDENTITY);
	AN(dest);
	AN(strings);

	if (slen >= 0)
		maxlen = (size_t)slen;

	for (i = 0; maxlen > 0 && i < strings->n; i++) {
		s = strings->p[i];
		if (s == NULL || *s == '\0')
			continue;
		len = strlen(s);
		if (len > maxlen)
			len = maxlen;
		if (len > destlen) {
			errno = ENOMEM;
			return (-1);
		}
		memcpy(dest, s, len);
		dest += len;
		destlen -= len;
		maxlen -= len;
		outlen += len;
	}

	assert(outlen <= (size_t)slen);
	return (outlen);
}

const struct blob_codec blob_codec_id = {
	.decode_len	= id_decode_len,
	.decode		= id_decode,
	.encode_len	= id_encode_len,
	.encode		= id_encode,
	.name		= &VENUM(IDENTITY),
	.case_sensitive = 1
};
