/*-
 * Copyright 2015-2017 UPLEX - Nils Goroll Systemoptimierung
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
#include <pthread.h>

#include "vcl.h"
#include "vdef.h"
#include "vrt.h"

#include "vcc_if.h"
#include "vmod_blob.h"
#include "wb.h"

struct vmod_blob_blob {
	unsigned magic;
#define VMOD_BLOB_MAGIC 0xfade4fa9
	struct vmod_priv blob;
	char *encoding[__MAX_ENCODING][2];
	pthread_mutex_t lock;
};

#define B64_FUNCS				   \
		.decode_l	= base64_decode_l, \
		.decode		= base64_decode,   \
		.encode		= base64_encode

static const struct vmod_blob_fptr {
	len_f		*const decode_l;
	decode_f	*const decode;
	len_f		*const encode_l;
	encode_f	*const encode;
} func[__MAX_ENCODING] = {
	[_INVALID] = {
		/* make implicit null init explicit for clarity */
		.decode_l	= NULL,
		.decode		= NULL,
		.encode_l	= NULL,
		.encode		= NULL
	},
	[IDENTITY] = {
		.decode_l	= id_decode_l,
		.decode		= id_decode,
		.encode_l	= id_encode_l,
		.encode		= id_encode
	},
	[BASE64] = {
		B64_FUNCS,
		.encode_l	= base64_encode_l
	},
	[BASE64URL] = {
		B64_FUNCS,
		.encode_l	= base64_encode_l
	},
	[BASE64URLNOPAD] = {
		B64_FUNCS,
		.encode_l	= base64nopad_encode_l
	},
	[HEX] = {
		.decode_l	= hex_decode_l,
		.decode		= hex_decode,
		.encode_l	= hex_encode_l,
		.encode		= hex_encode
	},
	[URL] = {
		.decode_l	= url_decode_l,
		.decode		= url_decode,
		.encode_l	= url_encode_l,
		.encode		= url_encode
	},
};

#undef B64_FUNCS

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

static const struct vmod_priv null_blob[1] =
{
	{
		.priv = empty,
		.len = 0,
		.free = NULL
	}
};

static inline size_t
decode_l_va(enum encoding dec, const char * const p, va_list ap)
{
	size_t len = 0;

	AENC(dec);

	for (const char *s = p; s != vrt_magic_string_end;
	     s = va_arg(ap, const char *))
		if (s != NULL && *s != '\0')
			len += strlen(s);

	return(func[dec].decode_l(len));
}

