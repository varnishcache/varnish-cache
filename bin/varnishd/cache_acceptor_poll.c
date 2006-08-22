/*
 * $Id$
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

static void *
vca_main(void *arg)
{
	unsigned v;
	struct sess *sp, *sp2;
	struct timespec ts;
	int i;

	(void)arg;

	vca_poll(pipes[0]);

	while (1) {
		v = poll(pollfd, npoll, 100);
		if (v && pollfd[pipes[0]].revents) {
			v--;
			i = read(pipes[0], &sp, sizeof sp);
			assert(i == sizeof sp);
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
			TAILQ_INSERT_TAIL(&sesshead, sp, list);
			vca_poll(sp->fd);
		}
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec -= params->sess_timeout;
		TAILQ_FOREACH_SAFE(sp, &sesshead, list, sp2) {
			if (v == 0)
				break;
			CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		    	if (pollfd[sp->fd].revents) {
				v--;
				i = vca_pollsession(sp);
				if (i < 0)
					continue;
				TAILQ_REMOVE(&sesshead, sp, list);
				vca_unpoll(sp->fd);
				if (i == 0)
					vca_handover(sp, i);
				else
					SES_Delete(sp);
				continue;
			}
			if (sp->t_open.tv_sec > ts.tv_sec)
				continue;
			if (sp->t_open.tv_sec == ts.tv_sec &&
			    sp->t_open.tv_nsec > ts.tv_nsec)
				continue;
			TAILQ_REMOVE(&sesshead, sp, list);
			vca_unpoll(sp->fd);
			vca_close_session(sp, "timeout");
			SES_Delete(sp);
		}
	}
}

/*--------------------------------------------------------------------*/

static void
vca_poll_recycle(struct sess *sp)
{

	if (sp->fd < 0)
		SES_Delete(sp);
	else
		assert(sizeof sp == write(pipes[1], &sp, sizeof sp));
}

static void
vca_poll_init(void)
{
	AZ(pipe(pipes));
	AZ(pthread_create(&vca_poll_thread, NULL, vca_main, NULL));
}

struct acceptor acceptor_poll = {
	.name =		"poll",
	.init =		vca_poll_init,
	.recycle =	vca_poll_recycle,
};

#endif /* defined(HAVE_POLL) */
