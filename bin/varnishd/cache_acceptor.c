/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "vcli.h"
#include "cli_priv.h"
#include "cache.h"
#include "cache_waiter.h"

static struct waiter * const vca_waiters[] = {
#if defined(HAVE_KQUEUE)
	&waiter_kqueue,
#endif
#if defined(HAVE_EPOLL_CTL)
	&waiter_epoll,
#endif
#if defined(HAVE_PORT_CREATE)
	&waiter_ports,
#endif
	&waiter_poll,
	NULL,
};

static struct waiter const *vca_act;

pthread_t		VCA_thread;
static struct timeval	tv_sndtimeo;
static struct timeval	tv_rcvtimeo;

/*--------------------------------------------------------------------
 * Report waiter name to panics
 */

const char *
VCA_waiter_name(void)
{

	if (vca_act != NULL)
		return (vca_act->name);
	else
		return ("no_waiter");
}


/*--------------------------------------------------------------------
 * We want to get out of any kind of trouble-hit TCP connections as fast
 * as absolutely possible, so we set them LINGER enabled with zero timeout,
 * so that even if there are outstanding write data on the socket, a close(2)
 * will return immediately.
 */
static const struct linger linger = {
	.l_onoff	=	0,
};

static unsigned char	need_sndtimeo, need_rcvtimeo, need_linger, need_test;

int vca_pipes[2] = { -1, -1 };

static void
sock_test(int fd)
{
	struct linger lin;
	struct timeval tv;
	socklen_t l;
	int i;

	l = sizeof lin;
	i = getsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, &l);
	if (i) {
		VTCP_Assert(i);
		return;
	}
	assert(l == sizeof lin);
	if (memcmp(&lin, &linger, l))
		need_linger = 1;

#ifdef SO_SNDTIMEO_WORKS
	l = sizeof tv;
	i = getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, &l);
	if (i) {
		VTCP_Assert(i);
		return;
	}
	assert(l == sizeof tv);
	if (memcmp(&tv, &tv_sndtimeo, l))
		need_sndtimeo = 1;
#else
	(void)tv;
	(void)tv_sndtimeo;
	(void)need_sndtimeo;
#endif

#ifdef SO_RCVTIMEO_WORKS
	l = sizeof tv;
	i = getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, &l);
	if (i) {
		VTCP_Assert(i);
		return;
	}
	assert(l == sizeof tv);
	if (memcmp(&tv, &tv_rcvtimeo, l))
		need_rcvtimeo = 1;
#else
	(void)tv;
	(void)tv_rcvtimeo;
	(void)need_rcvtimeo;
#endif

	need_test = 0;
}

/*--------------------------------------------------------------------
 * Called once the workerthread gets hold of the session, to do setup
 * setup overhead, we don't want to bother the acceptor thread with.
 */

void
VCA_Prep(struct sess *sp)
{
	char addr[VTCP_ADDRBUFSIZE];
	char port[VTCP_PORTBUFSIZE];

	VTCP_name(sp->sockaddr, sp->sockaddrlen,
	    addr, sizeof addr, port, sizeof port);
	sp->addr = WS_Dup(sp->ws, addr);
	sp->port = WS_Dup(sp->ws, port);
	if (params->log_local_addr) {
		AZ(getsockname(sp->fd, (void*)sp->mysockaddr, &sp->mysockaddrlen));
		VTCP_name(sp->mysockaddr, sp->mysockaddrlen,
		    addr, sizeof addr, port, sizeof port);
		VSL(SLT_SessionOpen, sp->fd, "%s %s %s %s",
		    sp->addr, sp->port, addr, port);
	} else {
		VSL(SLT_SessionOpen, sp->fd, "%s %s %s",
		    sp->addr, sp->port, sp->mylsock->name);
	}
	sp->acct_ses.first = sp->t_open;
	if (need_test)
		sock_test(sp->fd);
	if (need_linger)
		VTCP_Assert(setsockopt(sp->fd, SOL_SOCKET, SO_LINGER,
		    &linger, sizeof linger));
#ifdef SO_SNDTIMEO_WORKS
	if (need_sndtimeo)
		VTCP_Assert(setsockopt(sp->fd, SOL_SOCKET, SO_SNDTIMEO,
		    &tv_sndtimeo, sizeof tv_sndtimeo));
#endif
#ifdef SO_RCVTIMEO_WORKS
	if (need_rcvtimeo)
		VTCP_Assert(setsockopt(sp->fd, SOL_SOCKET, SO_RCVTIMEO,
		    &tv_rcvtimeo, sizeof tv_rcvtimeo));
#endif
}

