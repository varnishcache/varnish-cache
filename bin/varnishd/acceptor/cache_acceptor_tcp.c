/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "cache/cache_varnishd.h"

#include "acceptor/cache_acceptor.h"
#include "acceptor/acceptor_priv.h"
#include "acceptor/acceptor_tcp.h"

#include "cache/cache_transport.h"
#include "cache/cache_pool.h"
#include "common/heritage.h"

#include "vcli_serve.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

extern vtim_dur acc_pace;
extern struct lock pace_mtx;
extern unsigned pool_accepting;

/*--------------------------------------------------------------------
 * TCP options we want to control
 */

union sock_arg {
	struct linger	lg;
	struct timeval	tv;
	int		i;
};

static struct sock_opt {
	int		level;
	int		optname;
	const char	*strname;
	unsigned	mod;
	socklen_t	sz;
	union sock_arg	arg[1];
} sock_opts[] = {
	/* Note: Setting the mod counter to something not-zero is needed
	 * to force the setsockopt() calls on startup */
#define SOCK_OPT(lvl, nam, typ) { lvl, nam, #nam, 1, sizeof(typ) },

	SOCK_OPT(SOL_SOCKET, SO_LINGER, struct linger)
	SOCK_OPT(SOL_SOCKET, SO_KEEPALIVE, int)
	SOCK_OPT(SOL_SOCKET, SO_SNDTIMEO, struct timeval)
	SOCK_OPT(SOL_SOCKET, SO_RCVTIMEO, struct timeval)

	SOCK_OPT(IPPROTO_TCP, TCP_NODELAY, int)

#if defined(HAVE_TCP_KEEP)
	SOCK_OPT(IPPROTO_TCP, TCP_KEEPIDLE, int)
	SOCK_OPT(IPPROTO_TCP, TCP_KEEPCNT, int)
	SOCK_OPT(IPPROTO_TCP, TCP_KEEPINTVL, int)
#elif defined(HAVE_TCP_KEEPALIVE)
	SOCK_OPT(IPPROTO_TCP, TCP_KEEPALIVE, int)
#endif

#undef SOCK_OPT
};

static const int n_sock_opts = sizeof sock_opts / sizeof sock_opts[0];

/*--------------------------------------------------------------------
 * We want to get out of any kind of trouble-hit TCP connections as fast
 * as absolutely possible, so we set them LINGER disabled, so that even if
 * there are outstanding write data on the socket, a close(2) will return
 * immediately.
 */
static const struct linger disable_so_linger = {
	.l_onoff	=	0,
};

/*
 * We turn on keepalives by default to assist in detecting clients that have
 * hung up on connections returning from waitinglists
 */
static const unsigned enable_so_keepalive = 1;

/* We disable Nagle's algorithm in favor of low latency setups.
 */
static const unsigned enable_tcp_nodelay = 1;

/*--------------------------------------------------------------------
 * Some kernels have bugs/limitations with respect to which options are
 * inherited from the accept/listen socket, so we have to keep track of
 * which, if any, sockopts we have to set on the accepted socket.
 */

