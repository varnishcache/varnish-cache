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
 * Storage method based on mmap'ed file
 */

#include "config.h"

#include "cache/cache_varnishd.h"
#include "common/heritage.h"

#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>

#include "storage/storage.h"
#include "storage/storage_simple.h"

#include "vnum.h"
#include "vfil.h"

#include "VSC_smf.h"

#ifndef MAP_NOCORE
#ifdef MAP_CONCEAL
#define MAP_NOCORE MAP_CONCEAL /* XXX OpenBSD */
#else
#define MAP_NOCORE 0 /* XXX Linux */
#endif
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

#define MINPAGES		128

/*
 * Number of buckets on free-list.
 *
 * Last bucket is "larger than" so choose number so that the second
 * to last bucket matches the 128k CHUNKSIZE in cache_fetch.c when
 * using a 4K minimal page size
 */
#define NBUCKET			(128 / 4 + 1)

static struct VSC_lck *lck_smf;

/*--------------------------------------------------------------------*/

VTAILQ_HEAD(smfhead, smf);

struct smf {
	unsigned		magic;
#define SMF_MAGIC		0x0927a8a0
	struct storage		s;
	struct smf_sc		*sc;

	int			alloc;

	off_t			size;
	off_t			offset;
	unsigned char		*ptr;

	VTAILQ_ENTRY(smf)	order;
	VTAILQ_ENTRY(smf)	status;
	struct smfhead		*flist;
};

struct smf_sc {
	unsigned		magic;
#define SMF_SC_MAGIC		0x52962ee7
	struct lock		mtx;
	struct VSC_smf		*stats;

	const char		*filename;
	int			fd;
	unsigned		pagesize;
	uintmax_t		filesize;
	int			advice;
	struct smfhead		order;
	struct smfhead		free[NBUCKET];
	struct smfhead		used;
};

/*--------------------------------------------------------------------*/

static void v_matchproto_(storage_init_f)
smf_init(struct stevedore *parent, int ac, char * const *av)
{
	const char *size, *fn, *r;
	struct smf_sc *sc;
	unsigned u;
	uintmax_t page_size;
	int advice = MADV_RANDOM;

	AZ(av[ac]);

	size = NULL;
	page_size = getpagesize();

	if (ac > 4)
		ARGV_ERR("(-sfile) too many arguments\n");
	if (ac < 1 || *av[0] == '\0')
		ARGV_ERR("(-sfile) path is mandatory\n");
	fn = av[0];
	if (ac > 1 && *av[1] != '\0')
		size = av[1];
	if (ac > 2 && *av[2] != '\0') {

		r = VNUM_2bytes(av[2], &page_size, 0);
		if (r != NULL)
			ARGV_ERR("(-sfile) granularity \"%s\": %s\n", av[2], r);
	}
	if (ac > 3) {
		if (!strcmp(av[3], "normal"))
			advice = MADV_NORMAL;
		else if (!strcmp(av[3], "random"))
			advice = MADV_RANDOM;
		else if (!strcmp(av[3], "sequential"))
			advice = MADV_SEQUENTIAL;
		else
			ARGV_ERR("(-s file) invalid advice: \"%s\"", av[3]);
	}

	AN(fn);

	ALLOC_OBJ(sc, SMF_SC_MAGIC);
	XXXAN(sc);
	VTAILQ_INIT(&sc->order);
	for (u = 0; u < NBUCKET; u++)
		VTAILQ_INIT(&sc->free[u]);
	VTAILQ_INIT(&sc->used);
	sc->pagesize = page_size;
	sc->advice = advice;
	parent->priv = sc;

	(void)STV_GetFile(fn, &sc->fd, &sc->filename, "-sfile");
	MCH_Fd_Inherit(sc->fd, "storage_file");
	sc->filesize = STV_FileSize(sc->fd, size, &sc->pagesize, "-sfile");
	if (VFIL_allocate(sc->fd, (off_t)sc->filesize, 0))
		ARGV_ERR("(-sfile) allocation error: %s\n", VAS_errtxt(errno));
}

/*--------------------------------------------------------------------
 * Insert/Remove from correct freelist
 */

static void
insfree(struct smf_sc *sc, struct smf *sp)
{
	off_t b, ns;
	struct smf *sp2;

	AZ(sp->alloc);
	assert(sp->flist == NULL);
	Lck_AssertHeld(&sc->mtx);
	b = sp->size / sc->pagesize;
	if (b >= NBUCKET) {
		b = NBUCKET - 1;
		sc->stats->g_smf_large++;
	} else {
		sc->stats->g_smf_frag++;
	}
	sp->flist = &sc->free[b];
	ns = b * sc->pagesize;
	VTAILQ_FOREACH(sp2, sp->flist, status) {
		assert(sp2->size >= ns);
		AZ(sp2->alloc);
		assert(sp2->flist == sp->flist);
		if (sp->offset < sp2->offset)
			break;
	}
	if (sp2 == NULL)
		VTAILQ_INSERT_TAIL(sp->flist, sp, status);
	else
		VTAILQ_INSERT_BEFORE(sp2, sp, status);
}

