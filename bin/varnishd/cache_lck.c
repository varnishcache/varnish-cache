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
 * The geniuses who came up with pthreads did not think operations like
 * pthread_assert_mutex_held() were important enough to include them in
 * the API.
 *
 * Build our own locks on top of pthread mutexes and hope that the next
 * civilization is better at such crucial details than this one.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>

#include <stdlib.h>

#include "cache.h"

/*The constability of lck depends on platform pthreads implementation */

struct ilck {
	unsigned		magic;
#define ILCK_MAGIC		0x7b86c8a5
	pthread_mutex_t		mtx;
	int			held;
	pthread_t		owner;
	VTAILQ_ENTRY(ilck)	list;
	const char		*w;
	struct vsc_lck		*stat;
};

static VTAILQ_HEAD(, ilck)	ilck_head =
    VTAILQ_HEAD_INITIALIZER(ilck_head);

static pthread_mutex_t		lck_mtx;

void __match_proto__()
Lck__Lock(struct lock *lck, const char *p, const char *f, int l)
{
	struct ilck *ilck;
	int r;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	if (!(params->diag_bitmap & 0x18)) {
		AZ(pthread_mutex_lock(&ilck->mtx));
		AZ(ilck->held);
		ilck->stat->locks++;
		ilck->owner = pthread_self();
		ilck->held = 1;
		return;
	}
	r = pthread_mutex_trylock(&ilck->mtx);
	assert(r == 0 || r == EBUSY);
	if (r) {
		ilck->stat->colls++;
		if (params->diag_bitmap & 0x8)
			VSL(SLT_Debug, 0, "MTX_CONTEST(%s,%s,%d,%s)",
			    p, f, l, ilck->w);
		AZ(pthread_mutex_lock(&ilck->mtx));
	} else if (params->diag_bitmap & 0x8) {
		VSL(SLT_Debug, 0, "MTX_LOCK(%s,%s,%d,%s)", p, f, l, ilck->w);
	}
	AZ(ilck->held);
	ilck->stat->locks++;
	ilck->owner = pthread_self();
	ilck->held = 1;
}

void __match_proto__()
Lck__Unlock(struct lock *lck, const char *p, const char *f, int l)
{
	struct ilck *ilck;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	assert(pthread_equal(ilck->owner, pthread_self()));
	AN(ilck->held);
	ilck->held = 0;
	AZ(pthread_mutex_unlock(&ilck->mtx));
	if (params->diag_bitmap & 0x8)
		VSL(SLT_Debug, 0, "MTX_UNLOCK(%s,%s,%d,%s)", p, f, l, ilck->w);
}

int __match_proto__()
Lck__Trylock(struct lock *lck, const char *p, const char *f, int l)
{
	struct ilck *ilck;
	int r;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	r = pthread_mutex_trylock(&ilck->mtx);
	assert(r == 0 || r == EBUSY);
	if (params->diag_bitmap & 0x8)
		VSL(SLT_Debug, 0,
		    "MTX_TRYLOCK(%s,%s,%d,%s) = %d", p, f, l, ilck->w);
	if (r == 0) {
		AZ(ilck->held);
		ilck->held = 1;
		ilck->owner = pthread_self();
	}
	return (r);
}

void
Lck__Assert(const struct lock *lck, int held)
{
	struct ilck *ilck;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	if (held)
		assert(ilck->held &&
		    pthread_equal(ilck->owner, pthread_self()));
	else
		assert(!ilck->held ||
		    !pthread_equal(ilck->owner, pthread_self()));
}

void __match_proto__()
Lck_CondWait(pthread_cond_t *cond, struct lock *lck)
{
	struct ilck *ilck;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	AN(ilck->held);
	assert(pthread_equal(ilck->owner, pthread_self()));
	ilck->held = 0;
	AZ(pthread_cond_wait(cond, &ilck->mtx));
	AZ(ilck->held);
	ilck->held = 1;
	ilck->owner = pthread_self();
}

void
Lck__New(struct lock *lck, struct vsc_lck *st, const char *w)
{
	struct ilck *ilck;

	AN(st);
	AZ(lck->priv);
	ALLOC_OBJ(ilck, ILCK_MAGIC);
	AN(ilck);
	ilck->w = w;
	ilck->stat = st;
	ilck->stat->creat++;
	AZ(pthread_mutex_init(&ilck->mtx, NULL));
	AZ(pthread_mutex_lock(&lck_mtx));
	VTAILQ_INSERT_TAIL(&ilck_head, ilck, list);
	AZ(pthread_mutex_unlock(&lck_mtx));
	lck->priv = ilck;
}

void
Lck_Delete(struct lock *lck)
{
	struct ilck *ilck;

	CAST_OBJ_NOTNULL(ilck, lck->priv, ILCK_MAGIC);
	ilck->stat->destroy++;
	lck->priv = NULL;
	AZ(pthread_mutex_lock(&lck_mtx));
	VTAILQ_REMOVE(&ilck_head, ilck, list);
	AZ(pthread_mutex_unlock(&lck_mtx));
	AZ(pthread_mutex_destroy(&ilck->mtx));
	FREE_OBJ(ilck);
}

#define LOCK(nam) struct vsc_lck *lck_##nam;
#include "locks.h"
#undef LOCK

void
LCK_Init(void)
{

	AZ(pthread_mutex_init(&lck_mtx, NULL));
#define LOCK(nam)						\
	lck_##nam = VSM_Alloc(sizeof(struct vsc_lck),		\
	   VSC_CLASS, VSC_TYPE_LCK, #nam);
#include "locks.h"
#undef LOCK
}
