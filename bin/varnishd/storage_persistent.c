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

#include <errno.h>
#include <math.h>
#include <stdio.h>
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
#include "vend.h"

#include "persistent.h"
#include "storage_persistent.h"

/*--------------------------------------------------------------------*/

/*
 * silos is unlocked, it only changes during startup when we are
 * single-threaded
 */
static VTAILQ_HEAD(,smp_sc)	silos = VTAILQ_HEAD_INITIALIZER(silos);

/*--------------------------------------------------------------------
 * Add bans to silos
 */

static void
smp_appendban(struct smp_sc *sc, struct smp_signctx *ctx,
    uint32_t len, const uint8_t *ban)
{
	uint8_t *ptr, *ptr2;

	(void)sc;
	ptr = ptr2 = SIGN_END(ctx);

	memcpy(ptr, "BAN", 4);
	ptr += 4;

	vbe32enc(ptr, len);
	ptr += 4;

	memcpy(ptr, ban, len);
	ptr += len;

	smp_append_sign(ctx, ptr2, ptr - ptr2);
}

/* Trust that cache_ban.c takes care of locking */

void
SMP_NewBan(const uint8_t *ban, unsigned ln)
{
	struct smp_sc *sc;

	VTAILQ_FOREACH(sc, &silos, list) {
		smp_appendban(sc, &sc->ban1, ln, ban);
		smp_appendban(sc, &sc->ban2, ln, ban);
	}
}

/*--------------------------------------------------------------------
 * Attempt to open and read in a ban list
 */

static int
smp_open_bans(struct smp_sc *sc, struct smp_signctx *ctx)
{
	uint8_t *ptr, *pe;
	uint32_t length;
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

		length = vbe32dec(ptr);
		ptr += 4;

		if (ptr + length > pe) {
			retval = 1003;
			break;
		}

		BAN_Reload(ptr, length);

		ptr += length;
	}
	assert(ptr <= pe);
	return (retval);
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

		sg->flags |= SMP_SEG_MUSTLOAD;

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
		if (sg->flags & SMP_SEG_MUSTLOAD)
			smp_load_seg(sp, sc, sg);

	sc->flags |= SMP_SC_LOADED;
	BAN_TailDeref(&sc->tailban);
	AZ(sc->tailban);
	printf("Silo completely loaded\n");
	while (1) {
		(void)sleep (1);
		sg = VTAILQ_FIRST(&sc->segments);
		if (sg != NULL && sg -> sc->cur_seg &&
		    sg->nobj == 0) {
			Lck_Lock(&sc->mtx);
			smp_save_segs(sc);
			Lck_Unlock(&sc->mtx);
		}
	}
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

	/*
	 * Grap a reference to the tail of the ban list, until the thread
	 * has loaded all objects, so we can be sure that all of our
	 * proto-bans survive until then.
	 */
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
		ss = (void*)(sc->base + sc->next_bot);
		sc->next_bot += max_size + IRNUP(sc, sizeof(*ss));
		sg->nalloc++;
		if (so != NULL) {
			sc->next_top -= sizeof(**so);
			*so = (void*)(sc->base + sc->next_top);
			/* Render this smp_object mostly harmless */
			(*so)->ttl = 0.;
			(*so)->ban = 0.;
			(*so)->ptr = 0;;
			sg->objs = *so;
			*idx = ++sg->p.lobjlist;
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
	AN(sp->wrk->exp.ttl > 0.);

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
	so->ttl = EXP_Grace(NULL, o);
	so->ptr = (uint8_t*)o - sc->base;
	so->ban = BAN_Time(oc->ban);

	smp_init_oc(oc, sg, objidx);

	Lck_Unlock(&sc->mtx);
	return (o);
}

/*--------------------------------------------------------------------
 * Allocate a bite
 */

static struct storage *
smp_alloc(struct stevedore *st, size_t size)
{

	return (smp_allocx(st,
	    size > 4096 ? 4096 : size, size, NULL, NULL, NULL));
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


/*--------------------------------------------------------------------*/

const struct stevedore smp_stevedore = {
	.magic	=	STEVEDORE_MAGIC,
	.name	=	"persistent",
	.init	=	smp_mgt_init,
	.open	=	smp_open,
	.close	=	smp_close,
	.alloc	=	smp_alloc,
	.allocobj =	smp_allocobj,
	.free	=	smp_free,
	.trim	=	smp_trim,
};

/*--------------------------------------------------------------------
 * Persistence is a bear to test unadultered, so we cheat by adding
 * a cli command we can use to make it do tricks for us.
 */

static void
debug_report_silo(struct cli *cli, const struct smp_sc *sc, int objs)
{
	struct smp_seg *sg;
	struct objcore *oc;

	cli_out(cli, "Silo: %s (%s)\n",
	    sc->stevedore->ident, sc->filename);
	VTAILQ_FOREACH(sg, &sc->segments, list) {
		cli_out(cli, "  Seg: [0x%jx ... +0x%jx]\n",
		   (uintmax_t)sg->p.offset, (uintmax_t)sg->p.length);
		if (sg == sc->cur_seg)
			cli_out(cli,
			   "    Alloc: [0x%jx ... 0x%jx] = 0x%jx free\n",
			   (uintmax_t)(sc->next_bot),
			   (uintmax_t)(sc->next_top),
			   (uintmax_t)(sc->next_top - sc->next_bot));
		cli_out(cli, "    %u nobj, %u alloc, %u lobjlist, %u fixed\n",
		    sg->nobj, sg->nalloc, sg->p.lobjlist, sg->nfixed);
		if (objs) {
			VTAILQ_FOREACH(oc, &sg->lru->lru_head, lru_list)
				cli_out(cli, "      OC %p\n", oc);
		}
	}
}

static void
debug_persistent(struct cli *cli, const char * const * av, void *priv)
{
	struct smp_sc *sc;

	(void)priv;

	if (av[2] == NULL) {
		VTAILQ_FOREACH(sc, &silos, list)
			debug_report_silo(cli, sc, 0);
		return;
	}
	VTAILQ_FOREACH(sc, &silos, list)
		if (!strcmp(av[2], sc->stevedore->ident))
			break;
	if (sc == NULL) {
		cli_out(cli, "Silo <%s> not found\n", av[2]);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	if (av[3] == NULL) {
		debug_report_silo(cli, sc, 0);
		return;
	}
	Lck_Lock(&sc->mtx);
	if (!strcmp(av[3], "sync")) {
		smp_close_seg(sc, sc->cur_seg);
		smp_new_seg(sc);
	} else if (!strcmp(av[3], "dump")) {
		debug_report_silo(cli, sc, 1);
	} else {
		cli_out(cli, "Unknown operation\n");
		cli_result(cli, CLIS_PARAM);
	}
	Lck_Unlock(&sc->mtx);
}

static struct cli_proto debug_cmds[] = {
        { "debug.persistent", "debug.persistent",
                "Persistent debugging magic:\n"
		"\tdebug.persistent [stevedore [cmd]]\n"
		"With no cmd arg, a summary of the silo is returned.\n"
		"Possible commands:\n"
		"\tsync\tClose current segment, open a new one\n"
		"\tdump\tinclude objcores in silo summary\n"
		"",
		0, 2, "d", debug_persistent },
        { NULL }
};

/*--------------------------------------------------------------------*/

void
SMP_Init(void)
{
	CLI_AddFuncs(debug_cmds);
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
			if (!(sc->flags & SMP_SC_LOADED))
				break;
		if (sc != NULL)
			(void)sleep(1);
	} while (sc != NULL);
}
