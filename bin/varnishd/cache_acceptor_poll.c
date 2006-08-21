/*
 * $Id: cache_acceptor.c 860 2006-08-21 09:49:43Z phk $
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#if defined(HAVE_POLL)

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"
#include "cache_acceptor.h"

static pthread_t vca_poll_thread;
static struct pollfd *pollfd;
static unsigned npoll;

static int pipes[2];

static TAILQ_HEAD(,sess) sesshead = TAILQ_HEAD_INITIALIZER(sesshead);

/*--------------------------------------------------------------------*/

static void
vca_pollspace(int fd)
{
	struct pollfd *p;
	unsigned u, v;

	if (fd < npoll)
		return;
	if (npoll == 0)
		npoll = 16;
	for (u = npoll; fd >= u; )
		u += u;
	VSL(SLT_Debug, 0, "Acceptor Pollspace %u", u);
	p = realloc(pollfd, u * sizeof *p);
	assert(p != NULL);
	memset(p + npoll, 0, (u - npoll) * sizeof *p);
	for (v = npoll ; v <= u; v++) 
		p->fd = -1;
	pollfd = p;
	npoll = u;
}

/*--------------------------------------------------------------------*/

static void
vca_poll(int fd)
{
	vca_pollspace(fd);
	pollfd[fd].fd = fd;
	pollfd[fd].events = POLLIN;
}

static void
vca_unpoll(int fd)
{
	vca_pollspace(fd);
	pollfd[fd].fd = -1;
	pollfd[fd].events = 0;
}

/*--------------------------------------------------------------------*/

static void
vca_rcvhdev(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	clock_gettime(CLOCK_MONOTONIC, &sp->t_idle);
	TAILQ_INSERT_TAIL(&sesshead, sp, list);
	vca_poll(sp->fd);
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
	unsigned u, v;
	struct sess *sp, *sp2;
	struct timespec t;
	int i;

	(void)arg;

	AZ(pipe(pipes));
	vca_poll(pipes[0]);

	if (heritage.socket >= 0)
		vca_poll(heritage.socket);

	while (1) {
		v = poll(pollfd, npoll, 5000);
		if (v && pollfd[pipes[0]].revents) {
			v--;
			i = read(pipes[0], &sp, sizeof sp);
			assert(i == sizeof sp);
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			if (http_RecvPrepAgain(sp->http))
				vca_handover(sp, 0);
			else
				vca_rcvhdev(sp);
		}
		if (heritage.socket >= 0 &&
		    pollfd[heritage.socket].revents) {
			accept_f(heritage.socket);
			v--;
		}
		clock_gettime(CLOCK_MONOTONIC, &t);
		TAILQ_FOREACH_SAFE(sp, &sesshead, list, sp2) {
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		    	if (pollfd[sp->fd].revents) {
				v--;
				i = http_RecvSome(sp->fd, sp->http);
				if (i < 0)
					continue;

				vca_unpoll(sp->fd);
				TAILQ_REMOVE(&sesshead, sp, list);
				vca_handover(sp, i);
				continue;
			}
			if (sp->t_idle.tv_sec + params->sess_timeout < t.tv_sec) {
				TAILQ_REMOVE(&sesshead, sp, list);
				vca_unpoll(sp->fd);
				vca_close_session(sp, "timeout");
				vca_return_session(sp);
				continue;
			}
			if (v == 0)
				break;
		}
	}

	INCOMPL();
}

/*--------------------------------------------------------------------*/

static void
vca_poll_recycle(struct sess *sp)
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
vca_poll_init(void)
{
	AZ(pthread_create(&vca_poll_thread, NULL, vca_main, NULL));
}

struct acceptor acceptor_poll = {
	.name =		"poll",
	.init =		vca_poll_init,
	.recycle =	vca_poll_recycle,
};

#endif /* defined(HAVE_POLL) */
