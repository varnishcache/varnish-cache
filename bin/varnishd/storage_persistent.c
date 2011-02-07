/*-
 * Copyright (c) 2008-2010 Linpro AS
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
 * Persistent storage method
 *
 * XXX: Before we start the client or maybe after it stops, we should give the
 * XXX: stevedores a chance to examine their storage for consistency.
 *
 * XXX: Do we ever free the LRU-lists ?
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/mman.h>

#include "cache.h"
#include "stevedore.h"
#include "hash_slinger.h"
#include "vsha256.h"
#include "cli.h"
#include "cli_priv.h"

#include "persistent.h"

#ifndef MAP_NOCORE
#define MAP_NOCORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

#define ASSERT_SILO_THREAD(sc) \
    do {assert(pthread_self() == (sc)->thread);} while (0)

#define OC_F_NEEDFIXUP OC_F_PRIV

/*
 * Context for a signature.
 *
 * A signature is a sequence of bytes in the silo, signed by a SHA256 hash
 * which follows the bytes.
 *
 * The context structure allows us to append to a signature without
 * recalculating the entire SHA256 hash.
 */

struct smp_signctx {
	struct smp_sign		*ss;
	struct SHA256Context	ctx;
	uint32_t		unique;
	const char		*id;
};

struct smp_sc;

/* XXX: name confusion with on-media version ? */
struct smp_seg {
	unsigned		magic;
#define SMP_SEG_MAGIC		0x45c61895

	struct smp_sc		*sc;
	struct lru		*lru;

	VTAILQ_ENTRY(smp_seg)	list;		/* on smp_sc.smp_segments */

	struct smp_segptr	p;

	unsigned		must_load;

	uint32_t		nobj;		/* Number of objects */
	uint32_t		nalloc;		/* Allocations */
	uint32_t		nalloc1;	/* Allocated objects */
	uint32_t		nfixed;		/* How many fixed objects */

	/* Only for open segment */
	struct smp_object	*objs;		/* objdesc array */
	uint64_t		next_bot;	/* next alloc address bottom */
	uint64_t		next_top;	/* next alloc address top */

	struct smp_signctx	ctx[1];
};

VTAILQ_HEAD(smp_seghead, smp_seg);

struct smp_sc {
	unsigned		magic;
#define SMP_SC_MAGIC		0x7b73af0a
	struct stevedore	*parent;

	unsigned		flags;
#define SMP_F_LOADED		(1 << 0)

	const struct stevedore	*stevedore;
	int			fd;
	const char		*filename;
	off_t			mediasize;
	uint64_t		align;		/* 64b to avoid casts */
	uint32_t		granularity;
	uint32_t		unique;

	uint8_t			*base;

	struct smp_ident	*ident;

	struct smp_seghead	segments;
	struct smp_seg		*cur_seg;
	uint64_t		free_offset;

	pthread_t		thread;

	VTAILQ_ENTRY(smp_sc)	list;

	struct smp_signctx	idn;
	struct smp_signctx	ban1;
	struct smp_signctx	ban2;
	struct smp_signctx	seg1;
	struct smp_signctx	seg2;

	struct ban		*tailban;

	struct lock		mtx;

	/* Cleaner metrics */

	unsigned		min_nseg;
	unsigned		aim_nseg;
	unsigned		max_nseg;

	unsigned		aim_nobj;

	uint64_t		min_segl;
	uint64_t		aim_segl;
	uint64_t		max_segl;

	uint64_t		free_reserve;
};

/*--------------------------------------------------------------------*/

/* Generic power-2 rounding */
#define PWR2(x)     ((((x)-1)&(x))==0)		/* Is a power of two */
#define RDN2(x, y)  ((x)&(~((y)-1)))		/* if y is powers of two */
#define RUP2(x, y)  (((x)+((y)-1))&(~((y)-1)))	/* if y is powers of two */

/* Pointer round up/down & assert */
#define PRNDN(sc, x)	((void*)RDN2((uintptr_t)(x), sc->align))
#define PRNUP(sc, x)	((void*)RUP2((uintptr_t)(x), sc->align))
#define PASSERTALIGN(sc, x)	assert(PRNDN(sc, x) == (x))

/* Integer round up/down & assert */
#define IRNDN(sc, x)	RDN2(x, sc->align)
#define IRNUP(sc, x)	RUP2(x, sc->align)
#define IASSERTALIGN(sc, x)	assert(IRNDN(sc, x) == (x))

/*--------------------------------------------------------------------*/

#define ASSERT_PTR_IN_SILO(sc, ptr) \
	assert((const void*)(ptr) >= (const void*)((sc)->base) && \
	    (const void*)(ptr) < (const void *)((sc)->base + (sc)->mediasize))

/*--------------------------------------------------------------------*/

/*
 * silos is unlocked, it only changes during startup when we are
 * single-threaded
 */
static VTAILQ_HEAD(,smp_sc)	silos = VTAILQ_HEAD_INITIALIZER(silos);

/*--------------------------------------------------------------------
 * SIGNATURE functions
 * The signature is SHA256 over:
 *    1. The smp_sign struct up to but not including the length field.
 *    2. smp_sign->length bytes, starting after the smp_sign structure
 *    3. The smp-sign->length field.
 * The signature is stored after the byte-range from step 2.
 */

#define SIGN_DATA(ctx)	((void *)((ctx)->ss + 1))
#define SIGN_END(ctx)	((void *)((int8_t *)SIGN_DATA(ctx) + (ctx)->ss->length))

/*--------------------------------------------------------------------
 * Define a signature by location and identifier.
 */

static void
smp_def_sign(const struct smp_sc *sc, struct smp_signctx *ctx,
    uint64_t off, const char *id)
{

	AZ(off & 7);			/* Alignment */
	assert(strlen(id) < sizeof ctx->ss->ident);

	memset(ctx, 0, sizeof ctx);
	ctx->ss = (void*)(sc->base + off);
	ctx->unique = sc->unique;
	ctx->id = id;
}

/*--------------------------------------------------------------------
 * Check that a signature is good, leave state ready for append
 */
