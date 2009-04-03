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
 * $Id$
 *
 * Storage method based on mmap'ed file
 */

#include "config.h"

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_ASPRINTF
#include "compat/asprintf.h"
#endif

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"

#ifndef MAP_NOCORE
#define MAP_NOCORE 0 /* XXX Linux */
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
 * using the a 4K minimal page size
 */
#define NBUCKET			(128 / 4 + 1)

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
	const char		*filename;
	int			fd;
	unsigned		pagesize;
	uintmax_t		filesize;
	struct smfhead		order;
	struct smfhead		free[NBUCKET];
	struct smfhead		used;
	struct lock		mtx;
};

/*--------------------------------------------------------------------*/

static void
smf_initfile(struct smf_sc *sc, const char *size)
{
	sc->filesize = STV_FileSize(sc->fd, size, &sc->pagesize, "-sfile");

	printf("storage_file: filename: %s size %ju MB.\n",
	      sc->filename, sc->filesize / (1024 * 1024));

	AZ(ftruncate(sc->fd, (off_t)sc->filesize));

	/* XXX: force block allocation here or in open ? */
}

static const char default_size[] = "50%";
static const char default_filename[] = ".";

static void
smf_init(struct stevedore *parent, int ac, char * const *av)
{
	const char *size, *fn, *r;
	struct smf_sc *sc;
	unsigned u;
	uintmax_t page_size;

	AZ(av[ac]);

	fn = default_filename;
	size = default_size;
	page_size = getpagesize();

	if (ac > 3)
		ARGV_ERR("(-sfile) too many arguments\n");
	if (ac > 0 && *av[0] != '\0')
		fn = av[0];
	if (ac > 1 && *av[1] != '\0')
		size = av[1];
	if (ac > 2 && *av[2] != '\0') {

		r = str2bytes(av[2], &page_size, 0);
		if (r != NULL)
			ARGV_ERR("(-sfile) granularity \"%s\": %s\n", av[2], r);
	}

	AN(fn);
	AN(size);

	sc = calloc(sizeof *sc, 1);
	XXXAN(sc);
	VTAILQ_INIT(&sc->order);
	for (u = 0; u < NBUCKET; u++)
		VTAILQ_INIT(&sc->free[u]);
	VTAILQ_INIT(&sc->used);
	sc->pagesize = page_size;

	parent->priv = sc;

	(void)STV_GetFile(fn, &sc->fd, &sc->filename, "-sfile");

	mgt_child_inherit(sc->fd, "storage_file");
	smf_initfile(sc, size);
}

/*--------------------------------------------------------------------
 * Insert/Remove from correct freelist
 */

