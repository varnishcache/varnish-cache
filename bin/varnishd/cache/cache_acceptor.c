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

#include <sys/types.h>

#include <errno.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "cache.h"
#include "cache_pool.h"
#include "common/heritage.h"

#include "vcli.h"
#include "vcli_priv.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

static pthread_t	VCA_thread;
static double vca_pace = 0.0;
static struct lock pace_mtx;
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
	struct listen_sock		*lsock;
	struct pool_task		task;
	struct pool			*pool;
};

/*--------------------------------------------------------------------
 * TCP options we want to control
 */

static struct tcp_opt {
	int		level;
	int		optname;
	const char	*strname;
	socklen_t	sz;
	void		*ptr;
	int		need;
} tcp_opts[] = {
#define TCPO(lvl, nam, sz) { lvl, nam, #nam, sizeof(sz), 0, 0},

	TCPO(SOL_SOCKET, SO_LINGER, struct linger)
	TCPO(SOL_SOCKET, SO_KEEPALIVE, int)
	TCPO(IPPROTO_TCP, TCP_NODELAY, int)

#ifdef SO_SNDTIMEO_WORKS
	TCPO(SOL_SOCKET, SO_SNDTIMEO, struct timeval)
#endif

#ifdef SO_RCVTIMEO_WORKS
	TCPO(SOL_SOCKET, SO_RCVTIMEO, struct timeval)
#endif

#ifdef HAVE_TCP_KEEP
	TCPO(IPPROTO_TCP, TCP_KEEPIDLE, int)
	TCPO(IPPROTO_TCP, TCP_KEEPCNT, int)
	TCPO(IPPROTO_TCP, TCP_KEEPINTVL, int)
#endif

#undef TCPO
};

static const int n_tcp_opts = sizeof tcp_opts / sizeof tcp_opts[0];

/*--------------------------------------------------------------------
 * We want to get out of any kind of trouble-hit TCP connections as fast
 * as absolutely possible, so we set them LINGER enabled with zero timeout,
 * so that even if there are outstanding write data on the socket, a close(2)
 * will return immediately.
 */
static const struct linger linger = {
	.l_onoff	=	0,
};

/*
 * We turn on keepalives by default to assist in detecting clients that have
 * hung up on connections returning from waitinglists
 */

static unsigned		need_test;

/*--------------------------------------------------------------------
 * Some kernels have bugs/limitations with respect to which options are
 * inherited from the accept/listen socket, so we have to keep track of
 * which, if any, sockopts we have to set on the accepted socket.
 */

static int
vca_tcp_opt_init(void)
{
	int n;
	int one = 1;
	struct tcp_opt *to;
	struct timeval tv;
	int chg = 0;
	int x;

	memset(&tv, 0, sizeof tv);
	memset(&x, 0, sizeof x);

	for (n = 0; n < n_tcp_opts; n++) {
		to = &tcp_opts[n];
		if (to->ptr == NULL)
			to->ptr = calloc(to->sz, 1);
		AN(to->ptr);
		if (!strcmp(to->strname, "SO_LINGER")) {
			assert(to->sz == sizeof linger);
			memcpy(to->ptr, &linger, sizeof linger);
			to->need = 1;
		} else if (!strcmp(to->strname, "TCP_NODELAY")) {
			assert(to->sz == sizeof one);
			memcpy(to->ptr, &one, sizeof one);
			to->need = 1;
		} else if (!strcmp(to->strname, "SO_KEEPALIVE")) {
			assert(to->sz == sizeof one);
			memcpy(to->ptr, &one, sizeof one);
			to->need = 1;
#define NEW_VAL(to, xx)						\
	do {							\
		assert(to->sz == sizeof xx);			\
		if (memcmp(to->ptr, &(xx), sizeof xx)) {	\
			memcpy(to->ptr, &(xx), sizeof xx);	\
			to->need = 1;				\
			chg = 1;				\
			need_test = 1;				\
		}						\
	} while (0)

#ifdef SO_SNDTIMEO_WORKS
		} else if (!strcmp(to->strname, "SO_SNDTIMEO")) {
			tv = VTIM_timeval(cache_param->idle_send_timeout);
			NEW_VAL(to, tv);
#endif
#ifdef SO_RCVTIMEO_WORKS
		} else if (!strcmp(to->strname, "SO_RCVTIMEO")) {
			tv = VTIM_timeval(cache_param->timeout_idle);
			NEW_VAL(to, tv);
#endif
#ifdef HAVE_TCP_KEEP
		} else if (!strcmp(to->strname, "TCP_KEEPIDLE")) {
			x = (int)(cache_param->tcp_keepalive_time);
			NEW_VAL(to, x);
		} else if (!strcmp(to->strname, "TCP_KEEPCNT")) {
			x = (int)(cache_param->tcp_keepalive_probes);
			NEW_VAL(to, x);
		} else if (!strcmp(to->strname, "TCP_KEEPINTVL")) {
			x = (int)(cache_param->tcp_keepalive_intvl);
			NEW_VAL(to, x);
#endif
		}
	}
	return (chg);
}