static int
smp_chk_sign(struct smp_signctx *ctx)
{
	struct SHA256Context cx;
	unsigned char sign[SHA256_LEN];
	int r = 0;

	if (strncmp(ctx->id, ctx->ss->ident, sizeof ctx->ss->ident))
		r = 1;
	else if (ctx->unique != ctx->ss->unique)
		r = 2;
	else if ((uintptr_t)ctx->ss != ctx->ss->mapped)
		r = 3;
	else {
		SHA256_Init(&ctx->ctx);
		SHA256_Update(&ctx->ctx, ctx->ss,
		    offsetof(struct smp_sign, length));
		SHA256_Update(&ctx->ctx, SIGN_DATA(ctx), ctx->ss->length);
		cx = ctx->ctx;
		SHA256_Update(&cx, &ctx->ss->length, sizeof(ctx->ss->length));
		SHA256_Final(sign, &cx);
		if (memcmp(sign, SIGN_END(ctx), sizeof sign))
			r = 4;
	}
	if (r) {
		fprintf(stderr, "CHK(%p %s %p %s) = %d\n",
		    ctx, ctx->id, ctx->ss,
		    r > 1 ? ctx->ss->ident : "<invalid>", r);
	}
	return (r);
}

/*--------------------------------------------------------------------
 * Append data to a signature
 */
static void
smp_append_sign(struct smp_signctx *ctx, const void *ptr, uint32_t len)
{
	struct SHA256Context cx;
	unsigned char sign[SHA256_LEN];

	if (len != 0) {
		SHA256_Update(&ctx->ctx, ptr, len);
		ctx->ss->length += len;
	}
	cx = ctx->ctx;
	SHA256_Update(&cx, &ctx->ss->length, sizeof(ctx->ss->length));
	SHA256_Final(sign, &cx);
	memcpy(SIGN_END(ctx), sign, sizeof sign);
XXXAZ(smp_chk_sign(ctx));
}

/*--------------------------------------------------------------------
 * Reset a signature to empty, prepare for appending.
 */

static void
smp_reset_sign(struct smp_signctx *ctx)
{

	memset(ctx->ss, 0, sizeof *ctx->ss);
	strcpy(ctx->ss->ident, ctx->id);
	ctx->ss->unique = ctx->unique;
	ctx->ss->mapped = (uintptr_t)ctx->ss;
	SHA256_Init(&ctx->ctx);
	SHA256_Update(&ctx->ctx, ctx->ss,
	    offsetof(struct smp_sign, length));
	smp_append_sign(ctx, NULL, 0);
}

/*--------------------------------------------------------------------
 * Force a write of a signature block to the backing store.
 */

static void
smp_sync_sign(const struct smp_signctx *ctx)
{
	int i;

	/* XXX: round to pages */
	i = msync((void*)ctx->ss, ctx->ss->length + SHA256_LEN, MS_SYNC);
	if (i && 0)
		fprintf(stderr, "SyncSign(%p %s) = %d %s\n",
		    ctx->ss, ctx->id, i, strerror(errno));
}

/*--------------------------------------------------------------------
 * Create and force a new signature to backing store
 */

static void
smp_new_sign(const struct smp_sc *sc, struct smp_signctx *ctx,
    uint64_t off, const char *id)
{
	smp_def_sign(sc, ctx, off, id);
	smp_reset_sign(ctx);
	smp_sync_sign(ctx);
}

/*--------------------------------------------------------------------
 * Caculate payload of some stuff
 */

static uint64_t
smp_stuff_len(const struct smp_sc *sc, unsigned stuff)
{
	uint64_t l;

	assert(stuff < SMP_END_STUFF);
	l = sc->ident->stuff[stuff + 1] - sc->ident->stuff[stuff];
	l -= SMP_SIGN_SPACE;
	return (l);
}

/*--------------------------------------------------------------------
 * Initialize a Silo with a valid but empty structure.
 *
 * XXX: more intelligent sizing of things.
 */

static void
smp_newsilo(struct smp_sc *sc)
{
	struct smp_ident	*si;

	ASSERT_MGT();
	assert(strlen(SMP_IDENT_STRING) < sizeof si->ident);

	/* Choose a new random number */
	sc->unique = random();

	smp_reset_sign(&sc->idn);
	si = sc->ident;

	memset(si, 0, sizeof *si);
	strcpy(si->ident, SMP_IDENT_STRING);
	si->byte_order = 0x12345678;
	si->size = sizeof *si;
	si->major_version = 2;
	si->unique = sc->unique;
	si->mediasize = sc->mediasize;
	si->granularity = sc->granularity;
	/*
	 * Aim for cache-line-width
	 */
	si->align = sizeof(void*) * 2;
	sc->align = si->align;

	si->stuff[SMP_BAN1_STUFF] = sc->granularity;
	si->stuff[SMP_BAN2_STUFF] = si->stuff[SMP_BAN1_STUFF] + 1024*1024;
	si->stuff[SMP_SEG1_STUFF] = si->stuff[SMP_BAN2_STUFF] + 1024*1024;
	si->stuff[SMP_SEG2_STUFF] = si->stuff[SMP_SEG1_STUFF] + 1024*1024;
	si->stuff[SMP_SPC_STUFF] = si->stuff[SMP_SEG2_STUFF] + 1024*1024;
	si->stuff[SMP_END_STUFF] = si->mediasize;
	assert(si->stuff[SMP_SPC_STUFF] < si->stuff[SMP_END_STUFF]);

	smp_new_sign(sc, &sc->ban1, si->stuff[SMP_BAN1_STUFF], "BAN 1");
	smp_new_sign(sc, &sc->ban2, si->stuff[SMP_BAN2_STUFF], "BAN 2");
	smp_new_sign(sc, &sc->seg1, si->stuff[SMP_SEG1_STUFF], "SEG 1");
	smp_new_sign(sc, &sc->seg2, si->stuff[SMP_SEG2_STUFF], "SEG 2");

	smp_append_sign(&sc->idn, si, sizeof *si);
	smp_sync_sign(&sc->idn);
}

/*--------------------------------------------------------------------
 * Check if a silo is valid.
 */

