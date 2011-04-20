/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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

#ifndef SHMLOG_H_INCLUDED
#define SHMLOG_H_INCLUDED

#define VSL_CLASS		"Log"

/*
 * Shared memory log format
 *
 * The log is structured as an array of 32bit unsigned integers.
 *
 * The first integer contains a non-zero serial number, which changes
 * whenever writing the log starts from the front.
 *
 * Each logrecord consist of:
 *	[n]		= ((type & 0xff) << 24) | (length & 0xffff)
 *	[n + 1]		= identifier
 *	[n + 2] ... [m]	= content
 */

#define VSL_WORDS(len)		(((len) + 3) / 4)
#define VSL_END(ptr, len)	((ptr) + 2 + VSL_WORDS(len))
#define VSL_NEXT(ptr)		VSL_END(ptr, VSL_LEN(ptr))
#define VSL_LEN(ptr)		((ptr)[0] & 0xffff)
#define VSL_TAG(ptr)		((ptr)[0] >> 24)
#define VSL_ID(ptr)		((ptr)[1])
#define VSL_DATA(ptr)		((char*)((ptr)+2))

#define VSL_ENDMARKER	(((uint32_t)SLT_Reserved << 24) | 0x454545) /* "EEE" */
#define VSL_WRAPMARKER	(((uint32_t)SLT_Reserved << 24) | 0x575757) /* "WWW" */

/*
 * The identifiers in shmlogtag are "SLT_" + XML tag.  A script may be run
 * on this file to extract the table rather than handcode it
 */
enum vsl_tag {
#define SLTM(foo)	SLT_##foo,
#include "vsl_tags.h"
#undef SLTM
	SLT_Reserved = 255
};

#endif
