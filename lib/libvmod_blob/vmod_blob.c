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
	const char *encoding[__MAX_ENCODING];
	pthread_mutex_t lock;
};

#define B64_FUNCS				   \
		.decode_l	= base64_decode_l, \
		.decode		= base64_decode,   \
		.encode		= base64_encode

#define HEX_FUNCS				\
		.decode_l	= hex_decode_l, \
		.decode		= hex_decode,	\
		.encode_l	= hex_encode_l, \
		.encode		= hex_encode

#define URL_FUNCS				\
		.decode_l	= url_decode_l, \
		.decode		= url_decode,	\
		.encode_l	= url_encode_l, \
		.encode		= url_encode

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
		HEX_FUNCS
	},
	[HEXUC] = {
		HEX_FUNCS
	},
	[HEXLC] = {
		HEX_FUNCS
	},
	[URL] = {
		URL_FUNCS
	},
	[URLUC] = {
		URL_FUNCS
	},
	[URLLC] = {
		URL_FUNCS
	}
};

#undef B64_FUNCS
#undef HEX_FUNCS
#undef URL_FUNCS

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

VCL_STRING
vmod_blob_encode(VRT_CTX, struct vmod_blob_blob *b, VCL_ENUM encs)
{
	enum encoding enc = parse_encoding(encs);
	AENC(enc);

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(b, VMOD_BLOB_MAGIC);

	if (b->blob.len == 0)
		return "";

	if (b->encoding[enc] == NULL) {
		AZ(pthread_mutex_lock(&b->lock));
		if (b->encoding[enc] == NULL) {
			ssize_t len = func[enc].encode_l(b->blob.len);

			assert(len >= 0);
			if (len == 0)
				b->encoding[enc] = "";
			else {
				b->encoding[enc] = malloc(len);
				if (b->encoding[enc] == NULL)
					ERRNOMEM(ctx, "cannot encode");
				else {
					const char *s = b->encoding[enc];
					len =
						func[enc].encode(
							enc, (void *) s, len,
							b->blob.priv,
							b->blob.len);
					assert(len >= 0);
					if (len == 0) {
						free((void *) s);
						b->encoding[enc] = "";
					}
					else
						*((char *)s + len) = '\0';
				}
			}
		}
		AZ(pthread_mutex_unlock(&b->lock));
	}
	return b->encoding[enc];
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
		if (b->encoding[i] != NULL && b->encoding[i][0] != '\0') {
			free((void *) b->encoding[i]);
			b->encoding[i] = NULL;
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
	     *p = va_arg(ap, const char *))
		if (*p == vrt_magic_string_end)
			return (vrt_magic_string_end);

	/* find next non-empty vararg */
	for (q = va_arg(ap, const char *);
	     q != vrt_magic_string_end && (q == NULL || *q == '\0');
	     q = va_arg(ap, const char *))
		;

	return (q);
}

/*
 * special case: we can avoid copying for identity decode if we need to
 * deal with a single vararg only - in which case we just have the blob
 * point to the input string
 */
static VCL_BLOB
decode_id_inplace(struct vmod_priv *b, VCL_INT n, const char *restrict p,
		  va_list ap) {
	const char *q;
	int l;

	if (n == 0)
		return null_blob;

	q = find_nonempty_va(&p, ap);

	if (p == vrt_magic_string_end)
		return null_blob;

	if (q == vrt_magic_string_end) {
		/* can use in-place decode */
		l = strlen(p);
		if (n > 0 && n < l)
			l = n;
		b->priv = (char *)p;
		b->len = l;
		b->free = NULL;
		return (b);
	}

	if (n == -1)
		return NULL;
	l = strlen(p);
	if (n > l)
		return NULL;
	b->priv = (char *)p;
	b->len = n;
	b->free = NULL;

	return (b);
}

static VCL_BLOB
decode(VRT_CTX, VCL_INT n, VCL_ENUM decs,
       const char *restrict const p, va_list ap) {
	enum encoding dec = parse_encoding(decs);
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

	if (dec == IDENTITY) {
		va_list aq;
		va_copy(aq, ap);
		const struct vmod_priv *bb = decode_id_inplace(b, n, p, aq);
		va_end(aq);
		if (bb == null_blob)
			WS_Reset(ctx->ws, snap);
		if (bb == b)
			return bb;
	}

	if (wb_create(ctx->ws, &wb) == NULL) {
		WS_Reset(ctx->ws, snap);
		ERRNOMEM(ctx, "cannot decode");
		return NULL;
	}
	buf = wb_buf(&wb);

	errno = 0;
	len = func[dec].decode(dec, buf, wb_space(&wb), n, p, ap);

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
	return b;
}

