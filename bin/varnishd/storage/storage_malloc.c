/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "cache/cache_varnishd.h"
#include "common/heritage.h"

#include <stdio.h>
#include <stdlib.h>

#include "storage/storage.h"
#include "storage/storage_simple.h"

#include "vnum.h"

#include "VSC_sma.h"

struct sma_sc {
	unsigned		magic;
#define SMA_SC_MAGIC		0x1ac8a345
	struct lock		sma_mtx;
	VCL_BYTES		sma_max;
	VCL_BYTES		sma_alloc;
	struct VSC_sma		*stats;
};

struct sma {
	unsigned		magic;
#define SMA_MAGIC		0x69ae9bb9
	struct storage		s;
	size_t			sz;
	struct sma_sc		*sc;
};

static struct VSC_lck *lck_sma;

static struct storage * v_matchproto_(sml_alloc_f)
sma_alloc(const struct stevedore *st, size_t size)
{
	struct sma_sc *sma_sc;
	struct sma *sma = NULL;
	void *p;

	CAST_OBJ_NOTNULL(sma_sc, st->priv, SMA_SC_MAGIC);
	Lck_Lock(&sma_sc->sma_mtx);
	sma_sc->stats->c_req++;
	if (sma_sc->sma_alloc + (VCL_BYTES)size > sma_sc->sma_max) {
		sma_sc->stats->c_fail++;
		size = 0;
	} else {
		sma_sc->sma_alloc += size;
		sma_sc->stats->c_bytes += size;
		sma_sc->stats->g_alloc++;
		sma_sc->stats->g_bytes += size;
		if (sma_sc->sma_max != VRT_INTEGER_MAX)
			sma_sc->stats->g_space -= size;
	}
	Lck_Unlock(&sma_sc->sma_mtx);

	if (size == 0)
		return (NULL);

	/*
	 * Do not collapse the sma allocation with sma->s.ptr: it is not
	 * a good idea.  Not only would it make ->trim impossible,
	 * performance-wise it would be a catastropy with chunksized
	 * allocations growing another full page, just to accommodate the sma.
	 */

	p = malloc(size);
	if (p != NULL) {
		ALLOC_OBJ(sma, SMA_MAGIC);
		if (sma != NULL)
			sma->s.ptr = p;
		else
			free(p);
	}
	if (sma == NULL) {
		Lck_Lock(&sma_sc->sma_mtx);
		/*
		 * XXX: Not nice to have counters go backwards, but we do
		 * XXX: Not want to pick up the lock twice just for stats.
		 */
		sma_sc->stats->c_fail++;
		sma_sc->sma_alloc -= size;
		sma_sc->stats->c_bytes -= size;
		sma_sc->stats->g_alloc--;
		sma_sc->stats->g_bytes -= size;
		if (sma_sc->sma_max != VRT_INTEGER_MAX)
			sma_sc->stats->g_space += size;
		Lck_Unlock(&sma_sc->sma_mtx);
		return (NULL);
	}
	sma->sc = sma_sc;
	sma->sz = size;
	sma->s.priv = sma;
	sma->s.len = 0;
	sma->s.space = size;
	sma->s.magic = STORAGE_MAGIC;
	return (&sma->s);
}

static void v_matchproto_(sml_free_f)
sma_free(struct storage *s)
{
	struct sma_sc *sma_sc;
	struct sma *sma;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	CAST_OBJ_NOTNULL(sma, s->priv, SMA_MAGIC);
	sma_sc = sma->sc;
	assert(sma->sz == sma->s.space);
	Lck_Lock(&sma_sc->sma_mtx);
	sma_sc->sma_alloc -= sma->sz;
	sma_sc->stats->g_alloc--;
	sma_sc->stats->g_bytes -= sma->sz;
	sma_sc->stats->c_freed += sma->sz;
	if (sma_sc->sma_max != VRT_INTEGER_MAX)
		sma_sc->stats->g_space += sma->sz;
	Lck_Unlock(&sma_sc->sma_mtx);
	free(sma->s.ptr);
	FREE_OBJ(sma);
}

static VCL_BYTES v_matchproto_(stv_var_used_space)
sma_used_space(const struct stevedore *st)
{
	struct sma_sc *sma_sc;

	CAST_OBJ_NOTNULL(sma_sc, st->priv, SMA_SC_MAGIC);
	return (sma_sc->sma_alloc);
}

static VCL_BYTES v_matchproto_(stv_var_free_space)
sma_free_space(const struct stevedore *st)
{
	struct sma_sc *sma_sc;

	CAST_OBJ_NOTNULL(sma_sc, st->priv, SMA_SC_MAGIC);
	return (sma_sc->sma_max - sma_sc->sma_alloc);
}

static void v_matchproto_(storage_init_f)
sma_init(struct stevedore *parent, int ac, char * const *av)
{
	const char *e;
	uintmax_t u;
	struct sma_sc *sc;

	ALLOC_OBJ(sc, SMA_SC_MAGIC);
	AN(sc);
	sc->sma_max = VRT_INTEGER_MAX;
	assert(sc->sma_max == VRT_INTEGER_MAX);
	parent->priv = sc;

	AZ(av[ac]);
	if (ac > 1)
		ARGV_ERR("(-s%s) too many arguments\n", parent->name);

	if (ac == 0 || *av[0] == '\0')
		 return;

	e = VNUM_2bytes(av[0], &u, 0);
	if (e != NULL)
		ARGV_ERR("(-s%s) size \"%s\": %s\n", parent->name, av[0], e);
	if ((u != (uintmax_t)(size_t)u))
		ARGV_ERR("(-s%s) size \"%s\": too big\n", parent->name, av[0]);
	if (u < 1024*1024)
		ARGV_ERR("(-s%s) size \"%s\": too small, "
		    "did you forget to specify M or G?\n", parent->name,
		    av[0]);

	sc->sma_max = u;
}

static void v_matchproto_(storage_open_f)
sma_open(struct stevedore *st)
{
	struct sma_sc *sma_sc;

	ASSERT_CLI();
	st->lru = LRU_Alloc();
	if (lck_sma == NULL)
		lck_sma = Lck_CreateClass(NULL, "sma");
	CAST_OBJ_NOTNULL(sma_sc, st->priv, SMA_SC_MAGIC);
	Lck_New(&sma_sc->sma_mtx, lck_sma);
	sma_sc->stats = VSC_sma_New(NULL, NULL, st->ident);
	if (sma_sc->sma_max != VRT_INTEGER_MAX)
		sma_sc->stats->g_space = sma_sc->sma_max;
}

const struct stevedore sma_stevedore = {
	.magic		=	STEVEDORE_MAGIC,
	.name		=	"malloc",
	.init		=	sma_init,
	.open		=	sma_open,
	.sml_alloc	=	sma_alloc,
	.sml_free	=	sma_free,
	.allocobj	=	SML_allocobj,
	.panic		=	SML_panic,
	.methods	=	&SML_methods,
	.var_free_space =	sma_free_space,
	.var_used_space =	sma_used_space,
	.allocbuf	=	SML_AllocBuf,
	.freebuf	=	SML_FreeBuf,
};
