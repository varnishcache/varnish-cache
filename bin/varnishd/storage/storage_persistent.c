/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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

#include <sys/param.h>
#include <sys/mman.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache/cache.h"
#include "storage/storage.h"

#include "hash/hash_slinger.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vsha256.h"
#include "vtim.h"

#include "persistent.h"
#include "storage/storage_persistent.h"

/*--------------------------------------------------------------------*/

/*
 * silos is unlocked, it only changes during startup when we are
 * single-threaded
 */
static VTAILQ_HEAD(,smp_sc)	silos = VTAILQ_HEAD_INITIALIZER(silos);

/*--------------------------------------------------------------------
 * Add bans to silos
 */

static int
smp_appendban(struct smp_sc *sc, struct smp_signspace *spc,
    uint32_t len, const uint8_t *ban)
{

	(void)sc;
	if (SIGNSPACE_FREE(spc) < len)
		return (-1);

	memcpy(SIGNSPACE_FRONT(spc), ban, len);
	smp_append_signspace(spc, len);

	return (0);
}

/* Trust that cache_ban.c takes care of locking */

static int
smp_baninfo(const struct stevedore *stv, enum baninfo event,
	    const uint8_t *ban, unsigned len)
{
	struct smp_sc *sc;
	int r = 0;

	CAST_OBJ_NOTNULL(sc, stv->priv, SMP_SC_MAGIC);

	switch (event) {
	case BI_NEW:
		r |= smp_appendban(sc, &sc->ban1, len, ban);
		r |= smp_appendban(sc, &sc->ban2, len, ban);
		break;
	default:
		/* Ignored */
		break;
	}

	return (r);
}

static void
smp_banexport_spc(struct smp_signspace *spc, const uint8_t *bans, unsigned len)
{
	smp_reset_signspace(spc);
	assert(SIGNSPACE_FREE(spc) >= len);
	memcpy(SIGNSPACE_DATA(spc), bans, len);
	smp_append_signspace(spc, len);
	smp_sync_sign(&spc->ctx);
}

static void
smp_banexport(const struct stevedore *stv, const uint8_t *bans, unsigned len)
{
	struct smp_sc *sc;

	CAST_OBJ_NOTNULL(sc, stv->priv, SMP_SC_MAGIC);
	smp_banexport_spc(&sc->ban1, bans, len);
	smp_banexport_spc(&sc->ban2, bans, len);
}

/*--------------------------------------------------------------------
 * Attempt to open and read in a ban list
 */

static int
smp_open_bans(struct smp_sc *sc, struct smp_signspace *spc)
{
	uint8_t *ptr, *pe;
	int i;

	ASSERT_CLI();
	(void)sc;
	i = smp_chk_signspace(spc);
	if (i)
		return (i);

	ptr = SIGNSPACE_DATA(spc);
	pe = SIGNSPACE_FRONT(spc);
	BAN_Reload(ptr, pe - ptr);

	return (0);
}

/*--------------------------------------------------------------------
 * Attempt to open and read in a segment list
 */

