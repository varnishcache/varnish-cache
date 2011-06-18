/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Varnish Software AS
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
 * This is the reference hash(/lookup) implementation
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "hash_slinger.h"

/*--------------------------------------------------------------------*/

static VTAILQ_HEAD(, objhead)	hsl_head = VTAILQ_HEAD_INITIALIZER(hsl_head);
static struct lock hsl_mtx;

/*--------------------------------------------------------------------
 * The ->init method is called during process start and allows
 * initialization to happen before the first lookup.
 */

static void
hsl_start(void)
{

	Lck_New(&hsl_mtx, lck_hsl);
}

/*--------------------------------------------------------------------
 * Lookup and possibly insert element.
 * If nobj != NULL and the lookup does not find key, nobj is inserted.
 * If nobj == NULL and the lookup does not find key, NULL is returned.
 * A reference to the returned object is held.
 */

static struct objhead *
hsl_lookup(const struct sess *sp, struct objhead *noh)
{
	struct objhead *oh;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(noh, OBJHEAD_MAGIC);
	Lck_Lock(&hsl_mtx);
	VTAILQ_FOREACH(oh, &hsl_head, hoh_list) {
		i = memcmp(oh->digest, noh->digest, sizeof oh->digest);
		if (i < 0)
			continue;
		if (i > 0)
			break;
		oh->refcnt++;
		Lck_Unlock(&hsl_mtx);
		return (oh);
	}

	if (oh != NULL)
		VTAILQ_INSERT_BEFORE(oh, noh, hoh_list);
	else
		VTAILQ_INSERT_TAIL(&hsl_head, noh, hoh_list);

	Lck_Unlock(&hsl_mtx);
	return (noh);
}

/*--------------------------------------------------------------------
 * Dereference and if no references are left, free.
 */

static int
hsl_deref(struct objhead *oh)
{
	int ret;

	Lck_Lock(&hsl_mtx);
	if (--oh->refcnt == 0) {
		VTAILQ_REMOVE(&hsl_head, oh, hoh_list);
		ret = 0;
	} else
		ret = 1;
	Lck_Unlock(&hsl_mtx);
	return (ret);
}

/*--------------------------------------------------------------------*/

const struct hash_slinger hsl_slinger = {
	.magic	=	SLINGER_MAGIC,
	.name	=	"simple",
	.start	=	hsl_start,
	.lookup =	hsl_lookup,
	.deref	=	hsl_deref,
};
