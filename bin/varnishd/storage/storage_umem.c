/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * Copyright 2017 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *	    Nils Goroll <nils.goroll@uplex.de>
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
 * Storage method based on libumem
 */

#include "config.h"

#if defined(HAVE_UMEM_H)

#include "cache/cache_varnishd.h"

#include <stdio.h>
#include <stdlib.h>
#include <umem.h>
#include <dlfcn.h>
#include <link.h>
#include <string.h>

#include "storage/storage.h"
#include "storage/storage_simple.h"

#include "vnum.h"
#include "common/heritage.h"

#include "VSC_smu.h"

struct smu_sc {
	unsigned		magic;
#define SMU_SC_MAGIC		0x7695f68e
	struct lock		smu_mtx;
	VCL_BYTES		smu_max;
	VCL_BYTES		smu_alloc;
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

/*
 * We only want the umem slab allocator for cache storage, not also as a
 * substitute for malloc and friends. So we don't link with libumem, but
 * use dlopen/dlsym to get the slab allocator interface into function
 * pointers.
 */
typedef void * (*umem_alloc_f)(size_t size, int flags);
typedef void (*umem_free_f)(void *buf, size_t size);
typedef umem_cache_t * (*umem_cache_create_f)(char *debug_name, size_t bufsize,
    size_t align, umem_constructor_t *constructor,
    umem_destructor_t *destructor, umem_reclaim_t *reclaim,
    void *callback_data, vmem_t *source, int cflags);
typedef void (*umem_cache_destroy_f)(umem_cache_t *cache);
typedef void * (*umem_cache_alloc_f)(umem_cache_t *cache, int flags);
typedef void (*umem_cache_free_f)(umem_cache_t *cache, void *buffer);

static void *libumem_hndl = NULL;
static umem_alloc_f umem_allocf = NULL;
static umem_free_f umem_freef = NULL;
static umem_cache_create_f umem_cache_createf = NULL;
static umem_cache_destroy_f umem_cache_destroyf = NULL;
static umem_cache_alloc_f umem_cache_allocf = NULL;
static umem_cache_free_f umem_cache_freef = NULL;

static const char * const def_umem_options = "perthread_cache=0,backend=mmap";
static const char * const env_umem_options = "UMEM_OPTIONS";

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
	if (smu_sc->smu_alloc + (int64_t)size > smu_sc->smu_max) {
		smu_sc->stats->c_fail++;
		size = 0;
	} else {
		smu_sc->smu_alloc += size;
		smu_sc->stats->c_bytes += size;
		smu_sc->stats->g_alloc++;
		smu_sc->stats->g_bytes += size;
		if (smu_sc->smu_max != VRT_INTEGER_MAX)
			smu_sc->stats->g_space -= size;
	}
	Lck_Unlock(&smu_sc->smu_mtx);

	if (size == 0)
		return (NULL);

	/*
	 * Do not collapse the smu allocation with smu->s.ptr: it is not
	 * a good idea.  Not only would it make ->trim impossible,
	 * performance-wise it would be a catastropy with chunksized
	 * allocations growing another full page, just to accommodate the smu.
	 */

	p = umem_allocf(size, UMEM_DEFAULT);
	if (p != NULL) {
		AN(smu_sc->smu_cache);
		smu = umem_cache_allocf(smu_sc->smu_cache, UMEM_DEFAULT);
		if (smu != NULL)
			smu->s.ptr = p;
		else
			umem_freef(p, size);
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
		if (smu_sc->smu_max != VRT_INTEGER_MAX)
			smu_sc->stats->g_space += size;
		Lck_Unlock(&smu_sc->smu_mtx);
		return (NULL);
	}
	smu->sz = size;
	smu->s.space = size;
#ifndef BUG3210
	assert(smu->sc == smu_sc);
	assert(smu->s.priv == smu);
	AZ(smu->s.len);
#endif
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
	if (sc->smu_max != VRT_INTEGER_MAX)
		sc->stats->g_space += smu->sz;
	Lck_Unlock(&sc->smu_mtx);

