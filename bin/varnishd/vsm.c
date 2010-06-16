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
void			*vsm_end;

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
	if ((void*)(*pp) >= vsm_end) {
		*pp = NULL;
		return;
	}
	CHECK_OBJ_NOTNULL(*pp, VSM_CHUNK_MAGIC);
}

/*--------------------------------------------------------------------*/

void *
VSM_Alloc(unsigned size, const char *class, const char *type, const char *ident)
{
	struct vsm_chunk *sha, *sha2;
	unsigned seq;

	CHECK_OBJ_NOTNULL(vsm_head, VSM_HEAD_MAGIC);

	/* Round up to pointersize */
	size += sizeof(void *) - 1;
	size &= ~(sizeof(void *) - 1);

	size += sizeof *sha;		/* Make space for the header */

	VSM_ITER(sha) {
		CHECK_OBJ_NOTNULL(sha, VSM_CHUNK_MAGIC);

		if (strcmp(sha->class, "Free"))
			continue;

		xxxassert(size <= sha->len);

		sha2 = (void*)((uintptr_t)sha + size);

		/* Mark as inconsistent while we write string fields */
		seq = vsm_head->alloc_seq;
		vsm_head->alloc_seq = 0;
		VWMB();

		memset(sha2, 0, sizeof *sha2);
		sha2->magic = VSM_CHUNK_MAGIC;
		sha2->len = sha->len - size;
		bprintf(sha2->class, "%s", "Free");

		sha->len = size;
		bprintf(sha->class, "%s", class);
		bprintf(sha->type, "%s", type);
		bprintf(sha->ident, "%s", ident);

		VWMB();
		if (seq != 0)
			do
				loghead->alloc_seq = ++seq;
			while (loghead->alloc_seq == 0);

		return (VSM_PTR(sha));
	}
	return (NULL);
}
