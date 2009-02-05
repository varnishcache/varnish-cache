/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * A classic bucketed hash
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "shmlog.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

struct hcl_entry {
	unsigned		magic;
#define HCL_ENTRY_MAGIC		0x0ba707bf
	VTAILQ_ENTRY(hcl_entry)	list;
	struct hcl_hd		*head;
	struct objhead		*oh;
	unsigned		refcnt;
	unsigned		digest;
	unsigned		hash;
};

struct hcl_hd {
	unsigned		magic;
#define HCL_HEAD_MAGIC		0x0f327016
	VTAILQ_HEAD(, hcl_entry)	head;
	MTX			mtx;
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
		MTX_INIT(&hcl_head[u].mtx);
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
	struct objhead *roh;
	struct hcl_entry *he, *he2;
	struct hcl_hd *hp;
	unsigned u1, digest, r;
	unsigned u, v;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->http, HTTP_MAGIC);
	CHECK_OBJ_ORNULL(noh, OBJHEAD_MAGIC);

	digest = ~0U;
	for (u = 0; u < sp->ihashptr; u += 2) {
		v = pdiff(sp->hashptr[u], sp->hashptr[u + 1]);
		digest = crc32(digest, sp->hashptr[u], v);
	}
	digest ^= ~0U;

	u1 = digest % hcl_nhash;
	hp = &hcl_head[u1];
	he2 = NULL;

	for (r = 0; r < 2; r++ ) {
		LOCK(&hp->mtx);
		VTAILQ_FOREACH(he, &hp->head, list) {
			CHECK_OBJ_NOTNULL(he, HCL_ENTRY_MAGIC);
			if (sp->lhashptr < he->oh->hashlen)
				continue;
			if (sp->lhashptr > he->oh->hashlen)
				break;
			if (he->digest < digest)
				continue;
			if (he->digest > digest)
				break;
			i = HSH_Compare(sp, he->oh);
			if (i < 0)
				continue;
			if (i > 0)
				break;
			he->refcnt++;
			roh = he->oh;
			UNLOCK(&hp->mtx);
			/*
			 * If we loose the race, we need to clean up
			 * the work we did for our second attempt.
			 */
			if (he2 != NULL)
				free(he2);
			if (noh != NULL && noh->hash != NULL) {
				free(noh->hash);
				noh->hash = NULL;
			}
			return (roh);
		}
		if (noh == NULL) {
			UNLOCK(&hp->mtx);
			return (NULL);
		}
		if (he2 != NULL) {
			if (he != NULL)
				VTAILQ_INSERT_BEFORE(he, he2, list);
			else
				VTAILQ_INSERT_TAIL(&hp->head, he2, list);
			he2->refcnt++;
			noh = he2->oh;
			UNLOCK(&hp->mtx);
			return (noh);
		}
		UNLOCK(&hp->mtx);

		he2 = calloc(sizeof *he2, 1);
		XXXAN(he2);
		he2->magic = HCL_ENTRY_MAGIC;
		he2->oh = noh;
		he2->digest = digest;
		he2->hash = u1;
		he2->head = hp;

		noh->hashpriv = he2;
		AZ(noh->hash);
		noh->hash = malloc(sp->lhashptr);
		XXXAN(noh->hash);
		noh->hashlen = sp->lhashptr;
		HSH_Copy(sp, noh);
	}
	assert(he2 == NULL);		/* FlexeLint */
	INCOMPL();
}

/*--------------------------------------------------------------------
 * Dereference and if no references are left, free.
 */

static int
hcl_deref(const struct objhead *oh)
{
	struct hcl_entry *he;
	struct hcl_hd *hp;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	CAST_OBJ_NOTNULL(he, oh->hashpriv, HCL_ENTRY_MAGIC);
	hp = he->head;
	CHECK_OBJ_NOTNULL(hp, HCL_HEAD_MAGIC);
	assert(he->refcnt > 0);
	assert(he->hash < hcl_nhash);
	assert(hp == &hcl_head[he->hash]);
	LOCK(&hp->mtx);
	if (--he->refcnt == 0)
		VTAILQ_REMOVE(&hp->head, he, list);
	else
		he = NULL;
	UNLOCK(&hp->mtx);
	if (he == NULL)
		return (1);
	free(he);
	return (0);
}

/*--------------------------------------------------------------------*/

struct hash_slinger hcl_slinger = {
	.magic	=	SLINGER_MAGIC,
	.name	=	"classic",
	.init	=	hcl_init,
	.start	=	hcl_start,
	.lookup =	hcl_lookup,
	.deref	=	hcl_deref,
};