static void
vca_tcp_opt_test(int sock)
{
	int i, n;
	struct tcp_opt *to;
	socklen_t l;
	void *ptr;

	for (n = 0; n < n_tcp_opts; n++) {
		to = &tcp_opts[n];
		to->need = 1;
		ptr = calloc(to->sz, 1);
		AN(ptr);
		l = to->sz;
		i = getsockopt(sock, to->level, to->optname, ptr, &l);
		if (i == 0 && !memcmp(ptr, to->ptr, to->sz))
			to->need = 0;
		free(ptr);
		if (i && errno != ENOPROTOOPT)
			VTCP_Assert(i);
	}
}

static void
vca_tcp_opt_set(int sock, int force)
{
	int n;
	struct tcp_opt *to;

	for (n = 0; n < n_tcp_opts; n++) {
		to = &tcp_opts[n];
		if (to->need || force) {
			VTCP_Assert(setsockopt(sock,
			    to->level, to->optname, to->ptr, to->sz));
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
 * The pool-task for a newly accepted session
 *
 * Called from assigned worker thread
 */

static void __match_proto__(task_func_t)
vca_make_session(struct worker *wrk, void *arg)
{
	struct sess *sp;
	struct wrk_accept *wa;
	struct sockaddr_storage ss;
	struct suckaddr *sa;
	socklen_t sl;
	char laddr[VTCP_ADDRBUFSIZE];
	char lport[VTCP_PORTBUFSIZE];
	char raddr[VTCP_ADDRBUFSIZE];
	char rport[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(wa, arg, WRK_ACCEPT_MAGIC);

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
		(void)VTCP_nonblocking(wa->acceptsock);
		AZ(close(wa->acceptsock));
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
	sp->sess_step = wa->acceptlsock->first_step;

	assert(wa->acceptaddrlen <= vsa_suckaddr_len);
	SES_Reserve_remote_addr(sp, &sa);
	AN(VSA_Build(sa, &wa->acceptaddr, wa->acceptaddrlen));
	sp->sattr[SA_CLIENT_ADDR] = sp->sattr[SA_REMOTE_ADDR];

	VTCP_name(sa, raddr, sizeof raddr, rport, sizeof rport);
	SES_Set_String_Attr(sp, SA_CLIENT_IP, raddr);
	SES_Set_String_Attr(sp, SA_CLIENT_PORT, rport);

	sl = sizeof ss;
	AZ(getsockname(sp->fd, (void*)&ss, &sl));
	SES_Reserve_local_addr(sp, &sa);
	AN(VSA_Build(sa, &ss, sl));
	sp->sattr[SA_SERVER_ADDR] = sp->sattr[SA_LOCAL_ADDR];

	VTCP_name(sa, laddr, sizeof laddr, lport, sizeof lport);

	VSL(SLT_Begin, sp->vxid, "sess 0 %s", wa->acceptlsock->proto_name);
	VSL(SLT_SessOpen, sp->vxid, "%s %s %s %s %s %.6f %d",
	    raddr, rport, wa->acceptlsock->name, laddr, lport,
	    sp->t_open, sp->fd);

	WS_Release(wrk->aws, 0);

	vca_pace_good();
	wrk->stats->sess_conn++;

	if (need_test) {
		vca_tcp_opt_test(sp->fd);
		need_test = 0;
	}
	vca_tcp_opt_set(sp->fd, 0);

	/* SES_Proto_Sess() must be sceduled with reserved WS */
	assert(8 <= WS_Reserve(sp->ws, 8));
	wrk->task.func = SES_Proto_Sess;
	wrk->task.priv = sp;
}

/*--------------------------------------------------------------------
 * This function accepts on a single socket for a single thread pool.
 *
 * As long as we can stick the accepted connection to another thread
 * we do so, otherwise we put the socket back on the "BACK" pool
 * and handle the new connection ourselves.
 */

static void __match_proto__(task_func_t)
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

	while (1) {
		INIT_OBJ(&wa, WRK_ACCEPT_MAGIC);
		wa.acceptlsock = ls;

		vca_pace_check();

		wa.acceptaddrlen = sizeof wa.acceptaddr;
		do {
			i = accept(ls->sock, (void*)&wa.acceptaddr,
				   &wa.acceptaddrlen);
		} while (i < 0 && errno == EAGAIN);

		if (i < 0 && ls->sock == -2) {
			/* Shut down in progress */
			sleep(2);
			continue;
		}

		if (i < 0) {
			switch (errno) {
			case ECONNABORTED:
				break;
			case EMFILE:
				VSL(SLT_Debug, ls->sock, "Too many open files");
				vca_pace_bad();
				break;
			case EBADF:
				VSL(SLT_Debug, ls->sock, "Accept failed: %s",
				    strerror(errno));
				vca_pace_bad();
				break;
			default:
				VSL(SLT_Debug, ls->sock, "Accept failed: %s",
				    strerror(errno));
				vca_pace_bad();
				break;
			}
			wrk->stats->sess_fail++;
			(void)Pool_TrySumstat(wrk);
			continue;
		}

		wa.acceptsock = i;

		if (!Pool_Task_Arg(wrk, TASK_QUEUE_VCA,
		    vca_make_session, &wa, sizeof wa)) {
			/*
			 * We couldn't get another thread, so we will handle
			 * the request in this worker thread, but first we
			 * must reschedule the listening task so it will be
			 * taken up by another thread again.
			 */
			AZ(Pool_Task(wrk->pool, &ps->task, TASK_QUEUE_VCA));
			return;
		}

		/*
		 * We were able to hand off, so release this threads VCL
		 * reference (if any) so we don't hold on to discarded VCLs.
		 */
		if (wrk->vcl != NULL)
			VCL_Rel(&wrk->vcl);
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
		AZ(Pool_Task(pp, &ps->task, TASK_QUEUE_VCA));
	}
}

/*--------------------------------------------------------------------*/

static void *
vca_acct(void *arg)
{
	struct listen_sock *ls;
	double t0, now;

	THR_SetName("cache-acceptor");
	THR_Init();
	(void)arg;

	(void)vca_tcp_opt_init();

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		assert (ls->sock > 0);		// We know where stdin is
		if (cache_param->tcp_fastopen) {
			int i;
			i = VTCP_fastopen(ls->sock, cache_param->listen_depth);
			if (i)
				VSL(SLT_Error, ls->sock,
				    "Kernel TCP Fast Open: sock=%d, ret=%d %s",
				    ls->sock, i, strerror(errno));
		}
		AZ(listen(ls->sock, cache_param->listen_depth));
		vca_tcp_opt_set(ls->sock, 1);
		if (cache_param->accept_filter) {
			int i;
			i = VTCP_filter_http(ls->sock);
			if (i)
				VSL(SLT_Error, ls->sock,
				    "Kernel filtering: sock=%d, ret=%d %s",
				    ls->sock, i, strerror(errno));
		}
	}

	need_test = 1;
	pool_accepting = 1;

	t0 = VTIM_real();
	while (1) {
		(void)sleep(1);
		if (vca_tcp_opt_init()) {
			VTAILQ_FOREACH(ls, &heritage.socks, list) {
				if (ls->sock == -2)
					continue;	// raced VCA_Shutdown
				assert (ls->sock > 0);
				vca_tcp_opt_set(ls->sock, 1);
			}
		}
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
	while(!pool_accepting)
		VTIM_sleep(.1);

	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		VTCP_myname(ls->sock, h, sizeof h, p, sizeof p);
		VCLI_Out(cli, "%s %s\n", h, p);
	}
}

/*--------------------------------------------------------------------*/

static struct cli_proto vca_cmds[] = {
	{ CLI_SERVER_START,	"i", ccf_start },
	{ "debug.listen_address", "debug.listen_address",
	    "\tReport the actual listen address.", 0, 0,
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
		i = ls->sock;
		ls->sock = -2;
		(void)close(i);
	}
}