static int
smp_open_segs(struct smp_sc *sc, struct smp_signspace *spc)
{
	uint64_t length, l;
	struct smp_segptr *ss, *se;
	struct smp_seg *sg, *sg1, *sg2;
	int i, n = 0;

	ASSERT_CLI();
	i = smp_chk_signspace(spc);
	if (i)
		return (i);

	ss = SIGNSPACE_DATA(spc);
	length = SIGNSPACE_LEN(spc);

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

	assert(l >= sc->free_reserve);


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

static void * __match_proto__(bgthread_t)
smp_thread(struct worker *wrk, void *priv)
{
	struct smp_sc	*sc;
	struct smp_seg *sg;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(sc, priv, SMP_SC_MAGIC);
	sc->thread = pthread_self();

	/* First, load all the objects from all segments */
	VTAILQ_FOREACH(sg, &sc->segments, list)
		if (sg->flags & SMP_SEG_MUSTLOAD)
			smp_load_seg(wrk, sc, sg);

	sc->flags |= SMP_SC_LOADED;
	BAN_TailDeref(&sc->tailban);
	AZ(sc->tailban);
	printf("Silo completely loaded\n");

	/* Housekeeping loop */
	Lck_Lock(&sc->mtx);
	while (!(sc->flags & SMP_SC_STOP)) {
		sg = VTAILQ_FIRST(&sc->segments);
		if (sg != NULL && sg != sc->cur_seg && sg->nobj == 0)
			smp_save_segs(sc);

		Lck_Unlock(&sc->mtx);
		VTIM_sleep(3.14159265359 - 2);
		Lck_Lock(&sc->mtx);
	}

	smp_save_segs(sc);

	Lck_Unlock(&sc->mtx);
	pthread_exit(0);

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

	AZ(mprotect((void*)sc->base, 4096, PROT_READ));

	sc->ident = SIGN_DATA(&sc->idn);

	/* Check ban lists */
	if (smp_chk_signspace(&sc->ban1)) {
		/* Ban list 1 is broken, use ban2 */
		AZ(smp_chk_signspace(&sc->ban2));
		smp_copy_signspace(&sc->ban1, &sc->ban2);
		smp_sync_sign(&sc->ban1.ctx);
	} else {
		/* Ban1 is OK, copy to ban2 for consistency */
		smp_copy_signspace(&sc->ban2, &sc->ban1);
		smp_sync_sign(&sc->ban2.ctx);
	}
	AZ(smp_open_bans(sc, &sc->ban1));

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
	WRK_BgThread(&sc->bgthread, "persistence", smp_thread, sc);

	VTAILQ_INSERT_TAIL(&silos, sc, list);
	Lck_Unlock(&sc->mtx);
}

/*--------------------------------------------------------------------
 * Close a silo
 */

static void
smp_signal_close(const struct stevedore *st)
{
	struct smp_sc	*sc;

	ASSERT_CLI();

	CAST_OBJ_NOTNULL(sc, st->priv, SMP_SC_MAGIC);
	Lck_Lock(&sc->mtx);
	if (sc->cur_seg != NULL)
		smp_close_seg(sc, sc->cur_seg);
	AZ(sc->cur_seg);
	sc->flags |= SMP_SC_STOP;
	Lck_Unlock(&sc->mtx);
}

static void
smp_close(const struct stevedore *st)
{
	struct smp_sc	*sc;
	void *status;

	ASSERT_CLI();

	CAST_OBJ_NOTNULL(sc, st->priv, SMP_SC_MAGIC);

	AZ(pthread_join(sc->bgthread, &status));
	AZ(status);
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

	left = 0;
	if (sc->cur_seg != NULL)
		left = smp_spaceleft(sc, sc->cur_seg);
	if (left < extra + min_size) {
		if (sc->cur_seg != NULL)
			smp_close_seg(sc, sc->cur_seg);
		smp_new_seg(sc);
		if (sc->cur_seg != NULL)
			left = smp_spaceleft(sc, sc->cur_seg);
		else
			left = 0;
	}

	if (left >= extra + min_size)  {
		AN(sc->cur_seg);
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
			(*so)->ptr = 0;
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
	if (ssg != NULL)
		*ssg = sg;
	return (ss);
}

/*--------------------------------------------------------------------
 * Allocate an object
 */

static struct object *
smp_allocobj(struct stevedore *stv, struct busyobj *bo,
    unsigned ltot, const struct stv_objsecrets *soc)
{
	struct object *o;
	struct storage *st;
	struct smp_sc	*sc;
	struct smp_seg *sg;
	struct smp_object *so;
	struct objcore *oc;
	unsigned objidx;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (bo->fetch_objcore == NULL)
		return (NULL);		/* from cnt_error */
	CAST_OBJ_NOTNULL(sc, stv->priv, SMP_SC_MAGIC);
	AN((bo->exp.ttl + bo->exp.grace + bo->exp.keep) > 0.);

	ltot = IRNUP(sc, ltot);

	st = smp_allocx(stv, ltot, ltot, &so, &objidx, &sg);
	if (st == NULL)
		return (NULL);

	assert(st->space >= ltot);
	ltot = st->len = st->space;

	o = STV_MkObject(stv, bo, st->ptr, ltot, soc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	o->objstore = st;

	oc = o->objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	Lck_Lock(&sc->mtx);
	sg->nfixed++;
	sg->nobj++;

	/* We have to do this somewhere, might as well be here... */
	assert(sizeof so->hash == DIGEST_LEN);
	memcpy(so->hash, oc->objhead->digest, DIGEST_LEN);
	so->ttl = oc->timer_when;
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
	.signal_close = smp_signal_close,
	.baninfo =	smp_baninfo,
	.banexport =	smp_banexport,
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

	VCLI_Out(cli, "Silo: %s (%s)\n",
	    sc->stevedore->ident, sc->filename);
	VTAILQ_FOREACH(sg, &sc->segments, list) {
		VCLI_Out(cli, "  Seg: [0x%jx ... +0x%jx]\n",
		   (uintmax_t)sg->p.offset, (uintmax_t)sg->p.length);
		if (sg == sc->cur_seg)
			VCLI_Out(cli,
			   "    Alloc: [0x%jx ... 0x%jx] = 0x%jx free\n",
			   (uintmax_t)(sc->next_bot),
			   (uintmax_t)(sc->next_top),
			   (uintmax_t)(sc->next_top - sc->next_bot));
		VCLI_Out(cli, "    %u nobj, %u alloc, %u lobjlist, %u fixed\n",
		    sg->nobj, sg->nalloc, sg->p.lobjlist, sg->nfixed);
		if (objs) {
			VTAILQ_FOREACH(oc, &sg->lru->lru_head, lru_list)
				VCLI_Out(cli, "      OC %p\n", oc);
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
		VCLI_Out(cli, "Silo <%s> not found\n", av[2]);
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	if (av[3] == NULL) {
		debug_report_silo(cli, sc, 0);
		return;
	}
	Lck_Lock(&sc->mtx);
	if (!strcmp(av[3], "sync")) {
		if (sc->cur_seg != NULL)
			smp_close_seg(sc, sc->cur_seg);
		smp_new_seg(sc);
	} else if (!strcmp(av[3], "dump")) {
		debug_report_silo(cli, sc, 1);
	} else {
		VCLI_Out(cli, "Unknown operation\n");
		VCLI_SetResult(cli, CLIS_PARAM);
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
