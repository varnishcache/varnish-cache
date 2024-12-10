/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include "cache_varnishd.h"

#include "cache_ban.h"
#include "cache_objhead.h"

#include "vtim.h"

static struct objcore oc_mark_cnt = { .magic = OBJCORE_MAGIC, };
static struct objcore oc_mark_end = { .magic = OBJCORE_MAGIC, };
static unsigned ban_batch;
static unsigned ban_generation;

pthread_cond_t	ban_lurker_cond;

void
ban_kick_lurker(void)
{

	Lck_AssertHeld(&ban_mtx);
	ban_generation++;
	PTOK(pthread_cond_signal(&ban_lurker_cond));
}

/*
 * ban_cleantail: clean the tail of the ban list up to the first ban which is
 * still referenced. For already completed bans, we update statistics
 * accordingly, but otherwise just skip the completion step and remove directly
 *
 * if an obans list is passed, we clean its tail as well
 */

static void
ban_cleantail(struct banhead_s *obans)
{
	struct ban *b, *bt;
	struct banhead_s freelist = VTAILQ_HEAD_INITIALIZER(freelist);

	/* handle the zero-length tail unprotected */
	if (VTAILQ_LAST(&ban_head, banhead_s) == VTAILQ_FIRST(&ban_head))
		return;

	Lck_Lock(&ban_mtx);
	do {
		b = VTAILQ_LAST(&ban_head, banhead_s);
		if (b != VTAILQ_FIRST(&ban_head) && b->refcount == 0) {
			assert(VTAILQ_EMPTY(&b->objcore));
			if (b->flags & BANS_FLAG_COMPLETED)
				VSC_C_main->bans_completed--;
			if (b->flags & BANS_FLAG_OBJ)
				VSC_C_main->bans_obj--;
			if (b->flags & BANS_FLAG_REQ)
				VSC_C_main->bans_req--;
			VSC_C_main->bans--;
			VSC_C_main->bans_deleted++;
			VTAILQ_REMOVE(&ban_head, b, list);
			VTAILQ_INSERT_TAIL(&freelist, b, list);
			bans_persisted_fragmentation +=
			    ban_len(b->spec);
			VSC_C_main->bans_persisted_fragmentation =
			    bans_persisted_fragmentation;
			ban_info_drop(b->spec, ban_len(b->spec));
		} else {
			b = NULL;
		}
	} while (b != NULL);

	Lck_Unlock(&ban_mtx);

	/* oban order is head to tail, freelist tail to head */
	if (obans != NULL)
		bt = VTAILQ_LAST(obans, banhead_s);
	else
		bt = NULL;

	if (bt != NULL) {
		AN(obans);
		VTAILQ_FOREACH(b, &freelist, list) {
			if (b != bt)
				continue;
			VTAILQ_REMOVE(obans, b, l_list);
			bt = VTAILQ_LAST(obans, banhead_s);
			if (bt == NULL)
				break;
		}
	}

	VTAILQ_FOREACH_SAFE(b, &freelist, list, bt)
		BAN_Free(b);

	return;
}

/*--------------------------------------------------------------------
 * Our task here is somewhat tricky:  The canonical locking order is
 * objhead->mtx first, then ban_mtx, because that is the order which
 * makes most sense in HSH_Lookup(), but we come the other way.
 * We optimistically try to get them the other way, and get out of
 * the way if that fails, and retry again later.
 *
 * To avoid hammering on contested ocs, we first move those behind a marker
 * once. When we only have contested ocs left, we stop moving them around and
 * re-try them in order.
 */