static int
smp_valid_silo(struct smp_sc *sc)
{
	struct smp_ident	*si;
	int i, j;

	assert(strlen(SMP_IDENT_STRING) < sizeof si->ident);

	if (smp_chk_sign(&sc->idn))
		return (1);

	si = sc->ident;
	if (strcmp(si->ident, SMP_IDENT_STRING))
		return (2);
	if (si->byte_order != 0x12345678)
		return (3);
	if (si->size != sizeof *si)
		return (4);
	if (si->major_version != 2)
		return (5);
	if (si->mediasize != sc->mediasize)
		return (7);
	if (si->granularity != sc->granularity)
		return (8);
	if (si->align < sizeof(void*))
		return (9);
	if (!PWR2(si->align))
		return (10);
	sc->align = si->align;
	sc->unique = si->unique;

	/* XXX: Sanity check stuff[6] */

	assert(si->stuff[SMP_BAN1_STUFF] > sizeof *si + SHA256_LEN);
	assert(si->stuff[SMP_BAN2_STUFF] > si->stuff[SMP_BAN1_STUFF]);
	assert(si->stuff[SMP_SEG1_STUFF] > si->stuff[SMP_BAN2_STUFF]);
	assert(si->stuff[SMP_SEG2_STUFF] > si->stuff[SMP_SEG1_STUFF]);
	assert(si->stuff[SMP_SPC_STUFF] > si->stuff[SMP_SEG2_STUFF]);
	assert(si->stuff[SMP_END_STUFF] == sc->mediasize);

	assert(smp_stuff_len(sc, SMP_SEG1_STUFF) > 65536);
	assert(smp_stuff_len(sc, SMP_SEG1_STUFF) ==
	  smp_stuff_len(sc, SMP_SEG2_STUFF));

	assert(smp_stuff_len(sc, SMP_BAN1_STUFF) > 65536);
	assert(smp_stuff_len(sc, SMP_BAN1_STUFF) ==
	  smp_stuff_len(sc, SMP_BAN2_STUFF));

	smp_def_sign(sc, &sc->ban1, si->stuff[SMP_BAN1_STUFF], "BAN 1");
	smp_def_sign(sc, &sc->ban2, si->stuff[SMP_BAN2_STUFF], "BAN 2");
	smp_def_sign(sc, &sc->seg1, si->stuff[SMP_SEG1_STUFF], "SEG 1");
	smp_def_sign(sc, &sc->seg2, si->stuff[SMP_SEG2_STUFF], "SEG 2");

	/* We must have one valid BAN table */
	i = smp_chk_sign(&sc->ban1);
	j = smp_chk_sign(&sc->ban2);
	if (i && j)
		return (100 + i * 10 + j);

	/* We must have one valid SEG table */
	i = smp_chk_sign(&sc->seg1);
	j = smp_chk_sign(&sc->seg2);
	if (i && j)
		return (200 + i * 10 + j);
	return (0);
}

/*--------------------------------------------------------------------
 * Calculate cleaner metrics from silo dimensions
 */

static void
smp_metrics(struct smp_sc *sc)
{

	/*
	 * We do not want to loose too big chunks of the silos
	 * content when we are forced to clean a segment.
	 *
	 * For now insist that a segment covers no more than 1% of the silo.
	 *
	 * XXX: This should possibly depend on the size of the silo so
	 * XXX: trivially small silos do not run into trouble along
	 * XXX: the lines of "one object per silo".
	 */

	sc->min_nseg = 10;
	sc->max_segl = smp_stuff_len(sc, SMP_SPC_STUFF) / sc->min_nseg;

	fprintf(stderr, "min_nseg = %u, max_segl = %ju\n",
	    sc->min_nseg, (uintmax_t)sc->max_segl);

	/*
	 * The number of segments are limited by the size of the segment
	 * table(s) and from that follows the minimum size of a segmement.
	 */

	sc->max_nseg = smp_stuff_len(sc, SMP_SEG1_STUFF) / sc->min_nseg;
	sc->min_segl = smp_stuff_len(sc, SMP_SPC_STUFF) / sc->max_nseg;

	while (sc->min_segl < sizeof(struct object)) {
		sc->max_nseg /= 2;
		sc->min_segl = smp_stuff_len(sc, SMP_SPC_STUFF) / sc->max_nseg;
	}

	fprintf(stderr, "max_nseg = %u, min_segl = %ju\n",
	    sc->max_nseg, (uintmax_t)sc->min_segl);

	/*
	 * Set our initial aim point at the exponential average of the
	 * two extremes.
	 *
	 * XXX: This is a pretty arbitrary choice, but having no idea
	 * XXX: object count, size distribution or ttl pattern at this
	 * XXX: point, we have to do something.
	 */

	sc->aim_nseg =
	   (unsigned) exp((log(sc->min_nseg) + log(sc->max_nseg))*.5);
	sc->aim_segl = smp_stuff_len(sc, SMP_SPC_STUFF) / sc->aim_nseg;

	fprintf(stderr, "aim_nseg = %u, aim_segl = %ju\n",
	    sc->aim_nseg, (uintmax_t)sc->aim_segl);

	/*
	 * Objects per segment
	 *
	 * XXX: calculate size of minimum object (workspace, http etc)
	 */

	sc->aim_nobj = sc->max_segl / 4000;

	fprintf(stderr, "aim_nobj = %u\n", sc->aim_nobj);

	/*
	 * How much space in the free reserve pool ?
	 */
	sc->free_reserve = sc->aim_segl * 10;

	fprintf(stderr, "free_reserve = %ju\n", sc->free_reserve);
}

/*--------------------------------------------------------------------
 * Set up persistent storage silo in the master process.
 */

