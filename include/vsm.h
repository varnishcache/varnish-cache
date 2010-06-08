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

#ifndef VSM_H_INCLUDED
#define VSM_H_INCLUDED

#define VSM_FILENAME		"_.vsm"

#include <time.h>
#include <sys/types.h>

/*
 * This structure describes each allocation from the shmlog
 */

struct vsm_chunk {
#define VSM_CHUNK_MAGIC		0x43907b6e	/* From /dev/random */
	unsigned		magic;
	unsigned		len;
	char			class[8];
	char			type[8];
	char			ident[16];
};

#define VSM_NEXT(sha)		((void*)((uintptr_t)(sha) + (sha)->len))
#define VSM_PTR(sha)		((void*)((uintptr_t)((sha) + 1)))

struct vsm_head {
#define VSM_HEAD_MAGIC		4185512502U	/* From /dev/random */
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
	struct vsm_chunk	head;
};

#endif
