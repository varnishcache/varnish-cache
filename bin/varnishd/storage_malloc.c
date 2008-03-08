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
 * Storage method based on malloc(3)
 */

#include "config.h"

#include <sys/types.h>

#include <stdlib.h>

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"

struct sma {
	struct storage		s;
};

static struct storage *
sma_alloc(struct stevedore *st, size_t size)
{
	struct sma *sma;

	VSL_stats->sm_nreq++;
	sma = calloc(sizeof *sma, 1);
	if (sma == NULL)
		return (NULL);
	sma->s.priv = sma;
	sma->s.ptr = malloc(size);
	XXXAN(sma->s.ptr);
	sma->s.len = 0;
	sma->s.space = size;
	sma->s.fd = -1;
	sma->s.stevedore = st;
	sma->s.magic = STORAGE_MAGIC;
	VSL_stats->sm_nobj++;
	VSL_stats->sm_balloc += sma->s.space;
	return (&sma->s);
}

static void
sma_free(const struct storage *s)
{
	struct sma *sma;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	sma = s->priv;
	VSL_stats->sm_nobj--;
	VSL_stats->sm_balloc -= sma->s.space;
	free(sma->s.ptr);
	free(sma);
}

static void
sma_trim(const struct storage *s, size_t size)
{
	struct sma *sma;
	void *p;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	sma = s->priv;
	if ((p = realloc(sma->s.ptr, size)) != NULL) {
		VSL_stats->sm_balloc -= sma->s.space;
		sma->s.ptr = p;
		sma->s.space = size;
		VSL_stats->sm_balloc += sma->s.space;
	}
}

struct stevedore sma_stevedore = {
	.name =		"malloc",
	.alloc =	sma_alloc,
	.free =		sma_free,
	.trim =		sma_trim,
};
