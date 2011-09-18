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

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "vcli.h"
#include "cli_priv.h"
#include "cache.h"

static pthread_t	VCA_thread;
static struct timeval	tv_sndtimeo;
static struct timeval	tv_rcvtimeo;

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

	VTCP_name(&sp->sockaddr, sp->sockaddrlen,
	    addr, sizeof addr, port, sizeof port);
	sp->addr = WS_Dup(sp->ws, addr);
	sp->port = WS_Dup(sp->ws, port);
	if (params->log_local_addr) {
		AZ(getsockname(sp->fd, (void*)&sp->mysockaddr, &sp->mysockaddrlen));
		VTCP_name(&sp->mysockaddr, sp->mysockaddrlen,
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

/*--------------------------------------------------------------------
 * If accept(2)'ing fails, we pace ourselves to relive any resource
 * shortage if possible.
 */

static double vca_pace = 0.0;
static struct lock pace_mtx;

static void
vca_pace_check(void)
{
	double p;

	if (vca_pace == 0.0) 
		return;
	Lck_Lock(&pace_mtx);
	p = vca_pace;
	Lck_Unlock(&pace_mtx);
	if (p > 0.0)
		TIM_sleep(p);
}

static void
vca_pace_bad(void)
{

	Lck_Lock(&pace_mtx);
	vca_pace += params->acceptor_sleep_incr;
	if (vca_pace > params->acceptor_sleep_max)
		vca_pace = params->acceptor_sleep_max;
	Lck_Unlock(&pace_mtx);
}

static void
vca_pace_good(void)
{

	if (vca_pace == 0.0) 
		return;
	Lck_Lock(&pace_mtx);
	vca_pace *= params->acceptor_sleep_decay;
	if (vca_pace < params->acceptor_sleep_incr)
		vca_pace = 0.0;
	Lck_Unlock(&pace_mtx);
}

/*--------------------------------------------------------------------
 * Accept on a listen socket, and handle error returns.
 */

static int hack_ready;

int
VCA_Accept(int sock, socklen_t *slp, struct sockaddr_storage *sap)
{
	int i;

	assert(sock >= 0);
	vca_pace_check();

	while(!hack_ready)
		(void)usleep(100*1000);

	*slp = sizeof *sap;
	i = accept(sock, (void*)sap, slp);

	if (i < 0) {
		VSC_C_main->accept_fail++;
		switch (errno) {
		case EAGAIN:
		case ECONNABORTED:
			break;
		case EMFILE:
			VSL(SLT_Debug, sock, "Too many open files");
			vca_pace_bad();
			break;
		default:
			VSL(SLT_Debug, sock, "Accept failed: %s",
			    strerror(errno));
			vca_pace_bad();
			break;
		}
	}
	return (i);
}

/*--------------------------------------------------------------------*/

void
VCA_SetupSess(struct worker *w)
{
	struct sess *sp;

	sp = w->sp;
	if (sp == NULL) {
		AZ(close(w->acceptsock));
		w->acceptsock = -1;
		VSC_C_main->client_drop++;
		/* XXX: 50x Reply ? */
		vca_pace_bad();
		INCOMPL();
	}
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sp->fd = w->acceptsock;
	sp->id = w->acceptsock;
	w->acceptsock = -1;
	sp->t_open = TIM_real();
	sp->t_end = sp->t_end;
	sp->mylsock = w->acceptlsock;
	assert(w->acceptaddrlen <= sp->sockaddrlen);
	memcpy(&sp->sockaddr, &w->acceptaddr, w->acceptaddrlen);
	sp->sockaddrlen = w->acceptaddrlen;
	sp->step = STP_FIRST;
	vca_pace_good();
	w->sp = sp;
	w->stats.client_conn++;
}

/*--------------------------------------------------------------------*/

static void *
vca_acct(void *arg)
{
#ifdef SO_RCVTIMEO_WORKS
	double sess_timeout = 0;
#endif
#ifdef SO_SNDTIMEO_WORKS
	double send_timeout = 0;
#endif
	struct listen_sock *ls;
	double t0, now;

	THR_SetName("cache-acceptor");
	(void)arg;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		AZ(listen(ls->sock, params->listen_depth));
		AZ(setsockopt(ls->sock, SOL_SOCKET, SO_LINGER,
		    &linger, sizeof linger));
	}

	hack_ready = 1;

	need_test = 1;
	t0 = TIM_real();
	while (1) {
		(void)sleep(1);
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
		now = TIM_real();
		VSC_C_main->uptime = (uint64_t)(now - t0);
	}
	NEEDLESS_RETURN(NULL);
}


/*--------------------------------------------------------------------*/

static void
ccf_start(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;

	AZ(pthread_create(&VCA_thread, NULL, vca_acct, NULL));
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
	Lck_New(&pace_mtx, lck_vcapace);
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
