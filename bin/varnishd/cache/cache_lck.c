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
 * The geniuses who came up with pthreads did not think operations like
 * pthread_assert_mutex_held() were important enough to include them in
 * the API.
 *
 * Build our own locks on top of pthread mutexes and hope that the next
 * civilization is better at such crucial details than this one.
 */

#include "config.h"

#include "cache.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

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

static pthread_mutexattr_t attr;

/*--------------------------------------------------------------------*/

static void
Lck_Witness_Lock(const struct ilck *il, const char *p, int l, const char *try)
{
	char *q, t[10];
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
		VSL(SLT_Witness, 0, "%s", q);
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
		VSL(SLT_Witness, 0, "Unlock %s @ %s <%s>", il->w, r, q);
	else
		*r = '\0';
}

/*--------------------------------------------------------------------*/

void __match_proto__()
Lck__Lock(struct lock *lck, const char *p, int l)
{
	struct ilck *ilck;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	if (DO_DEBUG(DBG_WITNESS))
		Lck_Witness_Lock(ilck, p, l, "");
	AZ(pthread_mutex_lock(&ilck->mtx));
	AZ(ilck->held);
	ilck->stat->locks++;
	ilck->owner = pthread_self();
	ilck->held = 1;
}

void __match_proto__()
Lck__Unlock(struct lock *lck, const char *p, int l)
{
	struct ilck *ilck;

	(void)p;
	(void)l;

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
	memset(&ilck->owner, 0, sizeof ilck->owner);
	AZ(pthread_mutex_unlock(&ilck->mtx));
	if (DO_DEBUG(DBG_WITNESS))
		Lck_Witness_Unlock(ilck);
}

int __match_proto__()
Lck__Trylock(struct lock *lck, const char *p, int l)
{
	struct ilck *ilck;
	int r;

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
	}
	return (r);
}

int
Lck__Held(const struct lock *lck)
{
	struct ilck *ilck;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	return (ilck->held);
}

int
Lck__Owned(const struct lock *lck)
{
	struct ilck *ilck;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	AN(ilck->held);
	return (pthread_equal(ilck->owner, pthread_self()));
}

int __match_proto__()
Lck_CondWait(pthread_cond_t *cond, struct lock *lck, double when)
{
	struct ilck *ilck;
	struct timespec ts;
	double t;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	AN(ilck->held);
	assert(pthread_equal(ilck->owner, pthread_self()));
	ilck->held = 0;
	if (when == 0) {
		errno = pthread_cond_wait(cond, &ilck->mtx);
		AZ(errno);
	} else {
		assert(when > 1e9);
		ts.tv_nsec = (long)(modf(when, &t) * 1e9);
		ts.tv_sec = (long)t;
		errno = pthread_cond_timedwait(cond, &ilck->mtx, &ts);
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
	AZ(lck->priv);
	ALLOC_OBJ(ilck, ILCK_MAGIC);
	AN(ilck);
	ilck->w = w;
	ilck->stat = st;
	ilck->stat->creat++;
	AZ(pthread_mutex_init(&ilck->mtx, &attr));
	lck->priv = ilck;
}

void
Lck_Delete(struct lock *lck)
{
	struct ilck *ilck;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	ilck->stat->destroy++;
	lck->priv = NULL;
	AZ(pthread_mutex_destroy(&ilck->mtx));
	FREE_OBJ(ilck);
}

struct VSC_lck *
Lck_CreateClass(const char *name)
{
	return(VSC_lck_New(name));
}

#define LOCK(nam) struct VSC_lck *lck_##nam;
#include "tbl/locks.h"

void
LCK_Init(void)
{

	AZ(pthread_mutexattr_init(&attr));
#if !defined(__APPLE__) && !defined(__MACH__)
	AZ(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
#endif
#define LOCK(nam)	lck_##nam = Lck_CreateClass(#nam);
#include "tbl/locks.h"
}
