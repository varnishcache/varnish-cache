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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/epoll.h>

#ifndef EPOLLRDHUP
#  define EPOLLRDHUP 0
#endif

#include "cache.h"
#include "cache_waiter.h"

#define NEEV	100

static pthread_t vca_epoll_thread;
static pthread_t vca_epoll_timeout_thread;;
static int epfd = -1;

static VTAILQ_HEAD(,sess) sesshead = VTAILQ_HEAD_INITIALIZER(sesshead);
int dotimer_pipe[2];

static void
vca_modadd(int fd, void *data, short arm)
{

	/* XXX: EPOLLET (edge triggered) can cause rather Bad Things to
	 * XXX: happen: If NEEV+1 threads get stuck in write(), all threads
	 * XXX: will hang. See #644.
	 */
	assert(fd >= 0);
	if (data == vca_pipes || data == dotimer_pipe) {
		struct epoll_event ev = {
		    EPOLLIN | EPOLLPRI , { data }
		};
		AZ(epoll_ctl(epfd, arm, fd, &ev));
	} else {
		struct sess *sp = (struct sess *)data;
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		sp->ev.data.ptr = data;
		sp->ev.events = EPOLLIN | EPOLLPRI | EPOLLONESHOT | EPOLLRDHUP;
		AZ(epoll_ctl(epfd, arm, fd, &sp->ev));
	}
}

static void
vca_cond_modadd(int fd, void *data)
{
	struct sess *sp = (struct sess *)data;

	assert(fd >= 0);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->ev.data.ptr)
		AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &sp->ev));
	else {
		sp->ev.data.ptr = data;
		sp->ev.events = EPOLLIN | EPOLLPRI | EPOLLONESHOT | EPOLLRDHUP;
		AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &sp->ev));
	}
}

static void
vca_eev(const struct epoll_event *ep)
{
	struct sess *ss[NEEV], *sp;
	int i, j;

	AN(ep->data.ptr);
	if (ep->data.ptr == vca_pipes) {
		if (ep->events & EPOLLIN || ep->events & EPOLLPRI) {
			j = 0;
			i = read(vca_pipes[0], ss, sizeof ss);
			if (i == -1 && errno == EAGAIN)
				return;
			while (i >= sizeof ss[0]) {
				CHECK_OBJ_NOTNULL(ss[j], SESS_MAGIC);
				assert(ss[j]->fd >= 0);
				AZ(ss[j]->obj);
				VTAILQ_INSERT_TAIL(&sesshead, ss[j], list);
				vca_cond_modadd(ss[j]->fd, ss[j]);
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
				vca_modadd(sp->fd, sp, EPOLL_CTL_MOD);
				return;	/* more needed */
			}
			VTAILQ_REMOVE(&sesshead, sp, list);
			vca_handover(sp, i);
		} else if (ep->events & EPOLLERR) {
			VTAILQ_REMOVE(&sesshead, sp, list);
			vca_close_session(sp, "ERR");
			SES_Delete(sp);
		} else if (ep->events & EPOLLHUP) {
			VTAILQ_REMOVE(&sesshead, sp, list);
			vca_close_session(sp, "HUP");
			SES_Delete(sp);
		} else if (ep->events & EPOLLRDHUP) {
			VTAILQ_REMOVE(&sesshead, sp, list);
			vca_close_session(sp, "RHUP");
			SES_Delete(sp);
		}
	}
}

/*--------------------------------------------------------------------*/

static void *
vca_main(void *arg)
{
	struct epoll_event ev[NEEV], *ep;
	struct sess *sp;
	char junk;
	double deadline;
	int dotimer, i, n;

	THR_SetName("cache-epoll");
	(void)arg;

	epfd = epoll_create(1);
	assert(epfd >= 0);

	vca_modadd(vca_pipes[0], vca_pipes, EPOLL_CTL_ADD);
	vca_modadd(dotimer_pipe[0], dotimer_pipe, EPOLL_CTL_ADD);

	while (1) {
		dotimer = 0;
		n = epoll_wait(epfd, ev, NEEV, -1);
		for (ep = ev, i = 0; i < n; i++, ep++) {
			if (ep->data.ptr == dotimer_pipe &&
			    (ep->events == EPOLLIN || ep->events == EPOLLPRI))
			{
				assert(read(dotimer_pipe[0], &junk, 1));
				dotimer = 1;
			} else
				vca_eev(ep);
		}
		if (!dotimer)
			continue;

		/* check for timeouts */
		deadline = TIM_real() - params->sess_timeout;
		for (;;) {
			sp = VTAILQ_FIRST(&sesshead);
			if (sp == NULL)
				break;
			if (sp->t_open > deadline)
				break;
			VTAILQ_REMOVE(&sesshead, sp, list);
			// XXX: not yet VTCP_linger(sp->fd, 0);
			vca_close_session(sp, "timeout");
			SES_Delete(sp);
		}
	}
	return NULL;
}

/*--------------------------------------------------------------------*/

static void *
vca_sess_timeout_ticker(void *arg)
{
	char ticker = 'R';

	THR_SetName("cache-epoll-sess_timeout_ticker");
	(void)arg;

	while (1) {
		/* ticking */
		assert(write(dotimer_pipe[1], &ticker, 1));
		TIM_sleep(100 * 1e-3);
	}
	return NULL;
}

/*--------------------------------------------------------------------*/

static void
vca_epoll_init(void)
{
	int i;

	i = fcntl(vca_pipes[0], F_GETFL);
	assert(i != -1);
	i |= O_NONBLOCK;
	i = fcntl(vca_pipes[0], F_SETFL, i);
	assert(i != -1);

	AZ(pipe(dotimer_pipe));
	i = fcntl(dotimer_pipe[0], F_GETFL);
	assert(i != -1);
	i |= O_NONBLOCK;
	i = fcntl(dotimer_pipe[0], F_SETFL, i);
	assert(i != -1);

	AZ(pthread_create(&vca_epoll_timeout_thread,
	    NULL, vca_sess_timeout_ticker, NULL));
	AZ(pthread_create(&vca_epoll_thread, NULL, vca_main, NULL));
}

struct waiter waiter_epoll = {
	.name =		"epoll",
	.init =		vca_epoll_init,
};

#endif /* defined(HAVE_EPOLL_CTL) */
