/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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

#include <fcntl.h>
#include <stdlib.h>

#include "cache.h"

#include "cache_waiter.h"
#include "vtim.h"

#ifndef EPOLLRDHUP
#  define EPOLLRDHUP 0
#endif

#define NEEV	100

struct vwe {
	unsigned		magic;
#define VWE_MAGIC		0x6bd73424

	pthread_t		epoll_thread;
	pthread_t		timer_thread;
	int			epfd;

	VTAILQ_HEAD(,sess)	sesshead;
	int			pipes[2];
	int			timer_pipes[2];
};

static void
vwe_modadd(struct vwe *vwe, int fd, void *data, short arm)
{

	/* XXX: EPOLLET (edge triggered) can cause rather Bad Things to
	 * XXX: happen: If NEEV+1 threads get stuck in write(), all threads
	 * XXX: will hang. See #644.
	 */
	assert(fd >= 0);
	if (data == vwe->pipes || data == vwe->timer_pipes) {
		struct epoll_event ev = {
		    EPOLLIN | EPOLLPRI , { data }
		};
		AZ(epoll_ctl(vwe->epfd, arm, fd, &ev));
	} else {
		struct sess *sp = (struct sess *)data;
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		sp->ev.data.ptr = data;
		sp->ev.events = EPOLLIN | EPOLLPRI | EPOLLONESHOT | EPOLLRDHUP;
		AZ(epoll_ctl(vwe->epfd, arm, fd, &sp->ev));
	}
}

static void
vwe_cond_modadd(struct vwe *vwe, int fd, void *data)
{
	struct sess *sp = (struct sess *)data;

	assert(fd >= 0);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->ev.data.ptr)
		AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_MOD, fd, &sp->ev));
	else {
		sp->ev.data.ptr = data;
		sp->ev.events = EPOLLIN | EPOLLPRI | EPOLLONESHOT | EPOLLRDHUP;
		AZ(epoll_ctl(vwe->epfd, EPOLL_CTL_ADD, fd, &sp->ev));
	}
}

static void
vwe_eev(struct vwe *vwe, const struct epoll_event *ep)
{
	struct sess *ss[NEEV], *sp;
	int i, j;

	AN(ep->data.ptr);
	if (ep->data.ptr == vwe->pipes) {
		if (ep->events & EPOLLIN || ep->events & EPOLLPRI) {
			j = 0;
			i = read(vwe->pipes[0], ss, sizeof ss);
			if (i == -1 && errno == EAGAIN)
				return;
			while (i >= sizeof ss[0]) {
				CHECK_OBJ_NOTNULL(ss[j], SESS_MAGIC);
				assert(ss[j]->fd >= 0);
				AZ(ss[j]->obj);
				VTAILQ_INSERT_TAIL(&vwe->sesshead, ss[j], list);
				vwe_cond_modadd(vwe, ss[j]->fd, ss[j]);
				j++;
				i -= sizeof ss[0];
			}
			assert(i == 0);
		}
	} else {
		CAST_OBJ_NOTNULL(sp, ep->data.ptr, SESS_MAGIC);
		if (ep->events & EPOLLIN || ep->events & EPOLLPRI) {
			i = HTC_Rx(sp->htc);
			if (i == 0) {
				vwe_modadd(vwe, sp->fd, sp, EPOLL_CTL_MOD);
				return;	/* more needed */
			}
			VTAILQ_REMOVE(&vwe->sesshead, sp, list);
			SES_Handle(sp, i);
		} else if (ep->events & EPOLLERR) {
			VTAILQ_REMOVE(&vwe->sesshead, sp, list);
			SES_Delete(sp, "ERR");
		} else if (ep->events & EPOLLHUP) {
			VTAILQ_REMOVE(&vwe->sesshead, sp, list);
			SES_Delete(sp, "HUP");
		} else if (ep->events & EPOLLRDHUP) {
			VTAILQ_REMOVE(&vwe->sesshead, sp, list);
			SES_Delete(sp, "RHUP");
		}
	}
}

