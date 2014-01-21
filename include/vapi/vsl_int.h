/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * NB: THIS IS NOT A PUBLIC API TO VARNISH!
 */

#ifndef VAPI_VSL_FMT_H_INCLUDED
#define VAPI_VSL_FMT_H_INCLUDED

#include "vapi/vsm_int.h"

#define VSL_CLASS		"Log"
#define VSL_SEGMENTS		8

/*
 * Shared memory log format
 *
 * The segments array has index values providing safe entry points into
 * the log, where each element N gives the index of the first log record
 * in the Nth fraction of the log. An index value of -1 indicated that no
 * log records in this fraction exists.
 *
 * The segment member shows the current segment where Varnish is currently
 * appending log data.
 *
 * The seq member contains a non-zero seq number randomly initialized,
 * which increases whenever writing the log starts from the front.
 *
 * The log member points to an array of 32bit unsigned integers containing
 * log records.
 *
 * Each logrecord consist of:
 *	[n]		= ((type & 0xff) << 24) | (length & 0xffff)
 *	[n + 1]		= ((marker & 0x03) << 30) | (identifier & 0x3fffffff)
 *	[n + 2] ... [m]	= content (NUL-terminated)
 *
 * Logrecords are NUL-terminated so that string functions can be run
 * directly on the shmlog data.
 *
 * Notice that the constants in these macros cannot be changed without
 * changing corresponding magic numbers in varnishd/cache/cache_shmlog.c
 */

struct VSL_head {
#define VSL_HEAD_MARKER		"VSLHEAD0"	/* Incr. as version# */
	char			marker[VSM_MARKER_LEN];
	volatile ssize_t	segments[VSL_SEGMENTS];
	volatile unsigned	segment;	/* Current varnishd segment */
	volatile unsigned	seq;		/* Non-zero seq number */
	uint32_t		log[];
};

#define VSL_CLIENTMARKER	(1U<<30)
#define VSL_BACKENDMARKER	(1U<<31)
#define VSL_IDENTMASK		(~(3U<<30))

#define VSL_LENMASK		0xffff
#define VSL_WORDS(len)		(((len) + 3) / 4)
#define VSL_BYTES(words)	((words) * 4)
#define VSL_END(ptr, len)	((ptr) + 2 + VSL_WORDS(len))
#define VSL_NEXT(ptr)		VSL_END(ptr, VSL_LEN(ptr))
#define VSL_LEN(ptr)		((ptr)[0] & VSL_LENMASK)
#define VSL_TAG(ptr)		((ptr)[0] >> 24)
#define VSL_ID(ptr)		(((ptr)[1]) & VSL_IDENTMASK)
#define VSL_CLIENT(ptr)		(((ptr)[1]) & VSL_CLIENTMARKER)
#define VSL_BACKEND(ptr)	(((ptr)[1]) & VSL_BACKENDMARKER)
#define VSL_DATA(ptr)		((char*)((ptr)+2))
#define VSL_CDATA(ptr)		((const char*)((ptr)+2))
#define VSL_BATCHLEN(ptr)	((ptr)[1])
#define VSL_BATCHID(ptr)	(VSL_ID((ptr) + 2))

#define VSL_ENDMARKER	(((uint32_t)SLT__Reserved << 24) | 0x454545) /* "EEE" */
#define VSL_WRAPMARKER	(((uint32_t)SLT__Reserved << 24) | 0x575757) /* "WWW" */

/*
 * The identifiers in shmlogtag are "SLT_" + XML tag.  A script may be run
 * on this file to extract the table rather than handcode it
 */
#define SLT__MAX 256
enum VSL_tag_e {
	SLT__Bogus = 0,
#define SLTM(foo,flags,sdesc,ldesc)	SLT_##foo,
#include "tbl/vsl_tags.h"
#undef SLTM
	SLT__Reserved = 254,
	SLT__Batch = 255
};

/* VSL tag flags */
#define SLT_F_UNUSED		(1 << 0)
#define SLT_F_BINARY		(1 << 1)

#endif /* VAPI_VSL_FMT_H_INCLUDED */