static struct objcore *
ban_lurker_getfirst(struct vsl_log *vsl, struct ban *bt)
{
	struct objhead *oh;
	struct objcore *oc, *noc;
	int move_oc = 1;

	Lck_Lock(&ban_mtx);

	oc = VTAILQ_FIRST(&bt->objcore);
	while (1) {
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

		if (oc == &oc_mark_cnt) {
			if (VTAILQ_NEXT(oc, ban_list) == &oc_mark_end) {
				/* done with this ban's oc list */
				VTAILQ_REMOVE(&bt->objcore, &oc_mark_cnt,
				    ban_list);
				VTAILQ_REMOVE(&bt->objcore, &oc_mark_end,
				    ban_list);
				oc = NULL;
				break;
			}
			oc = VTAILQ_NEXT(oc, ban_list);
			CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
			move_oc = 0;
		} else if (oc == &oc_mark_end) {
			assert(move_oc == 0);

			/* hold off to give lookup a chance and reiterate */
			VSC_C_main->bans_lurker_contention++;
			Lck_Unlock(&ban_mtx);
			VSL_Flush(vsl, 0);
			VTIM_sleep(cache_param->ban_lurker_holdoff);
			Lck_Lock(&ban_mtx);

			oc = VTAILQ_FIRST(&bt->objcore);
			assert(oc == &oc_mark_cnt);
			continue;
		}

		assert(oc != &oc_mark_cnt);
		assert(oc != &oc_mark_end);

		oh = oc->objhead;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		if (!Lck_Trylock(&oh->mtx)) {
			if (oc->flags & OC_F_BUSY) {
				Lck_Unlock(&oh->mtx);
			} else if (oc->refcnt == 0 ||
			    oc->flags & (OC_F_DYING | OC_F_FAILED)) {
				/*
				 * We seize the opportunity to remove
				 * the object completely off the ban
				 * list, now that we have both the oh
				 * and ban mutexes.
				 */
				noc = VTAILQ_NEXT(oc, ban_list);
				VTAILQ_REMOVE(&bt->objcore, oc, ban_list);
				oc->ban = NULL;
				bt->refcount--;
				Lck_Unlock(&oh->mtx);
				oc = noc;
				continue;
			} else {
				/*
				 * We got the lock, and the oc is not being
				 * dismantled under our feet - grab a ref
				 */
				AZ(oc->flags & OC_F_BUSY);
				oc->refcnt += 1;
				VTAILQ_REMOVE(&bt->objcore, oc, ban_list);
				VTAILQ_INSERT_TAIL(&bt->objcore, oc, ban_list);
				Lck_Unlock(&oh->mtx);
				break;
			}
		}

		noc = VTAILQ_NEXT(oc, ban_list);

		if (move_oc) {
			/* contested ocs go between the two markers */
			VTAILQ_REMOVE(&bt->objcore, oc, ban_list);
			VTAILQ_INSERT_BEFORE(&oc_mark_end, oc, ban_list);
		}

		oc = noc;
	}
	Lck_Unlock(&ban_mtx);
	return (oc);
}

static void
ban_lurker_test_ban(struct worker *wrk, struct ban *bt,
    struct banhead_s *obans, struct ban *bd, int kill)
{
	struct ban *bl, *bln;
	struct objcore *oc;
	unsigned tests;
	int i;
	uint64_t tested = 0, tested_tests = 0, lok = 0, lokc = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	/*
	 * First see if there is anything to do, and if so, insert markers
	 */
	Lck_Lock(&ban_mtx);
	oc = VTAILQ_FIRST(&bt->objcore);
	if (oc != NULL) {
		VTAILQ_INSERT_TAIL(&bt->objcore, &oc_mark_cnt, ban_list);
		VTAILQ_INSERT_TAIL(&bt->objcore, &oc_mark_end, ban_list);
	}
	Lck_Unlock(&ban_mtx);
	if (oc == NULL)
		return;

	while (1) {
		if (++ban_batch > cache_param->ban_lurker_batch) {
			VTIM_sleep(cache_param->ban_lurker_sleep);
			ban_batch = 0;
		}
		oc = ban_lurker_getfirst(wrk->vsl, bt);
		if (oc == NULL) {
			if (tested == 0 && lokc == 0) {
				AZ(tested_tests);
				AZ(lok);
				return;
			}
			Lck_Lock(&ban_mtx);
			VSC_C_main->bans_lurker_tested += tested;
			VSC_C_main->bans_lurker_tests_tested += tested_tests;
			VSC_C_main->bans_lurker_obj_killed += lok;
			VSC_C_main->bans_lurker_obj_killed_cutoff += lokc;
			Lck_Unlock(&ban_mtx);
			return;
		}
		i = 0;
		VTAILQ_FOREACH_REVERSE_SAFE(bl, obans, banhead_s, l_list, bln) {
			if (oc->ban != bt) {
				/*
				 * HSH_Lookup() grabbed this oc, killed
				 * it or tested it to top.  We're done.
				 */
				break;
			}
			if (bl->flags & BANS_FLAG_COMPLETED) {
				/* Ban was overtaken by new (dup) ban */
				VTAILQ_REMOVE(obans, bl, l_list);
				continue;
			}
			if (kill == 1)
				i = 1;
			else {
				AZ(bl->flags & BANS_FLAG_REQ);
				tests = 0;
				i = ban_evaluate(wrk, bl->spec, oc, NULL,
				    &tests);
				tested++;
				tested_tests += tests;
			}
			if (i) {
				if (kill) {
					VSLb(wrk->vsl, SLT_ExpBan,
					    "%ju killed for lurker cutoff",
					    VXID(ObjGetXID(wrk, oc)));
					lokc++;
				} else {
					VSLb(wrk->vsl, SLT_ExpBan,
					    "%ju banned by lurker",
					    VXID(ObjGetXID(wrk, oc)));
					lok++;
				}
				HSH_Kill(oc);
				break;
			}
		}
		if (i == 0 && oc->ban == bt) {
			Lck_Lock(&ban_mtx);
			VSC_C_main->bans_lurker_tested += tested;
			VSC_C_main->bans_lurker_tests_tested += tested_tests;
			VSC_C_main->bans_lurker_obj_killed += lok;
			VSC_C_main->bans_lurker_obj_killed_cutoff += lokc;
			tested = tested_tests = lok = lokc = 0;
			if (oc->ban == bt && bt != bd) {
				bt->refcount--;
				VTAILQ_REMOVE(&bt->objcore, oc, ban_list);
				oc->ban = bd;
				bd->refcount++;
				VTAILQ_INSERT_TAIL(&bd->objcore, oc, ban_list);
				i = 1;
			}
			Lck_Unlock(&ban_mtx);
			if (i)
				ObjSendEvent(wrk, oc, OEV_BANCHG);
		}
		(void)HSH_DerefObjCore(wrk, &oc);
	}
}

