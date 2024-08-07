/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Rogerio Carvalho Schneider <stockrt@gmail.com>
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
 * Recommended reading: libev(3) "EVBACKEND_EPOLL" section
 * - thank you, Marc Alexander Lehmann
 */

#include "config.h"

#if defined(HAVE_EPOLL_CTL)

#include <stdlib.h>

#include <sys/epoll.h>


#include "cache/cache_varnishd.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "vtim.h"

#ifndef EPOLLRDHUP
#  define EPOLLRDHUP 0
#endif

#define NEEV	8192

struct vwe {
	unsigned		magic;
#define VWE_MAGIC		0x6bd73424
	int			epfd;
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
vwe_thread(void *priv)
{
	struct epoll_event *ev, *ep;
	struct waited *wp;
	struct waiter *w;
	double now, then;
	int i, n, active;
	struct vwe *vwe;
	char c;

	CAST_OBJ_NOTNULL(vwe, priv, VWE_MAGIC);
	w = vwe->waiter;
	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	THR_SetName("cache-epoll");
	THR_Init();
	ev = malloc(sizeof(struct epoll_event) * NEEV);
	AN(ev);

	now = VTIM_real();
	while (1) {
		while (1) {
			Lck_Lock(&vwe->mtx);
			/*
			 * XXX: We could avoid many syscalls here if we were
			 * XXX: allowed to just close the fd's on timeout.
			 */
			then = Wait_HeapDue(w, &wp);
			if (wp == NULL) {
				vwe->next = now + 100;
				break;
			} else if (then > now) {
				vwe->next = then;
				break;
			}
			CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
			AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_DEL, wp->fd, NULL));
			vwe->nwaited--;
			AN(Wait_HeapDelete(w, wp));
			Lck_Unlock(&vwe->mtx);
			Wait_Call(w, wp, WAITER_TIMEOUT, now);
		}
		then = vwe->next - now;
		i = (int)ceil(1e3 * then);
		assert(i > 0);
		Lck_Unlock(&vwe->mtx);
		do {
			/* Due to a linux kernel bug, epoll_wait can
			   return EINTR when the process is subjected to
			   ptrace or waking from OS suspend. */
			n = epoll_wait(vwe->epfd, ev, NEEV, i);
		} while (n < 0 && errno == EINTR);
		assert(n >= 0);
		assert(n <= NEEV);
		now = VTIM_real();
		for (ep = ev, i = 0; i < n; i++, ep++) {
			if (ep->data.ptr == vwe) {
				assert(read(vwe->pipe[0], &c, 1) == 1);
				continue;
			}
			CAST_OBJ_NOTNULL(wp, ep->data.ptr, WAITED_MAGIC);
			Lck_Lock(&vwe->mtx);
			active = Wait_HeapDelete(w, wp);
			Lck_Unlock(&vwe->mtx);
			if (!active) {
				VSL(SLT_Debug, NO_VXID,
				    "epoll: spurious event (%d)", wp->fd);
				continue;
			}
			AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_DEL, wp->fd, NULL));
			vwe->nwaited--;
			if (ep->events & EPOLLIN) {
				if (ep->events & EPOLLRDHUP &&
				    recv(wp->fd, &c, 1, MSG_PEEK) == 0)
					Wait_Call(w, wp, WAITER_REMCLOSE, now);
				else
					Wait_Call(w, wp, WAITER_ACTION, now);
			} else if (ep->events & EPOLLERR)
				Wait_Call(w, wp, WAITER_REMCLOSE, now);
			else if (ep->events & EPOLLHUP)
				Wait_Call(w, wp, WAITER_REMCLOSE, now);
			else
				Wait_Call(w, wp, WAITER_REMCLOSE, now);
		}
		if (vwe->nwaited == 0 && vwe->die)
			break;
	}
	free(ev);
	closefd(&vwe->pipe[0]);
	closefd(&vwe->pipe[1]);
	closefd(&vwe->epfd);
	return (NULL);
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(waiter_enter_f)
vwe_enter(void *priv, struct waited *wp)
{
	struct vwe *vwe;
	struct epoll_event ee;

	CAST_OBJ_NOTNULL(vwe, priv, VWE_MAGIC);
	ee.events = EPOLLIN | EPOLLRDHUP;
	ee.data.ptr = wp;
	Lck_Lock(&vwe->mtx);
	vwe->nwaited++;
	Wait_HeapInsert(vwe->waiter, wp);
	AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_ADD, wp->fd, &ee));
	/* If the epoll isn't due before our timeout, poke it via the pipe */
	if (Wait_When(wp) < vwe->next)
		assert(write(vwe->pipe[1], "X", 1) == 1);
	Lck_Unlock(&vwe->mtx);
	return (0);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(waiter_init_f)
vwe_init(struct waiter *w)
{
	struct vwe *vwe;
	struct epoll_event ee;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vwe = w->priv;
	INIT_OBJ(vwe, VWE_MAGIC);
	vwe->waiter = w;

	vwe->epfd = epoll_create(1);
	assert(vwe->epfd >= 0);
	Lck_New(&vwe->mtx, lck_waiter);
	AZ(pipe(vwe->pipe));
	ee.events = EPOLLIN | EPOLLRDHUP;
	ee.data.ptr = vwe;
	AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_ADD, vwe->pipe[0], &ee));

	PTOK(pthread_create(&vwe->thread, NULL, vwe_thread, vwe));
}

/*--------------------------------------------------------------------
 * It is the callers responsibility to trigger all fd's waited on to
 * fail somehow.
 */

static void v_matchproto_(waiter_fini_f)
vwe_fini(struct waiter *w)
{
	struct vwe *vwe;
	void *vp;

	CAST_OBJ_NOTNULL(vwe, w->priv, VWE_MAGIC);

	Lck_Lock(&vwe->mtx);
	vwe->die = 1;
	assert(write(vwe->pipe[1], "Y", 1) == 1);
	Lck_Unlock(&vwe->mtx);
	PTOK(pthread_join(vwe->thread, &vp));
	Lck_Delete(&vwe->mtx);
}

/*--------------------------------------------------------------------*/

#include "waiter/mgt_waiter.h"

const struct waiter_impl waiter_epoll = {
	.name =		"epoll",
	.init =		vwe_init,
	.fini =		vwe_fini,
	.enter =	vwe_enter,
	.size =		sizeof(struct vwe),
};

#endif /* defined(HAVE_EPOLL_CTL) */
