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
 * A classic bucketed hash
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "shmlog.h"
#include "cache.h"
#include "hash_slinger.h"

/*--------------------------------------------------------------------*/

struct hcl_hd {
	unsigned		magic;
#define HCL_HEAD_MAGIC		0x0f327016
	VTAILQ_HEAD(, objhead)	head;
	struct lock		mtx;
};

static unsigned			hcl_nhash = 16383;
static struct hcl_hd		*hcl_head;

/*--------------------------------------------------------------------
 * The ->init method allows the management process to pass arguments
 */

static void
hcl_init(int ac, char * const *av)
{
	int i;
	unsigned u;

	if (ac == 0)
		return;
	if (ac > 1)
		ARGV_ERR("(-hclassic) too many arguments\n");
	i = sscanf(av[0], "%u", &u);
	if (i <= 0 || u == 0)
		return;
	if (u > 2 && !(u & (u - 1))) {
		fprintf(stderr,
		    "NOTE:\n"
		    "\tA power of two number of hash buckets is "
		    "marginally less efficient\n"
		    "\twith systematic URLs.  Reducing by one"
		    " hash bucket.\n");
		u--;
	}
	hcl_nhash = u;
	fprintf(stderr, "Classic hash: %u buckets\n", hcl_nhash);
	return;
}

/*--------------------------------------------------------------------
 * The ->start method is called during cache process start and allows
 * initialization to happen before the first lookup.
 */

static void
hcl_start(void)
{
	unsigned u;

	hcl_head = calloc(sizeof *hcl_head, hcl_nhash);
	XXXAN(hcl_head);

	for (u = 0; u < hcl_nhash; u++) {
		VTAILQ_INIT(&hcl_head[u].head);
		Lck_New(&hcl_head[u].mtx);
		hcl_head[u].magic = HCL_HEAD_MAGIC;
	}
}

/*--------------------------------------------------------------------
 * Lookup and possibly insert element.
 * If nobj != NULL and the lookup does not find key, nobj is inserted.
 * If nobj == NULL and the lookup does not find key, NULL is returned.
 * A reference to the returned object is held.
 * We use a two-pass algorithm to handle inserts as they are quite
 * rare and collisions even rarer.
 */

static struct objhead *
hcl_lookup(const struct sess *sp, struct objhead *noh)
{
	struct objhead *oh;
	struct hcl_hd *hp;
	unsigned u1, digest;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(noh, OBJHEAD_MAGIC);

	assert(sizeof noh->digest > sizeof digest);
	memcpy(&digest, noh->digest, sizeof digest);
	u1 = digest % hcl_nhash;
	hp = &hcl_head[u1];

	Lck_Lock(&hp->mtx);
	VTAILQ_FOREACH(oh, &hp->head, hoh_list) {
		i = memcmp(oh->digest, noh->digest, sizeof oh->digest);
		if (i < 0)
			continue;
		if (i > 0)
			break;
		oh->refcnt++;
		Lck_Unlock(&hp->mtx);
		return (oh);
	}

	if (oh != NULL)
		VTAILQ_INSERT_BEFORE(oh, noh, hoh_list);
	else
		VTAILQ_INSERT_TAIL(&hp->head, noh, hoh_list);

	noh->hoh_head = hp;

	Lck_Unlock(&hp->mtx);
	return (noh);
}

/*--------------------------------------------------------------------
 * Dereference and if no references are left, free.
 */

static int
hcl_deref(struct objhead *oh)
{
	struct hcl_hd *hp;
	int ret;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	CAST_OBJ_NOTNULL(hp, oh->hoh_head, HCL_HEAD_MAGIC);
	assert(oh->refcnt > 0);
	Lck_Lock(&hp->mtx);
	if (--oh->refcnt == 0) {
		VTAILQ_REMOVE(&hp->head, oh, hoh_list);
		ret = 0;
	} else
		ret = 1;
	Lck_Unlock(&hp->mtx);
	return (ret);
}

/*--------------------------------------------------------------------*/

const struct hash_slinger hcl_slinger = {
	.magic	=	SLINGER_MAGIC,
	.name	=	"classic",
	.init	=	hcl_init,
	.start	=	hcl_start,
	.lookup =	hcl_lookup,
	.deref	=	hcl_deref,
};