static int
acc_tcp_sockopt_init(void)
{
	struct sock_opt *so;
	union sock_arg tmp;
	int n, chg = 0;
	size_t sz;

	memset(&tmp, 0, sizeof tmp);

	for (n = 0; n < n_sock_opts; n++) {
		so = &sock_opts[n];

#define SET_VAL(nm, so, fld, val)					\
	do {								\
		if (!strcmp(#nm, so->strname)) {			\
			assert(so->sz == sizeof so->arg->fld);		\
			so->arg->fld = (val);				\
		}							\
	} while (0)

#define NEW_VAL(nm, so, fld, val)					\
	do {								\
		if (!strcmp(#nm, so->strname)) {			\
			sz = sizeof tmp.fld;				\
			assert(so->sz == sz);				\
			tmp.fld = (val);				\
			if (memcmp(&so->arg->fld, &(tmp.fld), sz)) {	\
				memcpy(&so->arg->fld, &(tmp.fld), sz);	\
				so->mod++;				\
				chg = 1;				\
			}						\
		}							\
	} while (0)

		SET_VAL(SO_LINGER, so, lg, disable_so_linger);
		SET_VAL(SO_KEEPALIVE, so, i, enable_so_keepalive);
		NEW_VAL(SO_SNDTIMEO, so, tv,
		    VTIM_timeval(cache_param->idle_send_timeout));
		NEW_VAL(SO_RCVTIMEO, so, tv,
		    VTIM_timeval(cache_param->timeout_idle));
		SET_VAL(TCP_NODELAY, so, i, enable_tcp_nodelay);
#if defined(HAVE_TCP_KEEP)
		NEW_VAL(TCP_KEEPIDLE, so, i,
		    (int)cache_param->tcp_keepalive_time);
		NEW_VAL(TCP_KEEPCNT, so, i,
		    (int)cache_param->tcp_keepalive_probes);
		NEW_VAL(TCP_KEEPINTVL, so, i,
		    (int)cache_param->tcp_keepalive_intvl);
#elif defined(HAVE_TCP_KEEPALIVE)
		NEW_VAL(TCP_KEEPALIVE, so, i,
		    (int)cache_param->tcp_keepalive_time);
#endif
	}

	return (chg);
}

static void
acc_tcp_sockopt_test(const struct listen_sock *ls, const struct sess *sp)
{
	struct conn_heritage *ch;
	struct sock_opt *so;
	union sock_arg tmp;
	socklen_t l;
	int i, n;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	for (n = 0; n < n_sock_opts; n++) {
		so = &sock_opts[n];
		ch = &ls->conn_heritage[n];

		if (ch->sess_set) {
			VSL(SLT_Debug, sp->vxid,
			    "sockopt: Not testing nonhereditary %s for %s=%s",
			    so->strname, ls->name, ls->endpoint);
			continue;
		}

		memset(&tmp, 0, sizeof tmp);
		l = so->sz;
		i = getsockopt(sp->fd, so->level, so->optname, &tmp, &l);

		if (i == 0 && memcmp(&tmp, so->arg, so->sz)) {
			VSL(SLT_Debug, sp->vxid,
			    "sockopt: Test confirmed %s non heredity for %s=%s",
			    so->strname, ls->name, ls->endpoint);
			ch->sess_set = 1;
		}

		if (i && errno != ENOPROTOOPT)
			VTCP_Assert(i);
	}
}

static void
acc_tcp_sockopt_set(const struct listen_sock *ls, const struct sess *sp)
{
	struct conn_heritage *ch;
	struct sock_opt *so;
	vxid_t vxid;
	int n, sock;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

	if (sp != NULL) {
		CHECK_OBJ(sp, SESS_MAGIC);
		sock = sp->fd;
		vxid = sp->vxid;
	} else {
		sock = ls->sock;
		vxid = NO_VXID;
	}

	for (n = 0; n < n_sock_opts; n++) {
		so = &sock_opts[n];
		ch = &ls->conn_heritage[n];

		if (sp == NULL && ch->listen_mod == so->mod) {
			VSL(SLT_Debug, vxid,
			    "sockopt: Not setting unmodified %s for %s=%s",
			    so->strname, ls->name, ls->endpoint);
			continue;
		}

		if  (sp != NULL && !ch->sess_set) {
			VSL(SLT_Debug, sp->vxid,
			    "sockopt: %s may be inherited for %s=%s",
			    so->strname, ls->name, ls->endpoint);
			continue;
		}

		VSL(SLT_Debug, vxid,
		    "sockopt: Setting %s for %s=%s",
		    so->strname, ls->name, ls->endpoint);
		VTCP_Assert(setsockopt(sock,
		    so->level, so->optname, so->arg, so->sz));

		if (sp == NULL)
			ch->listen_mod = so->mod;
	}
}

static void
acc_tcp_init(void)
{

}

static int
acc_tcp_listen(struct cli *cli, struct listen_sock *ls)
{

	CHECK_OBJ_NOTNULL(ls->transport, TRANSPORT_MAGIC);
	assert (ls->sock > 0);	// We know where stdin is

	if (cache_param->tcp_fastopen &&
	    VTCP_fastopen(ls->sock, cache_param->listen_depth))
		VSL(SLT_Error, NO_VXID,
		    "Kernel TCP Fast Open: sock=%d, errno=%d %s",
		    ls->sock, errno, VAS_errtxt(errno));

	if (listen(ls->sock, cache_param->listen_depth)) {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "Listen failed on socket '%s': %s",
		    ls->endpoint, VAS_errtxt(errno));
		return (-1);
	}

	AZ(ls->conn_heritage);
	ls->conn_heritage = calloc(n_sock_opts,
	    sizeof *ls->conn_heritage);
	AN(ls->conn_heritage);

	ls->test_heritage = 1;
	acc_tcp_sockopt_set(ls, NULL);

	if (cache_param->accept_filter && VTCP_filter_http(ls->sock))
		VSL(SLT_Error, NO_VXID,
		    "Kernel filtering: sock=%d, errno=%d %s",
		    ls->sock, errno, VAS_errtxt(errno));

	return (0);
}

static void
acc_tcp_start(struct cli *cli)
{
	struct listen_sock *ls;

	ASSERT_CLI();

	(void)acc_tcp_sockopt_init();

	VTAILQ_FOREACH(ls, &TCP_acceptor.socks, acclist) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

		if (acc_tcp_listen(cli, ls))
			return;
	}
}

