/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
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
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#if defined(HAVE_EPOLL_CTL)

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/epoll.h>

#ifndef HAVE_CLOCK_GETTIME
#include "compat/clock_gettime.h"
#endif

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"
#include "cache_acceptor.h"

static pthread_t vca_epoll_thread;
static int epfd = -1;
static int pipes[2];

static TAILQ_HEAD(,sess) sesshead = TAILQ_HEAD_INITIALIZER(sesshead);

static void
vca_add(int fd, void *data)
{
	struct epoll_event ev = { EPOLLIN | EPOLLPRI, { data } };
	AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev));
}

static void
vca_del(int fd)
{
	struct epoll_event ev = { 0, { 0 } };
	AZ(epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev));
}

static void *
vca_main(void *arg)
{
	struct epoll_event ev;
	struct timespec ts;
	struct sess *sp, *sp2;
	int i;

	(void)arg;

	epfd = epoll_create(16);
	assert(epfd >= 0);

	vca_add(pipes[0], pipes);

	while (1) {
		if (epoll_wait(epfd, &ev, 1, 100) > 0) {
			if (ev.data.ptr == pipes) {
				i = read(pipes[0], &sp, sizeof sp);
				assert(i == sizeof sp);
				CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
				TAILQ_INSERT_TAIL(&sesshead, sp, list);
				vca_add(sp->fd, sp);
			} else {
				CAST_OBJ_NOTNULL(sp, ev.data.ptr, SESS_MAGIC);
				i = vca_pollsession(sp);
				if (i >= 0) {
					TAILQ_REMOVE(&sesshead, sp, list);
					if (sp->fd != -1)
						vca_del(sp->fd);
					if (i == 0)
						vca_handover(sp, i);
					else
						SES_Delete(sp);
				}
			}
		}
		/* check for timeouts */
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec -= params->sess_timeout;
		TAILQ_FOREACH_SAFE(sp, &sesshead, list, sp2) {
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			if (sp->t_open.tv_sec > ts.tv_sec)
				continue;
			if (sp->t_open.tv_sec == ts.tv_sec &&
			    sp->t_open.tv_nsec > ts.tv_nsec)
				continue;
			TAILQ_REMOVE(&sesshead, sp, list);
			vca_del(sp->fd);
			vca_close_session(sp, "timeout");
			SES_Delete(sp);
		}
	}
}

/*--------------------------------------------------------------------*/

static void
vca_epoll_recycle(struct sess *sp)
{

	if (sp->fd < 0)
		SES_Delete(sp);
	else
		assert(sizeof sp == write(pipes[1], &sp, sizeof sp));
}

static void
vca_epoll_init(void)
{

	AZ(pipe(pipes));
	AZ(pthread_create(&vca_epoll_thread, NULL, vca_main, NULL));
}

struct acceptor acceptor_epoll = {
	.name =		"epoll",
	.init =		vca_epoll_init,
	.recycle =	vca_epoll_recycle,
};

#endif /* defined(HAVE_EPOLL_CTL) */
