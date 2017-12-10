/*-
 * Copyright 2015-2016 UPLEX - Nils Goroll Systemoptimierung
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

#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>

enum encoding {
	_INVALID = 0,
#define VMODENUM(x) x,
#include "tbl_encodings.h"
	__MAX_ENCODING
};

#define AENC(enc) assert((enc) > _INVALID && (enc) < __MAX_ENCODING)

/*
 * The enums MUST appear in this order, since LOWER and UPPER are used to
 * index the array of cached encodings for the blob object.
 */
enum case_e {
#define VMODENUM(x) x,
#include "tbl_case.h"
};

/*
 * Length estimate interface
 */
typedef
size_t  len_f(size_t);

/*
 * General interface for an encoder: encode the data at in of length inlen
 * into a null-terminated string at buf, and return the length of the
 * encoding.
 *
 * enc: encoding enum (from parse_encoding.h)
 * kase: case enum (for encodings with hex digits)
 * buf: destination of the encoded string
 * buflen: maximum length available at buf
 * in: source of data to be encoded
 * inlen: length of data to be encoded
 *
 * The regions pointed to by buf and in MUST NOT overlap (this is the
 * contract imposed by restrict).
 * An encoder SHALL NOT append the terminating null byte (this must
 * be done by the caller).
 *
 * Returns:
 * -1, if there is insufficient space at buf, *including* space for the
 *     terminating null byte
 * 0, if the length of the encoding is 0 -- the caller should return
 *    the static constant empty string (literal "")
 * otherwise, the number of bytes written (note that this does not
 *            include any terminating null byte)
 */
typedef
ssize_t encode_f(const enum encoding enc, const enum case_e kase,
		 char *restrict const buf, const size_t buflen,
		 const char *restrict const in, const size_t inlen);

/*
 * General interface for a decoder: decode the concatenation of strings
 * in p and ap (obtained from a STRING_LIST) into buf, and return the
 * length of decoded data.
 *
 * dec: decoding enum (from parse_encoding.h)
 * buf: destination of the decoded data
 * buflen: maximum length available at buf
 * inlen: maximum length to read or -1 to read up to \0
 * p, ap: strings obtained from a VCL STRING_LIST
 *
 * The regions pointed to by buf and any of the strings in p or ap MUST
 * NOT overlap (per restrict).
 * Note that the p,ap list is terminated by vrt_magic_string_end, and
 * any member of the list may be NULL or empty.
 *
 * Returns:
 * -1, if there is insufficient space at buf, or if the decoding is
 *     invalid; errno SHALL be set to ENOMEM or EINVALID, respectively
 * 0, if the length of the decoding is 0 -- the caller should return
 *    a static constant empty BLOB
 * otherwise, the number of bytes written
 */
typedef
ssize_t decode_f(const enum encoding dec, char *restrict const buf,
		 const size_t buflen, const ssize_t inlen,
		 const char *restrict const p, va_list ap);

/* id.c */
len_f	 id_encode_l;
len_f	 id_decode_l;

encode_f id_encode;
decode_f id_decode;

/* base64.c */
len_f	 base64_decode_l;
len_f	 base64nopad_encode_l;
len_f	 base64_encode_l;

encode_f base64_encode;
decode_f base64_decode;

 /* hex.c */
len_f	 hex_encode_l;
len_f	 hex_decode_l;

encode_f hex_encode;
decode_f hex_decode;

/* url.c */
len_f	 url_encode_l;
len_f	 url_decode_l;

encode_f url_encode;
decode_f url_decode;
