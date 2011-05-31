/*-
 * Copyright (c) 2010 Redpill Linpro AS
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
 * VSM stuff common to manager and child.
 *
 * We have three potential conflicts we need to lock against here:
 *
 * VSM-studying programs (varnishstat...) vs. everybody else
 *	The VSM studying programs only have read-only access to the VSM
 *	so everybody else must use memory barriers, stable storage and
 *	similar tricks to keep the VSM image in sync (long enough) for
 *	the studying programs.
 *
 * Manager process vs child process.
 *	Will only muck about in VSM when child process is not running
 *	Responsible for cleaning up any mess left behind by dying child.
 *
 * Child process threads
 *	Pthread locking necessary.
 *
 * XXX: not all of this is in place yet.
 */

#include "config.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "miniobj.h"
#include "libvarnish.h"
#include "common.h"
#include "vsm.h"
#include "vmb.h"

/* These two come from beyond (mgt_shmem.c actually) */
struct VSM_head		*VSM_head;
const struct VSM_chunk	*vsm_end;

static unsigned
vsm_mark(void)
{
	unsigned seq;

	seq = VSM_head->alloc_seq;
	VSM_head->alloc_seq = 0;
	VWMB();
	return (seq);
}

static void
vsm_release(unsigned seq)
{

	if (seq == 0)
		return;
	VWMB();
	do
		VSM_head->alloc_seq = ++seq;
	while (VSM_head->alloc_seq == 0);
}

/*--------------------------------------------------------------------*/

static void
vsm_cleanup(void)
{
	unsigned now = (unsigned)TIM_mono();
	struct VSM_chunk *sha, *sha2;
	unsigned seq;

	CHECK_OBJ_NOTNULL(VSM_head, VSM_HEAD_MAGIC);
	VSM_ITER(sha) {
		if (strcmp(sha->class, VSM_CLASS_COOL))
			continue;
		if (sha->state + VSM_COOL_TIME < now)
			break;
	}
	if (sha == NULL)
		return;
	seq = vsm_mark();
	/* First pass, free, and collapse with next if applicable */
	VSM_ITER(sha) {
		if (strcmp(sha->class, VSM_CLASS_COOL))
			continue;
		if (sha->state + VSM_COOL_TIME >= now)
			continue;

		bprintf(sha->class, "%s", VSM_CLASS_FREE);
		bprintf(sha->type, "%s", "");
		bprintf(sha->ident, "%s", "");
		sha2 = VSM_NEXT(sha);
		assert(sha2 <= vsm_end);
		if (sha2 == vsm_end)
			break;
		CHECK_OBJ_NOTNULL(sha2, VSM_CHUNK_MAGIC);
		if (!strcmp(sha2->class, VSM_CLASS_FREE)) {
			sha->len += sha2->len;
			memset(sha2, 0, sizeof *sha2);
		}
		sha->state = 0;
	}
	/* Second pass, collaps with prev if applicable */
	VSM_ITER(sha) {
		if (strcmp(sha->class, VSM_CLASS_FREE))
			continue;
		sha2 = VSM_NEXT(sha);
		assert(sha2 <= vsm_end);
		if (sha2 == vsm_end)
			break;
		CHECK_OBJ_NOTNULL(sha2, VSM_CHUNK_MAGIC);
		if (!strcmp(sha2->class, VSM_CLASS_FREE)) {
			sha->len += sha2->len;
			memset(sha2, 0, sizeof *sha2);
		}
	}
	vsm_release(seq);
}

/*--------------------------------------------------------------------*/

void *
VSM__Alloc(unsigned size, const char *class, const char *type, const char *ident)
{
	struct VSM_chunk *sha, *sha2;
	unsigned seq;

	CHECK_OBJ_NOTNULL(VSM_head, VSM_HEAD_MAGIC);

	vsm_cleanup();

	/* Round up to pointersize */
	size = RUP2(size, sizeof(void*));

	size += sizeof *sha;		/* Make space for the header */

	VSM_ITER(sha) {
		CHECK_OBJ_NOTNULL(sha, VSM_CHUNK_MAGIC);

		if (strcmp(sha->class, VSM_CLASS_FREE))
			continue;

		if (size > sha->len)
			continue;

		/* Mark as inconsistent while we write string fields */
		seq = vsm_mark();

		if (size + sizeof (*sha) < sha->len) {
			sha2 = (void*)((uintptr_t)sha + size);

			memset(sha2, 0, sizeof *sha2);
			sha2->magic = VSM_CHUNK_MAGIC;
			sha2->len = sha->len - size;
			bprintf(sha2->class, "%s", VSM_CLASS_FREE);
			sha->len = size;
		}

		bprintf(sha->class, "%s", class);
		bprintf(sha->type, "%s", type);
		bprintf(sha->ident, "%s", ident);

		vsm_release(seq);
		return (VSM_PTR(sha));
	}
	return (NULL);
}

/*--------------------------------------------------------------------*/

void
VSM__Free(const void *ptr)
{
	struct VSM_chunk *sha;
	unsigned seq;

	CHECK_OBJ_NOTNULL(VSM_head, VSM_HEAD_MAGIC);
	VSM_ITER(sha)
		if (VSM_PTR(sha) == ptr)
			break;
	AN(sha);
	seq = vsm_mark();
	bprintf(sha->class, "%s", VSM_CLASS_COOL);
	sha->state = (unsigned)TIM_mono();
	vsm_release(seq);
}

/*--------------------------------------------------------------------
 * Free all allocations after the mark (ie: allocated by child).
 */

void
VSM__Clean(void)
{
	struct VSM_chunk *sha;
	unsigned f, seq;

	CHECK_OBJ_NOTNULL(VSM_head, VSM_HEAD_MAGIC);
	f = 0;
	seq = vsm_mark();
	VSM_ITER(sha) {
		if (f == 0 && !strcmp(sha->class, VSM_CLASS_MARK)) {
			f = 1;
			continue;
		}
		if (f == 0)
			continue;
		if (strcmp(sha->class, VSM_CLASS_FREE) &&
		    strcmp(sha->class, VSM_CLASS_COOL))
			VSM__Free(VSM_PTR(sha));
	}
	vsm_release(seq);
}
