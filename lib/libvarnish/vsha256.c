/*-
 * Copyright 2005 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * From:
 * $FreeBSD: sys/crypto/sha2/sha256c.c 300966 2016-05-29 17:26:40Z cperciva $
 */

#include "config.h"

#if defined(HAVE_SYS_ENDIAN_H)
#  include <sys/types.h>
#  include <sys/endian.h>
#  define VBYTE_ORDER	_BYTE_ORDER
#  define VBIG_ENDIAN	_BIG_ENDIAN
#elif defined(HAVE_ENDIAN_H)
#  include <endian.h>
#  define VBYTE_ORDER	__BYTE_ORDER
#  define VBIG_ENDIAN	__BIG_ENDIAN
#endif

#include <stdint.h>
#include <string.h>

#include "vdef.h"

#include "vas.h"
#include "vend.h"
#include "vsha256.h"

#if defined(VBYTE_ORDER) && VBYTE_ORDER == VBIG_ENDIAN

/* Copy a vector of big-endian uint32_t into a vector of bytes */
#define be32enc_vect(dst, src, len)	\
	memcpy((void *)dst, (const void *)src, (size_t)len)

/* Copy a vector of bytes into a vector of big-endian uint32_t */
#define be32dec_vect(dst, src, len)	\
	memcpy((void *)dst, (const void *)src, (size_t)len)

#else /* BYTE_ORDER != BIG_ENDIAN or in doubt... */

/*
 * Encode a length len/4 vector of (uint32_t) into a length len vector of
 * (unsigned char) in big-endian form.  Assumes len is a multiple of 4.
 */
static void
be32enc_vect(unsigned char *dst, const uint32_t *src, size_t len)
{
	size_t i;

	for (i = 0; i < len / 4; i++)
		vbe32enc(dst + i * 4, src[i]);
}

/*
 * Decode a big-endian length len vector of (unsigned char) into a length
 * len/4 vector of (uint32_t).  Assumes len is a multiple of 4.
 */
static void
be32dec_vect(uint32_t *dst, const unsigned char *src, size_t len)
{
	size_t i;

	for (i = 0; i < len / 4; i++)
		dst[i] = vbe32dec(src + i * 4);
}

#endif /* BYTE_ORDER != BIG_ENDIAN */

/* SHA256 round constants. */
static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Elementary functions used by SHA256 */
#define Ch(x, y, z)	((x & (y ^ z)) ^ z)
#define Maj(x, y, z)	((x & (y | z)) | (y & z))
#define SHR(x, n)	(x >> n)
#define ROTR(x, n)	((x >> n) | (x << (32 - n)))
#define S0(x)		(ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x)		(ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x)		(ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define s1(x)		(ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

/* SHA256 round function */
#define RND(a, b, c, d, e, f, g, h, k)				\
	do {							\
		h += S1(e) + Ch(e, f, g) + (k);			\
		d += h;						\
		h += S0(a) + Maj(a, b, c);			\
	} while (0)

/* Adjusted round function for rotating state */
#define RNDr(S, W, i, ii)			\
	RND(S[(64 - i) % 8], S[(65 - i) % 8],	\
	    S[(66 - i) % 8], S[(67 - i) % 8],	\
	    S[(68 - i) % 8], S[(69 - i) % 8],	\
	    S[(70 - i) % 8], S[(71 - i) % 8],	\
	    W[i + ii] + K[i + ii])

/* Message schedule computation */
#define MSCH(W, ii, i)						\
	do {							\
		W[i + ii + 16] =				\
		    s1(W[i + ii + 14]) + W[i + ii + 9] +	\
		    s0(W[i + ii + 1]) + W[i + ii];		\
	} while (0)

/*
 * SHA256 block compression function.  The 256-bit state is transformed via
 * the 512-bit input block to produce a new state.
 */
static void
VSHA256_Transform(uint32_t * state, const unsigned char block[64])
{
	uint32_t W[64];
	uint32_t S[8];
	int i;

	/* 1. Prepare the first part of the message schedule W. */
	be32dec_vect(W, block, 64);

	/* 2. Initialize working variables. */
	memcpy(S, state, 32);

	/* 3. Mix. */
	for (i = 0; i < 64; i += 16) {
		RNDr(S, W, 0, i);
		RNDr(S, W, 1, i);
		RNDr(S, W, 2, i);
		RNDr(S, W, 3, i);
		RNDr(S, W, 4, i);
		RNDr(S, W, 5, i);
		RNDr(S, W, 6, i);
		RNDr(S, W, 7, i);
		RNDr(S, W, 8, i);
		RNDr(S, W, 9, i);
		RNDr(S, W, 10, i);
		RNDr(S, W, 11, i);
		RNDr(S, W, 12, i);
		RNDr(S, W, 13, i);
		RNDr(S, W, 14, i);
		RNDr(S, W, 15, i);

		if (i == 48)
			break;
		MSCH(W, 0, i);
		MSCH(W, 1, i);
		MSCH(W, 2, i);
		MSCH(W, 3, i);
		MSCH(W, 4, i);
		MSCH(W, 5, i);
		MSCH(W, 6, i);
		MSCH(W, 7, i);
		MSCH(W, 8, i);
		MSCH(W, 9, i);
		MSCH(W, 10, i);
		MSCH(W, 11, i);
		MSCH(W, 12, i);
		MSCH(W, 13, i);
		MSCH(W, 14, i);
		MSCH(W, 15, i);
	}

	/* 4. Mix local working variables into global state */
	for (i = 0; i < 8; i++)
		state[i] += S[i];
}

