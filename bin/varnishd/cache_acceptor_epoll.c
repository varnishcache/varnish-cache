/*
 * $Id: cache_acceptor.c 860 2006-08-21 09:49:43Z phk $
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

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
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
	AZ(epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL));
}

static void
vca_rcvhdev(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	clock_gettime(CLOCK_MONOTONIC, &sp->t_idle);
	TAILQ_INSERT_TAIL(&sesshead, sp, list);
	vca_add(sp->fd, sp);
}

static void
accept_f(int fd)
{
	struct sess *sp;

	sp = vca_accept_sess(fd);
	if (sp == NULL)
		return;
	http_RecvPrep(sp->http);
	vca_rcvhdev(sp);
}

static void *
vca_main(void *arg)
{
	struct epoll_event ev;
	struct timespec t;
	struct sess *sp, *sp2;
	int i;

	(void)arg;

	epfd = epoll_create(16);
	assert(epfd >= 0);

	AZ(pipe(pipes));
	vca_add(pipes[0], pipes);

	if (heritage.socket >= 0)
		vca_add(heritage.socket, accept_f);

	while (1) {
		if (epoll_wait(epfd, &ev, 1, 5000) > 0) {
			if (ev.data.ptr == pipes) {
				i = read(pipes[0], &sp, sizeof sp);
				assert(i == sizeof sp);
				CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
				if (http_RecvPrepAgain(sp->http))
					vca_handover(sp, 0);
				else
					vca_rcvhdev(sp);
			} else if (ev.data.ptr == accept_f) {
				accept_f(heritage.socket);
			} else {
				CAST_OBJ_NOTNULL(sp, ev.data.ptr, SESS_MAGIC);
				i = http_RecvSome(sp->fd, sp->http);
				if (i != -1) {
					TAILQ_REMOVE(&sesshead, sp, list);
					vca_del(sp->fd);
					vca_handover(sp, i);
				}
			}
		}
		/* check for timeouts */
		clock_gettime(CLOCK_MONOTONIC, &t);
		TAILQ_FOREACH_SAFE(sp, &sesshead, list, sp2) {
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			if (sp->t_idle.tv_sec + params->sess_timeout < t.tv_sec) {
				TAILQ_REMOVE(&sesshead, sp, list);
				vca_del(sp->fd);
				vca_close_session(sp, "timeout");
				vca_return_session(sp);
				continue;
			}
		}
	}

	INCOMPL();
}

/*--------------------------------------------------------------------*/

static void
vca_epoll_recycle(struct sess *sp)
{

	if (sp->fd < 0) {
		SES_Delete(sp);
		return;
	}
	(void)clock_gettime(CLOCK_REALTIME, &sp->t_open);
	VSL(SLT_SessionReuse, sp->fd, "%s %s", sp->addr, sp->port);
	assert(sizeof sp == write(pipes[1], &sp, sizeof sp));
}

static void
vca_epoll_init(void)
{
	AZ(pthread_create(&vca_epoll_thread, NULL, vca_main, NULL));
}

struct acceptor acceptor_epoll = {
	.name =		"epoll",
	.init =		vca_epoll_init,
	.recycle =	vca_epoll_recycle,
};

#endif /* defined(HAVE_EPOLL_CTL) */
