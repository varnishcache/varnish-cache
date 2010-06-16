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

#include "miniobj.h"
#include "libvarnish.h"
#include "common.h"
#include "vsm.h"

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