static void
smp_init(struct stevedore *parent, int ac, char * const *av)
{
	struct smp_sc		*sc;
	int i;

	ASSERT_MGT();

	AZ(av[ac]);
#define SIZOF(foo)       fprintf(stderr, \
    "sizeof(%s) = %zu = 0x%zx\n", #foo, sizeof(foo), sizeof(foo));
	SIZOF(struct smp_ident);
	SIZOF(struct smp_sign);
	SIZOF(struct smp_segptr);
	SIZOF(struct smp_object);
#undef SIZOF

	/* See comments in persistent.h */
	assert(sizeof(struct smp_ident) == SMP_IDENT_SIZE);

	/* Allocate softc */
	ALLOC_OBJ(sc, SMP_SC_MAGIC);
	XXXAN(sc);
	sc->parent = parent;
	sc->fd = -1;
	VTAILQ_INIT(&sc->segments);

	/* Argument processing */
	if (ac != 2)
		ARGV_ERR("(-spersistent) wrong number of arguments\n");

	i = STV_GetFile(av[0], &sc->fd, &sc->filename, "-spersistent");
	if (i == 2)
		ARGV_ERR("(-spersistent) need filename (not directory)\n");

	sc->align = sizeof(void*) * 2;
	sc->granularity = getpagesize();
	sc->mediasize = STV_FileSize(sc->fd, av[1], &sc->granularity,
	    "-spersistent");

	AZ(ftruncate(sc->fd, sc->mediasize));

	sc->base = mmap(NULL, sc->mediasize, PROT_READ|PROT_WRITE,
	    MAP_NOCORE | MAP_NOSYNC | MAP_SHARED, sc->fd, 0);

	if (sc->base == MAP_FAILED)
		ARGV_ERR("(-spersistent) failed to mmap (%s)\n",
		    strerror(errno));

	smp_def_sign(sc, &sc->idn, 0, "SILO");
	sc->ident = SIGN_DATA(&sc->idn);

	i = smp_valid_silo(sc);
	if (i)
		smp_newsilo(sc);
	AZ(smp_valid_silo(sc));

	smp_metrics(sc);

	parent->priv = sc;

	/* XXX: only for sendfile I guess... */
	mgt_child_inherit(sc->fd, "storage_persistent");
}


/*--------------------------------------------------------------------
 * Write the segmentlist back to the silo.
 *
 * We write the first copy, sync it synchronously, then write the
 * second copy and sync it synchronously.
 *
 * Provided the kernel doesn't lie, that means we will always have
 * at least one valid copy on in the silo.
 */

static void
smp_save_seg(const struct smp_sc *sc, struct smp_signctx *ctx)
{
	struct smp_segptr *ss;
	struct smp_seg *sg;
	uint64_t length;

	Lck_AssertHeld(&sc->mtx);
	smp_reset_sign(ctx);
	ss = SIGN_DATA(ctx);
	length = 0;
	VTAILQ_FOREACH(sg, &sc->segments, list) {
		assert(sg->p.offset < sc->mediasize);
		assert(sg->p.offset + sg->p.length <= sc->mediasize);
		*ss = sg->p;
		ss++;
		length += sizeof *ss;
	}
	smp_append_sign(ctx, SIGN_DATA(ctx), length);
	smp_sync_sign(ctx);
}

static void
smp_save_segs(struct smp_sc *sc)
{
	struct smp_seg *sg, *sg2;

	Lck_AssertHeld(&sc->mtx);

	/*
	 * Remove empty segments from the front of the list
	 * before we write the segments to disk.
	 */
	VTAILQ_FOREACH_SAFE(sg, &sc->segments, list, sg2) {
		if (sg->nobj > 0)
			break;
		if (sg == sc->cur_seg)
			continue;
		VTAILQ_REMOVE(&sc->segments, sg, list);
		free(sg);
	}
	smp_save_seg(sc, &sc->seg1);
	smp_save_seg(sc, &sc->seg2);
}


/*---------------------------------------------------------------------
 */

static struct smp_object *
smp_find_so(const struct smp_seg *sg, const struct objcore *oc)
{
	struct smp_object *so;
	unsigned smp_idx;

	smp_idx = oc->priv2;
	assert(smp_idx > 0);
	assert(smp_idx <= sg->nalloc1);
	so = &sg->objs[sg->nalloc1 - smp_idx];
	return (so);
}

/*---------------------------------------------------------------------
 * objcore methods for persistent objects
 */

static struct object *
smp_oc_getobj(struct worker *wrk, struct objcore *oc)
{
	struct object *o;
	struct smp_seg *sg;
	struct smp_object *so;

	/* Some calls are direct, but they should match anyway */
	assert(oc->methods->getobj == smp_oc_getobj);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	if (wrk == NULL)
		AZ(oc->flags & OC_F_NEEDFIXUP);

	CAST_OBJ_NOTNULL(sg, oc->priv, SMP_SEG_MAGIC);
	so = smp_find_so(sg, oc);

	o = so->ptr;
	/*
	 * The object may not be in this segment since we allocate it
	 * In a separate operation than the smp_object.  We could check
	 * that it is in a later segment, but that would be complicated.
	 * XXX: For now, be happy if it is inside th silo
	 */
	ASSERT_PTR_IN_SILO(sg->sc, o);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);

	/*
	 * If this flag is not set, it will not be, and the lock is not
	 * needed to test it.
	 */
	if (!(oc->flags & OC_F_NEEDFIXUP))
		return (o);

	AN(wrk);
	Lck_Lock(&sg->sc->mtx);
	/* Check again, we might have raced. */
	if (oc->flags & OC_F_NEEDFIXUP) {
		/* refcnt is >=1 because the object is in the hash */
		o->objcore = oc;

		sg->nfixed++;
		wrk->stats.n_object++;
		wrk->stats.n_vampireobject--;
		oc->flags &= ~OC_F_NEEDFIXUP;
	}
	Lck_Unlock(&sg->sc->mtx);
	return (o);
}

static void
smp_oc_updatemeta(struct objcore *oc)
{
	struct object *o;
	struct smp_seg *sg;
	struct smp_object *so;
	double mttl;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	o = smp_oc_getobj(NULL, oc);
	AN(o);

	CAST_OBJ_NOTNULL(sg, oc->priv, SMP_SEG_MAGIC);
	CHECK_OBJ_NOTNULL(sg->sc, SMP_SC_MAGIC);
	so = smp_find_so(sg, oc);

	if (isnan(o->grace))
		mttl = o->ttl;
	else
		mttl = - (o->ttl + o->grace);

	if (sg == sg->sc->cur_seg) {
		/* Lock necessary, we might race close_seg */
		Lck_Lock(&sg->sc->mtx);
		so->ban = o->ban_t;
		so->ttl = mttl;
		Lck_Unlock(&sg->sc->mtx);
	} else {
		so->ban = o->ban_t;
		so->ttl = mttl;
	}
}