/*--------------------------------------------------------------------*/

static void *
vca_acct(void *arg)
{
	struct sess *sp;
	socklen_t l;
	struct sockaddr_storage addr_s;
	struct sockaddr *addr;
#ifdef SO_RCVTIMEO_WORKS
	double sess_timeout = 0;
#endif
#ifdef SO_SNDTIMEO_WORKS
	double send_timeout = 0;
#endif
	int i;
	struct pollfd *pfd;
	struct listen_sock *ls;
	unsigned u;
	double t0, now, pace;

	THR_SetName("cache-acceptor");
	(void)arg;

	/* Set up the poll argument */
	pfd = calloc(sizeof *pfd, heritage.nsocks);
	AN(pfd);
	i = 0;
	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		AZ(listen(ls->sock, params->listen_depth));
		AZ(setsockopt(ls->sock, SOL_SOCKET, SO_LINGER,
		    &linger, sizeof linger));
		pfd[i].events = POLLIN;
		pfd[i++].fd = ls->sock;
	}

	need_test = 1;
	pace = 0;
	t0 = TIM_real();
	while (1) {
#ifdef SO_SNDTIMEO_WORKS
		if (params->send_timeout != send_timeout) {
			need_test = 1;
			send_timeout = params->send_timeout;
			tv_sndtimeo = TIM_timeval(send_timeout);
			VTAILQ_FOREACH(ls, &heritage.socks, list) {
				if (ls->sock < 0)
					continue;
				AZ(setsockopt(ls->sock, SOL_SOCKET,
				    SO_SNDTIMEO,
				    &tv_sndtimeo, sizeof tv_sndtimeo));
			}
		}
#endif
#ifdef SO_RCVTIMEO_WORKS
		if (params->sess_timeout != sess_timeout) {
			need_test = 1;
			sess_timeout = params->sess_timeout;
			tv_rcvtimeo = TIM_timeval(sess_timeout);
			VTAILQ_FOREACH(ls, &heritage.socks, list) {
				if (ls->sock < 0)
					continue;
				AZ(setsockopt(ls->sock, SOL_SOCKET,
				    SO_RCVTIMEO,
				    &tv_rcvtimeo, sizeof tv_rcvtimeo));
			}
		}
#endif
		/* Bound the pacing delay by parameter */
		if (pace > params->acceptor_sleep_max)
			pace = params->acceptor_sleep_max;
		if (pace < params->acceptor_sleep_incr)
			pace = 0.0;
		if (pace > 0.0)
			TIM_sleep(pace);
		i = poll(pfd, heritage.nsocks, 1000);
		now = TIM_real();
		VSC_C_main->uptime = (uint64_t)(now - t0);
		u = 0;
		VTAILQ_FOREACH(ls, &heritage.socks, list) {
			if (ls->sock < 0)
				continue;
			if (pfd[u++].revents == 0)
				continue;
			VSC_C_main->client_conn++;
			l = sizeof addr_s;
			addr = (void*)&addr_s;
			i = accept(ls->sock, addr, &l);
			if (i < 0) {
				VSC_C_main->accept_fail++;
				switch (errno) {
				case EAGAIN:
				case ECONNABORTED:
					break;
				case EMFILE:
					VSL(SLT_Debug, ls->sock,
					    "Too many open files "
					    "when accept(2)ing. Sleeping.");
					pace += params->acceptor_sleep_incr;
					break;
				default:
					VSL(SLT_Debug, ls->sock,
					    "Accept failed: %s",
					    strerror(errno));
					pace += params->acceptor_sleep_incr;
					break;
				}
				continue;
			}
			sp = SES_New();
			if (sp == NULL) {
				AZ(close(i));
				VSC_C_main->client_drop++;
				pace += params->acceptor_sleep_incr;
				continue;
			}
			sp->fd = i;
			sp->id = i;
			sp->t_open = now;
			sp->t_end = now;
			sp->mylsock = ls;
			assert(l < sp->sockaddrlen);
			memcpy(sp->sockaddr, addr, l);
			sp->sockaddrlen = l;

			sp->step = STP_FIRST;
			if (WRK_QueueSession(sp)) {
				VSC_C_main->client_drop++;
				pace += params->acceptor_sleep_incr;
			} else {
				pace *= params->acceptor_sleep_decay;
			}
		}
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

void
vca_handover(struct sess *sp, int status)
{

	switch (status) {
	case -2:
		vca_close_session(sp, "blast");
		SES_Delete(sp);
		break;
	case -1:
		vca_close_session(sp, "no request");
		SES_Delete(sp);
		break;
	case 1:
		sp->step = STP_START;
		if (WRK_QueueSession(sp))
			VSC_C_main->client_drop_late++;
		break;
	default:
		INCOMPL();
	}
}

/*--------------------------------------------------------------------*/

void
vca_close_session(struct sess *sp, const char *why)
{
	int i;

	VSL(SLT_SessionClose, sp->id, "%s", why);
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
	assert(sp->fd >= 0);
	/*
	 * Set nonblocking in the worker-thread, before passing to the
	 * acceptor thread, to reduce syscall density of the latter.
	 */
	if (VTCP_nonblocking(sp->fd))
		vca_close_session(sp, "remote closed");
	else if (vca_act->pass == NULL)
		assert(sizeof sp == write(vca_pipes[1], &sp, sizeof sp));
	else
		vca_act->pass(sp);
}

/*--------------------------------------------------------------------*/

static void
ccf_start(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;

	if (vca_act == NULL)
		vca_act = vca_waiters[0];

	AN(vca_act);
	AN(vca_act->name);

	if (vca_act->pass == NULL)
		AZ(pipe(vca_pipes));
	vca_act->init();
	AZ(pthread_create(&VCA_thread, NULL, vca_acct, NULL));
	VSL(SLT_Debug, 0, "Acceptor is %s", vca_act->name);
}

/*--------------------------------------------------------------------*/

static void
ccf_listen_address(struct cli *cli, const char * const *av, void *priv)
{
	struct listen_sock *ls;
	char h[32], p[32];

	(void)cli;
	(void)av;
	(void)priv;
	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		VTCP_myname(ls->sock, h, sizeof h, p, sizeof p);
		VCLI_Out(cli, "%s %s\n", h, p);
	}
}