static void
err_decode(VRT_CTX, const char *enc)
{
	switch(errno) {
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

static inline enum case_e
parse_case(VCL_ENUM case_s)
{
	switch(*case_s) {
	case 'D':
		AZ(strcmp(case_s + 1, "EFAULT"));
		return DEFAULT;
	case 'L':
		AZ(strcmp(case_s + 1, "OWER"));
		return LOWER;
	case 'U':
		AZ(strcmp(case_s + 1, "PPER"));
		return UPPER;
	default:
		WRONG("illegal case enum");
	}
}

static inline int
encodes_hex(enum encoding enc)
{
	return (enc == HEX || enc == URL);
}

/* Require case DEFAULT for all encodings besides HEX and URL. */

static inline int
check_enc_case(VRT_CTX, VCL_ENUM encs, VCL_ENUM case_s, enum encoding enc,
	       enum case_e kase)
{
	if (!encodes_hex(enc) && kase != DEFAULT) {
		VERR(ctx, "case %s is illegal with encoding %s", case_s, encs);
		return 0;
	}
	return 1;
}

/* Objects */

VCL_VOID __match_proto__(td_blob_blob__init)
vmod_blob__init(VRT_CTX, struct vmod_blob_blob **blobp, const char *vcl_name,
		VCL_ENUM decs, const char *p, ...)
{
	struct vmod_blob_blob *b;
	enum encoding dec = parse_encoding(decs);
	va_list ap;
	ssize_t len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(blobp);
	AZ(*blobp);
	AN(vcl_name);
	AENC(dec);

	ALLOC_OBJ(b, VMOD_BLOB_MAGIC);
	AN(b);
	*blobp = b;
	b->blob.free = NULL;
	AZ(pthread_mutex_init(&b->lock, NULL));

	va_start(ap, p);
	len = decode_l_va(dec, p, ap);
	va_end(ap);
	if (len == 0) {
		b->blob.len = 0;
		b->blob.priv = NULL;
		return;
	}
	assert(len > 0);

	b->blob.priv = malloc(len);
	if (b->blob.priv == NULL) {
		VERRNOMEM(ctx, "cannot create blob %s", vcl_name);
		return;
	}

	va_start(ap, p);
	errno = 0;
	len = func[dec].decode(dec, b->blob.priv, len, -1, p, ap);
	va_end(ap);

	if (len == -1) {
		assert(errno == EINVAL);
		free(b->blob.priv);
		b->blob.priv = NULL;
		VERR(ctx, "cannot create blob %s, illegal encoding beginning "
		    "with \"%s\"", vcl_name, p);
		return;
	}
	if (len == 0) {
		b->blob.len = 0;
		free(b->blob.priv);
		b->blob.priv = NULL;
		return;
	}
	b->blob.len = len;
}

VCL_BLOB __match_proto__(td_blob_blob_get)
vmod_blob_get(VRT_CTX, struct vmod_blob_blob *b)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(b, VMOD_BLOB_MAGIC);
	return &b->blob;
}

VCL_STRING __match_proto__(td_blob_blob_encode)
vmod_blob_encode(VRT_CTX, struct vmod_blob_blob *b, VCL_ENUM encs,
		 VCL_ENUM case_s)
{
	enum encoding enc = parse_encoding(encs);
	AENC(enc);
	enum case_e kase = parse_case(case_s);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(b, VMOD_BLOB_MAGIC);

	if (!check_enc_case(ctx, encs, case_s, enc, kase))
		return NULL;
	if (b->blob.len == 0)
		return "";
	if (kase == DEFAULT)
		kase = LOWER;

	if (b->encoding[enc][kase] == NULL) {
		AZ(pthread_mutex_lock(&b->lock));
		if (b->encoding[enc][kase] == NULL) {
			ssize_t len = func[enc].encode_l(b->blob.len);

			assert(len >= 0);
			if (len == 0)
				b->encoding[enc][kase] = empty;
			else {
				b->encoding[enc][kase] = malloc(len);
				if (b->encoding[enc][kase] == NULL)
					ERRNOMEM(ctx, "cannot encode");
				else {
					char *s = b->encoding[enc][kase];
					len =
						func[enc].encode(
							enc, kase, s, len,
							b->blob.priv,
							b->blob.len);
					assert(len >= 0);
					if (len == 0) {
						free(s);
						b->encoding[enc][kase] = empty;
					}
					else
						s[len] = '\0';
				}
			}
		}
		AZ(pthread_mutex_unlock(&b->lock));
	}
	return b->encoding[enc][kase];
}

VCL_VOID __match_proto__(td_blob_blob__fini)
vmod_blob__fini(struct vmod_blob_blob **blobp)
{
	struct vmod_blob_blob *b;

	if (blobp == NULL || *blobp == NULL)
		return;

	b = *blobp;
	*blobp = NULL;
	CHECK_OBJ(b, VMOD_BLOB_MAGIC);
	if (b->blob.priv != NULL) {
		free(b->blob.priv);
		b->blob.priv = NULL;
	}
	for (int i = 0; i < __MAX_ENCODING; i++)
		for (int j = 0; j < 2; j++) {
			char *s = b->encoding[i][j];
			if (s != NULL && s != empty) {
				free(s);
				b->encoding[i][j] = NULL;
			}
		}
	AZ(pthread_mutex_destroy(&b->lock));
	FREE_OBJ(b);
}

/* Functions */

static inline const char *
find_nonempty_va(const char *restrict *p, va_list ap)
{
	const char *q;

	/* find first non-empty vararg */
	for (; *p == vrt_magic_string_end || *p == NULL || **p == '\0';
	     *p = va_arg(ap, char *))
		if (*p == vrt_magic_string_end)
			return (vrt_magic_string_end);

	/* find next non-empty vararg */
	for (q = va_arg(ap, const char *);
	     q != vrt_magic_string_end && (q == NULL || *q == '\0');
	     q = va_arg(ap, const char *))
		;

	return (q);
}

VCL_BLOB __match_proto__(td_blob_decode)
vmod_decode(VRT_CTX, VCL_ENUM decs, VCL_INT length, const char *p, ...)
{
	enum encoding dec = parse_encoding(decs);
	va_list ap;
	struct wb_s wb;
	struct vmod_priv *b;
	char *buf;
	uintptr_t snap;
	ssize_t len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AENC(dec);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);

	snap = WS_Snapshot(ctx->ws);
	if ((b = WS_Alloc(ctx->ws, sizeof(struct vmod_priv))) == NULL) {
		ERRNOMEM(ctx, "cannot decode");
		return NULL;
	}

	if (wb_create(ctx->ws, &wb) == NULL) {
		WS_Reset(ctx->ws, snap);
		ERRNOMEM(ctx, "cannot decode");
		return NULL;
	}
	buf = wb_buf(&wb);

	if (length <= 0)
		length = -1;
	va_start(ap, p);
	errno = 0;
	len = func[dec].decode(dec, buf, wb_space(&wb), length, p, ap);
	va_end(ap);

	if (len == -1) {
		err_decode(ctx, p);
		wb_reset(&wb);
		WS_Reset(ctx->ws, snap);
		return NULL;
	}
	if (len == 0) {
		wb_reset(&wb);
		WS_Reset(ctx->ws, snap);
		return null_blob;
	}
	wb_advance(&wb, len);
	WS_ReleaseP(ctx->ws, wb_buf(&wb));
	b->priv = buf;
	b->len = len;
	b->free = NULL;
	return (b);
}

