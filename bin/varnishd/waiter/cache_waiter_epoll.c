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

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "vtim.h"
#include "vfil.h"

#ifndef EPOLLRDHUP
#  define EPOLLRDHUP 0
#endif

#define NEEV	8192

struct vwe {
	unsigned		magic;
#define VWE_MAGIC		0x6bd73424
	struct waiter		*waiter;

	pthread_t		epoll_thread;
	int			epfd;
};

static void
vwe_inject(const struct waiter *w, struct waited *wp)
{
	struct vwe *vwe;

	CAST_OBJ_NOTNULL(vwe, w->priv, VWE_MAGIC);
	CHECK_OBJ_NOTNULL(wp, WAITED_MAGIC);
	assert(wp->fd >= 0);

	if (wp->ev.data.ptr)
		AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_MOD, wp->fd, &wp->ev));
	else {
		wp->ev.data.ptr = wp;
		wp->ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
		if (wp != w->pipe_w)
			wp->ev.events |= EPOLLONESHOT;
		AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_ADD, wp->fd, &wp->ev));
	}
}

static void
vwe_eev(struct vwe *vwe, const struct epoll_event *ep, double now)
{
	struct waited *sp;

	AN(ep->data.ptr);
	CAST_OBJ_NOTNULL(sp, ep->data.ptr, WAITED_MAGIC);
	if (ep->events & EPOLLIN || ep->events & EPOLLPRI) {
		Wait_Handle(vwe->waiter, sp, WAITER_ACTION, now);
	} else if (ep->events & EPOLLERR) {
		Wait_Handle(vwe->waiter, sp, WAITER_REMCLOSE, now);
	} else if (ep->events & EPOLLHUP) {
		Wait_Handle(vwe->waiter, sp, WAITER_REMCLOSE, now);
	} else if (ep->events & EPOLLRDHUP) {
		Wait_Handle(vwe->waiter, sp, WAITER_REMCLOSE, now);
	}
}

/*--------------------------------------------------------------------*/

static void *
vwe_thread(void *priv)
{
	struct epoll_event ev[NEEV], *ep;
	double now;
	int i, n;
	struct vwe *vwe;

	CAST_OBJ_NOTNULL(vwe, priv, VWE_MAGIC);

	THR_SetName("cache-epoll");

	while (1) {
		n = epoll_wait(vwe->epfd, ev, NEEV, -1);
		now = VTIM_real();
		for (ep = ev, i = 0; i < n; i++, ep++)
			vwe_eev(vwe, ep, now);
	}
	return (NULL);
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

	Wait_UsePipe(w);

	AZ(pthread_create(&vwe->epoll_thread, NULL, vwe_thread, vwe));
}

/*--------------------------------------------------------------------*/

const struct waiter_impl waiter_epoll = {
	.name =		"epoll",
	.init =		vwe_init,
	.inject =	vwe_inject,
	.size =		sizeof(struct vwe),
};

#endif /* defined(HAVE_EPOLL_CTL) */