/*--------------------------------------------------------------------
 * Ban lurker thread:
 *
 * try to move ocs as far up the ban list as possible (to bd)
 *
 * BANS_FLAG_REQ bans act as barriers, for bans further down, ocs get moved to
 * them. But still all bans up to the initial bd get checked and marked
 * completed.
 */

static vtim_dur
ban_lurker_work(struct worker *wrk)
{
	struct ban *b, *bd;
	struct banhead_s obans;
	vtim_real d;
	vtim_dur dt, n;
	unsigned count = 0, cutoff = UINT_MAX;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	dt = 49.62;		// Random, non-magic
	if (cache_param->ban_lurker_sleep == 0) {
		ban_cleantail(NULL);
		return (dt);
	}
	if (cache_param->ban_cutoff > 0)
		cutoff = cache_param->ban_cutoff;

	Lck_Lock(&ban_mtx);
	b = ban_start;
	Lck_Unlock(&ban_mtx);
	d = VTIM_real() - cache_param->ban_lurker_age;
	bd = NULL;
	VTAILQ_INIT(&obans);
	for (; b != NULL; b = VTAILQ_NEXT(b, list), count++) {
		if (bd != NULL)
			ban_lurker_test_ban(wrk, b, &obans, bd,
			    count > cutoff ? 1 : 0);
		if (b->flags & BANS_FLAG_COMPLETED)
			continue;
		if (b->flags & BANS_FLAG_REQ && count <= cutoff) {
			if (bd != NULL)
				bd = VTAILQ_NEXT(b, list);
			continue;
		}
		n = ban_time(b->spec) - d;
		if (n < 0) {
			VTAILQ_INSERT_TAIL(&obans, b, l_list);
			if (bd == NULL)
				bd = b;
		} else if (n < dt) {
			dt = n;
		}
	}

	/*
	 * conceptually, all obans are now completed. Remove the tail.
	 * If any bans to be completed remain after the tail is cut,
	 * mark them completed
	 */
	ban_cleantail(&obans);

	if (VTAILQ_FIRST(&obans) == NULL)
		return (dt);

	Lck_Lock(&ban_mtx);
	VTAILQ_FOREACH(b, &obans, l_list)
		ban_mark_completed(b);
	Lck_Unlock(&ban_mtx);
	return (dt);
}

void * v_matchproto_(bgthread_t)
ban_lurker(struct worker *wrk, void *priv)
{
	struct vsl_log vsl;
	vtim_dur dt;
	unsigned gen = ban_generation + 1;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AZ(priv);

	VSL_Setup(&vsl, NULL, 0);
	AZ(wrk->vsl);
	wrk->vsl = &vsl;

	while (!ban_shutdown) {
		dt = ban_lurker_work(wrk);
		if (DO_DEBUG(DBG_LURKER))
			VSLb(&vsl, SLT_Debug, "lurker: sleep = %lf", dt);
		Lck_Lock(&ban_mtx);
		if (gen == ban_generation) {
			Pool_Sumstat(wrk);
			(void)Lck_CondWaitTimeout(
			    &ban_lurker_cond, &ban_mtx, dt);
			ban_batch = 0;
		}
		gen = ban_generation;
		Lck_Unlock(&ban_mtx);
	}
	wrk->vsl = NULL;
	pthread_exit(0);
	NEEDLESS(return (NULL));
}
