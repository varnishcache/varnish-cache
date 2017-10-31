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
 * $FreeBSD: head/lib/libmd/sha256.h 154479 2006-01-17 15:35:57Z phk $
 */

#ifndef _VSHA256_H_
#define _VSHA256_H_

#define VSHA256_LEN		32
#define VSHA256_DIGEST_LENGTH	32

typedef struct VSHA256Context {
	uint32_t state[8];
	uint64_t count;
	unsigned char buf[64];
} VSHA256_CTX;

void	VSHA256_Init(VSHA256_CTX *);
void	VSHA256_Update(VSHA256_CTX *, const void *, size_t);
void	VSHA256_Final(unsigned char [VSHA256_LEN], VSHA256_CTX *);
void	VSHA256_Test(void);

#define SHA256_LEN		VSHA256_LEN
#define SHA256_DIGEST_LENGTH	VSHA256_DIGEST_LENGTH
#define SHA256Context		VSHA256Context
#define SHA256_CTX		VSHA256_CTX
#define SHA256_Init		VSHA256_Init
#define SHA256_Update		VSHA256_Update
#define SHA256_Final		VSHA256_Final
#define SHA256_Test		VSHA256_Test

#endif /* !_VSHA256_H_ */
