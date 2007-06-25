/*-
 * Copyright (c) 2007 Linpro AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@linpro.no>
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
 */

#include "shmlog.h"
#include "cache.h"
#include "queue.h"

/*
 * For performance reasons, objects are only moved to the head of the LRU
 * list when they've been in their current position for at least LRU_DELAY
 * seconds, rather than on every access.  This should probably be a
 * run-time parameter.
 */
#define LRU_DELAY 2

static pthread_mutex_t lru_mtx = PTHREAD_MUTEX_INITIALIZER;
static TAILQ_HEAD(lru_head, object) lru_list;

/*
 * Enter an object into the LRU list, or move it to the head of the list
 * if it's already in it and hasn't moved in a while.
 */
void
LRU_Enter(struct object *o, time_t stamp)
{

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	assert(stamp > 0);
	if (o->lru_stamp < stamp - LRU_DELAY && o != lru_list.tqh_first) {
		// VSL(SLT_LRU_enter, 0, "%u %u %u", o->xid, o->lru_stamp, stamp);
		pthread_mutex_lock(&lru_mtx);
		if (o->lru_stamp != 0)
			TAILQ_REMOVE(&lru_list, o, lru);
		TAILQ_INSERT_HEAD(&lru_list, o, lru);
		o->lru_stamp = stamp;
		pthread_mutex_unlock(&lru_mtx);
	}
}

/*
 * Remove an object from the LRU list.
 */
void
LRU_Remove(struct object *o)
{

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	if (o->lru_stamp != 0) {
		// VSL(SLT_LRU_remove, 0, "%u", o->xid);
		pthread_mutex_lock(&lru_mtx);
		TAILQ_REMOVE(&lru_list, o, lru);
		pthread_mutex_unlock(&lru_mtx);
	}
}

/*
 * Walk through the LRU list, starting at the back, and retire objects
 * until our quota is reached or we run out of objects to retire.
 */
void
LRU_DiscardSpace(struct sess *sp, uint64_t quota)
{
	struct object *o, *so;

	pthread_mutex_lock(&lru_mtx);
	while ((o = TAILQ_LAST(&lru_list, lru_head))) {
		TAILQ_REMOVE(&lru_list, o, lru);
		so = sp->obj;
		sp->obj = o;
		VCL_discard_method(sp);
		sp->obj = so;
		if (sp->handling == VCL_RET_DISCARD) {
			/* discard: place on deathrow */
			EXP_Retire(o);
			o->lru_stamp = 0;
			if (o->len > quota)
				break;
			quota -= o->len;
		} else {
			/* keep: move to front of list */
			if ((so = TAILQ_FIRST(&lru_list)))
				o->lru_stamp = so->lru_stamp;
			TAILQ_INSERT_HEAD(&lru_list, o, lru);
		}
	}
	pthread_mutex_unlock(&lru_mtx);
}

/*
 * Walk through the LRU list, starting at the back, and retire objects
 * that haven't been accessed since the specified cutoff date.
 */
void
LRU_DiscardTime(struct sess *sp, time_t cutoff)
{
	struct object *o, *so;

	pthread_mutex_lock(&lru_mtx);
	while ((o = TAILQ_LAST(&lru_list, lru_head))) {
		if (o->lru_stamp >= cutoff)
			break;
		TAILQ_REMOVE(&lru_list, o, lru);
		so = sp->obj;
		sp->obj = o;
		VCL_discard_method(sp);
		sp->obj = so;
		if (sp->handling == VCL_RET_DISCARD) {
			/* discard: place on deathrow */
			EXP_Retire(o);
		} else {
			/* keep: move to front of list */
			if ((so = TAILQ_FIRST(&lru_list)) && so->lru_stamp > cutoff)
				o->lru_stamp = so->lru_stamp;
			else
				o->lru_stamp = cutoff;
			TAILQ_INSERT_HEAD(&lru_list, o, lru);
		}
	}
	pthread_mutex_unlock(&lru_mtx);
}
