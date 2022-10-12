/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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
 * The geniuses who came up with pthreads did not think operations like
 * pthread_assert_mutex_held() were important enough to include them in
 * the API.
 *
 * Build our own locks on top of pthread mutexes and hope that the next
 * civilization is better at such crucial details than this one.
 */

#include "config.h"

#include "cache_varnishd.h"

#include <stdlib.h>
#include <stdio.h>

#include "vtim.h"

#include "VSC_lck.h"

struct ilck {
	unsigned		magic;
#define ILCK_MAGIC		0x7b86c8a5
	int			held;
	pthread_mutex_t		mtx;
	pthread_t		owner;
	const char		*w;
	struct VSC_lck		*stat;
};

/*--------------------------------------------------------------------*/

static void
Lck_Witness_Lock(const struct ilck *il, const char *p, int l, const char *try)
{
	char *q, t[10];	//lint -e429
	int emit;

	AN(p);
	q = pthread_getspecific(witness_key);
	if (q == NULL) {
		q = calloc(1, 1024);
		AN(q);
		AZ(pthread_setspecific(witness_key, q));
	}
	emit = *q != '\0';
	strcat(q, " ");
	strcat(q, il->w);
	strcat(q, try);
	strcat(q, ",");
	strcat(q, p);
	strcat(q, ",");
	bprintf(t, "%d", l);
	strcat(q, t);
	if (emit)
		VSL(SLT_Witness, NO_VXID, "%s", q);
}

static void
Lck_Witness_Unlock(const struct ilck *il)
{
	char *q, *r;

	q = pthread_getspecific(witness_key);
	if (q == NULL)
		return;
	r = strrchr(q, ' ');
	if (r == NULL)
		r = q;
	else
		*r++ = '\0';
	if (memcmp(r, il->w, strlen(il->w)))
		VSL(SLT_Witness, NO_VXID, "Unlock %s @ %s <%s>", il->w, r, q);
	else
		*r = '\0';
}

/*--------------------------------------------------------------------*/

void v_matchproto_()
Lck__Lock(struct lock *lck, const char *p, int l)
{
	struct ilck *ilck;
	int r = EINVAL;

	AN(lck);
	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	if (DO_DEBUG(DBG_WITNESS))
		Lck_Witness_Lock(ilck, p, l, "");
	if (DO_DEBUG(DBG_LCK)) {
		r = pthread_mutex_trylock(&ilck->mtx);
		assert(r == 0 || r == EBUSY);
	}
	if (r)
		AZ(pthread_mutex_lock(&ilck->mtx));
	AZ(ilck->held);
	if (r == EBUSY)
		ilck->stat->dbg_busy++;
	ilck->stat->locks++;
	ilck->owner = pthread_self();
	ilck->held = 1;
}

void v_matchproto_()
Lck__Unlock(struct lock *lck, const char *p, int l)
{
	struct ilck *ilck;

	(void)p;
	(void)l;

	AN(lck);
	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	assert(pthread_equal(ilck->owner, pthread_self()));
	AN(ilck->held);
	ilck->held = 0;
	/*
	 * #ifdef POSIX_STUPIDITY:
	 * The pthread_t type has no defined assignment or comparison
	 * operators, this is why pthread_equal() is necessary.
	 * Unfortunately POSIX forgot to define a NULL value for pthread_t
	 * so you can never unset a pthread_t variable.
	 * We hack it and fill it with zero bits, hoping for sane
	 * implementations of pthread.
	 * #endif
	 */
#ifdef PTHREAD_NULL
	ilck->owner = PTHREAD_NULL;
#else
	memset(&ilck->owner, 0, sizeof ilck->owner);
#endif
	AZ(pthread_mutex_unlock(&ilck->mtx));
	if (DO_DEBUG(DBG_WITNESS))
		Lck_Witness_Unlock(ilck);
}

int v_matchproto_()
Lck__Trylock(struct lock *lck, const char *p, int l)
{
	struct ilck *ilck;
	int r;

	AN(lck);
	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	if (DO_DEBUG(DBG_WITNESS))
		Lck_Witness_Lock(ilck, p, l, "?");
	r = pthread_mutex_trylock(&ilck->mtx);
	assert(r == 0 || r == EBUSY);
	if (r == 0) {
		AZ(ilck->held);
		ilck->held = 1;
		ilck->stat->locks++;
		ilck->owner = pthread_self();
	} else if (DO_DEBUG(DBG_LCK))
		ilck->stat->dbg_try_fail++;
	return (r);
}