static VCL_STRING
encode(VRT_CTX, enum encoding enc, enum case_e kase, VCL_BLOB b)
{
	struct wb_s wb;
	ssize_t len;

	AENC(enc);

	if (b == NULL)
		return NULL;

	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	if (wb_create(ctx->ws, &wb) == NULL) {
		ERRNOMEM(ctx, "cannot encode");
		return NULL;
	}

	len = func[enc].encode(enc, kase,
			       wb_buf(&wb), wb_space(&wb), b->priv, b->len);

	if (len == -1) {
		ERRNOMEM(ctx, "cannot encode");
		wb_reset(&wb);
		return NULL;
	}
	if (len == 0) {
		wb_reset(&wb);
		return "";
	}
	wb_advance(&wb, len);
	return wb_finish(&wb, NULL);
}

VCL_STRING __match_proto__(td_blob_encode)
vmod_encode(VRT_CTX, VCL_ENUM encs, VCL_ENUM case_s, VCL_BLOB b)
{
	enum encoding enc = parse_encoding(encs);
	enum case_e kase = parse_case(case_s);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (!check_enc_case(ctx, encs, case_s, enc, kase))
		return NULL;
	return encode(ctx, enc, kase, b);
}

VCL_STRING __match_proto__(td_blob_transcode)
vmod_transcode(VRT_CTX, VCL_ENUM decs, VCL_ENUM encs, VCL_ENUM case_s,
	       VCL_INT length, const char *p, ...)
{
	enum encoding dec = parse_encoding(decs);
	enum encoding enc = parse_encoding(encs);
	enum case_e kase = parse_case(case_s);
	va_list ap;
	struct vmod_priv b;
	VCL_STRING r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);

	AENC(dec);
	AENC(enc);

	if (!check_enc_case(ctx, encs, case_s, enc, kase))
		return NULL;

	/*
	 * Allocate space for the decoded blob on the stack
	 * ignoring the limitation imposed by n
	 */
	va_start(ap, p);
	size_t l = decode_l_va(dec, p, ap);
	va_end(ap);
	if (l == 0)
		return "";
	/* XXX: handle stack overflow? */
	char buf[l];
	b.free = NULL;
	b.priv = buf;

	if (length <= 0)
		length = -1;
	va_start(ap, p);
	errno = 0;
	b.len = func[dec].decode(dec, buf, l, length, p, ap);
	va_end(ap);

	if (b.len == -1) {
		err_decode(ctx, p);
		return NULL;
	}

	/*
	 * If the encoding and decoding are the same, and the decoding was
	 * legal, just return the string, if there was only one in the
	 * STRING_LIST, or else the concatenated string.
	 * For encodings with hex digits, we cannot assume the same result.
	 * since the call may specify upper- or lower-case that differs
	 * from the encoded string.
	 */
	if (length == -1 && enc == dec && !encodes_hex(enc)) {
		const char *q, *pp = p;
		va_start(ap, p);
		q = find_nonempty_va(&pp, ap);
		va_end(ap);

		if (pp == vrt_magic_string_end)
			return "";

		if (q == vrt_magic_string_end)
			return pp;

		r = VRT_String(ctx->ws, NULL, p, ap);
		return r;
	}

	r = encode(ctx, enc, kase, &b);
	return (r);
}

