/*
 * $Id$
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

#undef ACCEPTOR_USE_KQUEUE
#undef ACCEPTOR_USE_EPOLL
#undef ACCEPTOR_USE_POLL

#if defined(HAVE_KQUEUE)
#define ACCEPTOR_USE_KQUEUE 1
#elif defined(HAVE_EPOLL_CTL)
#define ACCEPTOR_USE_EPOLL 1
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

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

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

	tv.tv_sec = params->send_timeout;
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
		SES_Delete(sp);
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
#ifdef ACCEPTOR_USE_EPOLL

#include <sys/epoll.h>

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

#endif /* ACCEPTOR_USE_EPOLL */
/*====================================================================*/
#ifdef ACCEPTOR_USE_KQUEUE

#include <sys/event.h>

static int kq = -1;

static void
vca_kq_sess(struct sess *sp, int arm)
{
	struct kevent ke[2];
	int i, j;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	memset(ke, 0, sizeof ke);
	j = 0;
	/* close(2) automatically removes the EVFILT_READ event */
	if (sp->fd >= 0)
		EV_SET(&ke[j++], sp->fd, EVFILT_READ, arm, 0, 0, sp);
	EV_SET(&ke[j++], sp->id, EVFILT_TIMER | EV_ONESHOT,
	    arm == EV_ADD || arm == EV_ENABLE ? EV_ADD : EV_DELETE,
	    0, params->sess_timeout * 1000, sp);
	if (arm == EV_ADD || arm == EV_ENABLE)
		sp->kqa = 1;
	else 
		sp->kqa = 0;
	i = kevent(kq, ke, j, NULL, 0, NULL);
	assert(i == 0);
}

static struct sess *
vca_kev(struct kevent *kp)
{
	int i;
	struct sess *sp;

	if (kp->udata == vca_accept_sess) {
		assert(kp->data > 0);
		while (kp->data-- > 0) {
			sp = vca_accept_sess(kp->ident);
			if (sp == NULL)
				return (NULL);
			clock_gettime(CLOCK_MONOTONIC, &sp->t_idle);
			http_RecvPrep(sp->http);
			vca_kq_sess(sp, EV_ADD);
		}
		return (NULL);
	}
	if (kp->udata == NULL) {
		VSL(SLT_Debug, 0,
		    "KQ RACE %s flags %x fflags %x data %x",
		    kp->filter == EVFILT_READ ? "R" : "T",
		    kp->flags, kp->fflags, kp->data);
		return (NULL);
	}
	CAST_OBJ_NOTNULL(sp, kp->udata, SESS_MAGIC);
	if (sp->kqa == 0) {
		VSL(SLT_Debug, sp->fd,
		    "KQ %s flags %x fflags %x data %x",
		    kp->filter == EVFILT_READ ? "R" : "T",
		    kp->flags, kp->fflags, kp->data);
		return (NULL);
	}
	if (kp->filter == EVFILT_READ) {
		if (kp->data > 0) {
			i = http_RecvSome(sp->fd, sp->http);
			switch (i) {
			case -1:
				return (NULL);
			case 0:
				vca_kq_sess(sp, EV_DISABLE);
				vca_handover(sp, i);
				return (NULL);	 /* ?? */
			case 1:
				vca_close_session(sp, "overflow");
				break;
			case 2:
				vca_close_session(sp, "no request");
				break;
			default:
				INCOMPL();
			}
			return (sp);
		}
		if (kp->flags == EV_EOF) {
			vca_close_session(sp, "EOF");
			return (sp);
		}
		INCOMPL();
	}
	if (kp->filter == EVFILT_TIMER) {
		vca_close_session(sp, "timeout");
		return (sp);
	}
	INCOMPL();
}


#define NKEV	100

static void *
vca_main(void *arg)
{
	struct kevent ke[NKEV], *kp;
	int i, j, n;
	struct sess *sp;

	(void)arg;

	kq = kqueue();
	assert(kq >= 0);


	assert(heritage.socket >= 0);
	EV_SET(&ke[0], heritage.socket,
	    EVFILT_READ, EV_ADD, 0, 0, vca_accept_sess);
	AZ(kevent(kq, &ke[0], 1, NULL, 0, NULL));

	while (1) {
		n = kevent(kq, NULL, 0, ke, NKEV, NULL);
		assert(n >= 1 && n <= NKEV);
		for (kp = ke, j = 0; j < n; j++, kp++) {
			sp = vca_kev(kp);
			if (sp != NULL) {
				vca_kq_sess(sp, EV_DELETE);
				SES_Delete(sp);
				for (i = j; i < n; i++)
					if (ke[i].udata == sp)
						ke[i].udata = NULL;
			}
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
	if (http_RecvPrepAgain(sp->http))
		vca_handover(sp, 0);
	else 
		vca_kq_sess(sp, EV_ENABLE);
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
