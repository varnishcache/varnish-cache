/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * This is the reference hash(/lookup) implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shmlog.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

struct hsl_entry {
	TAILQ_ENTRY(hsl_entry)	list;
	char			*key1;
	char			*key2;
	struct objhead		*obj;
	unsigned		refcnt;
};

static TAILQ_HEAD(, hsl_entry)	hsl_head = TAILQ_HEAD_INITIALIZER(hsl_head);
static MTX hsl_mutex;

/*--------------------------------------------------------------------
 * The ->init method is called during process start and allows
 * initialization to happen before the first lookup.
 */

static void
hsl_start(void)
{

	MTX_INIT(&hsl_mutex);
}

/*--------------------------------------------------------------------
 * Lookup and possibly insert element.
 * If nobj != NULL and the lookup does not find key, nobj is inserted.
 * If nobj == NULL and the lookup does not find key, NULL is returned.
 * A reference to the returned object is held.
 */

static struct objhead *
hsl_lookup(const char *key1, const char *key2, struct objhead *nobj)
{
	struct hsl_entry *he, *he2;
	int i;

	LOCK(&hsl_mutex);
	TAILQ_FOREACH(he, &hsl_head, list) {
		i = strcmp(key1, he->key1);
		if (i < 0)
			continue;
		if (i > 0)
			break;
		i = strcmp(key2, he->key2);
		if (i < 0)
			continue;
		if (i > 0)
			break;
		he->refcnt++;
		nobj = he->obj;
		nobj->hashpriv = he;
		UNLOCK(&hsl_mutex);
		return (nobj);
	}
	if (nobj == NULL) {
		UNLOCK(&hsl_mutex);
		return (NULL);
	}
	he2 = calloc(sizeof *he2, 1);
	XXXAN(he2);
	he2->obj = nobj;
	he2->refcnt = 1;
	he2->key1 = strdup(key1);
	XXXAN(he2->key1);
	he2->key2 = strdup(key2);
	XXXAN(he2->key2);
	nobj->hashpriv = he2;
	if (he != NULL)
		TAILQ_INSERT_BEFORE(he, he2, list);
	else
		TAILQ_INSERT_TAIL(&hsl_head, he2, list);
	UNLOCK(&hsl_mutex);
	return (nobj);
}

/*--------------------------------------------------------------------
 * Dereference and if no references are left, free.
 */

static int
hsl_deref(struct objhead *obj)
{
	struct hsl_entry *he;
	int ret;

	AN(obj->hashpriv);
	he = obj->hashpriv;
	LOCK(&hsl_mutex);
	if (--he->refcnt == 0) {
		free(he->key1);
		free(he->key2);
		TAILQ_REMOVE(&hsl_head, he, list);
		free(he);
		ret = 0;
	} else
		ret = 1;
	UNLOCK(&hsl_mutex);
	return (ret);
}

/*--------------------------------------------------------------------*/

struct hash_slinger hsl_slinger = {
	"simple_list",
	NULL,
	hsl_start,
	hsl_lookup,
	hsl_deref,
};
