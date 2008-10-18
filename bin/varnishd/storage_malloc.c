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

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"

static size_t			sma_max = SIZE_MAX;
static MTX			sma_mtx;

struct sma {
	struct storage		s;
	size_t			sz;
};

static struct storage *
sma_alloc(struct stevedore *st, size_t size)
{
	struct sma *sma;

	LOCK(&sma_mtx);
	VSL_stats->sma_nreq++;
	if (VSL_stats->sma_nbytes + size > sma_max)
		size = 0;
	else {
		VSL_stats->sma_nobj++;
		VSL_stats->sma_nbytes += size;
		VSL_stats->sma_balloc += size;
	}
	UNLOCK(&sma_mtx);

	if (size == 0)
		return (NULL);

	sma = calloc(sizeof *sma, 1);
	if (sma == NULL)
		return (NULL);
	sma->sz = size;
	sma->s.priv = sma;
	sma->s.ptr = malloc(size);
	XXXAN(sma->s.ptr);
	sma->s.len = 0;
	sma->s.space = size;
	sma->s.fd = -1;
	sma->s.stevedore = st;
	sma->s.magic = STORAGE_MAGIC;
	return (&sma->s);
}

/*lint -e{818} not const-able */
static void
sma_free(struct storage *s)
{
	struct sma *sma;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	sma = s->priv;
	assert(sma->sz == sma->s.space);
	LOCK(&sma_mtx);
	VSL_stats->sma_nobj--;
	VSL_stats->sma_nbytes -= sma->sz;
	VSL_stats->sma_bfree += sma->sz;
	UNLOCK(&sma_mtx);
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
	assert(sma->sz == sma->s.space);
	if ((p = realloc(sma->s.ptr, size)) != NULL) {
		LOCK(&sma_mtx);
		VSL_stats->sma_nbytes -= (sma->sz - size);
		VSL_stats->sma_bfree += sma->sz - size;
		sma->sz = size;
		UNLOCK(&sma_mtx);
		sma->s.ptr = p;
		sma->s.space = size;
	}
}

static void
sma_init(struct stevedore *parent, int ac, char * const *av)
{
	const char *e;
	uintmax_t u;

	(void)parent;

	AZ(av[ac]);
	if (ac > 1)
		ARGV_ERR("(-smalloc) too many arguments\n");

	if (ac == 0 || *av[0] == '\0')
		 return;

	e = str2bytes(av[0], &u, 0);
	if (e != NULL)
		ARGV_ERR("(-smalloc) size \"%s\": %s\n", av[0], e);
	if ((u != (uintmax_t)(size_t)u))
		ARGV_ERR("(-smalloc) size \"%s\": too big\n", av[0]);
	sma_max = u;
}

static void
sma_open(const struct stevedore *st)
{
	(void)st;
	AZ(pthread_mutex_init(&sma_mtx, NULL));
}

struct stevedore sma_stevedore = {
	.magic	=	STEVEDORE_MAGIC,
	.name	=	"malloc",
	.init	=	sma_init,
	.open	=	sma_open,
	.alloc	=	sma_alloc,
	.free	=	sma_free,
	.trim	=	sma_trim,
};
