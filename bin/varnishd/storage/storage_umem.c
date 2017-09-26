/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * Copyright 2017 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *	    Nils Goroll <nils.goroll@uplex.de>
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
 * Storage method based on libumem
 */

#include "config.h"

#if defined(HAVE_LIBUMEM)

#include "cache/cache.h"

#include <stdio.h>
#include <stdlib.h>

#include "storage/storage.h"
#include "storage/storage_simple.h"

#include "vrt.h"
#include "vnum.h"

#include "VSC_smu.h"

struct smu_sc {
	unsigned		magic;
#define SMU_SC_MAGIC		0x1ac8a345
	struct lock		smu_mtx;
	size_t			smu_max;
	size_t			smu_alloc;
	struct VSC_smu		*stats;
};

struct smu {
	unsigned		magic;
#define SMU_MAGIC		0x69ae9bb9
	struct storage		s;
	size_t			sz;
	struct smu_sc		*sc;
};

static struct VSC_lck *lck_smu;

static struct storage * __match_proto__(sml_alloc_f)
smu_alloc(const struct stevedore *st, size_t size)
{
	struct smu_sc *smu_sc;
	struct smu *smu = NULL;
	void *p;

	CAST_OBJ_NOTNULL(smu_sc, st->priv, SMU_SC_MAGIC);
	Lck_Lock(&smu_sc->smu_mtx);
	smu_sc->stats->c_req++;
	if (smu_sc->smu_alloc + size > smu_sc->smu_max) {
		smu_sc->stats->c_fail++;
		size = 0;
	} else {
		smu_sc->smu_alloc += size;
		smu_sc->stats->c_bytes += size;
		smu_sc->stats->g_alloc++;
		smu_sc->stats->g_bytes += size;
		if (smu_sc->smu_max != SIZE_MAX)
			smu_sc->stats->g_space -= size;
	}
	Lck_Unlock(&smu_sc->smu_mtx);

	if (size == 0)
		return (NULL);

	/*
	 * Do not collaps the smu allocation with smu->s.ptr: it is not
	 * a good idea.  Not only would it make ->trim impossible,
	 * performance-wise it would be a catastropy with chunksized
	 * allocations growing another full page, just to accommodate the smu.
	 */

	p = malloc(size);
	if (p != NULL) {
		ALLOC_OBJ(smu, SMU_MAGIC);
		if (smu != NULL)
			smu->s.ptr = p;
		else
			free(p);
	}
	if (smu == NULL) {
		Lck_Lock(&smu_sc->smu_mtx);
		/*
		 * XXX: Not nice to have counters go backwards, but we do
		 * XXX: Not want to pick up the lock twice just for stats.
		 */
		smu_sc->stats->c_fail++;
		smu_sc->smu_alloc -= size;
		smu_sc->stats->c_bytes -= size;
		smu_sc->stats->g_alloc--;
		smu_sc->stats->g_bytes -= size;
		if (smu_sc->smu_max != SIZE_MAX)
			smu_sc->stats->g_space += size;
		Lck_Unlock(&smu_sc->smu_mtx);
		return (NULL);
	}
	smu->sc = smu_sc;
	smu->sz = size;
	smu->s.priv = smu;
	smu->s.len = 0;
	smu->s.space = size;
	smu->s.magic = STORAGE_MAGIC;
	return (&smu->s);
}

static void __match_proto__(sml_free_f)
smu_free(struct storage *s)
{
	struct smu_sc *smu_sc;
	struct smu *smu;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	CAST_OBJ_NOTNULL(smu, s->priv, SMU_MAGIC);
	smu_sc = smu->sc;
	assert(smu->sz == smu->s.space);
	Lck_Lock(&smu_sc->smu_mtx);
	smu_sc->smu_alloc -= smu->sz;
	smu_sc->stats->g_alloc--;
	smu_sc->stats->g_bytes -= smu->sz;
	smu_sc->stats->c_freed += smu->sz;
	if (smu_sc->smu_max != SIZE_MAX)
		smu_sc->stats->g_space += smu->sz;
	Lck_Unlock(&smu_sc->smu_mtx);
	free(smu->s.ptr);
	free(smu);
}

static VCL_BYTES __match_proto__(stv_var_used_space)
smu_used_space(const struct stevedore *st)
{
	struct smu_sc *smu_sc;

	CAST_OBJ_NOTNULL(smu_sc, st->priv, SMU_SC_MAGIC);
	return (smu_sc->smu_alloc);
}

static VCL_BYTES __match_proto__(stv_var_free_space)
smu_free_space(const struct stevedore *st)
{
	struct smu_sc *smu_sc;

	CAST_OBJ_NOTNULL(smu_sc, st->priv, SMU_SC_MAGIC);
	return (smu_sc->smu_max - smu_sc->smu_alloc);
}

static void
smu_init(struct stevedore *parent, int ac, char * const *av)
{
	const char *e;
	uintmax_t u;
	struct smu_sc *sc;

	ASSERT_MGT();
	ALLOC_OBJ(sc, SMU_SC_MAGIC);
	AN(sc);
	sc->smu_max = SIZE_MAX;
	assert(sc->smu_max == SIZE_MAX);
	parent->priv = sc;

	AZ(av[ac]);
	if (ac > 1)
		ARGV_ERR("(-sumem) too many arguments\n");

	if (ac == 0 || *av[0] == '\0')
		 return;

	e = VNUM_2bytes(av[0], &u, 0);
	if (e != NULL)
		ARGV_ERR("(-sumem) size \"%s\": %s\n", av[0], e);
	if ((u != (uintmax_t)(size_t)u))
		ARGV_ERR("(-sumem) size \"%s\": too big\n", av[0]);
	if (u < 1024*1024)
		ARGV_ERR("(-sumem) size \"%s\": too smull, "
			 "did you forget to specify M or G?\n", av[0]);

	sc->smu_max = u;
}

static void __match_proto__(storage_open_f)
smu_open(struct stevedore *st)
{
	struct smu_sc *smu_sc;

	ASSERT_CLI();
	st->lru = LRU_Alloc();
	if (lck_smu == NULL)
		lck_smu = Lck_CreateClass("smu");
	CAST_OBJ_NOTNULL(smu_sc, st->priv, SMU_SC_MAGIC);
	Lck_New(&smu_sc->smu_mtx, lck_smu);
	smu_sc->stats = VSC_smu_New(st->ident);
	if (smu_sc->smu_max != SIZE_MAX)
		smu_sc->stats->g_space = smu_sc->smu_max;
}

const struct stevedore smu_stevedore = {
	.magic		=	STEVEDORE_MAGIC,
	.name		=	"umem",
	.init		=	smu_init,
	.open		=	smu_open,
	.sml_alloc	=	smu_alloc,
	.sml_free	=	smu_free,
	.allocobj	=	SML_allocobj,
	.panic		=	SML_panic,
	.methods	=	&SML_methods,
	.var_free_space =	smu_free_space,
	.var_used_space =	smu_used_space,
};

#endif /* HAVE_UMEM_H */
