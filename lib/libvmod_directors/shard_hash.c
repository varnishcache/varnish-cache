/*-
 * Copyright 2009-2013 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Nils Goroll <nils.goroll@uplex.de>
 *          Geoffrey Simmons <geoff.simmons@uplex.de>
 *          Julian Wiesener <jw@uplex.de>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <stdint.h>
#include <string.h>

#include "cache/cache.h"

#include "vrt.h"
#include "vsha256.h"
#include "vend.h"

#include "shard_parse_vcc_enums.h"
#include "shard_hash.h"

/*
 * XXX use the crc32 from libvgz, but declare it here to avoid an include
 * dependency nightmare (at least for now)
 */

unsigned long crc32(unsigned long, const unsigned char *buf, unsigned len);

static uint32_t __match_proto__(hash_func)
shard_hash_crc32(VCL_STRING s)
{
	uint32_t crc;
	crc = crc32(~0U, (const unsigned char *)s, strlen(s));
	crc ^= ~0U;
	return (crc);
}

static uint32_t __match_proto__(hash_func)
shard_hash_sha256(VCL_STRING s)
{
	struct SHA256Context sha256;
	union {
		unsigned char digest[32];
		uint32_t uint32_digest[8];
	} sha256_digest;
	uint32_t r;

	SHA256_Init(&sha256);
	SHA256_Update(&sha256, s, strlen(s));
	SHA256_Final(sha256_digest.digest, &sha256);

	/*
	 * use low 32 bits only
	 * XXX: Are these the best bits to pick?
	 */
	vle32enc(&r, sha256_digest.uint32_digest[7]);
	return (r);
}

static uint32_t __match_proto__(hash_func)
shard_hash_rs(VCL_STRING s)
{
	uint32_t res = 0;
	/* hash function from Robert Sedgwicks 'Algorithms in C' book */
	const uint32_t b    = 378551;
	uint32_t a          = 63689;

	while (*s) {
		res = res * a + (*s++);
		a *= b;
	}

	return (res);
}

static uint32_t __match_proto__(hash_func)
_shard_hash_invalid(VCL_STRING s)
{
	(void) s;
	WRONG("invalid hash fp _ALG_E_ENVALID");
	NEEDLESS(return(0));
}

const hash_func shard_hash_f[_ALG_E_MAX] = {
	[_ALG_E_INVALID] = _shard_hash_invalid,
	[CRC32]	 = shard_hash_crc32,
	[SHA256]	 = shard_hash_sha256,
	[RS]		 = shard_hash_rs
};
