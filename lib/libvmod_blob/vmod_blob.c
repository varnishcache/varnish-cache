/*-
 * Copyright 2015-2017 UPLEX - Nils Goroll Systemoptimierung
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

#define VMOD_BLOB_TYPE 0xfade4faa

static const struct blob_codec *codecs[] = {
	&blob_codec_id,
	&blob_codec_base64,
	&blob_codec_base64url,
	&blob_codec_base64urlnopad,
	&blob_codec_hex,
	&blob_codec_url
};

#define __MAX_ENCODING (sizeof (codecs) / sizeof (*codecs))

struct vmod_blob_blob {
	unsigned magic;
#define VMOD_BLOB_MAGIC 0xfade4fa9
	struct vrt_blob blob;
	void *freeptr;
	char *encoding[__MAX_ENCODING][2];
	pthread_mutex_t lock;
};

#define ERR(ctx, msg) \
	VRT_fail((ctx), "vmod blob error: " msg)

#define VERR(ctx, fmt, ...) \
	VRT_fail((ctx), "vmod blob error: " fmt, __VA_ARGS__)

#define ERRINVAL(ctx, enc) \
	VERR((ctx), "cannot decode, illegal encoding beginning with \"%s\"", \
	     (enc))

#define VERRNOMEM(ctx, fmt, ...) \
	VERR((ctx), fmt ", out of space", __VA_ARGS__)

#define ERRNOMEM(ctx, msg) \
	ERR((ctx), msg ", out of space")

static char empty[1] = { '\0' };

static const struct vrt_blob null_blob[1] = {{
#define VMOD_BLOB_NULL_TYPE 0xfade4fa0
	.type = VMOD_BLOB_NULL_TYPE,
	.len = 0,
	.blob = empty,
}};

static const struct blob_codec *
parse_encoding(VCL_ENUM e, int *idxp)
{
	int i;

	AN(e);

	for (i = 0; i < __MAX_ENCODING; i++) {
		if (*codecs[i]->name == e) {
			if (idxp != NULL)
				*idxp = i;
			return (codecs[i]);
		}
	}
	WRONG("illegal encoding enum");
}

static enum case_e
parse_case(VCL_ENUM e)
{
#define VMODENUM(n) if (e == VENUM(n)) return (n);
#include "tbl_case.h"
	WRONG("illegal case enum");
}


static inline size_t
decode_len(BLOB_CODEC, VCL_STRANDS s)
{
	size_t len = 0;
	int i;

	AN(codec);

	for (i = 0; i < s->n; i++)
		if (s->p[i] != NULL && *s->p[i] != '\0')
			len += strlen(s->p[i]);

	return (codec->decode_len(len));
}

static void
err_decode(VRT_CTX, const char *enc)
{
	switch (errno) {
	case EINVAL:
		ERRINVAL(ctx, enc);
		break;
	case ENOMEM:
		ERRNOMEM(ctx, "cannot decode");
		break;
	default:
		WRONG("invalid errno");
	}
}

/* Require case DEFAULT for all encodings besides HEX and URL. */

static inline int
check_enc_case(VRT_CTX, BLOB_CODEC, VCL_ENUM case_s, enum case_e kase)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(codec);

	if (codec->case_sensitive && kase != DEFAULT) {
		VERR(ctx, "case %s is illegal with encoding %s", case_s,
		    *codec->name);
		return (0);
	}
	return (1);
}

/* Objects */

VCL_VOID v_matchproto_(td_blob_blob__init)
vmod_blob__init(VRT_CTX, struct vmod_blob_blob **blobp, const char *vcl_name,
    VCL_ENUM decs, VCL_STRANDS strings)
{
	const struct blob_codec *dec;
	struct vmod_blob_blob *b;
	void *buf;
	ssize_t len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(blobp);
	AZ(*blobp);
	AN(vcl_name);
	AN(decs);
	AN(strings);

	dec = parse_encoding(decs, NULL);
	AN(dec);

	ALLOC_OBJ(b, VMOD_BLOB_MAGIC);
	AN(b);
	*blobp = b;
	AZ(pthread_mutex_init(&b->lock, NULL));

	b->blob.type = VMOD_BLOB_TYPE;

	len = decode_len(dec, strings);
	if (len == 0)
		return;

	assert(len > 0);

	buf = malloc(len);
	if (buf == NULL) {
		VERRNOMEM(ctx, "cannot create blob %s", vcl_name);
		return;
	}

	errno = 0;
	len = dec->decode(dec, buf, len, -1, strings);

	if (len == -1) {
		assert(errno == EINVAL);
		free(buf);
		VERR(ctx, "cannot create blob %s, illegal encoding beginning "
		    "with \"%s\"", vcl_name, strings->p[0]);
		return;
	}
	if (len == 0) {
		free(buf);
		memcpy(&b->blob, null_blob, sizeof b->blob);
		return;
	}
	b->blob.len = len;
	b->blob.blob = b->freeptr = buf;
}

