/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 *
 * $Id$
 *
 * Define the layout of the shared memory log segment.
 *
 * NB: THIS IS NOT A PUBLIC API TO VARNISH!
 *
 */

#define SHMLOG_FILENAME		"/tmp/_.vsl"

#include <time.h>

#include "stats.h"

struct shmloghead {
#define SHMLOGHEAD_MAGIC	4185512498U	/* From /dev/random */
	unsigned		magic;

	unsigned		hdrsize;

	time_t			starttime;

	/*
	 * Byte offset into the file where the fifolog starts
 	 * This allows the header to expand later.
	 */
	unsigned		start;

	/* Length of the fifolog area in bytes */
	unsigned		size;

	/* Current write position relative to the beginning of start */
	unsigned		ptr;

	struct varnish_stats	stats;
};

/*
 * Record format is as follows:
 *
 *	1 byte		field type (enum shmlogtag)
 *	1 byte		length of contents
 *	2 byte		record identifier
 *	n bytes		field contents (isgraph(c) || isspace(c)) allowed.
 */

/*
 * The identifiers in shmlogtag are "SLT_" + XML tag.  A script may be run
 * on this file to extract the table rather than handcode it
 */
enum shmlogtag {
	SLT_ENDMARKER = 0,
#define SLTM(foo)	SLT_##foo,
#include "shmlog_tags.h"
#undef SLTM
	SLT_WRAPMARKER = 255
};
