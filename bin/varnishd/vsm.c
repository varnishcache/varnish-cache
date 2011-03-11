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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "miniobj.h"
#include "libvarnish.h"
#include "common.h"
#include "vsm.h"
#include "vmb.h"

struct vsm_head		*vsm_head;
const struct vsm_chunk	*vsm_end;

static unsigned
vsm_mark(void)
{
	unsigned seq;

	seq = vsm_head->alloc_seq;
	vsm_head->alloc_seq = 0;
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
		vsm_head->alloc_seq = ++seq;
	while (vsm_head->alloc_seq == 0);
}

/*--------------------------------------------------------------------*/

struct vsm_chunk *
vsm_iter_0(void)
{

	CHECK_OBJ_NOTNULL(vsm_head, VSM_HEAD_MAGIC);
	CHECK_OBJ_NOTNULL(&vsm_head->head, VSM_CHUNK_MAGIC);
	return (&vsm_head->head);
}

void
vsm_iter_n(struct vsm_chunk **pp)
{

	CHECK_OBJ_NOTNULL(vsm_head, VSM_HEAD_MAGIC);
	CHECK_OBJ_NOTNULL(*pp, VSM_CHUNK_MAGIC);
	*pp = VSM_NEXT(*pp);
	if (*pp >= vsm_end) {
		*pp = NULL;
		return;
	}
	CHECK_OBJ_NOTNULL(*pp, VSM_CHUNK_MAGIC);
}

/*--------------------------------------------------------------------*/

static void
vsm_cleanup(void)
{
	unsigned now = (unsigned)TIM_mono();
	struct vsm_chunk *sha, *sha2;
	unsigned seq;

	CHECK_OBJ_NOTNULL(vsm_head, VSM_HEAD_MAGIC);
	VSM_ITER(sha) {
		if (strcmp(sha->class, VSM_CLASS_COOL))
			continue;
		if (sha->state + VSM_COOL_TIME < now)
			break;
	}
	if (sha == NULL)
		return;
	seq = vsm_mark();
	/* First pass, free, and collaps with next if applicable */
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
VSM_Alloc(unsigned size, const char *class, const char *type, const char *ident)
{
	struct vsm_chunk *sha, *sha2;
	unsigned seq;

	CHECK_OBJ_NOTNULL(vsm_head, VSM_HEAD_MAGIC);

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
VSM_Free(const void *ptr)
{
	struct vsm_chunk *sha;
	unsigned seq;

	CHECK_OBJ_NOTNULL(vsm_head, VSM_HEAD_MAGIC);
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
VSM_Clean(void)
{
	struct vsm_chunk *sha;
	unsigned f, seq;

	CHECK_OBJ_NOTNULL(vsm_head, VSM_HEAD_MAGIC);
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
			VSM_Free(VSM_PTR(sha));
	}
	vsm_release(seq);
}
