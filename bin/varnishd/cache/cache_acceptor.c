/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>

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
static unsigned pool_accepting;
static pthread_mutex_t shut_mtx = PTHREAD_MUTEX_INITIALIZER;

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
	struct listen_sock		*lsock;
	struct pool_task		task;
	struct pool			*pool;
};

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
	int		need;
	socklen_t	sz;
	union sock_arg	arg[1];
} sock_opts[] = {
#define SOCK_OPT(lvl, nam, typ) { lvl, nam, #nam, 0, sizeof(typ) },

	SOCK_OPT(SOL_SOCKET, SO_LINGER, struct linger)
	SOCK_OPT(SOL_SOCKET, SO_KEEPALIVE, int)
	SOCK_OPT(SOL_SOCKET, SO_SNDTIMEO, struct timeval)
	SOCK_OPT(SOL_SOCKET, SO_RCVTIMEO, struct timeval)

	SOCK_OPT(IPPROTO_TCP, TCP_NODELAY, int)

#ifdef HAVE_TCP_KEEP
	SOCK_OPT(IPPROTO_TCP, TCP_KEEPIDLE, int)
	SOCK_OPT(IPPROTO_TCP, TCP_KEEPCNT, int)
	SOCK_OPT(IPPROTO_TCP, TCP_KEEPINTVL, int)
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

static unsigned		need_test;

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
				so->need = 1;				\
				chg = 1;				\
				need_test = 1;				\
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
#ifdef HAVE_TCP_KEEP
		NEW_VAL(TCP_KEEPIDLE, so, i,
		    (int)cache_param->tcp_keepalive_time);
		NEW_VAL(TCP_KEEPCNT, so, i,
		    (int)cache_param->tcp_keepalive_probes);
		NEW_VAL(TCP_KEEPINTVL, so, i,
		    (int)cache_param->tcp_keepalive_intvl);
#endif
	}
	return (chg);
}

static void
vca_sock_opt_test(const struct listen_sock *ls, const struct sess *sp)
{
	struct sock_opt *so;
	union sock_arg tmp;
	socklen_t l;
	int i, n;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	for (n = 0; n < n_sock_opts; n++) {
		so = &sock_opts[n];
		if (so->level == IPPROTO_TCP && ls->uds)
			continue;
		so->need = 1;
		memset(&tmp, 0, sizeof tmp);
		l = so->sz;
		i = getsockopt(sp->fd, so->level, so->optname, &tmp, &l);
		if (i == 0 && !memcmp(&tmp, so->arg, so->sz))
			so->need = 0;
		if (i && errno != ENOPROTOOPT)
			VTCP_Assert(i);
	}
}