	umem_freef(smu->s.ptr, smu->sz);
	smu_smu_init(smu, sc);
	AN(sc->smu_cache);
	umem_cache_freef(sc->smu_cache, smu);
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
smu_umem_loaded_warn(void)
{
	const char *e;
	static int warned = 0;

	if (warned++)
		return;

	fprintf(stderr, "notice:\tlibumem was already found to be loaded\n"
		"\tand will likely be used for all allocations\n");

	e = getenv(env_umem_options);
	if (e == NULL || ! strstr(e, def_umem_options))
		fprintf(stderr, "\tit is recommended to set %s=%s "
			"before starting varnish\n",
			env_umem_options, def_umem_options);
}

static int
smu_umem_loaded(void)
{
	void *h = NULL;

	h = dlopen("libumem.so", RTLD_NOLOAD);
	if (h) {
		AZ(dlclose(h));
		return (1);
	}

	h = dlsym(RTLD_DEFAULT, "umem_alloc");
	if (h)
		return (1);

	return (0);
}

static void v_matchproto_(storage_init_f)
smu_init(struct stevedore *parent, int ac, char * const *av)
{
	static int inited = 0;
	const char *e;
	uintmax_t u;
	struct smu_sc *sc;

	ALLOC_OBJ(sc, SMU_SC_MAGIC);
	AN(sc);
	sc->smu_max = VRT_INTEGER_MAX;
	assert(sc->smu_max == VRT_INTEGER_MAX);
	parent->priv = sc;

	AZ(av[ac]);
	if (ac > 1)
		ARGV_ERR("(-sumem) too many arguments\n");

	if (ac == 1 && *av[0] != '\0') {
		e = VNUM_2bytes(av[0], &u, 0);
		if (e != NULL)
			ARGV_ERR("(-sumem) size \"%s\": %s\n", av[0], e);
		if ((u != (uintmax_t)(size_t)u))
			ARGV_ERR("(-sumem) size \"%s\": too big\n", av[0]);
		if (u < 1024*1024)
			ARGV_ERR("(-sumem) size \"%s\": too small, "
				 "did you forget to specify M or G?\n", av[0]);
		sc->smu_max = u;
	}

	if (inited++)
		return;

	if (smu_umem_loaded())
		smu_umem_loaded_warn();
	else
		AZ(setenv(env_umem_options, def_umem_options, 0));

	/* Check if these load in the management process. */
	(void) dlerror();
	libumem_hndl = dlmopen(LM_ID_NEWLM, "libumem.so", RTLD_LAZY);
	if (libumem_hndl == NULL)
		ARGV_ERR("(-sumem) cannot open libumem.so: %s", dlerror());

#define DLSYM_UMEM(fptr,sym)						\
	do {								\
		(void) dlerror();					\
		if (dlsym(libumem_hndl, #sym) == NULL)			\
			ARGV_ERR("(-sumem) cannot find symbol "		\
				 #sym ": %s",				\
				 dlerror());				\
		fptr = NULL;						\
	} while(0)

	DLSYM_UMEM(umem_allocf, umem_alloc);
	DLSYM_UMEM(umem_freef, umem_free);
	DLSYM_UMEM(umem_cache_createf, umem_cache_create);
	DLSYM_UMEM(umem_cache_destroyf, umem_cache_destroy);
	DLSYM_UMEM(umem_cache_allocf, umem_cache_alloc);
	DLSYM_UMEM(umem_cache_freef, umem_cache_free);

#undef DLSYM_UMEM

	AZ(dlclose(libumem_hndl));
	libumem_hndl = NULL;
}

/*
 * Load the symbols for use in the child process, assert if they fail to load.
 */
static void
smu_open_init(void)
{
	static int inited = 0;

	if (inited++) {
		AN(libumem_hndl);
		AN(umem_allocf);
		return;
	}

	if (smu_umem_loaded())
		smu_umem_loaded_warn();
	else
		AN(getenv(env_umem_options));

	AZ(libumem_hndl);
	libumem_hndl = dlopen("libumem.so", RTLD_LAZY);
	AN(libumem_hndl);

#define DLSYM_UMEM(fptr,sym)					\
	do {							\
		fptr = (sym ## _f) dlsym(libumem_hndl, #sym);	\
		AN(fptr);					\
	} while(0)

	DLSYM_UMEM(umem_allocf, umem_alloc);
	DLSYM_UMEM(umem_freef, umem_free);
	DLSYM_UMEM(umem_cache_createf, umem_cache_create);
	DLSYM_UMEM(umem_cache_destroyf, umem_cache_destroy);
	DLSYM_UMEM(umem_cache_allocf, umem_cache_alloc);
	DLSYM_UMEM(umem_cache_freef, umem_cache_free);

#undef DLSYM_UMEM
}

static void v_matchproto_(storage_open_f)
smu_open(struct stevedore *st)
{
	struct smu_sc *smu_sc;
	char ident[strlen(st->ident) + 1];

	ASSERT_CLI();
	st->lru = LRU_Alloc();
	if (lck_smu == NULL)
		lck_smu = Lck_CreateClass(NULL, "smu");
	CAST_OBJ_NOTNULL(smu_sc, st->priv, SMU_SC_MAGIC);
	Lck_New(&smu_sc->smu_mtx, lck_smu);
	smu_sc->stats = VSC_smu_New(NULL, NULL, st->ident);
	if (smu_sc->smu_max != VRT_INTEGER_MAX)
		smu_sc->stats->g_space = smu_sc->smu_max;

	smu_open_init();

	bstrcpy(ident, st->ident);
	smu_sc->smu_cache = umem_cache_createf(ident,
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

#ifdef WORKAROUND_3190
	/* see ticket 3190 for explanation */
	umem_cache_destroyf(smu_sc->smu_cache);
	smu_sc->smu_cache = NULL;
#endif

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
	.allocbuf	=	SML_AllocBuf,
	.freebuf	=	SML_FreeBuf,
};

#endif /* HAVE_UMEM_H */