static void
smp_oc_freeobj(struct objcore *oc)
{
	struct smp_seg *sg;
	struct object *o;
	struct smp_object *so;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	o = smp_oc_getobj(NULL, oc);
	AN(o);

	CAST_OBJ_NOTNULL(sg, oc->priv, SMP_SEG_MAGIC);
	so = smp_find_so(sg, oc);

	Lck_Lock(&sg->sc->mtx);
	so->ttl = 0;
	so->ptr = 0;

	assert(sg->nobj > 0);
	assert(sg->nfixed > 0);
	sg->nobj--;
	sg->nfixed--;

	Lck_Unlock(&sg->sc->mtx);
}

static struct objcore_methods smp_oc_methods = {
	.getobj =		smp_oc_getobj,
	.updatemeta =		smp_oc_updatemeta,
	.freeobj =		smp_oc_freeobj,
};

/*--------------------------------------------------------------------
 * Add a new ban to all silos
 */

static void
smp_appendban(struct smp_sc *sc, struct smp_signctx *ctx, double t0,
    uint32_t flags, uint32_t len, const char *ban)
{
	uint8_t *ptr, *ptr2;

	(void)sc;
	ptr = ptr2 = SIGN_END(ctx);

	memcpy(ptr, "BAN", 4);
	ptr += 4;

	memcpy(ptr, &t0, sizeof t0);
	ptr += sizeof t0;

	memcpy(ptr, &flags, sizeof flags);
	ptr += sizeof flags;

	memcpy(ptr, &len, sizeof len);
	ptr += sizeof len;

	memcpy(ptr, ban, len);
	ptr += len;

	smp_append_sign(ctx, ptr2, ptr - ptr2);
}

void
SMP_NewBan(double t0, const char *ban)
{
	struct smp_sc *sc;
	uint32_t l = strlen(ban) + 1;

	VTAILQ_FOREACH(sc, &silos, list) {
		smp_appendban(sc, &sc->ban1, t0, 0, l, ban);
		smp_appendban(sc, &sc->ban2, t0, 0, l, ban);
	}
}

/*--------------------------------------------------------------------
 * Attempt to open and read in a ban list
 */

static int
smp_open_bans(struct smp_sc *sc, struct smp_signctx *ctx)
{
	uint8_t *ptr, *pe;
	double t0;
	uint32_t flags, length;
	int i, retval = 0;

	ASSERT_CLI();
	(void)sc;
	i = smp_chk_sign(ctx);
	if (i)
		return (i);
	ptr = SIGN_DATA(ctx);
	pe = ptr + ctx->ss->length;

	while (ptr < pe) {
		if (memcmp(ptr, "BAN", 4)) {
			retval = 1001;
			break;
		}
		ptr += 4;

		memcpy(&t0, ptr, sizeof t0);
		ptr += sizeof t0;

		memcpy(&flags, ptr, sizeof flags);
		ptr += sizeof flags;
		if (flags != 0) {
			retval = 1002;
			break;
		}

		memcpy(&length, ptr, sizeof length);
		ptr += sizeof length;
		if (ptr + length > pe) {
			retval = 1003;
			break;
		}

		if (ptr[length - 1] != '\0') {
			retval = 1004;
			break;
		}

		BAN_Reload(t0, flags, (const char *)ptr);

		ptr += length;
	}
	assert(ptr <= pe);
	return (retval);
}


/*--------------------------------------------------------------------*/

static uint64_t
smp_segend(const struct smp_seg *sg)
{

	return (sg->p.offset + sg->p.length);
}

static uint64_t
smp_spaceleft(const struct smp_sc *sc, const struct smp_seg *sg)
{

	IASSERTALIGN(sc, sg->next_bot);
	assert(sg->next_bot <= sg->next_top - IRNUP(sc, SMP_SIGN_SPACE));
	return ((sg->next_top - sg->next_bot) - IRNUP(sc, SMP_SIGN_SPACE));
}

/*--------------------------------------------------------------------
 * Load segments
 *
 * The overall objective is to register the existence of an object, based
 * only on the minimally sized struct smp_object, without causing the
 * main object to be faulted in.
 *
 * XXX: We can test this by mprotecting the main body of the segment
 * XXX: until the first fixup happens, or even just over this loop,
 * XXX: However: the requires that the smp_objects starter further
 * XXX: into the segment than a page so that they do not get hit
 * XXX: by the protection.
 */

static void
smp_load_seg(const struct sess *sp, const struct smp_sc *sc, struct smp_seg *sg)
{
	struct smp_object *so;
	struct objcore *oc;
	uint32_t no;
	double t_now = TIM_real();
	struct smp_signctx ctx[1];

	ASSERT_SILO_THREAD(sc);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sg, SMP_SEG_MAGIC);
	CHECK_OBJ_NOTNULL(sg->lru, LRU_MAGIC);
	assert(sg->must_load == 1);
	sg->must_load = 0;
	AN(sg->p.offset);
	if (sg->p.objlist == 0)
		return;
	smp_def_sign(sc, ctx, sg->p.offset, "SEGHEAD");
	if (smp_chk_sign(ctx))
		return;

	/* test SEGTAIL */
	/* test OBJIDX */
	so = (void*)(sc->base + sg->p.objlist);
	sg->objs = so;
	sg->nalloc1 = sg->p.nalloc;
	no = sg->p.nalloc;
	/* Clear the bogus "hold" count */
	sg->nobj = 0;
	for (;no > 0; so++,no--) {
		if (so->ttl > 0 && so->ttl < t_now)
			continue;
		if (so->ttl < 0 && -so->ttl < t_now)
			continue;
		HSH_Prealloc(sp);
		oc = sp->wrk->nobjcore;
		oc->flags |= OC_F_NEEDFIXUP | OC_F_LRUDONTMOVE;
		oc->flags &= ~OC_F_BUSY;
		oc->priv = sg;
		oc->priv2 = no;
		oc->methods = &smp_oc_methods;
		oc->ban = BAN_RefBan(oc, so->ban, sc->tailban);
		memcpy(sp->wrk->nobjhead->digest, so->hash, SHA256_LEN);
		(void)HSH_Insert(sp);
		AZ(sp->wrk->nobjcore);
		EXP_Inject(oc, sg->lru, fabs(so->ttl));
		sg->nobj++;
	}
	WRK_SumStat(sp->wrk);
}