VCL_BLOB v_matchproto_(td_blob_blob_get)
vmod_blob_get(VRT_CTX, struct vmod_blob_blob *b)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(b, VMOD_BLOB_MAGIC);
	return (&b->blob);
}

VCL_STRING v_matchproto_(td_blob_blob_encode)
vmod_blob_encode(VRT_CTX, struct vmod_blob_blob *b, VCL_ENUM encs,
    VCL_ENUM case_s)
{
	const struct blob_codec *enc;
	enum case_e kase = parse_case(case_s);
	ssize_t len;
	int idx = -1;
	char *s;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(b, VMOD_BLOB_MAGIC);
	AN(encs);
	AN(case_s);

	enc = parse_encoding(encs, &idx);
	AN(enc);
	assert(idx >= 0);

	if (!check_enc_case(ctx, enc, case_s, kase))
		return (NULL);
	if (b->blob.len == 0)
		return ("");
	if (kase == DEFAULT)
		kase = LOWER;

	if (b->encoding[idx][kase] != NULL)
		return (b->encoding[idx][kase]);

	AZ(pthread_mutex_lock(&b->lock));
	if (b->encoding[idx][kase] == NULL) {
		len = enc->encode_len(b->blob.len);

		assert(len >= 0);
		if (len == 0)
			b->encoding[idx][kase] = empty;
		else {
			b->encoding[idx][kase] = malloc(len);
			if (b->encoding[idx][kase] == NULL)
				ERRNOMEM(ctx, "cannot encode");
			else {
				s = b->encoding[idx][kase];
				len = enc->encode(enc, kase, s, len,
				    b->blob.blob, b->blob.len);
				assert(len >= 0);
				if (len == 0) {
					free(s);
					b->encoding[idx][kase] = empty;
				}
				else
					s[len] = '\0';
			}
		}
	}
	AZ(pthread_mutex_unlock(&b->lock));

	return (b->encoding[idx][kase]);
}

VCL_VOID v_matchproto_(td_blob_blob__fini)
vmod_blob__fini(struct vmod_blob_blob **blobp)
{
	struct vmod_blob_blob *b;
	char *s;
	int i, j;

	TAKE_OBJ_NOTNULL(b, blobp, VMOD_BLOB_MAGIC);

	if (b->freeptr != NULL) {
		free(b->freeptr);
		b->blob.blob = NULL;
	}

	for (i = 0; i < __MAX_ENCODING; i++)
		for (j = 0; j < 2; j++) {
			s = b->encoding[i][j];
			if (s != NULL && s != empty) {
				free(s);
				b->encoding[i][j] = NULL;
			}
		}

	AZ(pthread_mutex_destroy(&b->lock));
	FREE_OBJ(b);
}

/* Functions */

VCL_BLOB v_matchproto_(td_blob_decode)
vmod_decode(VRT_CTX, VCL_ENUM decs, VCL_INT length, VCL_STRANDS strings)
{
	const struct blob_codec *dec;
	char *buf;
	ssize_t len;
	unsigned space;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	AN(decs);
	AN(strings);

	dec = parse_encoding(decs, NULL);
	AN(dec);

	buf = WS_Front(ctx->ws);
	space = WS_ReserveAll(ctx->ws);

	if (length <= 0)
		length = -1;
	errno = 0;
	len = dec->decode(dec, buf, space, length, strings);

	if (len == -1) {
		err_decode(ctx, strings->p[0]);
		WS_Release(ctx->ws, 0);
		return (NULL);
	}
	if (len == 0) {
		WS_Release(ctx->ws, 0);
		return (null_blob);
	}
	WS_Release(ctx->ws, len);

	assert(len > 0);

	return (VRT_blob(ctx, "blob.decode", buf, len, VMOD_BLOB_TYPE));
}

static VCL_STRING
encode(VRT_CTX, BLOB_CODEC, enum case_e kase, VCL_BLOB b)
{
	ssize_t len;
	char *buf;
	uintptr_t snap;
	unsigned space;

	AN(codec);

	if (b == NULL)
		return (NULL);

	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	snap = WS_Snapshot(ctx->ws);
	buf = WS_Front(ctx->ws);
	space = WS_ReserveAll(ctx->ws);

	len = codec->encode(codec, kase, buf, space, b->blob, b->len);

	if (len == -1) {
		ERRNOMEM(ctx, "cannot encode");
		WS_Release(ctx->ws, 0);
		WS_Reset(ctx->ws, snap);
		return (NULL);
	}
	if (len == 0) {
		WS_Release(ctx->ws, 0);
		WS_Reset(ctx->ws, snap);
		return ("");
	}
	buf[len] = '\0';
	WS_Release(ctx->ws, len + 1);
	return (buf);
}

