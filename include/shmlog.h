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

struct shmloghead {
#define SHMLOGHEAD_MAGIC	4185512499U	/* From /dev/random */
	unsigned		magic;

	unsigned		hdrsize;

	time_t			starttime;
	pid_t			master_pid;
	pid_t			child_pid;

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

	/* Panic message buffer */
	char			panicstr[64 * 1024];
};

/*
 * Record format is as follows:
 *
 *	1 byte		field type (enum shmlogtag)
 *	2 bytes		length of contents
 *	4 bytes		record identifier
 *	n bytes		field contents (isgraph(c) || isspace(c)) allowed.
 */

#define SHMLOG_TAG	0
#define __SHMLOG_LEN_HIGH	1
#define __SHMLOG_LEN_LOW	2
#define __SHMLOG_ID_HIGH	3
#define __SHMLOG_ID_MEDHIGH	4
#define __SHMLOG_ID_MEDLOW	5
#define __SHMLOG_ID_LOW		6
#define SHMLOG_DATA		7
#define SHMLOG_NEXTTAG		8	/* ... + len */

#define SHMLOG_LEN(p)	(((p)[__SHMLOG_LEN_HIGH] << 8) | (p)[__SHMLOG_LEN_LOW])
#define SHMLOG_ID(p)	( \
	((p)[__SHMLOG_ID_HIGH] << 24) | \
	((p)[__SHMLOG_ID_MEDHIGH] << 16) | \
	((p)[__SHMLOG_ID_MEDLOW] << 8) | \
	 (p)[__SHMLOG_ID_LOW])

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

/* This function lives in both libvarnish and libvarnishapi */
int vin_n_arg(const char *n_arg, char **name, char **dir, char **vsl);
char *vin_L_arg(unsigned L_arg);
#define VIN_L_LOW	1024
#define VIN_L_HIGH	65000
#define VIN_L_OK(a)	(a >= VIN_L_LOW && a <= VIN_L_HIGH)
#define VIN_L_MSG	"-L argument must be [1024...65000]"

#endif
