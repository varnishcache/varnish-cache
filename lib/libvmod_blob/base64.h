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

#include "vmod_blob.h"

#define ILL ((int8_t) 127)
#define PAD ((int8_t) 126)

static const struct b64_alphabet {
	const char b64[64];
	const int8_t i64[256];
	const int padding;
} b64_alphabet[] = {
	[BASE64] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789+/",
		{
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL,  62, ILL, ILL, ILL,  63, /* +, -    */
			 52,  53,  54,  55,  56,  57,  58,  59, /* 0 - 7   */
			 60,  61, ILL, ILL, ILL, PAD, ILL, ILL, /* 8, 9, = */
			ILL,   0,   1,   2,   3,   4,   5,   6, /* A - G   */
			  7,   8,   9,  10,  11,  12,  13,  14, /* H - O   */
			 15,  16,  17,  18,  19,  20,  21,  22, /* P - W   */
			 23,  24,  25, ILL, ILL, ILL, ILL, ILL, /* X, Y, Z */
			ILL,  26,  27,  28,  29,  30,  31,  32, /* a - g   */
			 33,  34,  35,  36,  37,  38,  39,  40, /* h - o   */
			 41,  42,  43,  44,  45,  46,  47,  48, /* p - w   */
			 49,  50,  51, ILL, ILL, ILL, ILL, ILL, /* x, y, z */
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
		},
		'='
	},
	[BASE64URL] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789-_",
		{
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL,  62, ILL, ILL, /* -       */
			 52,  53,  54,  55,  56,  57,  58,  59, /* 0 - 7   */
			 60,  61, ILL, ILL, ILL, PAD, ILL, ILL, /* 8, 9, = */
			ILL,   0,   1,   2,   3,   4,   5,   6, /* A - G   */
			  7,   8,   9,  10,  11,  12,  13,  14, /* H - O   */
			 15,  16,  17,  18,  19,  20,  21,  22, /* P - W   */
			 23,  24,  25, ILL, ILL, ILL, ILL,  63, /* X-Z, _  */
			ILL,  26,  27,  28,  29,  30,  31,  32, /* a - g   */
			 33,  34,  35,  36,  37,  38,  39,  40, /* h - o   */
			 41,  42,  43,  44,  45,  46,  47,  48, /* p - w   */
			 49,  50,  51, ILL, ILL, ILL, ILL, ILL, /* x, y, z */
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
		},
		'='
	},
	[BASE64URLNOPAD] = {
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
		"ghijklmnopqrstuvwxyz0123456789-_",
		{
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL,  62, ILL, ILL, /* -       */
			 52,  53,  54,  55,  56,  57,  58,  59, /* 0 - 7   */
			 60,  61, ILL, ILL, ILL, ILL, ILL, ILL, /* 8, 9    */
			ILL,   0,   1,   2,   3,   4,   5,   6, /* A - G   */
			  7,   8,   9,  10,  11,  12,  13,  14, /* H - O   */
			 15,  16,  17,  18,  19,  20,  21,  22, /* P - W   */
			 23,  24,  25, ILL, ILL, ILL, ILL,  63, /* X-Z, _  */
			ILL,  26,  27,  28,  29,  30,  31,  32, /* a - g   */
			 33,  34,  35,  36,  37,  38,  39,  40, /* h - o   */
			 41,  42,  43,  44,  45,  46,  47,  48, /* p - w   */
			 49,  50,  51, ILL, ILL, ILL, ILL, ILL, /* x, y, z */
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
			ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
		},
		0
	},
};