/*--------------------------------------------------------------------
 * Attempt to open and read in a segment list
 */

static int
smp_open_segs(struct smp_sc *sc, struct smp_signctx *ctx)
{
	uint64_t length, l;
	struct smp_segptr *ss, *se;
	struct smp_seg *sg, *sg1, *sg2;
	int i, n = 0;

	ASSERT_CLI();
	i = smp_chk_sign(ctx);
	if (i)
		return (i);

	ss = SIGN_DATA(ctx);
	length = ctx->ss->length;

	if (length == 0) {
		/* No segments */
		sc->free_offset = sc->ident->stuff[SMP_SPC_STUFF];
		return (0);
	}
	se = ss + length / sizeof *ss;
	se--;
	assert(ss <= se);

	/*
	 * Locate the free reserve, there are only two basic cases,
	 * but once we start dropping segments, things gets more complicated.
	 */

	sc->free_offset = se->offset + se->length;
	l = sc->mediasize - sc->free_offset;
	if (se->offset > ss->offset && l >= sc->free_reserve) {
		/*
		 * [__xxxxyyyyzzzz___]
		 * Plenty of space at tail, do nothing.
		 */
	} else if (ss->offset > se->offset) {
		/*
		 * [zzzz____xxxxyyyy_]
		 * (make) space between ends
		 * We might nuke the entire tail end without getting
		 * enough space, in which case we fall through to the
		 * last check.
		 */
		while (ss < se && ss->offset > se->offset) {
			l = ss->offset - (se->offset + se->length);
			if (l > sc->free_reserve)
				break;
			ss++;
			n++;
		}
	}

	if (l < sc->free_reserve) {
		/*
		 * [__xxxxyyyyzzzz___]
		 * (make) space at front
		 */
		sc->free_offset = sc->ident->stuff[SMP_SPC_STUFF];
		while (ss < se) {
			l = ss->offset - sc->free_offset;
			if (l > sc->free_reserve)
				break;
			ss++;
			n++;
		}
	}

	assert (l >= sc->free_reserve);


	sg1 = NULL;
	sg2 = NULL;
	for(; ss <= se; ss++) {
		ALLOC_OBJ(sg, SMP_SEG_MAGIC);
		AN(sg);
		sg->lru = LRU_Alloc();
		CHECK_OBJ_NOTNULL(sg->lru, LRU_MAGIC);
		sg->p = *ss;

		sg->must_load = 1;

		/*
		 * HACK: prevent save_segs from nuking segment until we have
		 * HACK: loaded it.
		 */
		sg->nobj = 1;
		if (sg1 != NULL) {
			assert(sg1->p.offset != sg->p.offset);
			if (sg1->p.offset < sg->p.offset)
				assert(smp_segend(sg1) <= sg->p.offset);
			else
				assert(smp_segend(sg) <= sg1->p.offset);
		}
		if (sg2 != NULL) {
			assert(sg2->p.offset != sg->p.offset);
			if (sg2->p.offset < sg->p.offset)
				assert(smp_segend(sg2) <= sg->p.offset);
			else
				assert(smp_segend(sg) <= sg2->p.offset);
		}

		/* XXX: check that they are inside silo */
		/* XXX: check that they don't overlap */
		/* XXX: check that they are serial */
		sg->sc = sc;
		VTAILQ_INSERT_TAIL(&sc->segments, sg, list);
		sg2 = sg;
		if (sg1 == NULL)
			sg1 = sg;
	}
	printf("Dropped %d segments to make free_reserve\n", n);
	return (0);
}

/*--------------------------------------------------------------------
 * Create a new segment
 */

static void
smp_new_seg(struct smp_sc *sc)
{
	struct smp_seg *sg, *sg2;

	Lck_AssertHeld(&sc->mtx);
	ALLOC_OBJ(sg, SMP_SEG_MAGIC);
	AN(sg);
	sg->sc = sc;
	sg->lru = LRU_Alloc();
	CHECK_OBJ_NOTNULL(sg->lru, LRU_MAGIC);

	/* XXX: find where it goes in silo */

	sg->p.offset = sc->free_offset;
	// XXX: align */
	assert(sg->p.offset >= sc->ident->stuff[SMP_SPC_STUFF]);
	assert(sg->p.offset < sc->mediasize);

	sg->p.length = sc->aim_segl;
	sg->p.length &= ~7;

	if (smp_segend(sg) > sc->mediasize) {
		sc->free_offset = sc->ident->stuff[SMP_SPC_STUFF];
		sg->p.offset = sc->free_offset;
		sg2 = VTAILQ_FIRST(&sc->segments);
		if (smp_segend(sg) > sg2->p.offset) {
			printf("Out of space in persistent silo\n");
			printf("Committing suicide, restart will make space\n");
			exit (0);
		}
	}


	assert(smp_segend(sg) <= sc->mediasize);

	sg2 = VTAILQ_FIRST(&sc->segments);
	if (sg2 != NULL && sg2->p.offset > sc->free_offset) {
		if (smp_segend(sg) > sg2->p.offset) {
			printf("Out of space in persistent silo\n");
			printf("Committing suicide, restart will make space\n");
			exit (0);
		}
		assert(smp_segend(sg) <= sg2->p.offset);
	}

	sg->p.offset = IRNUP(sc, sg->p.offset);
	sg->p.length = IRNDN(sc, sg->p.length);
	sc->free_offset = sg->p.offset + sg->p.length;

	VTAILQ_INSERT_TAIL(&sc->segments, sg, list);

	/* Neuter the new segment in case there is an old one there */
	AN(sg->p.offset);
	smp_def_sign(sc, sg->ctx, sg->p.offset, "SEGHEAD");
	smp_reset_sign(sg->ctx);
	smp_sync_sign(sg->ctx);

	/* Set up our allocation points */
	sc->cur_seg = sg;
	sg->next_bot = sg->p.offset + IRNUP(sc, SMP_SIGN_SPACE);
	sg->next_top = smp_segend(sg);
	sg->next_top -= IRNUP(sc, SMP_SIGN_SPACE);
	IASSERTALIGN(sc, sg->next_bot);
	IASSERTALIGN(sc, sg->next_top);
	sg->objs = (void*)(sc->base + sg->next_top);
	sg->nalloc1 = 0;
}

