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

#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "cache/cache_varnishd.h"
#include "acceptor/cache_acceptor.h"
#include "acceptor/acceptor_priv.h"

#include "cache/cache_transport.h"
#include "cache/cache_pool.h"
#include "common/heritage.h"

#include "vcli_serve.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

static pthread_t	ACC_thread;
vtim_dur acc_pace = 0.0;
struct lock pace_mtx;
unsigned pool_accepting;
static pthread_mutex_t shut_mtx = PTHREAD_MUTEX_INITIALIZER;

/*--------------------------------------------------------------------
 * lacking a better place, we put some generic periodic updates
 * into the acc_acct() loop which we are running anyway
 */
static void
acc_periodic(vtim_real t0)
{
	vtim_real now;

	now = VTIM_real();
	VSC_C_main->uptime = (uint64_t)(now - t0);

	VTIM_postel = FEATURE(FEATURE_HTTP_DATE_POSTEL);
}

/*--------------------------------------------------------------------
 * If accept(2)'ing fails, we pace ourselves to relive any resource
 * shortage if possible.
 */

void
acc_pace_check(void)
{
	vtim_dur p;

	if (acc_pace == 0.0)
		return;
	Lck_Lock(&pace_mtx);
	p = acc_pace;
	Lck_Unlock(&pace_mtx);
	if (p > 0.0)
		VTIM_sleep(p);
}

void
acc_pace_bad(void)
{

	Lck_Lock(&pace_mtx);
	acc_pace += cache_param->acceptor_sleep_incr;
	if (acc_pace > cache_param->acceptor_sleep_max)
		acc_pace = cache_param->acceptor_sleep_max;
	Lck_Unlock(&pace_mtx);
}

void
acc_pace_good(void)
{

	if (acc_pace == 0.0)
		return;
	Lck_Lock(&pace_mtx);
	acc_pace *= cache_param->acceptor_sleep_decay;
	if (acc_pace < cache_param->acceptor_sleep_incr)
		acc_pace = 0.0;
	Lck_Unlock(&pace_mtx);
}

/*--------------------------------------------------------------------
 * Called when a worker and attached thread pool is created, to
 * allocate the tasks which will listen to sockets for that pool.
 */

void
ACC_NewPool(struct pool *pp)
{
	struct acceptor *acc;

	ACC_Foreach(acc) {
		CHECK_OBJ_NOTNULL(acc, ACCEPTOR_MAGIC);
		acc->accept(pp);
	}
}

void
ACC_DestroyPool(struct pool *pp)
{
	struct poolsock *ps;

	while (!VTAILQ_EMPTY(&pp->poolsocks)) {
		ps = VTAILQ_FIRST(&pp->poolsocks);
		VTAILQ_REMOVE(&pp->poolsocks, ps, list);
	}
}

/*--------------------------------------------------------------------*/

static void * v_matchproto_()
acc_acct(void *arg)
{
	struct acceptor *acc;
	vtim_real t0;

	// XXX Actually a mis-nomer now because the accept happens in a pool
	// thread. Rename to accept-nanny or so?
	THR_SetName("cache-acceptor");
	THR_Init();
	(void)arg;

	t0 = VTIM_real();
	acc_periodic(t0);

	pool_accepting = 1;

	while (1) {
		(void)sleep(1);

		ACC_Foreach(acc) {
			CHECK_OBJ_NOTNULL(acc, ACCEPTOR_MAGIC);
			acc->update(&shut_mtx);
		}

		acc_periodic(t0);
	}

	NEEDLESS(return (NULL));
}

/*--------------------------------------------------------------------*/

void
ACC_Start(struct cli *cli)
{
	struct acceptor *acc;

	ASSERT_CLI();

	ACC_Foreach(acc) {
		CHECK_OBJ_NOTNULL(acc, ACCEPTOR_MAGIC);
		acc->start(cli);
	}

	PTOK(pthread_create(&ACC_thread, NULL, acc_acct, NULL));
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
ccf_listen_address(struct cli *cli, const char * const *av, void *priv)
{
	struct listen_sock *ls;

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

	PTOK(pthread_mutex_lock(&shut_mtx));

	/*
	 * Varnishtest expects the list of listen sockets to come out in the
	 * same order as it is specified on the command line.
	 */
	VTAILQ_FOREACH(ls, &heritage.socks, list) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
		CHECK_OBJ_NOTNULL(ls->acc, ACCEPTOR_MAGIC);
		ls->acc->event(cli, ls, ACC_EVENT_LADDR);
	}

	PTOK(pthread_mutex_unlock(&shut_mtx));
}

/*--------------------------------------------------------------------*/

static struct cli_proto acc_cmds[] = {
	{ CLICMD_DEBUG_LISTEN_ADDRESS,	"d", ccf_listen_address },
	{ NULL }
};

void
ACC_Init(void)
{
	struct acceptor *acc;

	CLI_AddFuncs(acc_cmds);
	Lck_New(&pace_mtx, lck_accpace);

	ACC_Foreach(acc) {
		CHECK_OBJ_NOTNULL(acc, ACCEPTOR_MAGIC);
		acc->init();
	}
}

void
ACC_Shutdown(void)
{
	struct acceptor *acc;

	PTOK(pthread_mutex_lock(&shut_mtx));

	ACC_Foreach(acc) {
		CHECK_OBJ_NOTNULL(acc, ACCEPTOR_MAGIC);
		acc->shutdown();
	}

	PTOK(pthread_mutex_unlock(&shut_mtx));
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