static void
remfree(const struct smf_sc *sc, struct smf *sp)
{
	size_t b;

	AZ(sp->alloc);
	assert(sp->flist != NULL);
	Lck_AssertHeld(&sc->mtx);
	b = sp->size / sc->pagesize;
	if (b >= NBUCKET) {
		b = NBUCKET - 1;
		sc->stats->g_smf_large--;
	} else {
		sc->stats->g_smf_frag--;
	}
	assert(sp->flist == &sc->free[b]);
	VTAILQ_REMOVE(sp->flist, sp, status);
	sp->flist = NULL;
}

/*--------------------------------------------------------------------
 * Allocate a range from the first free range that is large enough.
 */

static struct smf *
alloc_smf(struct smf_sc *sc, off_t bytes)
{
	struct smf *sp, *sp2;
	off_t b;

	AZ(bytes % sc->pagesize);
	b = bytes / sc->pagesize;
	if (b >= NBUCKET)
		b = NBUCKET - 1;
	sp = NULL;
	for (; b < NBUCKET - 1; b++) {
		sp = VTAILQ_FIRST(&sc->free[b]);
		if (sp != NULL)
			break;
	}
	if (sp == NULL) {
		VTAILQ_FOREACH(sp, &sc->free[NBUCKET -1], status)
			if (sp->size >= bytes)
				break;
	}
	if (sp == NULL)
		return (sp);

	assert(sp->size >= bytes);
	remfree(sc, sp);

	if (sp->size == bytes) {
		sp->alloc = 1;
		VTAILQ_INSERT_TAIL(&sc->used, sp, status);
		return (sp);
	}

	/* Split from front */
	sp2 = malloc(sizeof *sp2);
	XXXAN(sp2);
	sc->stats->g_smf++;
	*sp2 = *sp;

	sp->offset += bytes;
	sp->ptr += bytes;
	sp->size -= bytes;

	sp2->size = bytes;
	sp2->alloc = 1;
	VTAILQ_INSERT_BEFORE(sp, sp2, order);
	VTAILQ_INSERT_TAIL(&sc->used, sp2, status);
	insfree(sc, sp);
	return (sp2);
}

/*--------------------------------------------------------------------
 * Free a range.  Attempt merge forward and backward, then sort into
 * free list according to age.
 */

static void
free_smf(struct smf *sp)
{
	struct smf *sp2;
	struct smf_sc *sc = sp->sc;

	CHECK_OBJ_NOTNULL(sp, SMF_MAGIC);
	AN(sp->alloc);
	assert(sp->size > 0);
	AZ(sp->size % sc->pagesize);
	VTAILQ_REMOVE(&sc->used, sp, status);
	sp->alloc = 0;

	sp2 = VTAILQ_NEXT(sp, order);
	if (sp2 != NULL &&
	    sp2->alloc == 0 &&
	    (sp2->ptr == sp->ptr + sp->size) &&
	    (sp2->offset == sp->offset + sp->size)) {
		sp->size += sp2->size;
		VTAILQ_REMOVE(&sc->order, sp2, order);
		remfree(sc, sp2);
		free(sp2);
		sc->stats->g_smf--;
	}

	sp2 = VTAILQ_PREV(sp, smfhead, order);
	if (sp2 != NULL &&
	    sp2->alloc == 0 &&
	    (sp->ptr == sp2->ptr + sp2->size) &&
	    (sp->offset == sp2->offset + sp2->size)) {
		remfree(sc, sp2);
		sp2->size += sp->size;
		VTAILQ_REMOVE(&sc->order, sp, order);
		free(sp);
		sc->stats->g_smf--;
		sp = sp2;
	}

	insfree(sc, sp);
}

/*--------------------------------------------------------------------
 * Insert a newly created range as busy, then free it to do any collapses
 */

static void
new_smf(struct smf_sc *sc, unsigned char *ptr, off_t off, size_t len)
{
	struct smf *sp, *sp2;

	AZ(len % sc->pagesize);
	ALLOC_OBJ(sp, SMF_MAGIC);
	XXXAN(sp);
	sp->s.magic = STORAGE_MAGIC;
	sc->stats->g_smf++;

	sp->sc = sc;
	sp->size = len;
	sp->ptr = ptr;
	sp->offset = off;
	sp->alloc = 1;

	VTAILQ_FOREACH(sp2, &sc->order, order) {
		if (sp->ptr < sp2->ptr) {
			VTAILQ_INSERT_BEFORE(sp2, sp, order);
			break;
		}
	}
	if (sp2 == NULL)
		VTAILQ_INSERT_TAIL(&sc->order, sp, order);

	VTAILQ_INSERT_HEAD(&sc->used, sp, status);

	free_smf(sp);
}

/*--------------------------------------------------------------------*/

/*
 * XXX: This may be too aggressive and soak up too much address room.
 * XXX: On the other hand, the user, directly or implicitly asked us to
 * XXX: use this much storage, so we should make a decent effort.
 * XXX: worst case (I think), malloc will fail.
 */