/*--------------------------------------------------------------------
 * Close a segment
 */

static void
smp_close_seg(struct smp_sc *sc, struct smp_seg *sg)
{
	uint64_t left, dst, len;
	void *dp;

	Lck_AssertHeld(&sc->mtx);

	assert(sg == sc->cur_seg);
	AN(sg->p.offset);
	sc->cur_seg = NULL;

	if (sg->nalloc == 0) {
		/* XXX: if segment is empty, delete instead */
		VTAILQ_REMOVE(&sc->segments, sg, list);
		free(sg);
		return;
	}

	assert(sg->next_bot <= sg->next_top - IRNUP(sc, SMP_SIGN_SPACE));
	IASSERTALIGN(sc, sg->next_bot);

	/*
	 * If there is enough space left, that we can move the smp_objects
	 * down without overwriting the present copy, we will do so to
	 * compact the segment.
	 */
	left = smp_spaceleft(sc, sg);
	len = sizeof(struct smp_object) * sg->nalloc1;
	if (len < left) {
		dst = sg->next_bot + IRNUP(sc, SMP_SIGN_SPACE);
		dp = sc->base + dst;
		assert((uintptr_t)dp + len < (uintptr_t)sg->objs);
		memcpy(dp, sg->objs, len);
		sg->next_top = dst;
		sg->objs = dp;
		sg->p.length = sg->next_top + len + IRNUP(sc, SMP_SIGN_SPACE);
		(void)smp_spaceleft(sc, sg); 	/* for asserts */
		
	}

	/* Update the segment header */
	sg->p.objlist = sg->next_top;
	sg->p.nalloc = sg->nalloc1;

	/* Write the (empty) OBJIDX signature */
	sg->next_top -= IRNUP(sc, SMP_SIGN_SPACE);
	assert(sg->next_top >= sg->next_bot);
	smp_def_sign(sc, sg->ctx, sg->next_top, "OBJIDX");
	smp_reset_sign(sg->ctx);
	smp_sync_sign(sg->ctx);

	/* Write the (empty) SEGTAIL signature */
	smp_def_sign(sc, sg->ctx,
	    sg->p.offset + sg->p.length - IRNUP(sc, SMP_SIGN_SPACE), "SEGTAIL");
	smp_reset_sign(sg->ctx);
	smp_sync_sign(sg->ctx);

	/* Save segment list */
	smp_save_segs(sc);
	sc->free_offset = smp_segend(sg);
}

/*--------------------------------------------------------------------
 * Silo worker thread
 */

