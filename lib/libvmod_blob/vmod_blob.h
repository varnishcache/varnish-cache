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

/* vrt.h must be included before this header (for struct strands). */

#include <stdlib.h>
#include <sys/types.h>

#include "vcc_if.h"

/*---------------------------------------------------------------------
 * The vmod_blob codec interface.
 */

struct blob_codec;

#define BLOB_CODEC const struct blob_codec *codec

#define CHECK_BLOB_CODEC(codec, nm)			\
	do {						\
		AN(codec);				\
		assert((codec)->name == &VENUM(nm));	\
	} while (0)

typedef char *restrict		blob_dest_t;
typedef const char *restrict	blob_src_t;

/*
 * The enums MUST appear in this order, since LOWER and UPPER are used to
 * index the array of cached encodings for the blob object.
 */
enum case_e {
#define VMODENUM(x) x,
#include "tbl_case.h"
};

/*
 * Length estimate interface.
 */
typedef size_t blob_len_f(size_t);

/*
 * General interface for a decoder: decode the strings STRANDS into dest,
 * and return the length of decoded data.
 *
 * dest:	destination of the decoded data
 * destlen:	maximum length available at dest
 * slen:	maximum length to read or -1 to read up to '\0'
 * strings:	a string in the form of a STRANDS
 *
 * The regions pointed to by dest and strings MUST NOT overlap (per
 * restrict). Note that any member of strings may be NULL or empty.
 *
 * Returns:
 *        -1, if there is insufficient space at dest, or if the decoding is
 *            invalid; errno SHALL be set to ENOMEM or EINVALID, respectively
 *         0, if the length of the decoding is 0 -- the caller should return
 *            a static constant empty BLOB
 * otherwise, the number of bytes written
 */
typedef ssize_t blob_decode_f(BLOB_CODEC, blob_dest_t dest, size_t destlen,
    ssize_t slen, VCL_STRANDS strings);

/*
 * General interface for an encoder: encode the data at src of length
 * srclen into a null-terminated string at dest, and return the length
 * of the encoded string.
 *
 * kase:	case enum (for encodings with hex digits)
 * dest:	destination of the encoded string
 * destlen:	maximum length available at dest
 * src:		source of data to be encoded
 * srclen:	length of data to be encoded
 *
 * The regions pointed to by dest and src MUST NOT overlap (this is the
 * contract imposed by restrict).
 * An encoder SHALL NOT append the terminating null byte (this must
 * be done by the caller).
 *
 * Returns:
 *        -1, if there is insufficient space at dest, *including* space
 *            for the terminating null byte
 *         0, if the length of the encoding is 0 -- the caller should
 *            return the static constant empty string (literal "")
 * otherwise, the number of bytes written (note that this does not
 *            include any terminating null byte)
 */
typedef ssize_t blob_encode_f(BLOB_CODEC, enum case_e kase, blob_dest_t dest,
    size_t destlen, blob_src_t src, size_t srclen);

struct blob_codec {
	blob_len_f	*decode_len;
	blob_len_f	*encode_len;
	blob_decode_f	*decode;
	blob_encode_f	*encode;
	VCL_ENUM	*name;
	const void	*priv;
	unsigned	case_sensitive;
};

/* id.c */
extern const struct blob_codec blob_codec_id;

/* base64.c */
extern const struct blob_codec blob_codec_base64;
extern const struct blob_codec blob_codec_base64url;
extern const struct blob_codec blob_codec_base64urlnopad;

/* hex.c */
extern const struct blob_codec blob_codec_hex;

/* url.c */
extern const struct blob_codec blob_codec_url;

/*---------------------------------------------------------------------
 * The deprecated codec interface.
 */

typedef const size_t			blob_len_t;
typedef const ssize_t			blob_slen_t;

enum encoding {
	__INVALID_ENCODING = 0,
#define VMODENUM(x) x,
#include "tbl_encodings.h"
	__MAX_ENCODING
};

#define AENC(enc) assert((enc) > __INVALID_ENCODING && (enc) < __MAX_ENCODING)

typedef size_t len_f(size_t);

typedef
ssize_t encode_f(const enum encoding enc, const enum case_e kase,
		 blob_dest_t buf, blob_len_t buflen,
		 blob_src_t in, blob_len_t inlen);

typedef ssize_t decode_f(const enum encoding dec, blob_dest_t buf,
    blob_len_t buflen, blob_slen_t inlen, VCL_STRANDS strings);

/* id.c */
len_f		old_id_encode_l;
len_f		old_id_decode_l;
encode_f	old_id_encode;
decode_f	old_id_decode;

/* base64.c */
len_f		old_base64_decode_l;
len_f		old_base64nopad_encode_l;
len_f		old_base64_encode_l;
encode_f	old_base64_encode;
decode_f	old_base64_decode;

 /* hex.c */
len_f		old_hex_encode_l;
len_f		old_hex_decode_l;
encode_f	old_hex_encode;
decode_f	old_hex_decode;

/* url.c */
len_f		old_url_encode_l;
len_f		old_url_decode_l;
encode_f	old_url_encode;
decode_f	old_url_decode;