static void
vca_sock_opt_set(const struct listen_sock *ls, const struct sess *sp)
{
	struct sock_opt *so;
	int n, sock;

	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
	CHECK_OBJ_ORNULL(sp, SESS_MAGIC);
	sock = sp != NULL ? sp->fd : ls->sock;

	for (n = 0; n < n_sock_opts; n++) {
		so = &sock_opts[n];
		if (so->level == IPPROTO_TCP && ls->uds)
			continue;
		if (so->need || sp == NULL) {
			VTCP_Assert(setsockopt(sock,
			    so->level, so->optname, so->arg, so->sz));
		}
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
	struct suckaddr *sa;
	struct sockaddr_storage ss;
	socklen_t sl;

	AN(SES_Reserve_remote_addr(sp, &sa));
	AN(VSA_Build(sa, &wa->acceptaddr, wa->acceptaddrlen));
	sp->sattr[SA_CLIENT_ADDR] = sp->sattr[SA_REMOTE_ADDR];

	VTCP_name(sa, raddr, VTCP_ADDRBUFSIZE, rport, VTCP_PORTBUFSIZE);
	AN(SES_Set_String_Attr(sp, SA_CLIENT_IP, raddr));
	AN(SES_Set_String_Attr(sp, SA_CLIENT_PORT, rport));

	sl = sizeof ss;
	AZ(getsockname(sp->fd, (void*)&ss, &sl));
	AN(SES_Reserve_local_addr(sp, &sa));
	AN(VSA_Build(sa, &ss, sl));
	sp->sattr[SA_SERVER_ADDR] = sp->sattr[SA_LOCAL_ADDR];
	VTCP_name(sa, laddr, VTCP_ADDRBUFSIZE, lport, VTCP_PORTBUFSIZE);
}

static void
vca_mk_uds(struct wrk_accept *wa, struct sess *sp, char *laddr, char *lport,
	   char *raddr, char *rport)
{
	struct suckaddr *sa;

	(void) wa;
	AN(SES_Reserve_remote_addr(sp, &sa));
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
	AN(wrk->aws->r);
	sp = SES_New(wrk->pool);
	if (sp == NULL) {
		/*
		 * We consider this a DoS situation and silently close the
		 * connection with minimum effort and fuzz, rather than try
		 * to send an intelligent message back.
		 */
		vca_pace_bad();
		VTCP_nonblocking(wa->acceptsock);
		closefd(&wa->acceptsock);
		wrk->stats->sess_drop++;
		WS_Release(wrk->aws, 0);
		return;
	}
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	wrk->stats->s_sess++;

	sp->t_open = VTIM_real();
	sp->t_idle = sp->t_open;
	sp->vxid = VXID_Get(wrk, VSL_CLIENTMARKER);

	sp->fd = wa->acceptsock;
	wa->acceptsock = -1;
	sp->listen_sock = wa->acceptlsock;

	assert(wa->acceptaddrlen <= vsa_suckaddr_len);

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

	WS_Release(wrk->aws, 0);

	vca_pace_good();
	wrk->stats->sess_conn++;

	if (need_test) {
		vca_sock_opt_test(wa->acceptlsock, sp);
		need_test = 0;
	}
	vca_sock_opt_set(wa->acceptlsock, sp);

	req = Req_New(wrk, sp);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	req->htc->rfd = &sp->fd;

	SES_SetTransport(wrk, sp, req, wa->acceptlsock->transport);
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

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(ps, arg, POOLSOCK_MAGIC);
	ls = ps->lsock;
	CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);

	while (!pool_accepting)
		VTIM_sleep(.1);

	/* Dont hold on to (possibly) discarded VCLs */
	if (wrk->vcl != NULL)
		VCL_Rel(&wrk->vcl);

	while (!ps->pool->die) {
		INIT_OBJ(&wa, WRK_ACCEPT_MAGIC);
		wa.acceptlsock = ls;

		vca_pace_check();

		wa.acceptaddrlen = sizeof wa.acceptaddr;
		do {
			i = accept(ls->sock, (void*)&wa.acceptaddr,
				   &wa.acceptaddrlen);
		} while (i < 0 && errno == EAGAIN);

		if (i < 0 && ps->pool->die) {
			VSL(SLT_Debug, 0, "XXX Accept thread dies %p", ps);
			FREE_OBJ(ps);
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
			wrk->stats->sess_fail++;
			VSL(SLT_Debug, ls->sock, "Accept failed: %s",
			    strerror(errno));
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
			if (!ps->pool->die)
				AZ(Pool_Task(wrk->pool, &ps->task,
				    TASK_QUEUE_VCA));
			return;
		}
		if (!ps->pool->die && DO_DEBUG(DBG_SLOW_ACCEPTOR))
			VTIM_sleep(2.0);

	}
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
		ps->task.func = vca_accept_task;
		ps->task.priv = ps;
		ps->pool = pp;
		VTAILQ_INSERT_TAIL(&pp->poolsocks, ps, list);
		AZ(Pool_Task(pp, &ps->task, TASK_QUEUE_VCA));
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

	pool_accepting = 1;

	while (1) {
		(void)sleep(1);
		if (vca_sock_opt_init()) {
			AZ(pthread_mutex_lock(&shut_mtx));
			VTAILQ_FOREACH(ls, &heritage.socks, list) {
				if (ls->sock == -2)
					continue;	// VCA_Shutdown
				assert (ls->sock > 0);
				vca_sock_opt_set(ls, NULL);
			}
			AZ(pthread_mutex_unlock(&shut_mtx));
		}
		vca_periodic(t0);
	}
	NEEDLESS(return NULL);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
ccf_start(struct cli *cli, const char * const *av, void *priv)
{
	struct listen_sock *ls;

	(void)cli;
	(void)av;
	(void)priv;

	(void)vca_sock_opt_init();

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		CHECK_OBJ_NOTNULL(ls->transport, TRANSPORT_MAGIC);
		assert (ls->sock > 0);	// We know where stdin is
		if (cache_param->tcp_fastopen) {
			int i;
			i = VTCP_fastopen(ls->sock, cache_param->listen_depth);
			if (i)
				VSL(SLT_Error, 0,
				    "Kernel TCP Fast Open: sock=%d, ret=%d %s",
				    ls->sock, i, strerror(errno));
		}
		if (listen(ls->sock, cache_param->listen_depth)) {
			VCLI_SetResult(cli, CLIS_CANT);
			VCLI_Out(cli, "Listen failed on socket '%s': %s",
			    ls->endpoint, strerror(errno));
			return;
		}
		vca_sock_opt_set(ls, NULL);
		if (cache_param->accept_filter) {
			int i;
			i = VTCP_filter_http(ls->sock);
			if (i)
				VSL(SLT_Error, 0,
				    "Kernel filtering: sock=%d, ret=%d %s",
				    ls->sock, i, strerror(errno));
		}
	}

	need_test = 1;

	AZ(pthread_create(&VCA_thread, NULL, vca_acct, NULL));
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
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
	while (!pool_accepting)
		VTIM_sleep(.1);

	AZ(pthread_mutex_lock(&shut_mtx));
	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		if (!ls->uds) {
			VTCP_myname(ls->sock, h, sizeof h, p, sizeof p);
			VCLI_Out(cli, "%s %s\n", h, p);
		}
		else
			VCLI_Out(cli, "%s -\n", ls->endpoint);
	}
	AZ(pthread_mutex_unlock(&shut_mtx));
}

/*--------------------------------------------------------------------*/

static struct cli_proto vca_cmds[] = {
	{ CLICMD_SERVER_START,		"", ccf_start },
	{ CLICMD_DEBUG_LISTEN_ADDRESS,	"d", ccf_listen_address },
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

	AZ(pthread_mutex_lock(&shut_mtx));
	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		i = ls->sock;
		ls->sock = -2;
		(void)close(i);
	}
	AZ(pthread_mutex_unlock(&shut_mtx));
}

/*--------------------------------------------------------------------
 * Transport protocol registration
 *
 */

static VTAILQ_HEAD(,transport)	transports =
    VTAILQ_HEAD_INITIALIZER(transports);

void
XPORT_Init(void)
{
	uint16_t n;
	struct transport *xp;

	ASSERT_MGT();

	VTAILQ_INSERT_TAIL(&transports, &PROXY_transport, list);
	VTAILQ_INSERT_TAIL(&transports, &HTTP1_transport, list);
	VTAILQ_INSERT_TAIL(&transports, &H2_transport, list);

	n = 0;
	VTAILQ_FOREACH(xp, &transports, list)
		xp->number = ++n;
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