static void *
smp_thread(struct sess *sp, void *priv)
{
	struct smp_sc	*sc;
	struct smp_seg *sg;

	(void)sp;
	CAST_OBJ_NOTNULL(sc, priv, SMP_SC_MAGIC);

	/* First, load all the objects from all segments */
	VTAILQ_FOREACH(sg, &sc->segments, list)
		if (sg->must_load)
			smp_load_seg(sp, sc, sg);

	sc->flags |= SMP_F_LOADED;
	BAN_Deref(&sc->tailban);
	sc->tailban = NULL;
	printf("Silo completely loaded\n");
	while (1)
		(void)sleep (1);
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------
 * Open a silo in the worker process
 */

static void
smp_open(const struct stevedore *st)
{
	struct smp_sc	*sc;

	ASSERT_CLI();

	CAST_OBJ_NOTNULL(sc, st->priv, SMP_SC_MAGIC);

	Lck_New(&sc->mtx, lck_smp);
	Lck_Lock(&sc->mtx);

	sc->stevedore = st;

	/* We trust the parent to give us a valid silo, for good measure: */
	AZ(smp_valid_silo(sc));

	AZ(mprotect(sc->base, 4096, PROT_READ));

	sc->ident = SIGN_DATA(&sc->idn);

	/* We attempt ban1 first, and if that fails, try ban2 */
	if (smp_open_bans(sc, &sc->ban1))
		AZ(smp_open_bans(sc, &sc->ban2));

	/* We attempt seg1 first, and if that fails, try seg2 */
	if (smp_open_segs(sc, &sc->seg1))
		AZ(smp_open_segs(sc, &sc->seg2));

	sc->tailban = BAN_TailRef();
	AN(sc->tailban);

	/* XXX: save segments to ensure consistency between seg1 & seg2 ? */

	/* XXX: abandon early segments to make sure we have free space ? */

	/* Open a new segment, so we are ready to write */
	smp_new_seg(sc);

	/* Start the worker silo worker thread, it will load the objects */
	WRK_BgThread(&sc->thread, "persistence", smp_thread, sc);

	VTAILQ_INSERT_TAIL(&silos, sc, list);
	Lck_Unlock(&sc->mtx);
}

/*--------------------------------------------------------------------
 * Close a silo
 */

static void
smp_close(const struct stevedore *st)
{
	struct smp_sc	*sc;

	ASSERT_CLI();

	CAST_OBJ_NOTNULL(sc, st->priv, SMP_SC_MAGIC);
	Lck_Lock(&sc->mtx);
	smp_close_seg(sc, sc->cur_seg);
	Lck_Unlock(&sc->mtx);

	/* XXX: reap thread */
}

/*--------------------------------------------------------------------
 * Allocate a bite.
 *
 * Allocate [min_size...max_size] space from the bottom of the segment,
 * as is convenient.
 *
 * If 'so' + 'idx' is given, also allocate a smp_object from the top 
 * of the segment.
 *
 * Return the segment in 'ssg' if given.
 */

static struct storage *
smp_allocx(struct stevedore *st, size_t min_size, size_t max_size,
    struct smp_object **so, unsigned *idx, struct smp_seg **ssg)
{
	struct smp_sc *sc;
	struct storage *ss;
	struct smp_seg *sg;
	unsigned tries;
	uint64_t left, extra;

	CAST_OBJ_NOTNULL(sc, st->priv, SMP_SC_MAGIC);
	assert(min_size <= max_size);

	max_size = IRNUP(sc, max_size);
	min_size = IRNUP(sc, min_size);

	extra = IRNUP(sc, sizeof(*ss));
	if (so != NULL) {
		extra += sizeof(**so);
		AN(idx);
	}

	Lck_Lock(&sc->mtx);
	sg = NULL;
	ss = NULL;
	for (tries = 0; tries < 3; tries++) {
		left = smp_spaceleft(sc, sc->cur_seg);
		if (left >= extra + min_size) 
			break;
		smp_close_seg(sc, sc->cur_seg);
		smp_new_seg(sc);
	}
	if (left >= extra + min_size)  {
		if (left < extra + max_size)
			max_size = IRNDN(sc, left - extra);

		sg = sc->cur_seg;
		ss = (void*)(sc->base + sg->next_bot);
		sg->next_bot += max_size + IRNUP(sc, sizeof(*ss));
		sg->nalloc++;
		if (so != NULL) {
			sg->next_top -= sizeof(**so);
			*so = (void*)(sc->base + sg->next_top);
			/* Render this smp_object mostly harmless */
			(*so)->ttl = 0.;
			(*so)->ban = 0.;
			(*so)->ptr = NULL;
			sg->objs = *so;
			*idx = ++sg->nalloc1;
		}
		(void)smp_spaceleft(sc, sg);	/* for the assert */
	}
	Lck_Unlock(&sc->mtx);

	if (ss == NULL)
		return (ss);
	AN(sg);
	assert(max_size >= min_size);

	/* Fill the storage structure */
	memset(ss, 0, sizeof *ss);
	ss->magic = STORAGE_MAGIC;
	ss->ptr = PRNUP(sc, ss + 1);
	ss->space = max_size;
	ss->priv = sc;
	ss->stevedore = st;
	ss->fd = sc->fd;
	if (ssg != NULL)
		*ssg = sg;
	return (ss);
}

/*--------------------------------------------------------------------
 * Find the per-segment lru list for this object
 */

static struct lru *
smp_getlru(const struct object *o)
{
	struct smp_seg *sg;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	CAST_OBJ_NOTNULL(sg, o->objcore->priv, SMP_SEG_MAGIC);
	return (sg->lru);
}

/*--------------------------------------------------------------------
 * Allocate an object
 */

static struct object *
smp_allocobj(struct stevedore *stv, struct sess *sp, unsigned ltot,
    const struct stv_objsecrets *soc)
{
	struct object *o;
	struct storage *st;
	struct smp_sc	*sc;
	struct smp_seg *sg;
	struct smp_object *so;
	struct objcore *oc;
	unsigned objidx;

	CAST_OBJ_NOTNULL(sc, stv->priv, SMP_SC_MAGIC);
	AN(sp->objcore);
	AN(sp->wrk->ttl >= 0);

	ltot = IRNUP(sc, ltot);

	st = smp_allocx(stv, ltot, ltot, &so, &objidx, &sg);
	if (st == NULL)
		return (NULL);

	assert(st->space >= ltot);
	ltot = st->len = st->space;

	o = STV_MkObject(sp, st->ptr, ltot, soc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	o->objstore = st;

	oc = o->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oc->flags |= OC_F_LRUDONTMOVE;

	Lck_Lock(&sc->mtx);
	sg->nfixed++;
	sg->nobj++;

	/* We have to do this somewhere, might as well be here... */
	assert(sizeof so->hash == DIGEST_LEN);
	memcpy(so->hash, oc->objhead->digest, DIGEST_LEN);
	so->ttl = o->ttl;	/* XXX: grace? */
	so->ptr = o;
	so->ban = o->ban_t;

	oc->priv = sg;
	oc->priv2 = objidx;
	oc->methods = &smp_oc_methods;

	Lck_Unlock(&sc->mtx);
	return (o);
}

/*--------------------------------------------------------------------
 * Allocate a bite
 */

static struct storage *
smp_alloc(struct stevedore *st, size_t size)
{

	return (smp_allocx(st, 4096, size, NULL, NULL, NULL));
}

/*--------------------------------------------------------------------
 * Trim a bite
 * XXX: We could trim the last allocation.
 */

static void
smp_trim(struct storage *ss, size_t size)
{

	(void)ss;
	(void)size;
}

/*--------------------------------------------------------------------
 * We don't track frees of storage, we track the objects which own the
 * storage and when there are no more objects in in the first segment,
 * it can be reclaimed.
 * XXX: We could free the last allocation, but does that happen ?
 */

static void __match_proto__(storage_free_f)
smp_free(struct storage *st)
{

	/* XXX */
	(void)st;
}

/*--------------------------------------------------------------------
 * Pause until all silos have loaded.
 */

void
SMP_Ready(void)
{
	struct smp_sc *sc;

	ASSERT_CLI();
	do {
		VTAILQ_FOREACH(sc, &silos, list) 
			if (!(sc->flags & SMP_F_LOADED))
				break;
		if (sc != NULL)
			(void)sleep(1);
	} while (sc != NULL);
}

/*--------------------------------------------------------------------*/

const struct stevedore smp_stevedore = {
	.magic	=	STEVEDORE_MAGIC,
	.name	=	"persistent",
	.init	=	smp_init,
	.open	=	smp_open,
	.close	=	smp_close,
	.alloc	=	smp_alloc,
	.allocobj =	smp_allocobj,
	.getlru	=	smp_getlru,
	.free	=	smp_free,
	.trim	=	smp_trim,
};

/*--------------------------------------------------------------------*/

static void
debug_persistent(struct cli *cli, const char * const * av, void *priv)
{
	struct smp_sc *sc;
	struct smp_seg *sg;

	(void)priv;

	if (av[2] == NULL || av[3] == NULL) {
		VTAILQ_FOREACH(sc, &silos, list) {
			if (av[2] != NULL &&
			    strcmp(av[2], sc->stevedore->ident))
				continue;
			cli_out(cli, "Silo: %s (%s)\n",
			    sc->stevedore->ident, sc->filename);
			VTAILQ_FOREACH(sg, &sc->segments, list) {
				cli_out(cli, "   Seg: %p\n", sg);
			}
		}
		return;
	}
}

static struct cli_proto debug_cmds[] = {
        { "debug.persistent", "debug.persistent",
                "Persistent debugging magic\n", 0, 2, "d", debug_persistent },
        { NULL }
};

void
SMP_Init(void)
{
	CLI_AddFuncs(debug_cmds);
}
