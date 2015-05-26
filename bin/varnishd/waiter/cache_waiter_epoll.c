/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Rogerio Carvalho Schneider <stockrt@gmail.com>
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
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#include "config.h"

#if defined(HAVE_EPOLL_CTL)

#include <sys/epoll.h>

#include <stdlib.h>

#include "cache/cache.h"

#include "waiter/waiter_priv.h"
#include "waiter/mgt_waiter.h"
#include "vtim.h"
#include "vfil.h"

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
	VTAILQ_HEAD(,waited)	list;
	struct lock		mtx;
	int			die;
};

static void
vwe_eev(struct vwe *vwe, const struct epoll_event *ep, double now)
{
	struct waited *wp;

	AN(ep->data.ptr);
	CAST_OBJ_NOTNULL(wp, ep->data.ptr, WAITED_MAGIC);
	if (ep->events & EPOLLIN) {
		Wait_Call(vwe->waiter, wp, WAITER_ACTION, now);
	} else if (ep->events & EPOLLERR) {
		Wait_Call(vwe->waiter, wp, WAITER_REMCLOSE, now);
	} else if (ep->events & EPOLLHUP) {
		Wait_Call(vwe->waiter, wp, WAITER_REMCLOSE, now);
	} else if (ep->events & EPOLLRDHUP) {
		Wait_Call(vwe->waiter, wp, WAITER_REMCLOSE, now);
	}
}

/*--------------------------------------------------------------------*/

static void *
vwe_thread(void *priv)
{
	struct epoll_event ev[NEEV], *ep;
	struct waited *wp, *wp2;
	double now, idle, last_idle;
	int i, n;
	struct vwe *vwe;
	VTAILQ_HEAD(,waited) tlist;

	CAST_OBJ_NOTNULL(vwe, priv, VWE_MAGIC);

	THR_SetName("cache-epoll");

	last_idle = 0.0;
	while (1) {
		i = floor(.3 * 1e3 * Wait_Tmo(vwe->waiter, NULL));
		n = epoll_wait(vwe->epfd, ev, NEEV, i);
		if (n < 0 && vwe->die)
			break;
		assert(n >= 0);
		now = VTIM_real();
		for (ep = ev, i = 0; i < n; i++, ep++) {
			CAST_OBJ_NOTNULL(wp, ep->data.ptr, WAITED_MAGIC);
			AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_DEL, wp->fd, NULL));
			Lck_Lock(&vwe->mtx);
			VTAILQ_REMOVE(&vwe->list, wp, list);
			Lck_Unlock(&vwe->mtx);
			vwe_eev(vwe, ep, now);
		}
		idle = now - Wait_Tmo(vwe->waiter, NULL);
		if (now - last_idle < .3 * Wait_Tmo(vwe->waiter, NULL))
			continue;
		last_idle = now;
		VTAILQ_INIT(&tlist);
		Lck_Lock(&vwe->mtx);
		VTAILQ_FOREACH_SAFE(wp, &vwe->list, list, wp2) {
			if (wp->idle > idle)
				continue;
			VTAILQ_REMOVE(&vwe->list, wp, list);
			VTAILQ_INSERT_TAIL(&tlist, wp, list);
			AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_DEL, wp->fd, NULL));
		}
		Lck_Unlock(&vwe->mtx);
		while(1) {
			wp = VTAILQ_FIRST(&tlist);
			if (wp == NULL)
				break;
			VTAILQ_REMOVE(&tlist, wp, list);
			Wait_Call(vwe->waiter, wp, WAITER_TIMEOUT, now);
		}
	}
	return (NULL);
}

/*--------------------------------------------------------------------*/

static int __match_proto__(waiter_enter_f)
vwe_enter(void *priv, struct waited *wp)
{
	struct vwe *vwe;
	struct epoll_event ee;

	CAST_OBJ_NOTNULL(vwe, priv, VWE_MAGIC);
	ee.events = EPOLLIN | EPOLLRDHUP;
	ee.data.ptr = wp;
	Lck_Lock(&vwe->mtx);
	VTAILQ_INSERT_TAIL(&vwe->list, wp, list);
	AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_ADD, wp->fd, &ee));
	Lck_Unlock(&vwe->mtx);
	return(0);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_init_f)
vwe_init(struct waiter *w)
{
	struct vwe *vwe;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vwe = w->priv;
	INIT_OBJ(vwe, VWE_MAGIC);
	vwe->waiter = w;

	vwe->epfd = epoll_create(1);
	assert(vwe->epfd >= 0);
	VTAILQ_INIT(&vwe->list);
	Lck_New(&vwe->mtx, lck_misc);

	AZ(pthread_create(&vwe->thread, NULL, vwe_thread, vwe));
}

/*--------------------------------------------------------------------
 * It is the callers responsibility to trigger all fd's waited on to
 * fail somehow.
 */

static void __match_proto__(waiter_fini_f)
vwe_fini(struct waiter *w)
{
	struct vwe *vwe;
	void *vp;
	int i;

	CAST_OBJ_NOTNULL(vwe, w->priv, VWE_MAGIC);

	Lck_Lock(&vwe->mtx);
	while (!VTAILQ_EMPTY(&vwe->list)) {
		Lck_Unlock(&vwe->mtx);
		(void)usleep(100000);
		Lck_Lock(&vwe->mtx);
	}
	vwe->die = 1;
	i = vwe->epfd;
	vwe->epfd = -1;
	AZ(close(i));
	Lck_Unlock(&vwe->mtx);
	AZ(pthread_join(vwe->thread, &vp));
	Lck_Delete(&vwe->mtx);
}

/*--------------------------------------------------------------------*/

const struct waiter_impl waiter_epoll = {
	.name =		"epoll",
	.init =		vwe_init,
	.fini =		vwe_fini,
	.enter =	vwe_enter,
	.size =		sizeof(struct vwe),
};

#endif /* defined(HAVE_EPOLL_CTL) */
