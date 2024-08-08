/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 *
 * Define the layout of the shared memory log segment.
 *
 * This file SHALL only be included from:
 *	bin/varnishd/cache/cache.h
 *	include/vsl_priv.h
 *	include/vapi/vsl.h
 *
 */

#ifndef VAPI_VSL_INT_H_INCLUDED
#define VAPI_VSL_INT_H_INCLUDED

#define VSL_CLASS		"Log"
#define VSL_SEGMENTS		8U	// power of two

/*
 * Shared memory log format
 *
 * The log member points to an array of 32bit unsigned integers containing
 * log records.
 *
 * Each logrecord consist of four or more 32 bit words, stored in
 * native endianness:
 *
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |     TAG       |  unused   |ver|          length               |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                   32 lower bits of VXID                       |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |B|C|    unused (zero)    |        19 upper bits of VXID        |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   | content ... (NUL-terminated)                                  |
 *   +-+-+                                                       +-+-+
 *   | ...							     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Logrecords are NUL-terminated so that string functions can be run
 * directly on the shmlog data.
 *
 * Notice that the constants in these macros cannot be changed without
 * changing corresponding magic numbers in varnishd/cache/cache_shmlog.c
 */

#define VSL_CLIENTMARKER	(1ULL<<62)
#define VSL_BACKENDMARKER	(1ULL<<63)
#define VSL_IDENTMASK		((1ULL<<51)-1)

#define VSL_LENMASK		0xffff
#define VSL_VERMASK		0x3
#define VSL_VERSHIFT		16
#define VSL_IDMASK		0xff
#define VSL_IDSHIFT		24
#define VSL_OVERHEAD		3
#define VSL_VERSION_2		0x0
#define VSL_VERSION_3		0x1
#define VSL_WORDS(len)		(((len) + 3) / 4)
#define VSL_BYTES(words)	((words) * 4)
#define VSL_END(ptr, len)	((ptr) + VSL_OVERHEAD + VSL_WORDS(len))
#define VSL_NEXT(ptr)		VSL_END(ptr, VSL_LEN(ptr))
#define VSL_LEN(ptr)		((ptr)[0] & VSL_LENMASK)
#define VSL_VER(ptr)		(((ptr)[0] >> VSL_VERSHIFT) & VSL_VERMASK)
#define VSL_TAG(ptr)		((enum VSL_tag_e)((ptr)[0] >> VSL_IDSHIFT))
#define VSL_ID64(ptr)		(((uint64_t)((ptr)[2])<<32) | ((ptr)[1]))
#define VSL_ID(ptr)		(VSL_ID64(ptr) & VSL_IDENTMASK)
#define VSL_CLIENT(ptr)		(((ptr)[2]) & (VSL_CLIENTMARKER >> 32))
#define VSL_BACKEND(ptr)	(((ptr)[2]) & (VSL_BACKENDMARKER >> 32))
#define VSL_DATA(ptr)		((char*)((ptr)+VSL_OVERHEAD))
#define VSL_CDATA(ptr)		((const char*)((ptr)+VSL_OVERHEAD))
#define VSL_BATCHLEN(ptr)	((ptr)[1])
#define VSL_BATCHID(ptr)	(VSL_ID((ptr) + VSL_OVERHEAD))

#define VSL_ENDMARKER	(((uint32_t)SLT__Reserved << 24) | 0x454545) /* "EEE" */
#define VSL_WRAPMARKER	(((uint32_t)SLT__Reserved << 24) | 0x575757) /* "WWW" */

/*
 * The identifiers in shmlogtag are "SLT_" + VSL tag.  A script may be run
 * on this file to extract the table rather than handcode it
 */
#define SLT__MAX 256
enum VSL_tag_e {
	SLT__Bogus = 0,
#define SLTM(foo,flags,sdesc,ldesc)	SLT_##foo,
#include "tbl/vsl_tags.h"
	SLT__Reserved = 254,
	SLT__Batch = 255
};

/* VSL tag flags */
#define SLT_F_UNUSED		(1 << 0)
#define SLT_F_UNSAFE		(1 << 1)
#define SLT_F_BINARY		(1 << 2)

#endif /* VAPI_VSL_INT_H_INCLUDED */
