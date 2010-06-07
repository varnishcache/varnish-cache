/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * $Id$
 *
 * Define the layout of the shared memory log segment.
 *
 * NB: THIS IS NOT A PUBLIC API TO VARNISH!
 */

#ifndef SHMLOG_H_INCLUDED
#define SHMLOG_H_INCLUDED

#define SHMLOG_FILENAME		"_.vsl"

#include <time.h>
#include <sys/types.h>

#include "stats.h"

/*
 * This structure describes each allocation from the shmlog
 */

struct shmalloc {
#define SHMALLOC_MAGIC		0x43907b6e	/* From /dev/random */
	unsigned		magic;
	unsigned		len;
	char			class[8];
	char			type[8];
	char			ident[16];
};

#define SHA_NEXT(sha)		((void*)((uintptr_t)(sha) + (sha)->len))
#define SHA_PTR(sha)		((void*)((uintptr_t)((sha) + 1)))

struct shmloghead {
#define SHMLOGHEAD_MAGIC	4185512502U	/* From /dev/random */
	unsigned		magic;

	unsigned		hdrsize;

	time_t			starttime;
	pid_t			master_pid;
	pid_t			child_pid;

	unsigned		shm_size;

	/* Panic message buffer */
	char			panicstr[64 * 1024];

	unsigned		alloc_seq;
	/* Must be last element */
	struct shmalloc		head;
};

#define VSL_CLASS_LOG		"Log"
#define VSL_CLASS_STAT		"Stat"

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

#define VSL_WORDS(len) (((len) + 3) / 4)
#define VSL_END(ptr, len) ((ptr) + 2 + VSL_WORDS(len))
#define VSL_NEXT(ptr) VSL_END(ptr, VSL_LEN(ptr))
#define VSL_LEN(ptr) ((ptr)[0] & 0xffff)
#define VSL_TAG(ptr) ((ptr)[0] >> 24)
#define VSL_ID(ptr) ((ptr)[1])
#define VSL_DATA(ptr) ((char*)((ptr)+2))

/*
 * The identifiers in shmlogtag are "SLT_" + XML tag.  A script may be run
 * on this file to extract the table rather than handcode it
 */
enum shmlogtag {
	SLT_ENDMARKER = 0,
#define SLTM(foo)	SLT_##foo,
#include "shmlog_tags.h"
#undef SLTM
	SLT_WRAPMARKER = 255U
};

/* This function lives in both libvarnish and libvarnishapi */
int vin_n_arg(const char *n_arg, char **name, char **dir, char **vsl);
char *vin_L_arg(unsigned L_arg);
#define VIN_L_LOW	1024
#define VIN_L_HIGH	65000
#define VIN_L_OK(a)	(a >= VIN_L_LOW && a <= VIN_L_HIGH)
#define VIN_L_MSG	"-L argument must be [1024...65000]"

#endif