VCL_BOOL __match_proto__(td_blob_same)
vmod_same(VRT_CTX, VCL_BLOB b1, VCL_BLOB b2)
{
	(void) ctx;

	if (b1 == NULL && b2 == NULL)
		return 1;
	if (b1 == NULL || b2 == NULL)
		return 0;
	return (b1->len == b2->len && b1->priv == b2->priv);
}

VCL_BOOL __match_proto__(td_blob_equal)
vmod_equal(VRT_CTX, VCL_BLOB b1, VCL_BLOB b2)
{
	(void) ctx;

	if (b1 == NULL && b2 == NULL)
		return 1;
	if (b1 == NULL || b2 == NULL)
		return 0;
	if (b1->len != b2->len)
		return 0;
	if (b1->priv == b2->priv)
		return 1;
	if (b1->priv == NULL || b2->priv == NULL)
		return 0;
	return (memcmp(b1->priv, b2->priv, b1->len) == 0);
}

VCL_INT __match_proto__(td_blob_length)
vmod_length(VRT_CTX, VCL_BLOB b)
{
	(void) ctx;

	if (b == NULL)
		return 0;
	return b->len;
}

VCL_BLOB __match_proto__(td_blob_sub)
vmod_sub(VRT_CTX, VCL_BLOB b, VCL_BYTES n, VCL_BYTES off)
{
	uintptr_t snap;
	struct vmod_priv *sub;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(n >= 0);
	assert(off >= 0);

	if (b == NULL || b->len == 0 || b->priv == NULL) {
		ERR(ctx, "blob is empty in blob.sub()");
		return NULL;
	}
	assert(b->len >= 0);
	if (off + n > b->len) {
		VERR(ctx, "size %lld from offset %lld requires more bytes than "
		     "blob length %d in blob.sub()", n, off, b->len);
		return NULL;
	}

	if (n == 0)
		return null_blob;

	snap = WS_Snapshot(ctx->ws);
	if ((sub = WS_Alloc(ctx->ws, sizeof(*sub))) == NULL) {
		ERRNOMEM(ctx, "Allocating BLOB result in blob.sub()");
		return NULL;
	}
	if ((sub->priv = WS_Alloc(ctx->ws, n)) == NULL) {
		VERRNOMEM(ctx, "Allocating %lld bytes in blob.sub()", n);
		WS_Reset(ctx->ws, snap);
		return NULL;
	}
	memcpy(sub->priv, (char *)b->priv + off, n);
	sub->len = n;
	return sub;
}
