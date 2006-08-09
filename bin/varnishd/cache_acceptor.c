/*
 * $Id$
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#undef ACCEPTOR_USE_KQUEUE
#undef ACCEPTOR_USE_POLL

#if defined(HAVE_KQUEUE)
#define ACCEPTOR_USE_KQUEUE 1
#elif defined(HAVE_POLL)
#define ACCEPTOR_USE_POLL 1
#else
#error No usable acceptors detected.
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "compat/srandomdev.h"

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

static pthread_t vca_thread;
static unsigned		xids;

static struct sess *
vca_accept_sess(int fd)
{
	socklen_t l;
	struct sockaddr addr[2];	/* XXX: IPv6 hack */
	struct sess *sp;
	int i;
	struct linger linger;

	VSL_stats->client_conn++;

	l = sizeof addr;
	i = accept(fd, addr, &l);
	if (i < 0) {
		VSL(SLT_Debug, fd, "Accept failed errno=%d", errno);
		/* XXX: stats ? */
		return (NULL);
	}
	sp = SES_New(addr, l);
	assert(sp != NULL);	/* XXX handle */

	(void)clock_gettime(CLOCK_REALTIME, &sp->t_open);
	sp->fd = i;
	sp->id = i;

#ifdef SO_NOSIGPIPE /* XXX Linux */
	i = 1;
	AZ(setsockopt(sp->fd, SOL_SOCKET, SO_NOSIGPIPE, &i, sizeof i));
#endif
#ifdef SO_LINGER /* XXX Linux*/
	linger.l_onoff = 0;
	linger.l_linger = 0;
	AZ(setsockopt(sp->fd, SOL_SOCKET, SO_LINGER, &linger, sizeof linger));
#endif
#ifdef SO_SNDTIMEO
	{
	struct timeval tv;

	tv.tv_sec = 600;
	tv.tv_usec = 0;
	AZ(setsockopt(sp->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv));
	}
#endif

	TCP_name(addr, l, sp->addr, sizeof sp->addr, sp->port, sizeof sp->port);
	VSL(SLT_SessionOpen, sp->fd, "%s %s", sp->addr, sp->port);
	return (sp);
}

static void
vca_handover(struct sess *sp, int bad)
{

	if (bad) {
		vca_close_session(sp,
		    bad == 1 ? "overflow" : "no request");
		vca_return_session(sp);
		return;
	}
	sp->step = STP_RECV;
	VSL_stats->client_req++;
	sp->xid = xids++;
	VSL(SLT_ReqStart, sp->fd, "XID %u", sp->xid);
	WRK_QueueSession(sp);
}

/*====================================================================*/
#ifdef ACCEPTOR_USE_POLL

#include <poll.h>

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
			if (sp->t_idle.tv_sec + 5 < t.tv_sec) {
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

void
vca_return_session(struct sess *sp)
{

	if (sp->fd < 0) {
		SES_Delete(sp);
		return;
	}
	(void)clock_gettime(CLOCK_REALTIME, &sp->t_open);
	VSL(SLT_SessionReuse, sp->fd, "%s %s", sp->addr, sp->port);
	assert(sizeof sp == write(pipes[1], &sp, sizeof sp));
}

#endif /* ACCEPTOR_USE_POLL */
/*====================================================================*/
#ifdef ACCEPTOR_USE_KQUEUE

#include <sys/event.h>

static int kq = -1;

static void
vca_kq_sess(struct sess *sp, int arm)
{
	struct kevent ke[2];
	int i;

	assert(arm == EV_ADD || arm == EV_DELETE);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	memset(ke, 0, sizeof ke);
	EV_SET(&ke[0], sp->fd, EVFILT_READ, arm, 0, 0, sp);
	EV_SET(&ke[1], sp->fd, EVFILT_TIMER, arm , 0, 5000, sp);
	i = kevent(kq, ke, 2, NULL, 0, NULL);
	if (arm == EV_ADD)
		assert(i == 0);
	else
		assert(i == 0 || errno == ENOENT);
}

static void
accept_f(int fd)
{
	struct sess *sp;

	sp = vca_accept_sess(fd);
	if (sp == NULL)
		return;
	clock_gettime(CLOCK_MONOTONIC, &sp->t_idle);
	http_RecvPrep(sp->http);
	vca_kq_sess(sp, EV_ADD);
}

static void *
vca_main(void *arg)
{
	unsigned u;
	struct kevent ke;
	int i;
	struct sess *sp;

	(void)arg;

	kq = kqueue();
	assert(kq >= 0);


	if (heritage.socket >= 0) {
		memset(&ke, 0, sizeof ke);
		EV_SET(&ke, heritage.socket,
		    EVFILT_READ, EV_ADD, 0, 0, accept_f);
		AZ(kevent(kq, &ke, 1, NULL, 0, NULL));
	}

	while (1) {
		i = kevent(kq, NULL, 0, &ke, 1, NULL);
		assert(i == 1);
#if 0
		printf("i = %d\n", i);
		printf("ke.ident = %ju\n", (uintmax_t)ke.ident);
		printf("ke.filter = %u\n", ke.filter);
		printf("ke.flags = %u\n", ke.flags);
		printf("ke.fflags = %u\n", ke.fflags);
		printf("ke.data = %jd\n", (intmax_t)ke.data);
		printf("ke.udata = %p\n", ke.udata);
#endif
		if (ke.udata == accept_f) {
			accept_f(ke.ident);
			continue;
		}
		CAST_OBJ_NOTNULL(sp, ke.udata, SESS_MAGIC);
		if (ke.filter == EVFILT_READ) {
			i = http_RecvSome(sp->fd, sp->http);
			if (i == -1)
				continue;
			vca_kq_sess(sp, EV_DELETE);
			vca_handover(sp, i);
			continue;
		}
		if (ke.filter == EVFILT_TIMER) {
			vca_kq_sess(sp, EV_DELETE);
			vca_close_session(sp, "timeout");
			vca_return_session(sp);
			continue;
		} 
		INCOMPL();
	}

	INCOMPL();
}

/*--------------------------------------------------------------------*/

void
vca_return_session(struct sess *sp)
{

	if (sp->fd < 0) {
		SES_Delete(sp);
		return;
	}
	(void)clock_gettime(CLOCK_REALTIME, &sp->t_open);
	VSL(SLT_SessionReuse, sp->fd, "%s %s", sp->addr, sp->port);
	if (http_RecvPrepAgain(sp->http))
		vca_handover(sp, 0);
	else
		vca_kq_sess(sp, EV_ADD);
}

#endif /* ACCEPTOR_USE_KQUEUE */
/*====================================================================*/

/*--------------------------------------------------------------------*/

void
vca_close_session(struct sess *sp, const char *why)
{

	VSL(SLT_SessionClose, sp->fd, why);
	if (sp->fd >= 0)
		AZ(close(sp->fd));
	sp->fd = -1;
}

/*--------------------------------------------------------------------*/

void
VCA_Init(void)
{

	AZ(pthread_create(&vca_thread, NULL, vca_main, NULL));
	srandomdev();
	xids = random();
}