static void
acc_tcp_event(struct cli *cli, struct listen_sock *ls, enum acc_event event)
{
	char h[VTCP_ADDRBUFSIZE], p[VTCP_PORTBUFSIZE];

	switch (event) {
	case ACC_EVENT_LADDR:
		VTCP_myname(ls->sock, h, sizeof h, p, sizeof p);
		VCLI_Out(cli, "%s %s %s\n", ls->name, h, p);
		break;
	default:
		WRONG("INVALID ACC_EVENT");
	}
}

/*--------------------------------------------------------------------
 * The pool-task for a newly accepted session
 *
 * Called from assigned worker thread
 */

static void
acc_mk_tcp(const struct wrk_accept *wa,
    struct sess *sp, char *laddr, char *lport, char *raddr, char *rport)
{
	struct suckaddr *sa = NULL;
	ssize_t sz;

	AN(SES_Reserve_remote_addr(sp, &sa, &sz));
	AN(sa);
	assert(sz == vsa_suckaddr_len);
	AN(VSA_Build(sa, &wa->acceptaddr, wa->acceptaddrlen));
	sp->sattr[SA_CLIENT_ADDR] = sp->sattr[SA_REMOTE_ADDR];

	VTCP_name(sa, raddr, VTCP_ADDRBUFSIZE, rport, VTCP_PORTBUFSIZE);
	AN(SES_Set_String_Attr(sp, SA_CLIENT_IP, raddr));
	AN(SES_Set_String_Attr(sp, SA_CLIENT_PORT, rport));


	AN(SES_Reserve_local_addr(sp, &sa, &sz));
	AN(VSA_getsockname(sp->fd, sa, sz));
	sp->sattr[SA_SERVER_ADDR] = sp->sattr[SA_LOCAL_ADDR];
	VTCP_name(sa, laddr, VTCP_ADDRBUFSIZE, lport, VTCP_PORTBUFSIZE);
}