static const unsigned char PAD[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Add padding and terminating bit-count. */
static void
VSHA256_Pad(VSHA256_CTX * ctx)
{
	size_t r;

	/* Figure out how many bytes we have buffered. */
	r = (ctx->count >> 3) & 0x3f;

	/* Pad to 56 mod 64, transforming if we finish a block en route. */
	if (r < 56) {
		/* Pad to 56 mod 64. */
		memcpy(&ctx->buf[r], PAD, 56 - r);
	} else {
		/* Finish the current block and mix. */
		memcpy(&ctx->buf[r], PAD, 64 - r);
		VSHA256_Transform(ctx->state, ctx->buf);

		/* The start of the final block is all zeroes. */
		memset(&ctx->buf[0], 0, 56);
	}

	/* Add the terminating bit-count. */
	vbe64enc(&ctx->buf[56], ctx->count);

	/* Mix in the final block. */
	VSHA256_Transform(ctx->state, ctx->buf);
}

/* SHA-256 initialization.  Begins a SHA-256 operation. */
void
VSHA256_Init(VSHA256_CTX * ctx)
{

	/* Zero bits processed so far */
	ctx->count = 0;

	/* Magic initialization constants */
	ctx->state[0] = 0x6A09E667;
	ctx->state[1] = 0xBB67AE85;
	ctx->state[2] = 0x3C6EF372;
	ctx->state[3] = 0xA54FF53A;
	ctx->state[4] = 0x510E527F;
	ctx->state[5] = 0x9B05688C;
	ctx->state[6] = 0x1F83D9AB;
	ctx->state[7] = 0x5BE0CD19;
}

/* Add bytes into the hash */
void
VSHA256_Update(VSHA256_CTX * ctx, const void *in, size_t len)
{
	uint64_t bitlen;
	uint32_t r;
	const unsigned char *src = in;

	/* Number of bytes left in the buffer from previous updates */
	r = (ctx->count >> 3) & 0x3f;

	/* Convert the length into a number of bits */
	bitlen = len << 3;

	/* Update number of bits */
	ctx->count += bitlen;

	/* Handle the case where we don't need to perform any transforms */
	if (len < 64 - r) {
		memcpy(&ctx->buf[r], src, len);
		return;
	}

	/* Finish the current block */
	memcpy(&ctx->buf[r], src, 64 - r);
	VSHA256_Transform(ctx->state, ctx->buf);
	src += 64 - r;
	len -= 64 - r;

	/* Perform complete blocks */
	while (len >= 64) {
		VSHA256_Transform(ctx->state, src);
		src += 64;
		len -= 64;
	}

	/* Copy left over data into buffer */
	memcpy(ctx->buf, src, len);
}

/*
 * SHA-256 finalization.  Pads the input data, exports the hash value,
 * and clears the context state.
 */
void
VSHA256_Final(unsigned char digest[static VSHA256_DIGEST_LENGTH],
    VSHA256_CTX *ctx)
{

	/* Add padding */
	VSHA256_Pad(ctx);

	/* Write the hash */
	be32enc_vect(digest, ctx->state, VSHA256_DIGEST_LENGTH);

	/* Clear the context state */
	memset((void *)ctx, 0, sizeof(*ctx));
}

/*
 * A few test-vectors, just in case
 */

static const struct sha256test {
	const char              *input;
	const unsigned char     output[32];
} sha256test[] = {
    { "",
	{0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
	 0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
	 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55} },
    { "message digest",
	{0xf7, 0x84, 0x6f, 0x55, 0xcf, 0x23, 0xe1, 0x4e, 0xeb, 0xea, 0xb5,
	 0xb4, 0xe1, 0x55, 0x0c, 0xad, 0x5b, 0x50, 0x9e, 0x33, 0x48, 0xfb,
	 0xc4, 0xef, 0xa3, 0xa1, 0x41, 0x3d, 0x39, 0x3c, 0xb6, 0x50} },
    { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
	{0xdb, 0x4b, 0xfc, 0xbd, 0x4d, 0xa0, 0xcd, 0x85, 0xa6, 0x0c, 0x3c,
	 0x37, 0xd3, 0xfb, 0xd8, 0x80, 0x5c, 0x77, 0xf1, 0x5f, 0xc6, 0xb1,
	 0xfd, 0xfe, 0x61, 0x4e, 0xe0, 0xa7, 0xc8, 0xfd, 0xb4, 0xc0} },
    { NULL }
};


void
VSHA256_Test(void)
{
	struct VSHA256Context c;
	const struct sha256test *p;
	unsigned char o[32];

	for (p = sha256test; p->input != NULL; p++) {
		VSHA256_Init(&c);
		VSHA256_Update(&c, p->input, strlen(p->input));
		VSHA256_Final(o, &c);
		AZ(memcmp(o, p->output, 32));
	}
}
