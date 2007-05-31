/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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
 * Expiry of cached objects and execution of prefetcher
 *
 * XXX: Objects can linger on deathrow as long as a slow client
 * XXX: tickles data away from it.  With many slow clients this could
 * XXX: possibly make deathrow very long and make the hangman waste
 * XXX: time.  The solution is to have another queue for such "pending
 * XXX: cases" and have HSH_Deref() move them to deathrow when they
 * XXX: are ready.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "shmlog.h"
#include "binary_heap.h"
#include "cache.h"

static pthread_t exp_thread;
static struct binheap *exp_heap;
static MTX exp_mtx;
static unsigned expearly = 30;
static TAILQ_HEAD(,object) exp_deathrow = TAILQ_HEAD_INITIALIZER(exp_deathrow);

/*--------------------------------------------------------------------*/

void
EXP_Insert(struct object *o)
{

	assert(o->heap_idx == 0);
	LOCK(&exp_mtx);
	binheap_insert(exp_heap, o);
	UNLOCK(&exp_mtx);
}

void
EXP_TTLchange(struct object *o)
{
	assert(o->heap_idx != 0);
	LOCK(&exp_mtx);
	binheap_delete(exp_heap, o->heap_idx);
	binheap_insert(exp_heap, o);
	UNLOCK(&exp_mtx);
}

/*--------------------------------------------------------------------
 * This thread monitors deathrow and kills objects when they time out.
 */

static void *
exp_hangman(void *arg)
{
	struct object *o;
	time_t t;

	(void)arg;

	t = time(NULL);
	while (1) {
		LOCK(&exp_mtx);
		TAILQ_FOREACH(o, &exp_deathrow, deathrow) {
			CHECK_OBJ(o, OBJECT_MAGIC);
			if (o->ttl >= t) {
				o = NULL;
				break;
			}
			if (o->busy) {
				VSL(SLT_Debug, 0,
				    "Grim Reaper: Busy object xid %u", o->xid);
				continue;
			}
			if (o->refcnt == 1)
				break;
		}
		if (o == NULL) {
			UNLOCK(&exp_mtx);
			AZ(sleep(1));
			t = time(NULL);
			continue;
		}
		TAILQ_REMOVE(&exp_deathrow, o, deathrow);
		VSL_stats->n_deathrow--;
		VSL_stats->n_expired++;
		UNLOCK(&exp_mtx);
		VSL(SLT_ExpKill, 0, "%u %d", o->xid, (int)(o->ttl - t));
		HSH_Deref(o);
	}
}

/*--------------------------------------------------------------------
 * This thread monitors the root of the binary heap and whenever an
 * object gets close enough, VCL is asked to decide if it should be
 * discarded or prefetched.
 * If discarded, the object is put on deathrow where exp_hangman() will
 * do what needs to be done.
 * XXX: If prefetched pass to the pool for pickup.
 */

static void *
exp_prefetch(void *arg)
{
	struct worker ww;
	struct object *o;
	time_t t;
	struct sess *sp;
	struct object *o2;

	(void)arg;

	sp = SES_New(NULL, 0);
	XXXAN(sp);
	sp->wrk = &ww;
	ww.magic = WORKER_MAGIC;
	ww.wlp = ww.wlog;
	ww.wle = ww.wlog + sizeof ww.wlog;

	sleep(10);		/* Takes time for VCL to arrive */
	VCL_Get(&sp->vcl);
	t = time(NULL);
	while (1) {
		LOCK(&exp_mtx);
		o = binheap_root(exp_heap);
		if (o != NULL)
			CHECK_OBJ(o, OBJECT_MAGIC);
		if (o == NULL || o->ttl > t + expearly) {
			UNLOCK(&exp_mtx);
			AZ(sleep(1));
			VCL_Refresh(&sp->vcl);
			t = time(NULL);
			continue;
		}
		binheap_delete(exp_heap, o->heap_idx);

		/* Sanity check */
		o2 = binheap_root(exp_heap);
		if (o2 != NULL)
			assert(o2->ttl >= o->ttl);

		UNLOCK(&exp_mtx);
		WSL(&ww, SLT_ExpPick, 0, "%u", o->xid);

		sp->obj = o;
		VCL_timeout_method(sp);

		if (sp->handling == VCL_RET_DISCARD) {
			LOCK(&exp_mtx);
			TAILQ_INSERT_TAIL(&exp_deathrow, o, deathrow);
			VSL_stats->n_deathrow++;
			UNLOCK(&exp_mtx);
			continue;
		}
		assert(sp->handling == VCL_RET_DISCARD);
	}
}

/*--------------------------------------------------------------------*/

static int
object_cmp(void *priv, void *a, void *b)
{
	struct object *aa, *bb;

	(void)priv;

	aa = a;
	bb = b;
	return (aa->ttl < bb->ttl);
}

static void
object_update(void *priv, void *p, unsigned u)
{
	struct object *o = p;

	(void)priv;
	o->heap_idx = u;
}

/*--------------------------------------------------------------------*/

void
EXP_Init(void)
{

	MTX_INIT(&exp_mtx);
	exp_heap = binheap_new(NULL, object_cmp, object_update);
	XXXAN(exp_heap);
	AZ(pthread_create(&exp_thread, NULL, exp_prefetch, NULL));
	AZ(pthread_create(&exp_thread, NULL, exp_hangman, NULL));
}
