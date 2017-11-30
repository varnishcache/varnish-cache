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

#include "cache/cache_varnishd.h"

#include <stdio.h>
#include <stdlib.h>
#include <umem.h>

#include "storage/storage.h"
#include "storage/storage_simple.h"

#include "vnum.h"
#include "common/heritage.h"

#include "VSC_smu.h"

struct smu_sc {
	unsigned		magic;
#define SMU_SC_MAGIC		0x7695f68e
	struct lock		smu_mtx;
	size_t			smu_max;
	size_t			smu_alloc;
	struct VSC_smu		*stats;
	umem_cache_t		*smu_cache;
};

struct smu {
	unsigned		magic;
#define SMU_MAGIC		0x3773300c
	struct storage		s;
	size_t			sz;
	struct smu_sc		*sc;
};

/* init required per cache get:
   smu->sz = size
   smu->s.ptr;
   smu->s.space = size
*/

static inline void
smu_smu_init(struct smu *smu, struct smu_sc *sc)
{
	INIT_OBJ(smu, SMU_MAGIC);
	smu->s.magic = STORAGE_MAGIC;
	smu->s.priv = smu;
	smu->sc = sc;
}

static int v_matchproto_(umem_constructor_t)
smu_smu_constructor(void *buffer, void *callback_data, int flags)
{
	struct smu *smu = buffer;
	struct smu_sc *sc;

	(void) flags;
	CAST_OBJ_NOTNULL(sc, callback_data, SMU_SC_MAGIC);
	smu_smu_init(smu, sc);
	return (0);
}

static void v_matchproto_(umem_destructor_t)
	smu_smu_destructor(void *buffer, void *callback_data)
{
	struct smu *smu;
	struct smu_sc *sc;

	CAST_OBJ_NOTNULL(smu, buffer, SMU_MAGIC);
	CAST_OBJ_NOTNULL(sc, callback_data, SMU_SC_MAGIC);
	CHECK_OBJ_NOTNULL(&(smu->s), STORAGE_MAGIC);
	assert(smu->s.priv == smu);
	assert(smu->sc == sc);
}

static struct VSC_lck *lck_smu;

static struct storage * v_matchproto_(sml_alloc_f)
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

	p = umem_alloc(size, UMEM_DEFAULT);
	if (p != NULL) {
		AN(smu_sc->smu_cache);
		smu = umem_cache_alloc(smu_sc->smu_cache, UMEM_DEFAULT);
		if (smu != NULL)
			smu->s.ptr = p;
		else
			umem_free(p, size);
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
	smu->sz = size;
	smu->s.space = size;
	return (&smu->s);
}

static void v_matchproto_(sml_free_f)
smu_free(struct storage *s)
{
	struct smu *smu;
	struct smu_sc *sc;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	CAST_OBJ_NOTNULL(smu, s->priv, SMU_MAGIC);
	CAST_OBJ_NOTNULL(sc, smu->sc, SMU_SC_MAGIC);

	Lck_Lock(&sc->smu_mtx);
	sc->smu_alloc -= smu->sz;
	sc->stats->g_alloc--;
	sc->stats->g_bytes -= smu->sz;
	sc->stats->c_freed += smu->sz;
	if (sc->smu_max != SIZE_MAX)
		sc->stats->g_space += smu->sz;
	Lck_Unlock(&sc->smu_mtx);

	umem_free(smu->s.ptr, smu->sz);
	smu_smu_init(smu, sc);
	umem_cache_free(sc->smu_cache, smu);
}

static VCL_BYTES v_matchproto_(stv_var_used_space)
smu_used_space(const struct stevedore *st)
{
	struct smu_sc *smu_sc;

	CAST_OBJ_NOTNULL(smu_sc, st->priv, SMU_SC_MAGIC);
	return (smu_sc->smu_alloc);
}

static VCL_BYTES v_matchproto_(stv_var_free_space)
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

static void v_matchproto_(storage_open_f)
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

	smu_sc->smu_cache = umem_cache_create(st->ident,
					  sizeof(struct smu),
					  0,		// align
					  smu_smu_constructor,
					  smu_smu_destructor,
					  NULL,		// reclaim
					  smu_sc,	// callback_data
					  NULL,		// source
					  0		// cflags
		);
	AN(smu_sc->smu_cache);
}

static void v_matchproto_(storage_close_f)
smu_close(const struct stevedore *st, int warn)
{
	struct smu_sc *smu_sc;

	ASSERT_CLI();

	CAST_OBJ_NOTNULL(smu_sc, st->priv, SMU_SC_MAGIC);
	if (warn)
		return;
	umem_cache_destroy(smu_sc->smu_cache);
	smu_sc->smu_cache = NULL;

	/*
	   XXX TODO?
	   - LRU_Free
	   - Lck Destroy
	*/
}

const struct stevedore smu_stevedore = {
	.magic		=	STEVEDORE_MAGIC,
	.name		=	"umem",
	.init		=	smu_init,
	.open		=	smu_open,
	.close		=	smu_close,
	.sml_alloc	=	smu_alloc,
	.sml_free	=	smu_free,
	.allocobj	=	SML_allocobj,
	.panic		=	SML_panic,
	.methods	=	&SML_methods,
	.var_free_space =	smu_free_space,
	.var_used_space =	smu_used_space,
};

#endif /* HAVE_UMEM_H */