static void v_matchproto_(task_func_t)
acc_tcp_make_session(struct worker *wrk, void *arg)
{
	char laddr[VTCP_ADDRBUFSIZE];
	char lport[VTCP_PORTBUFSIZE];
	char raddr[VTCP_ADDRBUFSIZE];
	char rport[VTCP_PORTBUFSIZE];
	struct wrk_accept *wa;
	struct sess *sp;
	struct req *req;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(wa, arg, WRK_ACCEPT_MAGIC);

	VTCP_blocking(wa->acceptsock);

	/* Turn accepted socket into a session */
	AN(WS_Reservation(wrk->aws));
	sp = SES_New(wrk->pool);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	wrk->stats->s_sess++;

	sp->t_open = VTIM_real();
	sp->t_idle = sp->t_open;
	sp->vxid = VXID_Get(wrk, VSL_CLIENTMARKER);

	sp->fd = wa->acceptsock;
	wa->acceptsock = -1;
	sp->listen_sock = wa->acceptlsock;

	assert((size_t)wa->acceptaddrlen <= vsa_suckaddr_len);

	acc_mk_tcp(wa, sp, laddr, lport, raddr, rport);

	AN(wa->acceptlsock->name);
	VSL(SLT_Begin, sp->vxid, "sess 0 %s",
	    wa->acceptlsock->transport->name);
	VSL(SLT_SessOpen, sp->vxid, "%s %s %s %s %s %.6f %d",
	    raddr, rport, wa->acceptlsock->name, laddr, lport,
	    sp->t_open, sp->fd);

	acc_pace_good();
	wrk->stats->sess_conn++;

	if (wa->acceptlsock->test_heritage) {
		acc_tcp_sockopt_test(wa->acceptlsock, sp);
		wa->acceptlsock->test_heritage = 0;
	}

	acc_tcp_sockopt_set(wa->acceptlsock, sp);

	req = Req_New(sp);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	req->htc->rfd = &sp->fd;

	SES_SetTransport(wrk, sp, req, wa->acceptlsock->transport);
	WS_Release(wrk->aws, 0);
}

/*--------------------------------------------------------------------
 * This function accepts on a single socket for a single thread pool.
 *
 * As long as we can stick the accepted connection to another thread
 * we do so, otherwise we put the socket back on the "BACK" pool
 * and handle the new connection ourselves.
 */

static void v_matchproto_(task_func_t)
acc_tcp_accept_task(struct worker *wrk, void *arg)
{
	char laddr[VTCP_ADDRBUFSIZE];
	char lport[VTCP_PORTBUFSIZE];
	struct listen_sock *ls;
	struct wrk_accept wa;
	struct poolsock *ps;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(ps, arg, POOLSOCK_MAGIC);
	ls = ps->lsock;
	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	CHECK_OBJ_NOTNULL(ps->pool, POOL_MAGIC);

	while (!pool_accepting)
		VTIM_sleep(.1);

	/* Dont hold on to (possibly) discarded VCLs */
	if (wrk->wpriv->vcl != NULL)
		VCL_Rel(&wrk->wpriv->vcl);

	while (!ps->pool->die) {
		INIT_OBJ(&wa, WRK_ACCEPT_MAGIC);
		wa.acceptlsock = ls;

		acc_pace_check();

		wa.acceptaddrlen = sizeof wa.acceptaddr;
		do {
			i = accept(ls->sock, (void*)&wa.acceptaddr,
			    &wa.acceptaddrlen);
		} while (i < 0 && errno == EAGAIN && !ps->pool->die);

		if (i < 0 && ps->pool->die)
			break;

		if (i < 0 && ls->sock == -2) {
			/* Shut down in progress */
			sleep(2);
			continue;
		}

		if (i < 0) {
			switch (errno) {
			case ECONNABORTED:
				wrk->stats->sess_fail_econnaborted++;
				break;
			case EINTR:
				wrk->stats->sess_fail_eintr++;
				break;
			case EMFILE:
				wrk->stats->sess_fail_emfile++;
				acc_pace_bad();
				break;
			case EBADF:
				wrk->stats->sess_fail_ebadf++;
				acc_pace_bad();
				break;
			case ENOBUFS:
			case ENOMEM:
				wrk->stats->sess_fail_enomem++;
				acc_pace_bad();
				break;
			default:
				wrk->stats->sess_fail_other++;
				acc_pace_bad();
				break;
			}

			i = errno;
			wrk->stats->sess_fail++;

			VTCP_myname(ls->sock, laddr, VTCP_ADDRBUFSIZE,
			    lport, VTCP_PORTBUFSIZE);

			VSL(SLT_SessError, NO_VXID, "%s %s %s %d %d \"%s\"",
			    wa.acceptlsock->name, laddr, lport,
			    ls->sock, i, VAS_errtxt(i));
			(void)Pool_TrySumstat(wrk);
			continue;
		}

		wa.acceptsock = i;

		if (!Pool_Task_Arg(wrk, TASK_QUEUE_REQ,
		    acc_tcp_make_session, &wa, sizeof wa)) {
			/*
			 * We couldn't get another thread, so we will handle
			 * the request in this worker thread, but first we
			 * must reschedule the listening task so it will be
			 * taken up by another thread again.
			 */
			if (!ps->pool->die) {
				AZ(Pool_Task(wrk->pool, ps->task,
				    TASK_QUEUE_ACC));
				return;
			}
		}
		if (!ps->pool->die && DO_DEBUG(DBG_SLOW_ACCEPTOR))
			VTIM_sleep(2.0);

	}

	VSL(SLT_Debug, NO_VXID, "XXX Accept thread dies %p", ps);
	FREE_OBJ(ps);
}

