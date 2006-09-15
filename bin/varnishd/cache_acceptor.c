/*
 * $Id$
 *
 * XXX: We need to pass sessions back into the event engine when they are
 * reused.  Not sure what the most efficient way is for that.  For now
 * write the session pointer to a pipe which the event engine monitors.
 */

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
#if defined(HAVE_POLL)
	&acceptor_poll,
#endif
	NULL,
};

static struct acceptor *vca_act;

static pthread_t 	vca_thread_acct;

static struct sess *
vca_accept_sess(int fd)
{
	socklen_t l;
	struct sockaddr addr[2];	/* XXX: IPv6 hack */
	struct sess *sp;
	int i;

	VSL_stats->client_conn++;

	l = sizeof addr;
	i = accept(fd, addr, &l);
	if (i < 0) {
		VSL(SLT_Debug, fd, "Accept failed errno=%d", errno);
		/* XXX: stats ? */
		return (NULL);
	}
	sp = SES_New(addr, l);
	XXXAN(sp);

	sp->fd = i;
	sp->id = i;
	(void)clock_gettime(CLOCK_REALTIME, &sp->t_open);

	return (sp);
}

void
VCA_Prep(struct sess *sp)
{
	struct linger linger;

	TCP_name(sp->sockaddr, sp->sockaddrlen,
	    sp->addr, sizeof sp->addr, sp->port, sizeof sp->port);
	VSL(SLT_SessionOpen, sp->fd, "%s %s", sp->addr, sp->port);
	sp->acct.first = sp->t_open.tv_sec;
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
#ifdef SO_RCVTIMEO
	{
	struct timeval tv;

	tv.tv_sec = params->sess_timeout;
	tv.tv_usec = 0;
	AZ(setsockopt(sp->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv));
	}
#endif
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
	WRK_QueueSession(sp);
}

/*--------------------------------------------------------------------*/

int
vca_pollsession(struct sess *sp)
{
	int i;

	i = http_RecvSome(sp->fd, sp->http);
	if (i < 1)
		return (i);
	if (i == 1)
		vca_close_session(sp, "overflow");
	else if (i == 2)
		vca_close_session(sp, "no request");
	return (1);
}

/*--------------------------------------------------------------------*/

void
vca_close_session(struct sess *sp, const char *why)
{
	int i;

	VSL(SLT_SessionClose, sp->fd, why);
	if (sp->fd >= 0) {
		i = close(sp->fd);
		assert(i == 0 || errno != EBADF);	/* XXX EINVAL seen */
	}
	sp->fd = -1;
}

void
vca_return_session(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->obj);
	AZ(sp->vcl);
	vca_act->recycle(sp);
}

/*--------------------------------------------------------------------*/

static void *
vca_acct(void *arg)
{
	struct sess *sp;

	(void)arg;
	while (1) {
		sp = vca_accept_sess(heritage.socket);
		if (sp == NULL)
			continue;
		http_RecvPrep(sp->http);
		sp->step = STP_FIRST;
		VSL_stats->client_req++;
		WRK_QueueSession(sp);
	}
}


/*--------------------------------------------------------------------*/

void
VCA_Init(void)
{


	/* XXX: Add selector mechanism at some point */
	vca_act = vca_acceptors[0];

	if (vca_act->name == NULL) {
		fprintf(stderr, "No acceptor in program\n");
		exit (2);
	}
	vca_act->init();
	AZ(pthread_create(&vca_thread_acct, NULL, vca_acct, NULL));
}