VCL_STRING v_matchproto_(td_blob_encode)
vmod_encode(VRT_CTX, VCL_ENUM encs, VCL_ENUM case_s, VCL_BLOB b)
{
	const struct blob_codec *enc;
	enum case_e kase = parse_case(case_s);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(encs);
	AN(case_s);

	enc = parse_encoding(encs, NULL);
	AN(enc);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (!check_enc_case(ctx, enc, case_s, kase))
		return (NULL);
	return (encode(ctx, enc, kase, b));
}

VCL_STRING v_matchproto_(td_blob_transcode)
vmod_transcode(VRT_CTX, VCL_ENUM decs, VCL_ENUM encs, VCL_ENUM case_s,
    VCL_INT length, VCL_STRANDS strings)
{
	const struct blob_codec *dec, *enc;
	enum case_e kase = parse_case(case_s);
	struct vrt_blob b;
	VCL_STRING r;
	size_t l;
	ssize_t len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	AN(decs);
	AN(encs);
	AN(case_s);
	AN(strings);

	dec = parse_encoding(decs, NULL);
	AN(dec);
	enc = parse_encoding(encs, NULL);
	AN(enc);

	if (!check_enc_case(ctx, enc, case_s, kase))
		return (NULL);

	/*
	 * Allocate space for the decoded blob on the stack
	 * ignoring the limitation imposed by n
	 */
	l = decode_len(dec, strings);
	if (l == 0)
		return ("");

	/* XXX: handle stack overflow? */
	char buf[l];

	if (length <= 0)
		length = -1;
	errno = 0;
	len = dec->decode(dec, buf, l, length, strings);

	if (len < 0) {
		err_decode(ctx, strings->p[0]);
		return (NULL);
	}

	b.len = len;
	b.blob = buf;

	/*
	 * If the encoding and decoding are the same, and the decoding was
	 * legal, just return the concatenated string.
	 * For encodings with hex digits, we cannot assume the same result.
	 * since the call may specify upper- or lower-case that differs
	 * from the encoded string.
	 */
	if (length == -1 && enc == dec && enc->case_sensitive) {
		/*
		 * Returns NULL and invokes VCL failure on workspace
		 * overflow. If there is only one string already in the
		 * workspace, then it is re-used.
		 */
		return (VRT_CollectStrands(ctx, strings));
	}

	r = encode(ctx, enc, kase, &b);
	return (r);
}

VCL_BOOL v_matchproto_(td_blob_same)
vmod_same(VRT_CTX, VCL_BLOB b1, VCL_BLOB b2)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (b1 == b2)
		return (1);
	if (b1 == NULL || b2 == NULL)
		return (0);
	return (b1->len == b2->len && b1->blob == b2->blob);
}

VCL_BOOL v_matchproto_(td_blob_equal)
vmod_equal(VRT_CTX, VCL_BLOB b1, VCL_BLOB b2)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (b1 == b2)
		return (1);
	if (b1 == NULL || b2 == NULL)
		return (0);
	if (b1->len != b2->len)
		return (0);
	if (b1->blob == b2->blob)
		return (1);
	if (b1->blob == NULL || b2->blob == NULL)
		return (0);
	return (memcmp(b1->blob, b2->blob, b1->len) == 0);
}

VCL_INT v_matchproto_(td_blob_length)
vmod_length(VRT_CTX, VCL_BLOB b)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (b == NULL)
		return (0);
	return (b->len);
}

VCL_BLOB v_matchproto_(td_blob_sub)
vmod_sub(VRT_CTX, VCL_BLOB b, VCL_BYTES n, VCL_BYTES off)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(n >= 0);
	assert(off >= 0);

	if (b == NULL || b->len == 0 || b->blob == NULL) {
		ERR(ctx, "blob is empty in blob.sub()");
		return (NULL);
	}

	assert(b->len > 0);

	// XXX check for > SIZE_MAX ?
	if (off < 0 || n < 0) {
		ERR(ctx, "size or offset negative in blob.sub()");
		return (NULL);
	}

	if ((size_t)off > b->len || (size_t)n > b->len ||
	    (size_t)off + (size_t)n > b->len) {
		VERR(ctx, "size %jd from offset %jd requires more bytes than "
		    "blob length %zd in blob.sub()",
		    (intmax_t)n, (intmax_t)off, b->len);
		return (NULL);
	}

	if (n == 0)
		return (null_blob);

	return (VRT_blob(ctx, "blob.sub",
	    (const char *)b->blob + off, n, b->type));
}
