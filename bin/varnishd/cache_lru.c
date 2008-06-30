/*-
 * Copyright (c) 2007 Linpro AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@des.no>
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
#include "vqueue.h"

/*
 * For performance reasons, objects are only moved to the head of the LRU
 * list when they've been in their current position for at least LRU_DELAY
 * seconds, rather than on every access.  This should probably be a
 * run-time parameter.
 */
#define LRU_DELAY 2

VTAILQ_HEAD(lru_head, object);

static struct lru_head lru_list = VTAILQ_HEAD_INITIALIZER(lru_list);
static pthread_mutex_t lru_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct sess *lru_session;
static struct worker lru_worker;

/*
 * Initialize the LRU data structures.
 */
static inline void
LRU_Init(void)
{
	if (lru_session == NULL) {
		lru_session = SES_New(NULL, 0);
		XXXAN(lru_session);
		lru_session->wrk = &lru_worker;
		lru_worker.magic = WORKER_MAGIC;
		lru_worker.wlp = lru_worker.wlog;
		lru_worker.wle = lru_worker.wlog + sizeof lru_worker.wlog;
		VCL_Get(&lru_session->vcl);
	} else {
		VCL_Refresh(&lru_session->vcl);
	}
}

/*
 * Enter an object into the LRU list, or move it to the head of the list
 * if it's already in it and hasn't moved in a while.
 */
void
LRU_Enter(struct object *o, double stamp)
{

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	assert(stamp > 0);
	if (o->lru_stamp < stamp - LRU_DELAY && o != lru_list.vtqh_first) {
		// VSL(SLT_LRU_enter, 0, "%u %u %u", o->xid, o->lru_stamp, stamp);
		LOCK(&lru_mtx);
		if (o->lru_stamp != 0)
			VTAILQ_REMOVE(&lru_list, o, lru);
		VTAILQ_INSERT_HEAD(&lru_list, o, lru);
		o->lru_stamp = stamp;
		UNLOCK(&lru_mtx);
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
		LOCK(&lru_mtx);
		VTAILQ_REMOVE(&lru_list, o, lru);
		UNLOCK(&lru_mtx);
	}
}

/*
 * With the LRU lock held, call VCL_discard().  Depending on the result,
 * either insert the object at the head of the list or dereference it.
 */
static int
LRU_DiscardLocked(struct object *o)
{
	struct object *so;

	if (o->busy)
		return (0);

	/* XXX this is a really bad place to do this */
	LRU_Init();

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	VTAILQ_REMOVE(&lru_list, o, lru);

	lru_session->obj = o;
	VCL_discard_method(lru_session);

	if (lru_session->handling == VCL_RET_DISCARD) {
		/* discard: release object */
		VSL(SLT_ExpKill, 0, "%u %d", o->xid, o->lru_stamp);
		o->lru_stamp = 0;
		EXP_Terminate(o);
		return (1);
	} else {
		/* keep: move to front of list */
		if ((so = VTAILQ_FIRST(&lru_list)))
			o->lru_stamp = so->lru_stamp;
		VTAILQ_INSERT_HEAD(&lru_list, o, lru);
		return (0);
	}
}

/*
 * Walk through the LRU list, starting at the back, and check each object
 * until we find one that can be retired.  Return the number of objects
 * that were discarded.
 */
int
LRU_DiscardOne(void)
{
	struct object *first = VTAILQ_FIRST(&lru_list);
	struct object *o;
	int count = 0;

	LOCK(&lru_mtx);
	while (!count && (o = VTAILQ_LAST(&lru_list, lru_head))) {
		if (LRU_DiscardLocked(o))
			++count;
		if (o == first) {
			/* full circle */
			break;
		}
	}
	UNLOCK(&lru_mtx);
	return (count);
}

/*
 * Walk through the LRU list, starting at the back, and retire objects
 * until our quota is reached or we run out of objects to retire.  Return
 * the number of objects that were discarded.
 */
int
LRU_DiscardSpace(int64_t quota)
{
	struct object *first = VTAILQ_FIRST(&lru_list);
	struct object *o;
	unsigned int len;
	int count = 0;

	LOCK(&lru_mtx);
	while (quota > 0 && (o = VTAILQ_LAST(&lru_list, lru_head))) {
		len = o->len;
		if (LRU_DiscardLocked(o)) {
			quota -= len;
			++count;
		}
		if (o == first) {
			/* full circle */
			break;
		}
	}
	UNLOCK(&lru_mtx);
	return (count);
}

/*
 * Walk through the LRU list, starting at the back, and retire objects
 * that haven't been accessed since the specified cutoff date.  Return the
 * number of objects that were discarded.
 */
int
LRU_DiscardTime(double cutoff)
{
	struct object *first = VTAILQ_FIRST(&lru_list);
	struct object *o;
	int count = 0;

	LOCK(&lru_mtx);
	while ((o = VTAILQ_LAST(&lru_list, lru_head)) && o->lru_stamp <= cutoff) {
		if (LRU_DiscardLocked(o))
			++count;
		if (o == first) {
			/* full circle */
			break;
		}
	}
	UNLOCK(&lru_mtx);
	return (count);
}