static void
insfree(struct smf_sc *sc, struct smf *sp)
{
	size_t b;
	struct smf *sp2;
	size_t ns;

	assert(sp->alloc == 0);
	assert(sp->flist == NULL);
	Lck_AssertHeld(&sc->mtx);
	b = sp->size / sc->pagesize;
	if (b >= NBUCKET) {
		b = NBUCKET - 1;
		VSL_stats->n_smf_large++;
	} else {
		VSL_stats->n_smf_frag++;
	}
	sp->flist = &sc->free[b];
	ns = b * sc->pagesize;
	VTAILQ_FOREACH(sp2, sp->flist, status) {
		assert(sp2->size >= ns);
		assert(sp2->alloc == 0);
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

	assert(sp->alloc == 0);
	assert(sp->flist != NULL);
	Lck_AssertHeld(&sc->mtx);
	b = sp->size / sc->pagesize;
	if (b >= NBUCKET) {
		b = NBUCKET - 1;
		VSL_stats->n_smf_large--;
	} else {
		VSL_stats->n_smf_frag--;
	}
	assert(sp->flist == &sc->free[b]);
	VTAILQ_REMOVE(sp->flist, sp, status);
	sp->flist = NULL;
}

/*--------------------------------------------------------------------
 * Allocate a range from the first free range that is large enough.
 */

static struct smf *
alloc_smf(struct smf_sc *sc, size_t bytes)
{
	struct smf *sp, *sp2;
	size_t b;

	assert(!(bytes % sc->pagesize));
	b = bytes / sc->pagesize;
	if (b >= NBUCKET)
		b = NBUCKET - 1;
	for (sp = NULL; b < NBUCKET - 1; b++) {
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
	VSL_stats->n_smf++;
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
	assert(sp->alloc != 0);
	assert(sp->size > 0);
	assert(!(sp->size % sc->pagesize));
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
		VSL_stats->n_smf--;
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
		VSL_stats->n_smf--;
		sp = sp2;
	}

	insfree(sc, sp);
}

/*--------------------------------------------------------------------
 * Trim the tail of a range.
 */

static void
trim_smf(struct smf *sp, size_t bytes)
{
	struct smf *sp2;
	struct smf_sc *sc = sp->sc;

	assert(sp->alloc != 0);
	assert(bytes > 0);
	assert(bytes < sp->size);
	assert(!(bytes % sc->pagesize));
	assert(!(sp->size % sc->pagesize));
	CHECK_OBJ_NOTNULL(sp, SMF_MAGIC);
	sp2 = malloc(sizeof *sp2);
	XXXAN(sp2);
	VSL_stats->n_smf++;
	*sp2 = *sp;

	sp2->size -= bytes;
	sp->size = bytes;
	sp2->ptr += bytes;
	sp2->offset += bytes;
	VTAILQ_INSERT_AFTER(&sc->order, sp, sp2, order);
	VTAILQ_INSERT_TAIL(&sc->used, sp2, status);
	free_smf(sp2);
}

/*--------------------------------------------------------------------
 * Insert a newly created range as busy, then free it to do any collapses
 */

static void
new_smf(struct smf_sc *sc, unsigned char *ptr, off_t off, size_t len)
{
	struct smf *sp, *sp2;

	assert(!(len % sc->pagesize));
	sp = calloc(sizeof *sp, 1);
	XXXAN(sp);
	sp->magic = SMF_MAGIC;
	sp->s.magic = STORAGE_MAGIC;
	VSL_stats->n_smf++;

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

	assert(sz != 0);
	assert(!(sz % sc->pagesize));

	if (*fail < (uintmax_t)sc->pagesize * MINPAGES)
		return;

	if (sz > 0 && sz < *fail && sz < SIZE_MAX) {
		p = mmap(NULL, sz, PROT_READ|PROT_WRITE,
		    MAP_NOCORE | MAP_NOSYNC | MAP_SHARED, sc->fd, off);
		if (p != MAP_FAILED) {
			(*sum) += sz;
			new_smf(sc, p, off, sz);
			return;
		}
	}

	if (sz < *fail)
		*fail = sz;

	h = sz / 2;
	if (h > SIZE_MAX)
		h = SIZE_MAX;
	h -= (h % sc->pagesize);

	smf_open_chunk(sc, h, off, fail, sum);
	smf_open_chunk(sc, sz - h, off + h, fail, sum);
}

static void
smf_open(const struct stevedore *st)
{
	struct smf_sc *sc;
	off_t fail = 1 << 30;	/* XXX: where is OFF_T_MAX ? */
	off_t sum = 0;

	sc = st->priv;

	Lck_New(&sc->mtx);
	Lck_Lock(&sc->mtx);
	smf_open_chunk(sc, sc->filesize, 0, &fail, &sum);
	Lck_Unlock(&sc->mtx);
	printf("managed to mmap %ju bytes of %ju\n",
	    (uintmax_t)sum, sc->filesize);

	/* XXX */
	if (sum < MINPAGES * (off_t)getpagesize())
		exit (2);

	VSL_stats->sm_bfree += sc->filesize;
}

/*--------------------------------------------------------------------*/

static struct storage *
smf_alloc(struct stevedore *st, size_t size)
{
	struct smf *smf;
	struct smf_sc *sc = st->priv;

	assert(size > 0);
	size += (sc->pagesize - 1);
	size &= ~(sc->pagesize - 1);
	Lck_Lock(&sc->mtx);
	VSL_stats->sm_nreq++;
	smf = alloc_smf(sc, size);
	if (smf == NULL) {
		Lck_Unlock(&sc->mtx);
		return (NULL);
	}
	CHECK_OBJ_NOTNULL(smf, SMF_MAGIC);
	VSL_stats->sm_nobj++;
	VSL_stats->sm_balloc += smf->size;
	VSL_stats->sm_bfree -= smf->size;
	Lck_Unlock(&sc->mtx);
	CHECK_OBJ_NOTNULL(&smf->s, STORAGE_MAGIC);	/*lint !e774 */
	XXXAN(smf);
	assert(smf->size == size);
	smf->s.space = size;
	smf->s.priv = smf;
	smf->s.ptr = smf->ptr;
	smf->s.len = 0;
	smf->s.stevedore = st;
	smf->s.fd = smf->sc->fd;
	smf->s.where = smf->offset;
	return (&smf->s);
}

/*--------------------------------------------------------------------*/

static void
smf_trim(struct storage *s, size_t size)
{
	struct smf *smf;
	struct smf_sc *sc;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	assert(size > 0);
	assert(size <= s->space);
	xxxassert(size > 0);	/* XXX: seen */
	CAST_OBJ_NOTNULL(smf, s->priv, SMF_MAGIC);
	assert(size <= smf->size);
	sc = smf->sc;
	size += (sc->pagesize - 1);
	size &= ~(sc->pagesize - 1);
	if (smf->size > size) {
		Lck_Lock(&sc->mtx);
		VSL_stats->sm_balloc -= (smf->size - size);
		VSL_stats->sm_bfree += (smf->size - size);
		trim_smf(smf, size);
		assert(smf->size == size);
		Lck_Unlock(&sc->mtx);
		s->space = size;
	}
}

/*--------------------------------------------------------------------*/

/*lint -e{818} not const-able */
static void
smf_free(struct storage *s)
{
	struct smf *smf;
	struct smf_sc *sc;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	CAST_OBJ_NOTNULL(smf, s->priv, SMF_MAGIC);
	sc = smf->sc;
	Lck_Lock(&sc->mtx);
	VSL_stats->sm_nobj--;
	VSL_stats->sm_balloc -= smf->size;
	VSL_stats->sm_bfree += smf->size;
	free_smf(smf);
	Lck_Unlock(&sc->mtx);
}

/*--------------------------------------------------------------------*/

struct stevedore smf_stevedore = {
	.magic	=	STEVEDORE_MAGIC,
	.name	=	"file",
	.init	=	smf_init,
	.open	=	smf_open,
	.alloc	=	smf_alloc,
	.trim	=	smf_trim,
	.free	=	smf_free,
};

#ifdef INCLUDE_TEST_DRIVER

void vca_flush(struct sess *sp) {}
void vca_close_session(struct sess *sp, const char *why) {}

#define N	100
#define M	(128*1024)

struct storage *s[N];

static void
dumpit(void)
{
	struct smf_sc *sc = smf_stevedore.priv;
	struct smf *s;

	return (0);
	printf("----------------\n");
	printf("Order:\n");
	VTAILQ_FOREACH(s, &sc->order, order) {
		printf("%10p %12ju %12ju %12ju\n",
		    s, s->offset, s->size, s->offset + s->size);
	}
	printf("Used:\n");
	VTAILQ_FOREACH(s, &sc->used, status) {
		printf("%10p %12ju %12ju %12ju\n",
		    s, s->offset, s->size, s->offset + s->size);
	}
	printf("Free:\n");
	VTAILQ_FOREACH(s, &sc->free, status) {
		printf("%10p %12ju %12ju %12ju\n",
		    s, s->offset, s->size, s->offset + s->size);
	}
	printf("================\n");
}

int
main(int argc, char **argv)
{
	int i, j;

	setbuf(stdout, NULL);
	smf_init(&smf_stevedore, "");
	smf_open(&smf_stevedore);
	while (1) {
		dumpit();
		i = random() % N;
		do
			j = random() % M;
		while (j == 0);
		if (s[i] == NULL) {
			s[i] = smf_alloc(&smf_stevedore, j);
			printf("A %10p %12d\n", s[i], j);
		} else if (j < s[i]->space) {
			smf_trim(s[i], j);
			printf("T %10p %12d\n", s[i], j);
		} else {
			smf_free(s[i]);
			printf("D %10p\n", s[i]);
			s[i] = NULL;
		}
	}
}

#endif /* INCLUDE_TEST_DRIVER */
