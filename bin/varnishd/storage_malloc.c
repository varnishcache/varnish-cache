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
 * Storage method based on malloc(3)
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"

struct sma_sc {
	unsigned		magic;
#define SMA_SC_MAGIC		0x1ac8a345
	struct lock		sma_mtx;
	size_t			sma_max;
};

struct sma {
	unsigned		magic;
#define SMA_MAGIC		0x69ae9bb9
	struct storage		s;
	size_t			sz;
	struct sma_sc		*sc;
};

static struct storage *
sma_alloc(struct stevedore *st, size_t size, struct objcore *oc)
{
	struct sma_sc *sma_sc;
	struct sma *sma;

	CAST_OBJ_NOTNULL(sma_sc, st->priv, SMA_SC_MAGIC);
	(void)oc;
	Lck_Lock(&sma_sc->sma_mtx);
	VSL_stats->sma_nreq++;
	if (VSL_stats->sma_nbytes + size > sma_sc->sma_max)
		size = 0;
	else {
		VSL_stats->sma_nobj++;
		VSL_stats->sma_nbytes += size;
		VSL_stats->sma_balloc += size;
	}
	Lck_Unlock(&sma_sc->sma_mtx);

	if (size == 0)
		return (NULL);

	/*
	 * Do not collaps the sma allocation with sma->s.ptr: it is not
	 * a good idea.  Not only would it make ->trim impossible,
	 * performance-wise it would be a catastropy with chunksized
	 * allocations growing another full page, just to accomodate the sma.
	 */
	ALLOC_OBJ(sma, SMA_MAGIC);
	if (sma == NULL)
		return (NULL);		/* XXX: stats suffer */
	sma->sc = sma_sc;
	sma->sz = size;
	sma->s.priv = sma;
	sma->s.ptr = malloc(size);
	if (sma->s.ptr == NULL) {
		free(sma);
		return (NULL);		/* XXX: stats suffer */
	}
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
	struct sma_sc *sma_sc;
	struct sma *sma;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	CAST_OBJ_NOTNULL(sma, s->priv, SMA_MAGIC);
	sma_sc = sma->sc;
	assert(sma->sz == sma->s.space);
	Lck_Lock(&sma_sc->sma_mtx);
	VSL_stats->sma_nobj--;
	VSL_stats->sma_nbytes -= sma->sz;
	VSL_stats->sma_bfree += sma->sz;
	Lck_Unlock(&sma_sc->sma_mtx);
	free(sma->s.ptr);
	free(sma);
}

static void
sma_trim(struct storage *s, size_t size)
{
	struct sma_sc *sma_sc;
	struct sma *sma;
	void *p;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	CAST_OBJ_NOTNULL(sma, s->priv, SMA_MAGIC);
	sma_sc = sma->sc;

	assert(sma->sz == sma->s.space);
	assert(size < sma->sz);
	if ((p = realloc(sma->s.ptr, size)) != NULL) {
		Lck_Lock(&sma_sc->sma_mtx);
		VSL_stats->sma_nbytes -= (sma->sz - size);
		VSL_stats->sma_bfree += sma->sz - size;
		sma->sz = size;
		Lck_Unlock(&sma_sc->sma_mtx);
		sma->s.ptr = p;
		s->space = size;
	}
}

static void
sma_init(struct stevedore *parent, int ac, char * const *av)
{
	const char *e;
	uintmax_t u;
	struct sma_sc *sc;

	ALLOC_OBJ(sc, SMA_SC_MAGIC);
	AN(sc);
	sc->sma_max = SIZE_MAX;
	parent->priv = sc;

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

	printf("storage.malloc.%s: max size %ju MB.\n", parent->ident,
	    u / (1024 * 1024));
	sc->sma_max = u;

}

static void
sma_open(const struct stevedore *st)
{
	struct sma_sc *sma_sc;

	CAST_OBJ_NOTNULL(sma_sc, st->priv, SMA_SC_MAGIC);
	Lck_New(&sma_sc->sma_mtx);
}

const struct stevedore sma_stevedore = {
	.magic	=	STEVEDORE_MAGIC,
	.name	=	"malloc",
	.init	=	sma_init,
	.open	=	sma_open,
	.alloc	=	sma_alloc,
	.free	=	sma_free,
	.trim	=	sma_trim,
};