VCL_BLOB __match_proto__(td_blob_decode)
vmod_decode(VRT_CTX, VCL_ENUM decs, const char *p, ...)
{
	va_list ap;
	VCL_BLOB r;

	va_start(ap, p);
	r = decode(ctx, -1, decs, p, ap);
	va_end(ap);

	return (r);
}

VCL_BLOB __match_proto__(td_blob_decode_n)
vmod_decode_n(VRT_CTX, VCL_INT n, VCL_ENUM decs, const char *p, ...)
{
	va_list ap;
	VCL_BLOB r;

	va_start(ap, p);
	r = decode(ctx, n, decs, p, ap);
	va_end(ap);

	return (r);
}

static VCL_STRING
encode(VRT_CTX, enum encoding enc, VCL_BLOB b)
{
	struct wb_s wb;
	ssize_t len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AENC(enc);

	if (b == NULL)
		return NULL;

	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	if (wb_create(ctx->ws, &wb) == NULL) {
		ERRNOMEM(ctx, "cannot encode");
		return NULL;
	}

	len = func[enc].encode(enc,
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
vmod_encode(VRT_CTX, VCL_ENUM encs, VCL_BLOB b)
{
	enum encoding enc = parse_encoding(encs);
	return encode(ctx, enc, b);
}

static VCL_STRING
transcode(VRT_CTX, VCL_INT n, VCL_ENUM decs, VCL_ENUM encs,
	  const char *restrict const p, va_list ap)
{
	enum encoding dec = parse_encoding(decs);
	enum encoding enc = parse_encoding(encs);
	va_list aq;
	struct vmod_priv b;
	VCL_STRING r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);

	AENC(dec);
	AENC(enc);

	if (dec == IDENTITY) {
		va_copy(aq, ap);
		const struct vmod_priv *bb = decode_id_inplace(&b, n, p, aq);
		va_end(aq);
		if (bb != NULL) {
			r = encode(ctx, enc, bb);
			return (r);
		}
	}

	/*
	 * Allocate space for the decoded blob on the stack
	 * ignoring the limitation imposed by n
	 */
	va_copy(aq, ap);
	size_t l = decode_l_va(dec, p, aq);
	va_end(aq);
	if (l == 0)
		return "";
	/* XXX: handle stack overflow? */
	char buf[l];
	b.free = NULL;
	b.priv = buf;

	errno = 0;
	va_copy(aq, ap);
	b.len = func[dec].decode(dec, buf, l, n, p, aq);
	va_end(aq);

	if (b.len == -1) {
		err_decode(ctx, p);
		return NULL;
	}

	/*
	 * If the encoding and decoding are the same, and the decoding was
	 * legal, just return the string, if there was only one in the
	 * STRING_LIST, or else the concatenated string.
	 */
	if (n == -1 && enc == dec) {
		const char *q, *pp = p;
		va_copy(aq, ap);
		q = find_nonempty_va(&pp, ap);
		va_end(aq);

		if (pp == vrt_magic_string_end)
			return "";

		if (q == vrt_magic_string_end)
			return pp;

		r = VRT_String(ctx->ws, NULL, p, ap);
		return r;
	}

	r = encode(ctx, enc, &b);
	return (r);
}

VCL_STRING __match_proto__(td_blob_transcode)
vmod_transcode(VRT_CTX, VCL_ENUM decs, VCL_ENUM encs,
	       const char *p, ...)
{
	va_list ap;
	VCL_STRING r;

	va_start(ap, p);
	r = transcode(ctx, -1, decs, encs, p, ap);
	va_end(ap);

	return (r);
}

VCL_STRING __match_proto__(td_blob_transcode_n)
vmod_transcode_n(VRT_CTX, VCL_INT n, VCL_ENUM decs, VCL_ENUM encs,
		 const char *p, ...)
{
	va_list ap;
	VCL_STRING r;

	va_start(ap, p);
	r = transcode(ctx, n, decs, encs, p, ap);
	va_end(ap);

	return (r);
}