static void
acc_tcp_accept(struct pool *pp)
{
	struct listen_sock *ls;
	struct poolsock *ps;

	VTAILQ_FOREACH(ls, &TCP_acceptor.socks, acclist) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

		ALLOC_OBJ(ps, POOLSOCK_MAGIC);
		AN(ps);
		ps->lsock = ls;
		ps->task->func = acc_tcp_accept_task;
		ps->task->priv = ps;
		ps->pool = pp;
		VTAILQ_INSERT_TAIL(&pp->poolsocks, ps, list);
		AZ(Pool_Task(pp, ps->task, TASK_QUEUE_ACC));
	}
}

static void
acc_tcp_update(pthread_mutex_t *shut_mtx)
{
	struct listen_sock *ls;

	if (!acc_tcp_sockopt_init())
		return;

	PTOK(pthread_mutex_lock(shut_mtx));

	VTAILQ_FOREACH(ls, &TCP_acceptor.socks, acclist) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

		if (ls->sock == -2)
			continue;	// ACC_Shutdown
		assert (ls->sock > 0);
		acc_tcp_sockopt_set(ls, NULL);
		/* If one of the options on a socket has
		 * changed, also force a retest of whether
		 * the values are inherited to the
		 * accepted sockets. This should then
		 * catch any false positives from previous
		 * tests that could happen if the set
		 * value of an option happened to just be
		 * the OS default for that value, and
		 * wasn't actually inherited from the
		 * listening socket. */
		ls->test_heritage = 1;
	}

	PTOK(pthread_mutex_unlock(shut_mtx));
}

static void
acc_tcp_shutdown(void)
{
	struct listen_sock *ls;
	int i;

	VTAILQ_FOREACH(ls, &TCP_acceptor.socks, acclist) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

		i = ls->sock;
		ls->sock = -2;
		(void)close(i);
	}
}

struct acceptor TCP_acceptor = {
	.magic		= ACCEPTOR_MAGIC,
	.name		= "tcp",
	.config		= acc_tcp_config,
	.init		= acc_tcp_init,
	.open		= acc_tcp_open,
	.reopen		= acc_tcp_reopen,
	.start		= acc_tcp_start,
	.event		= acc_tcp_event,
	.accept		= acc_tcp_accept,
	.update		= acc_tcp_update,
	.shutdown	= acc_tcp_shutdown,
};
