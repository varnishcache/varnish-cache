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
#include "cache_acceptor.h"


static struct acceptor *vca_acceptors[] = {
#if defined(HAVE_KQUEUE)
	&acceptor_kqueue,
#endif
#if defined(HAVE_EPOLL_CTL)
	&acceptor_epoll,
#endif
#if defined(HAVE_POLL_CTL)
	&acceptor_poll,
#endif
	NULL,
};

static unsigned		xids;

struct sess *
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

void
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

/*--------------------------------------------------------------------*/

void
vca_close_session(struct sess *sp, const char *why)
{

	VSL(SLT_SessionClose, sp->fd, why);
	if (sp->fd >= 0)
		AZ(close(sp->fd));
	sp->fd = -1;
}

void
vca_return_session(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vca_acceptors[0]->recycle(sp);
}

/*--------------------------------------------------------------------*/

void
VCA_Init(void)
{

	srandomdev();
	xids = random();

	/* XXX: Add selector mechanism at some point */
	vca_acceptors[0]->init();
}