/*--------------------------------------------------------------------*/

static struct cli_proto vca_cmds[] = {
	{ CLI_SERVER_START,	"i", ccf_start },
	{ "debug.listen_address",
	    "debug.listen_address",
	    "Report the actual listen address\n", 0, 0,
	    "d", ccf_listen_address, NULL },
	{ NULL }
};

void
VCA_Init(void)
{

	CLI_AddFuncs(vca_cmds);
}

void
VCA_Shutdown(void)
{
	struct listen_sock *ls;
	int i;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		i = ls->sock;
		ls->sock = -1;
		(void)close(i);
	}
}

void
VCA_tweak_waiter(struct cli *cli, const char *arg)
{
	int i;

	 ASSERT_MGT();

	if (arg == NULL) {
		if (vca_act == NULL)
			VCLI_Out(cli, "default");
		else
			VCLI_Out(cli, "%s", vca_act->name);

		VCLI_Out(cli, " (");
		for (i = 0; vca_waiters[i] != NULL; i++)
			VCLI_Out(cli, "%s%s", i == 0 ? "" : ", ",
			    vca_waiters[i]->name);
		VCLI_Out(cli, ")");
		return;
	}
	if (!strcmp(arg, "default")) {
		vca_act = NULL;
		return;
	}
	for (i = 0; vca_waiters[i]; i++) {
		if (!strcmp(arg, vca_waiters[i]->name)) {
			vca_act = vca_waiters[i];
			return;
		}
	}
	VCLI_Out(cli, "Unknown waiter");
	VCLI_SetResult(cli, CLIS_PARAM);
}
