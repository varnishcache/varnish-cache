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
 * This source file has the various trickery surrounding the accept/listen
 * sockets.
 *
 */

#include "config.h"

#include <signal.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "cache_varnishd.h"

#include "cache_transport.h"
#include "cache_pool.h"
#include "common/heritage.h"

#include "vcli_serve.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

static pthread_t	VCA_thread;
static vtim_dur vca_pace = 0.0;
static struct lock pace_mtx;
static struct lock shut_mtx;
static pthread_cond_t shut_cond = PTHREAD_COND_INITIALIZER;
static unsigned pool_accepting;

struct wrk_accept {
	unsigned		magic;
#define WRK_ACCEPT_MAGIC	0x8c4b4d59

	/* Accept stuff */
	struct sockaddr_storage	acceptaddr;
	socklen_t		acceptaddrlen;
	int			acceptsock;
	struct listen_sock	*acceptlsock;
};

struct poolsock {
	unsigned			magic;
#define POOLSOCK_MAGIC			0x1b0a2d38
	VTAILQ_ENTRY(poolsock)		list;
	VTAILQ_ENTRY(poolsock)		busy_list;
	pthread_t			busy_thr;
	struct worker			*busy_wrk;
	struct listen_sock		*lsock;
	struct pool_task		task[1];
	struct pool			*pool;
};

static VTAILQ_HEAD(,poolsock)		busy_socks =
    VTAILQ_HEAD_INITIALIZER(busy_socks);

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

struct conn_heritage {
	unsigned	sess_set;
	unsigned	listen_mod;
};

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
 * lacking a better place, we put some generic periodic updates
 * into the vca_acct() loop which we are running anyway
 */
static void
vca_periodic(vtim_real t0)
{
	vtim_real now;

	now = VTIM_real();
	VSC_C_main->uptime = (uint64_t)(now - t0);

	VTIM_postel = FEATURE(FEATURE_HTTP_DATE_POSTEL);
}

/*--------------------------------------------------------------------
 * Some kernels have bugs/limitations with respect to which options are
 * inherited from the accept/listen socket, so we have to keep track of
 * which, if any, sockopts we have to set on the accepted socket.
 */