/*--------------------------------------------------------------------*/

static void *
vwe_thread(void *priv)
{
	struct epoll_event ev[NEEV], *ep;
	struct sess *sp;
	char junk;
	double deadline;
	int dotimer, i, n;
	struct vwe *vwe;

	CAST_OBJ_NOTNULL(vwe, priv, VWE_MAGIC);

	THR_SetName("cache-epoll");

	vwe->epfd = epoll_create(1);
	assert(vwe->epfd >= 0);

	vwe_modadd(vwe, vwe->pipes[0], vwe->pipes, EPOLL_CTL_ADD);
	vwe_modadd(vwe, vwe->timer_pipes[0], vwe->timer_pipes, EPOLL_CTL_ADD);

	while (1) {
		dotimer = 0;
		n = epoll_wait(vwe->epfd, ev, NEEV, -1);
		for (ep = ev, i = 0; i < n; i++, ep++) {
			if (ep->data.ptr == vwe->timer_pipes &&
			    (ep->events == EPOLLIN || ep->events == EPOLLPRI))
			{
				assert(read(vwe->timer_pipes[0], &junk, 1));
				dotimer = 1;
			} else
				vwe_eev(vwe, ep);
		}
		if (!dotimer)
			continue;

		/* check for timeouts */
		deadline = VTIM_real() - params->sess_timeout;
		for (;;) {
			sp = VTAILQ_FIRST(&vwe->sesshead);
			if (sp == NULL)
				break;
			if (sp->t_open > deadline)
				break;
			VTAILQ_REMOVE(&vwe->sesshead, sp, list);
			// XXX: not yet VTCP_linger(sp->fd, 0);
			SES_Delete(sp, "timeout");
		}
	}
	return NULL;
}

/*--------------------------------------------------------------------*/

static void *
vwe_sess_timeout_ticker(void *priv)
{
	char ticker = 'R';
	struct vwe *vwe;

	CAST_OBJ_NOTNULL(vwe, priv, VWE_MAGIC);
	THR_SetName("cache-epoll-sess_timeout_ticker");

	while (1) {
		/* ticking */
		assert(write(vwe->timer_pipes[1], &ticker, 1));
		VTIM_sleep(100 * 1e-3);
	}
	return NULL;
}

/*--------------------------------------------------------------------*/

static void
vwe_pass(void *priv, const struct sess *sp)
{
	struct vwe *vwe;

	CAST_OBJ_NOTNULL(vwe, priv, VWE_MAGIC);
	assert(sizeof sp == write(vwe->pipes[1], &sp, sizeof sp));
}

/*--------------------------------------------------------------------*/

static void *
vwe_init(void)
{
	int i;
	struct vwe *vwe;

	ALLOC_OBJ(vwe, VWE_MAGIC);
	AN(vwe);
	VTAILQ_INIT(&vwe->sesshead);
	AZ(pipe(vwe->pipes));
	AZ(pipe(vwe->timer_pipes));

	i = fcntl(vwe->pipes[0], F_GETFL);
	assert(i != -1);
	i |= O_NONBLOCK;
	i = fcntl(vwe->pipes[0], F_SETFL, i);
	assert(i != -1);

	i = fcntl(vwe->timer_pipes[0], F_GETFL);
	assert(i != -1);
	i |= O_NONBLOCK;
	i = fcntl(vwe->timer_pipes[0], F_SETFL, i);
	assert(i != -1);

	AZ(pthread_create(&vwe->timer_thread,
	    NULL, vwe_sess_timeout_ticker, vwe));
	AZ(pthread_create(&vwe->epoll_thread, NULL, vwe_thread, vwe));
	return(vwe);
}

/*--------------------------------------------------------------------*/

const struct waiter waiter_epoll = {
	.name =		"epoll",
	.init =		vwe_init,
	.pass =		vwe_pass,
};

#endif /* defined(HAVE_EPOLL_CTL) */
