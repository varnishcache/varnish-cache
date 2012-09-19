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
 * This source file has the various trickery surrounding the accept/listen
 * sockets.
 *
 * The actual acceptance is done from cache_pool.c, by calling
 * into VCA_Accept() in this file.
 *
 * Once the session is allocated we move into it with a call to
 * VCA_SetupSess().
 *
 * If we fail to allocate a session we call VCA_FailSess() to clean up
 * and initiate pacing.
 */

#include "config.h"

#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "cache.h"
#include "common/heritage.h"

#include "vcli.h"
#include "vcli_priv.h"
#include "vtcp.h"
#include "vtim.h"

static pthread_t	VCA_thread;
static struct timeval	tv_sndtimeo;
static struct timeval	tv_rcvtimeo;
static int hack_ready;
static double vca_pace = 0.0;
static struct lock pace_mtx;

/*--------------------------------------------------------------------
 * We want to get out of any kind of trouble-hit TCP connections as fast
 * as absolutely possible, so we set them LINGER enabled with zero timeout,
 * so that even if there are outstanding write data on the socket, a close(2)
 * will return immediately.
 */
static const struct linger linger = {
	.l_onoff	=	0,
};

static unsigned char	need_sndtimeo, need_rcvtimeo, need_linger, need_test,
			need_tcpnodelay;

/*--------------------------------------------------------------------
 * Some kernels have bugs/limitations with respect to which options are
 * inherited from the accept/listen socket, so we have to keep track of
 * which, if any, sockopts we have to set on the accepted socket.
 */

static void
sock_test(int fd)
{
	struct linger lin;
	struct timeval tv;
	socklen_t l;
	int i, tcp_nodelay;

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

	l = sizeof tcp_nodelay;
	i = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, &l);
	if (i) {
		VTCP_Assert(i);
		return;
	}
	assert(l == sizeof tcp_nodelay);
	if (!tcp_nodelay)
		need_tcpnodelay = 1;

	need_test = 0;
}

/*--------------------------------------------------------------------
 * If accept(2)'ing fails, we pace ourselves to relive any resource
 * shortage if possible.
 */

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
		VTIM_sleep(p);
}

static void
vca_pace_bad(void)
{

	Lck_Lock(&pace_mtx);
	vca_pace += cache_param->acceptor_sleep_incr;
	if (vca_pace > cache_param->acceptor_sleep_max)
		vca_pace = cache_param->acceptor_sleep_max;
	Lck_Unlock(&pace_mtx);
}

static void
vca_pace_good(void)
{

	if (vca_pace == 0.0)
		return;
	Lck_Lock(&pace_mtx);
	vca_pace *= cache_param->acceptor_sleep_decay;
	if (vca_pace < cache_param->acceptor_sleep_incr)
		vca_pace = 0.0;
	Lck_Unlock(&pace_mtx);
}

/*--------------------------------------------------------------------
 * Accept on a listen socket, and handle error returns.
 *
 * Called from a worker thread from a pool
 */

int
VCA_Accept(struct listen_sock *ls, struct wrk_accept *wa)
{
	int i;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	vca_pace_check();

	while(!hack_ready)
		(void)usleep(100*1000);

	wa->acceptaddrlen = sizeof wa->acceptaddr;
	do {
		i = accept(ls->sock, (void*)&wa->acceptaddr,
			   &wa->acceptaddrlen);
	} while (i < 0 && errno == EAGAIN);

	if (i < 0) {
		switch (errno) {
		case ECONNABORTED:
			break;
		case EMFILE:
			VSL(SLT_Debug, ls->sock, "Too many open files");
			vca_pace_bad();
			break;
		default:
			VSL(SLT_Debug, ls->sock, "Accept failed: %s",
			    strerror(errno));
			vca_pace_bad();
			break;
		}
	}
	wa->acceptlsock = ls;
	wa->acceptsock = i;
	return (i);
}