static int
vca_sock_opt_init(void)
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
vca_sock_opt_test(const struct listen_sock *ls, const struct sess *sp)
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
		if (so->level == IPPROTO_TCP && ls->uds) {
			VSL(SLT_Debug, sp->vxid,
			    "sockopt: Not testing incompatible %s for %s=%s",
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
vca_sock_opt_set(const struct listen_sock *ls, const struct sess *sp)
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
		if (so->level == IPPROTO_TCP && ls->uds) {
			VSL(SLT_Debug, vxid,
			    "sockopt: Not setting incompatible %s for %s=%s",
			    so->strname, ls->name, ls->endpoint);
			continue;
		}
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

/*--------------------------------------------------------------------
 * If accept(2)'ing fails, we pace ourselves to relive any resource
 * shortage if possible.
 */

static void
vca_pace_check(void)
{
	vtim_dur p;

	if (!pool_accepting)
		vca_pace = 0.0;
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
 * The pool-task for a newly accepted session
 *
 * Called from assigned worker thread
 */

static void
vca_mk_tcp(const struct wrk_accept *wa,
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

static void
vca_mk_uds(struct wrk_accept *wa, struct sess *sp, char *laddr, char *lport,
	   char *raddr, char *rport)
{
	struct suckaddr *sa = NULL;
	ssize_t sz;

	(void) wa;
	AN(SES_Reserve_remote_addr(sp, &sa, &sz));
	AN(sa);
	assert(sz == vsa_suckaddr_len);
	AZ(SES_Set_remote_addr(sp, bogo_ip));
	sp->sattr[SA_CLIENT_ADDR] = sp->sattr[SA_REMOTE_ADDR];
	sp->sattr[SA_LOCAL_ADDR] = sp->sattr[SA_REMOTE_ADDR];
	sp->sattr[SA_SERVER_ADDR] = sp->sattr[SA_REMOTE_ADDR];
	AN(SES_Set_String_Attr(sp, SA_CLIENT_IP, "0.0.0.0"));
	AN(SES_Set_String_Attr(sp, SA_CLIENT_PORT, "0"));

	strcpy(laddr, "0.0.0.0");
	strcpy(raddr, "0.0.0.0");
	strcpy(lport, "0");
	strcpy(rport, "0");
}

static void v_matchproto_(task_func_t)
vca_make_session(struct worker *wrk, void *arg)
{
	struct sess *sp;
	struct req *req;
	struct wrk_accept *wa;
	char laddr[VTCP_ADDRBUFSIZE];
	char lport[VTCP_PORTBUFSIZE];
	char raddr[VTCP_ADDRBUFSIZE];
	char rport[VTCP_PORTBUFSIZE];

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

	if (wa->acceptlsock->uds)
		vca_mk_uds(wa, sp, laddr, lport, raddr, rport);
	else
		vca_mk_tcp(wa, sp, laddr, lport, raddr, rport);

	AN(wa->acceptlsock->name);
	VSL(SLT_Begin, sp->vxid, "sess 0 %s",
	    wa->acceptlsock->transport->name);
	VSL(SLT_SessOpen, sp->vxid, "%s %s %s %s %s %.6f %d",
	    raddr, rport, wa->acceptlsock->name, laddr, lport,
	    sp->t_open, sp->fd);

	vca_pace_good();
	wrk->stats->sess_conn++;

	if (wa->acceptlsock->test_heritage) {
		vca_sock_opt_test(wa->acceptlsock, sp);
		wa->acceptlsock->test_heritage = 0;
	}
	vca_sock_opt_set(wa->acceptlsock, sp);

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
vca_accept_task(struct worker *wrk, void *arg)
{
	struct wrk_accept wa;
	struct poolsock *ps;
	struct listen_sock *ls;
	int i;
	char laddr[VTCP_ADDRBUFSIZE];
	char lport[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(ps, arg, POOLSOCK_MAGIC);
	ls = ps->lsock;
	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

	while (!pool_accepting)
		VTIM_sleep(.1);

	/* Dont hold on to (possibly) discarded VCLs */
	if (wrk->wpriv->vcl != NULL)
		VCL_Rel(&wrk->wpriv->vcl);

	while (!ps->pool->die) {
		INIT_OBJ(&wa, WRK_ACCEPT_MAGIC);
		wa.acceptlsock = ls;

		vca_pace_check();

		wa.acceptaddrlen = sizeof wa.acceptaddr;

		Lck_Lock(&shut_mtx);
		AZ(ps->busy_wrk);
		if (pool_accepting) {
			ps->busy_wrk = wrk;
			ps->busy_thr = pthread_self();
			VTAILQ_INSERT_TAIL(&busy_socks, ps, busy_list);
		}
		Lck_Unlock(&shut_mtx);

		if (ps->busy_wrk == NULL) {
			AZ(Pool_Task(wrk->pool, ps->task, TASK_QUEUE_VCA));
			return;
		}

		do {
			i = accept(ls->sock, (void*)&wa.acceptaddr,
			    &wa.acceptaddrlen);
		} while (i < 0 && errno == EAGAIN && !ps->pool->die &&
		    pool_accepting);

		Lck_Lock(&shut_mtx);
		ps->busy_wrk = NULL;
		VTAILQ_REMOVE(&busy_socks, ps, busy_list);
		if (!pool_accepting && VTAILQ_EMPTY(&busy_socks))
			AZ(pthread_cond_signal(&shut_cond));
		Lck_Unlock(&shut_mtx);

		if (i < 0 && ps->pool->die)
			break;

		if (i < 0 && !pool_accepting) {
			AZ(Pool_Task(wrk->pool, ps->task, TASK_QUEUE_VCA));
			return;
		}

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
				vca_pace_bad();
				break;
			case EBADF:
				wrk->stats->sess_fail_ebadf++;
				vca_pace_bad();
				break;
			case ENOBUFS:
			case ENOMEM:
				wrk->stats->sess_fail_enomem++;
				vca_pace_bad();
				break;
			default:
				wrk->stats->sess_fail_other++;
				vca_pace_bad();
				break;
			}

			i = errno;
			wrk->stats->sess_fail++;

			if (wa.acceptlsock->uds) {
				bstrcpy(laddr, "0.0.0.0");
				bstrcpy(lport, "0");
			} else {
				VTCP_myname(ls->sock, laddr, VTCP_ADDRBUFSIZE,
				    lport, VTCP_PORTBUFSIZE);
			}

			VSL(SLT_SessError, NO_VXID, "%s %s %s %d %d \"%s\"",
			    wa.acceptlsock->name, laddr, lport,
			    ls->sock, i, VAS_errtxt(i));
			(void)Pool_TrySumstat(wrk);
			continue;
		}

		wa.acceptsock = i;

		if (!Pool_Task_Arg(wrk, TASK_QUEUE_REQ,
		    vca_make_session, &wa, sizeof wa)) {
			/*
			 * We couldn't get another thread, so we will handle
			 * the request in this worker thread, but first we
			 * must reschedule the listening task so it will be
			 * taken up by another thread again.
			 */
			if (!ps->pool->die) {
				AZ(Pool_Task(wrk->pool, ps->task,
				    TASK_QUEUE_VCA));
				return;
			}
		}
		if (!ps->pool->die && DO_DEBUG(DBG_SLOW_ACCEPTOR))
			VTIM_sleep(2.0);

	}

	VSL(SLT_Debug, NO_VXID, "XXX Accept thread dies %p", ps);
	FREE_OBJ(ps);
}

/*--------------------------------------------------------------------
 * Called when a worker and attached thread pool is created, to
 * allocate the tasks which will listen to sockets for that pool.
 */

void
VCA_NewPool(struct pool *pp)
{
	struct listen_sock *ls;
	struct poolsock *ps;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		ALLOC_OBJ(ps, POOLSOCK_MAGIC);
		AN(ps);
		ps->lsock = ls;
		ps->task->func = vca_accept_task;
		ps->task->priv = ps;
		ps->pool = pp;
		VTAILQ_INSERT_TAIL(&pp->poolsocks, ps, list);
		AZ(Pool_Task(pp, ps->task, TASK_QUEUE_VCA));
	}
}

void
VCA_DestroyPool(struct pool *pp)
{
	struct poolsock *ps;

	while (!VTAILQ_EMPTY(&pp->poolsocks)) {
		ps = VTAILQ_FIRST(&pp->poolsocks);
		VTAILQ_REMOVE(&pp->poolsocks, ps, list);
	}
}

/*--------------------------------------------------------------------*/

static void * v_matchproto_()
vca_acct(void *arg)
{
	struct listen_sock *ls;
	vtim_real t0;

	// XXX Actually a mis-nomer now because the accept happens in a pool
	// thread. Rename to accept-nanny or so?
	THR_SetName("cache-acceptor");
	THR_Init();
	(void)arg;

	t0 = VTIM_real();
	vca_periodic(t0);

	while (1) {
		(void)sleep(1);
		if (pool_accepting && vca_sock_opt_init()) {
			Lck_Lock(&shut_mtx);
			VTAILQ_FOREACH(ls, &heritage.socks, list) {
				if (ls->sock == -2)
					continue;	// VCA_Shutdown
				assert (ls->sock > 0);
				vca_sock_opt_set(ls, NULL);
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
			Lck_Unlock(&shut_mtx);
		}
		vca_periodic(t0);
	}
	NEEDLESS(return (NULL));
}

/*--------------------------------------------------------------------*/

static int
vca_fence(struct cli *cli)
{
	struct listen_sock *ls;

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		assert(ls->sock < 0);
		AN(ls->nonce);
		AN(*ls->nonce);
		ls->sock = SMUG_Fence(*ls->nonce);
		if (ls->sock < 0)
			break;
		*ls->nonce = 0;
	}

	if (ls == NULL)
		return (0);

	VCLI_SetResult(cli, CLIS_CANT);
	VCLI_Out(cli, "Could not acquire listen socket '%s': %s\n",
	    ls->endpoint, strerror(errno));
	return (-1);
}

static void
vca_start(struct cli *cli)
{
	struct listen_sock *ls;

	if (vca_fence(cli) < 0)
		return;

	(void)vca_sock_opt_init();

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		CHECK_OBJ_NOTNULL(ls->transport, TRANSPORT_MAGIC);
		assert (ls->sock > 0);	// We know where stdin is
		if (cache_param->tcp_fastopen &&
		    VTCP_fastopen(ls->sock, cache_param->listen_depth))
			VSL(SLT_Error, NO_VXID,
			    "Kernel TCP Fast Open: sock=%d, errno=%d %s",
			    ls->sock, errno, VAS_errtxt(errno));
		if (listen(ls->sock, cache_param->listen_depth)) {
			VCLI_SetResult(cli, CLIS_CANT);
			VCLI_Out(cli, "Listen failed on socket '%s': %s\n",
			    ls->endpoint, VAS_errtxt(errno));
			return;
		}
		AZ(ls->conn_heritage);
		ls->conn_heritage = calloc(n_sock_opts,
		    sizeof *ls->conn_heritage);
		AN(ls->conn_heritage);
		ls->test_heritage = 1;
		vca_sock_opt_set(ls, NULL);
		if (cache_param->accept_filter && VTCP_filter_http(ls->sock))
			VSL(SLT_Error, NO_VXID,
			    "Kernel filtering: sock=%d, errno=%d %s",
			    ls->sock, errno, VAS_errtxt(errno));
	}
	pool_accepting = 1;
}

static void v_matchproto_(cli_func_t)
ccf_start(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	(void)priv;

	if (cache_param->accept_traffic)
		vca_start(cli);
	/* XXX: why not a bgthread? */
	AZ(pthread_create(&VCA_thread, NULL, vca_acct, NULL));
	PTOK(pthread_create(&VCA_thread, NULL, vca_acct, NULL));
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
ccf_listen_address(struct cli *cli, const char * const *av, void *priv)
{
	struct listen_sock *ls;
	char h[VTCP_ADDRBUFSIZE], p[VTCP_PORTBUFSIZE];

	(void)cli;
	(void)av;
	(void)priv;

	if (!cache_param->accept_traffic) {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "Not accepting traffic\n");
		return;
	}

	/*
	 * This CLI command is primarily used by varnishtest.  Don't
	 * respond until listen(2) has been called, in order to avoid
	 * a race where varnishtest::client would attempt to connect(2)
	 * before listen(2) has been called.
	 */
	while (!pool_accepting)
		VTIM_sleep(.1);

	Lck_Lock(&shut_mtx);
	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (!ls->uds) {
			VTCP_myname(ls->sock, h, sizeof h, p, sizeof p);
			VCLI_Out(cli, "%s %s %s\n", ls->name, h, p);
		}
		else
			VCLI_Out(cli, "%s %s -\n", ls->name, ls->endpoint);
	}
	Lck_Unlock(&shut_mtx);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
ccf_traffic_accept(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	(void)priv;

	vca_start(cli);
	if (cli->result != CLIS_OK)
		VCA_Shutdown();
}

static void v_matchproto_(cli_func_t)
ccf_traffic_refuse(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;

	VCA_Shutdown();
}

/*--------------------------------------------------------------------*/

static struct cli_proto vca_cmds[] = {
	{ CLICMD_SERVER_START,		"", ccf_start },
	{ CLICMD_DEBUG_LISTEN_ADDRESS,	"d", ccf_listen_address },
	{ CLICMD_TRAFFIC_ACCEPT, "", ccf_traffic_accept },
	{ CLICMD_TRAFFIC_REFUSE, "", ccf_traffic_refuse },
	{ NULL }
};

void
VCA_Init(void)
{

	CLI_AddFuncs(vca_cmds);
	Lck_New(&pace_mtx, lck_vcapace);
	Lck_New(&shut_mtx, lck_vcashut);
}

void
VCA_Shutdown(void)
{
	struct listen_sock *ls;
	struct poolsock *ps;

	ASSERT_CLI();

	pool_accepting = 0;
	Lck_Lock(&shut_mtx);
	while (!VTAILQ_EMPTY(&busy_socks)) {
		VTAILQ_FOREACH(ps, &busy_socks, busy_list) {
			CHECK_OBJ_NOTNULL(ps, POOLSOCK_MAGIC);
			CHECK_OBJ_NOTNULL(ps->busy_wrk, WORKER_MAGIC);
			AZ(pthread_kill(ps->busy_thr, SIGUSR1));
		}
		Lck_CondWaitTimeout(&shut_cond, &shut_mtx, 0.1);
	}

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (ls->sock > 0) {
			closefd(&ls->sock);
			ls->sock = -2;
		}
		free(ls->conn_heritage);
		ls->conn_heritage = NULL;
	}
	Lck_Unlock(&shut_mtx);
}

/*--------------------------------------------------------------------
 * Transport protocol registration
 *
 */

static VTAILQ_HEAD(,transport)	transports =
    VTAILQ_HEAD_INITIALIZER(transports);

static uint16_t next_xport;

static void
XPORT_Register(struct transport *xp)
{

	CHECK_OBJ_NOTNULL(xp, TRANSPORT_MAGIC);
	AZ(xp->number);

	xp->number = ++next_xport;
	VTAILQ_INSERT_TAIL(&transports, xp, list);
}

void
XPORT_Init(void)
{

	ASSERT_MGT();

#define TRANSPORT_MACRO(name) XPORT_Register(&name##_transport);
	TRANSPORTS
#undef TRANSPORT_MACRO
}

const struct transport *
XPORT_Find(const char *name)
{
	const struct transport *xp;

	ASSERT_MGT();

	VTAILQ_FOREACH(xp, &transports, list)
		if (xp->proto_ident != NULL &&
		    !strcasecmp(xp->proto_ident, name))
			return (xp);
	return (NULL);
}

const struct transport *
XPORT_ByNumber(uint16_t no)
{
	const struct transport *xp;

	VTAILQ_FOREACH(xp, &transports, list)
		if (xp->number == no)
			return (xp);
	return (NULL);
}