int
Lck__Held(const struct lock *lck)
{
	struct ilck *ilck;

	AN(lck);
	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	return (ilck->held);
}

int
Lck__Owned(const struct lock *lck)
{
	struct ilck *ilck;

	AN(lck);
	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	AN(ilck->held);
	return (pthread_equal(ilck->owner, pthread_self()));
}

int v_matchproto_()
Lck_CondWait(pthread_cond_t *cond, struct lock *lck)
{
       return (Lck_CondWaitUntil(cond, lck, 0));
}

int v_matchproto_()
Lck_CondWaitTimeout(pthread_cond_t *cond, struct lock *lck, vtim_dur timeout)
{
	assert(timeout >= 0);
	assert(timeout < 3600);

	if (timeout == 0)
		return (Lck_CondWaitUntil(cond, lck, 0));
	else
		return (Lck_CondWaitUntil(cond, lck, VTIM_real() + timeout));
}

int v_matchproto_()
Lck_CondWaitUntil(pthread_cond_t *cond, struct lock *lck, vtim_real when)
{
	struct ilck *ilck;
	struct timespec ts;

	AN(lck);
	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	AN(ilck->held);
	assert(pthread_equal(ilck->owner, pthread_self()));
	ilck->held = 0;
	if (when == 0) {
		errno = pthread_cond_wait(cond, &ilck->mtx);
		AZ(errno);
	} else {
		assert(when > 1e9);
		ts = VTIM_timespec(when);
		assert(ts.tv_nsec >= 0 && ts.tv_nsec <= 999999999);
		errno = pthread_cond_timedwait(cond, &ilck->mtx, &ts);
#if defined (__APPLE__)
		/*
		 * I hate woo-doo programming in all it's forms and all it's
		 * manifestations, but for reasons I utterly fail to isolate,
		 * OSX sometimes throws an EINVAL.
		 *
		 * I have tried very hard to determine if any of the three
		 * arguments are in fact invalid, and found nothing which
		 * even hints that it might be the case.
		 *
		 * So far I have yet to see a failure if the exact same
		 * call is repeated after a very short sleep.
		 *
		 * Calling pthread_yield_np() instead of sleaping /mostly/
		 * works as well, but still fails sometimes.
		 *
		 * Env:
		 *	Darwin Kernel Version 20.5.0:
		 *	Sat May  8 05:10:31 PDT 2021;
		 *	root:xnu-7195.121.3~9/RELEASE_ARM64_T8101 arm64
		 *
		 * 20220329 /phk
		 */
		if (errno == EINVAL) {
			usleep(100);
			errno = pthread_cond_timedwait(cond, &ilck->mtx, &ts);
		}
#endif
		assert(errno == 0 ||
		    errno == ETIMEDOUT ||
		    errno == EINTR);
	}
	AZ(ilck->held);
	ilck->held = 1;
	ilck->owner = pthread_self();
	return (errno);
}

void
Lck__New(struct lock *lck, struct VSC_lck *st, const char *w)
{
	struct ilck *ilck;

	AN(st);
	AN(w);
	AN(lck);
	AZ(lck->priv);
	ALLOC_OBJ(ilck, ILCK_MAGIC);
	AN(ilck);
	ilck->w = w;
	ilck->stat = st;
	ilck->stat->creat++;
	AZ(pthread_mutex_init(&ilck->mtx, &mtxattr_errorcheck));
	lck->priv = ilck;
}

void
Lck_Delete(struct lock *lck)
{
	struct ilck *ilck;

	AN(lck);
	TAKE_OBJ_NOTNULL(ilck, &lck->priv, ILCK_MAGIC);
	ilck->stat->destroy++;
	AZ(pthread_mutex_destroy(&ilck->mtx));
	FREE_OBJ(ilck);
}

struct VSC_lck *
Lck_CreateClass(struct vsc_seg **sg, const char *name)
{
	return (VSC_lck_New(NULL, sg, name));
}

void
Lck_DestroyClass(struct vsc_seg **sg)
{
	VSC_lck_Destroy(sg);
}

#define LOCK(nam) struct VSC_lck *lck_##nam;
#include "tbl/locks.h"

void
LCK_Init(void)
{

#define LOCK(nam)	lck_##nam = Lck_CreateClass(NULL, #nam);
#include "tbl/locks.h"
}