/*--------------------------------------------------------------------
 * Fail a session
 *
 * This happens if we accept the socket, but cannot get a session
 * structure.
 *
 * We consider this a DoS situation (false positive:  Extremely popular
 * busy objects) and silently close the connection with minimum effort
 * and fuzz, rather than try to send an intelligent message back.
 */

void
VCA_FailSess(struct worker *wrk)
{
	struct wrk_accept *wa;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(wa, (void*)wrk->aws->f, WRK_ACCEPT_MAGIC);
	AZ(close(wa->acceptsock));
	wrk->stats.sess_drop++;
	vca_pace_bad();
	WS_Release(wrk->aws, 0);
}

/*--------------------------------------------------------------------
 * We have allocated a session, move our info into it.
 */

const char *
VCA_SetupSess(struct worker *wrk, struct sess *sp)
{
	struct wrk_accept *wa;
	const char *retval;
	int tcp_nodelay = 1;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CAST_OBJ_NOTNULL(wa, (void*)wrk->aws->f, WRK_ACCEPT_MAGIC);
	sp->fd = wa->acceptsock;
	wa->acceptsock = -1;
	retval = wa->acceptlsock->name;
	assert(wa->acceptaddrlen <= sp->sockaddrlen);
	memcpy(&sp->sockaddr, &wa->acceptaddr, wa->acceptaddrlen);
	sp->sockaddrlen = wa->acceptaddrlen;
	vca_pace_good();
	wrk->stats.sess_conn++;
	WS_Release(wrk->aws, 0);

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
	if (need_tcpnodelay)
		VTCP_Assert(setsockopt(sp->fd, IPPROTO_TCP, TCP_NODELAY,
		    &tcp_nodelay, sizeof tcp_nodelay));
	return (retval);
}

/*--------------------------------------------------------------------*/

static void *
vca_acct(void *arg)
{
#ifdef SO_RCVTIMEO_WORKS
	double timeout_idle = 0;
#endif
#ifdef SO_SNDTIMEO_WORKS
	double send_timeout = 0;
#endif
	int tcp_nodelay = 1;
	struct listen_sock *ls;
	double t0, now;
	int i;

	THR_SetName("cache-acceptor");
	(void)arg;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock < 0)
			continue;
		AZ(listen(ls->sock, cache_param->listen_depth));
		AZ(setsockopt(ls->sock, SOL_SOCKET, SO_LINGER,
		    &linger, sizeof linger));
		AZ(setsockopt(ls->sock, IPPROTO_TCP, TCP_NODELAY,
		    &tcp_nodelay, sizeof tcp_nodelay));
		if (cache_param->accept_filter) {
			i = VTCP_filter_http(ls->sock);
			if (i)
				VSL(SLT_Error, ls->sock,
				    "Kernel filtering: sock=%d, ret=%d %s\n",
				    ls->sock, i, strerror(errno));
		}
	}

	hack_ready = 1;

	need_test = 1;
	t0 = VTIM_real();
	while (1) {
		(void)sleep(1);
#ifdef SO_SNDTIMEO_WORKS
		if (cache_param->idle_send_timeout != send_timeout) {
			need_test = 1;
			send_timeout = cache_param->idle_send_timeout;
			tv_sndtimeo = VTIM_timeval(send_timeout);
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
		if (cache_param->timeout_idle != timeout_idle) {
			need_test = 1;
			timeout_idle = cache_param->timeout_idle;
			tv_rcvtimeo = VTIM_timeval(timeout_idle);
			VTAILQ_FOREACH(ls, &heritage.socks, list) {
				if (ls->sock < 0)
					continue;
				AZ(setsockopt(ls->sock, SOL_SOCKET,
				    SO_RCVTIMEO,
				    &tv_rcvtimeo, sizeof tv_rcvtimeo));
			}
		}
#endif
		now = VTIM_real();
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

	/*
	 * This CLI command is primarily used by varnishtest.  Don't
	 * respond until listen(2) has been called, in order to avoid
	 * a race where varnishtest::client would attempt to connect(2)
	 * before listen(2) has been called.
	 */
	while(!hack_ready)
		(void)usleep(100*1000);

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