static void
smf_open_chunk(struct smf_sc *sc, off_t sz, off_t off, off_t *fail, off_t *sum)
{
	void *p;
	off_t h;

	AN(sz);
	AZ(sz % sc->pagesize);

	if (*fail < (off_t)sc->pagesize * MINPAGES)
		return;

	if (sz > 0 && sz < *fail && sz < SSIZE_MAX) {
		p = mmap(NULL, sz, PROT_READ|PROT_WRITE,
		    MAP_NOCORE | MAP_NOSYNC | MAP_SHARED, sc->fd, off);
		if (p != MAP_FAILED) {
			(void)madvise(p, sz, sc->advice);
			(*sum) += sz;
			new_smf(sc, p, off, sz);
			return;
		}
	}

	if (sz < *fail)
		*fail = sz;

	h = sz / 2;
	h -= (h % sc->pagesize);

	smf_open_chunk(sc, h, off, fail, sum);
	smf_open_chunk(sc, sz - h, off + h, fail, sum);
}

static void v_matchproto_(storage_open_f)
smf_open(struct stevedore *st)
{
	struct smf_sc *sc;
	off_t fail = 1 << 30;	/* XXX: where is OFF_T_MAX ? */
	off_t sum = 0;

	ASSERT_CLI();
	st->lru = LRU_Alloc();
	if (lck_smf == NULL)
		lck_smf = Lck_CreateClass(NULL, "smf");
	CAST_OBJ_NOTNULL(sc, st->priv, SMF_SC_MAGIC);
	sc->stats = VSC_smf_New(NULL, NULL, st->ident);
	Lck_New(&sc->mtx, lck_smf);
	Lck_Lock(&sc->mtx);
	smf_open_chunk(sc, sc->filesize, 0, &fail, &sum);
	Lck_Unlock(&sc->mtx);
	if (sum < MINPAGES * (off_t)getpagesize()) {
		ARGV_ERR(
		    "-sfile too small for this architecture,"
		    " minimum size is %jd MB\n",
		    (MINPAGES * (intmax_t)getpagesize()) / (1<<20)
		);
	}
	printf("SMF.%s mmap'ed %ju bytes of %ju\n",
	    st->ident, (uintmax_t)sum, sc->filesize);

	/* XXX */
	if (sum < MINPAGES * (off_t)getpagesize())
		exit(4);

	sc->stats->g_space += sc->filesize;
}

/*--------------------------------------------------------------------*/

static struct storage * v_matchproto_(sml_alloc_f)
smf_alloc(const struct stevedore *st, size_t sz)
{
	struct smf *smf;
	struct smf_sc *sc;
	off_t size;

	CAST_OBJ_NOTNULL(sc, st->priv, SMF_SC_MAGIC);
	assert(sz > 0);
	// XXX missing OFF_T_MAX
	size = (off_t)sz;
	size += (sc->pagesize - 1UL);
	size &= ~(sc->pagesize - 1UL);
	Lck_Lock(&sc->mtx);
	sc->stats->c_req++;
	smf = alloc_smf(sc, size);
	if (smf == NULL) {
		sc->stats->c_fail++;
		Lck_Unlock(&sc->mtx);
		return (NULL);
	}
	CHECK_OBJ_NOTNULL(smf, SMF_MAGIC);
	sc->stats->g_alloc++;
	sc->stats->c_bytes += smf->size;
	sc->stats->g_bytes += smf->size;
	sc->stats->g_space -= smf->size;
	Lck_Unlock(&sc->mtx);
	CHECK_OBJ_NOTNULL(&smf->s, STORAGE_MAGIC);	/*lint !e774 */
	XXXAN(smf);
	assert(smf->size == size);
	smf->s.space = size;
	smf->s.priv = smf;
	smf->s.ptr = smf->ptr;
	smf->s.len = 0;
	return (&smf->s);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(sml_free_f)
smf_free(struct storage *s)
{
	struct smf *smf;
	struct smf_sc *sc;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	CAST_OBJ_NOTNULL(smf, s->priv, SMF_MAGIC);
	sc = smf->sc;
	Lck_Lock(&sc->mtx);
	sc->stats->g_alloc--;
	sc->stats->c_freed += smf->size;
	sc->stats->g_bytes -= smf->size;
	sc->stats->g_space += smf->size;
	free_smf(smf);
	Lck_Unlock(&sc->mtx);
}

/*--------------------------------------------------------------------*/

const struct stevedore smf_stevedore = {
	.magic		=	STEVEDORE_MAGIC,
	.name		=	"file",
	.init		=	smf_init,
	.open		=	smf_open,
	.sml_alloc	=	smf_alloc,
	.sml_free	=	smf_free,
	.allocobj	=	SML_allocobj,
	.panic		=	SML_panic,
	.methods	=	&SML_methods,
	.allocbuf	=	SML_AllocBuf,
	.freebuf	=	SML_FreeBuf,
};
