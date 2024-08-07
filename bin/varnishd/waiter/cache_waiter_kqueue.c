/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#if defined(HAVE_KQUEUE)

#include "cache/cache_varnishd.h"

#include <sys/event.h>

#include <stdlib.h>

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "vtim.h"

#define NKEV	256

struct vwk {
	unsigned		magic;
#define VWK_MAGIC		0x1cc2acc2
	int			kq;
	struct waiter		*waiter;
	pthread_t		thread;
	double			next;
	int			pipe[2];
	unsigned		nwaited;
	int			die;
	struct lock		mtx;
};

/*--------------------------------------------------------------------*/

static void *
vwk_thread(void *priv)
{
	struct vwk *vwk;
	struct kevent ke[NKEV], *kp;
	int j, n;
	double now, then;
	struct timespec ts;
	struct waited *wp;
	struct waiter *w;
	char c;

	CAST_OBJ_NOTNULL(vwk, priv, VWK_MAGIC);
	w = vwk->waiter;
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	THR_SetName("cache-kqueue");
	THR_Init();

	now = VTIM_real();
	while (1) {
		while (1) {
			Lck_Lock(&vwk->mtx);
			/*
			 * XXX: We could avoid many syscalls here if we were
			 * XXX: allowed to just close the fd's on timeout.
			 */
			then = Wait_HeapDue(w, &wp);
			if (wp == NULL) {
				vwk->next = now + 100;
				break;
			} else if (then > now) {
				vwk->next = then;
				break;
			}
			CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
			EV_SET(ke, wp->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
			AZ(kevent(vwk->kq, ke, 1, NULL, 0, NULL));
			AN(Wait_HeapDelete(w, wp));
			Lck_Unlock(&vwk->mtx);
			Wait_Call(w, wp, WAITER_TIMEOUT, now);
		}
		then = vwk->next - now;
		ts = VTIM_timespec(then);
		Lck_Unlock(&vwk->mtx);
		n = kevent(vwk->kq, NULL, 0, ke, NKEV, &ts);
		assert(n >= 0);
		assert(n <= NKEV);
		now = VTIM_real();
		for (kp = ke, j = 0; j < n; j++, kp++) {
			assert(kp->filter == EVFILT_READ);
			if ((uintptr_t)ke[j].udata == (uintptr_t)vwk) {
				assert(read(vwk->pipe[0], &c, 1) == 1);
				continue;
			}
			CAST_OBJ_NOTNULL(wp, (void*)ke[j].udata, WAITED_MAGIC);
			Lck_Lock(&vwk->mtx);
			AN(Wait_HeapDelete(w, wp));
			Lck_Unlock(&vwk->mtx);
			vwk->nwaited--;
			if (kp->flags & EV_EOF &&
			    recv(wp->fd, &c, 1, MSG_PEEK) == 0)
				Wait_Call(w, wp, WAITER_REMCLOSE, now);
			else
				Wait_Call(w, wp, WAITER_ACTION, now);
		}
		if (vwk->nwaited == 0 && vwk->die)
			break;
	}
	closefd(&vwk->pipe[0]);
	closefd(&vwk->pipe[1]);
	closefd(&vwk->kq);
	return (NULL);
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(waiter_enter_f)
vwk_enter(void *priv, struct waited *wp)
{
	struct vwk *vwk;
	struct kevent ke;

	CAST_OBJ_NOTNULL(vwk, priv, VWK_MAGIC);
	EV_SET(&ke, wp->fd, EVFILT_READ, EV_ADD|EV_ONESHOT, 0, 0, wp);
	Lck_Lock(&vwk->mtx);
	vwk->nwaited++;
	Wait_HeapInsert(vwk->waiter, wp);
	AZ(kevent(vwk->kq, &ke, 1, NULL, 0, NULL));

	/* If the kqueue isn't due before our timeout, poke it via the pipe */
	if (Wait_When(wp) < vwk->next)
		assert(write(vwk->pipe[1], "X", 1) == 1);

	Lck_Unlock(&vwk->mtx);
	return (0);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(waiter_init_f)
vwk_init(struct waiter *w)
{
	struct vwk *vwk;
	struct kevent ke;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vwk = w->priv;
	INIT_OBJ(vwk, VWK_MAGIC);
	vwk->waiter = w;

	vwk->kq = kqueue();
	assert(vwk->kq >= 0);
	Lck_New(&vwk->mtx, lck_waiter);
	AZ(pipe(vwk->pipe));
	EV_SET(&ke, vwk->pipe[0], EVFILT_READ, EV_ADD, 0, 0, vwk);
	AZ(kevent(vwk->kq, &ke, 1, NULL, 0, NULL));

	PTOK(pthread_create(&vwk->thread, NULL, vwk_thread, vwk));
}

/*--------------------------------------------------------------------
 * It is the callers responsibility to trigger all fd's waited on to
 * fail somehow.
 */

static void v_matchproto_(waiter_fini_f)
vwk_fini(struct waiter *w)
{
	struct vwk *vwk;
	void *vp;

	CAST_OBJ_NOTNULL(vwk, w->priv, VWK_MAGIC);
	Lck_Lock(&vwk->mtx);
	vwk->die = 1;
	assert(write(vwk->pipe[1], "Y", 1) == 1);
	Lck_Unlock(&vwk->mtx);
	PTOK(pthread_join(vwk->thread, &vp));
	Lck_Delete(&vwk->mtx);
}

/*--------------------------------------------------------------------*/

#include "waiter/mgt_waiter.h"

const struct waiter_impl waiter_kqueue = {
	.name =		"kqueue",
	.init =		vwk_init,
	.fini =		vwk_fini,
	.enter =	vwk_enter,
	.size =		sizeof(struct vwk),
};

#endif /* defined(HAVE_KQUEUE) */
