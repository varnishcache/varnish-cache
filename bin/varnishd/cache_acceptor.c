/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
static struct timeval	tv_sndtimeo;
static struct timeval	tv_rcvtimeo;
static struct linger	linger;

static unsigned char	need_sndtimeo, need_rcvtimeo, need_linger, need_test;

static void
sock_test(int fd)
{
	struct linger lin;
	struct timeval tv;
	socklen_t l;

	l = sizeof lin;
	AZ(getsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, &l));
	assert(l == sizeof lin);
	if (memcmp(&lin, &linger, l))
		need_linger = 1;

	l = sizeof tv;
	AZ(getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, &l));
	assert(l == sizeof tv);
	if (memcmp(&tv, &tv_sndtimeo, l))
		need_sndtimeo = 1;

	l = sizeof tv;
	AZ(getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, &l));
	assert(l == sizeof tv);
	if (memcmp(&tv, &tv_rcvtimeo, l))
		need_rcvtimeo = 1;
	need_test = 0;
	printf("socktest: linger=%d sndtimeo=%d rcvtimeo=%d\n",
	    need_linger, need_sndtimeo, need_rcvtimeo);
}

void
VCA_Prep(struct sess *sp)
{

	TCP_name(sp->sockaddr, sp->sockaddrlen,
	    sp->addr, sizeof sp->addr, sp->port, sizeof sp->port);
	VSL(SLT_SessionOpen, sp->fd, "%s %s", sp->addr, sp->port);
	sp->acct.first = sp->t_open.tv_sec;
	if (need_test)
		sock_test(sp->fd);
	if (need_linger)
		AZ(setsockopt(sp->fd, SOL_SOCKET, SO_LINGER,
		    &linger, sizeof linger));
	if (need_sndtimeo)
		AZ(setsockopt(sp->fd, SOL_SOCKET, SO_SNDTIMEO,
		    &tv_sndtimeo, sizeof tv_sndtimeo));
	if (need_rcvtimeo)
		AZ(setsockopt(sp->fd, SOL_SOCKET, SO_RCVTIMEO,
		    &tv_rcvtimeo, sizeof tv_rcvtimeo));
}

/*--------------------------------------------------------------------*/

static void *
vca_acct(void *arg)
{
	struct sess *sp;
	socklen_t l;
	struct sockaddr addr[2];	/* XXX: IPv6 hack */
	int i;

	(void)arg;
	need_test = 1;
	AZ(setsockopt(heritage.socket, SOL_SOCKET, SO_LINGER,
	    &linger, sizeof linger));
	while (1) {
		if (params->send_timeout != tv_sndtimeo.tv_sec) {
			need_test = 1;
			tv_sndtimeo.tv_sec = params->send_timeout;
			AZ(setsockopt(heritage.socket, SOL_SOCKET,
			    SO_SNDTIMEO, &tv_sndtimeo, sizeof tv_sndtimeo));
		}
		if (params->sess_timeout != tv_rcvtimeo.tv_sec) {
			need_test = 1;
			tv_rcvtimeo.tv_sec = params->sess_timeout;
			AZ(setsockopt(heritage.socket, SOL_SOCKET,
			    SO_RCVTIMEO, &tv_rcvtimeo, sizeof tv_rcvtimeo));
		}
		VSL_stats->client_conn++;

		l = sizeof addr;
		i = accept(heritage.socket, addr, &l);
		if (i < 0) {
			if (errno != EAGAIN) {
				VSL(SLT_Debug, heritage.socket,
				    "Accept failed errno=%d", errno);
				/* XXX: stats ? */
			}
			continue;
		}
		sp = SES_New(addr, l);
		XXXAN(sp);

		sp->fd = i;
		sp->id = i;
		(void)clock_gettime(CLOCK_REALTIME, &sp->t_open);

		http_RecvPrep(sp->http);
		sp->step = STP_FIRST;
		WRK_QueueSession(sp);
	}
}

/*--------------------------------------------------------------------*/

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
