/*-
 * Copyright (c) 2015 Varnish Software AS
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
 * TCP connection pools.
 *
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>

#include "cache_varnishd.h"

#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"
#include "waiter/waiter.h"

#include "cache_tcp_pool.h"
#include "cache_pool.h"

typedef int cp_open_f(const struct tcp_pool *, double tmo, const void **privp);
typedef void cp_close_f(struct pfd *);

struct cp_methods {
	cp_open_f				*open;
	cp_close_f				*close;
};

struct tcp_pool {
	unsigned				magic;
#define TCP_POOL_MAGIC				0x28b0e42a

	const struct cp_methods			*methods;

	const void				*id;
	struct suckaddr				*ip4;
	struct suckaddr				*ip6;

	VTAILQ_ENTRY(tcp_pool)			list;
	int					refcnt;
	struct lock				mtx;

	VTAILQ_HEAD(, pfd)			connlist;
	int					n_conn;

	VTAILQ_HEAD(, pfd)			killlist;
	int					n_kill;

	int					n_used;
};

static struct lock		tcp_pools_mtx;
static VTAILQ_HEAD(, tcp_pool)	tcp_pools = VTAILQ_HEAD_INITIALIZER(tcp_pools);

/*--------------------------------------------------------------------
 */

static int v_matchproto_(cp_open_f)
vtp_open(const struct tcp_pool *tp, double tmo, const void **privp)
{
	int s;
	int msec;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	msec = (int)floor(tmo * 1000.0);
	if (cache_param->prefer_ipv6) {
		*privp = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
		if (s >= 0)
			return (s);
	}
	*privp = tp->ip4;
	s = VTCP_connect(tp->ip4, msec);
	if (s >= 0)
		return (s);
	if (!cache_param->prefer_ipv6) {
		*privp = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
	}
	return (s);
}

static void v_matchproto_(cp_close_f)
vtp_close(struct pfd *pfd)
{

	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	VTCP_close(&pfd->fd);
}

static const struct cp_methods vtp_methods = {
	.open = vtp_open,
	.close = vtp_close,
};

/*--------------------------------------------------------------------
 * Waiter-handler
 */

static void  v_matchproto_(waiter_handle_f)
tcp_handle(struct waited *w, enum wait_event ev, double now)
{
	struct pfd *pfd;
	struct tcp_pool *tp;

	CAST_OBJ_NOTNULL(pfd, w->priv1, PFD_MAGIC);
	(void)ev;
	(void)now;
	CHECK_OBJ_NOTNULL(pfd->tcp_pool, TCP_POOL_MAGIC);
	tp = pfd->tcp_pool;

	Lck_Lock(&tp->mtx);

	switch (pfd->state) {
	case PFD_STATE_STOLEN:
		pfd->state = PFD_STATE_USED;
		VTAILQ_REMOVE(&tp->connlist, pfd, list);
		AN(pfd->cond);
		AZ(pthread_cond_signal(pfd->cond));
		break;
	case PFD_STATE_AVAIL:
		tp->methods->close(pfd);
		VTAILQ_REMOVE(&tp->connlist, pfd, list);
		tp->n_conn--;
		FREE_OBJ(pfd);
		break;
	case PFD_STATE_CLEANUP:
		tp->methods->close(pfd);
		tp->n_kill--;
		VTAILQ_REMOVE(&tp->killlist, pfd, list);
		memset(pfd, 0x11, sizeof *pfd);
		free(pfd);
		break;
	default:
		WRONG("Wrong pfd state");
	}
	Lck_Unlock(&tp->mtx);
}

/*--------------------------------------------------------------------
 * Reference a TCP pool given by {ip4, ip6} pair.  Create if it
 * doesn't exist already.
 */

struct tcp_pool *
VTP_Ref(const struct suckaddr *ip4, const struct suckaddr *ip6, const void *id)
{
	struct tcp_pool *tp;

	assert(ip4 != NULL || ip6 != NULL);
	Lck_Lock(&tcp_pools_mtx);
	VTAILQ_FOREACH(tp, &tcp_pools, list) {
		assert(tp->refcnt > 0);
		if (tp->id != id)
			continue;
		if (ip4 == NULL) {
			if (tp->ip4 != NULL)
				continue;
		} else {
			if (tp->ip4 == NULL)
				continue;
			if (VSA_Compare(ip4, tp->ip4))
				continue;
		}
		if (ip6 == NULL) {
			if (tp->ip6 != NULL)
				continue;
		} else {
			if (tp->ip6 == NULL)
				continue;
			if (VSA_Compare(ip6, tp->ip6))
				continue;
		}
		tp->refcnt++;
		Lck_Unlock(&tcp_pools_mtx);
		return (tp);
	}
	Lck_Unlock(&tcp_pools_mtx);

	ALLOC_OBJ(tp, TCP_POOL_MAGIC);
	AN(tp);
	tp->methods = &vtp_methods;
	if (ip4 != NULL)
		tp->ip4 = VSA_Clone(ip4);
	if (ip6 != NULL)
		tp->ip6 = VSA_Clone(ip6);
	tp->refcnt = 1;
	tp->id = id;
	Lck_New(&tp->mtx, lck_tcp_pool);
	VTAILQ_INIT(&tp->connlist);
	VTAILQ_INIT(&tp->killlist);

	Lck_Lock(&tcp_pools_mtx);
	VTAILQ_INSERT_HEAD(&tcp_pools, tp, list);
	Lck_Unlock(&tcp_pools_mtx);

	return (tp);
}

/*--------------------------------------------------------------------
 * Add a reference to a tcp_pool
 */

void
VTP_AddRef(struct tcp_pool *tp)
{
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	Lck_Lock(&tcp_pools_mtx);
	assert(tp->refcnt > 0);
	tp->refcnt++;
	Lck_Unlock(&tcp_pools_mtx);
}

/*--------------------------------------------------------------------
 * Release TCP pool, destroy if last reference.
 */

void
VTP_Rel(struct tcp_pool **tpp)
{
	struct tcp_pool *tp;
	struct pfd *pfd, *pfd2;

	TAKE_OBJ_NOTNULL(tp, tpp, TCP_POOL_MAGIC);

	Lck_Lock(&tcp_pools_mtx);
	assert(tp->refcnt > 0);
	if (--tp->refcnt > 0) {
		Lck_Unlock(&tcp_pools_mtx);
		return;
	}
	AZ(tp->n_used);
	VTAILQ_REMOVE(&tcp_pools, tp, list);
	Lck_Unlock(&tcp_pools_mtx);

	free(tp->ip4);
	free(tp->ip6);
	Lck_Lock(&tp->mtx);
	VTAILQ_FOREACH_SAFE(pfd, &tp->connlist, list, pfd2) {
		VTAILQ_REMOVE(&tp->connlist, pfd, list);
		tp->n_conn--;
		assert(pfd->state == PFD_STATE_AVAIL);
		pfd->state = PFD_STATE_CLEANUP;
		(void)shutdown(pfd->fd, SHUT_WR);
		VTAILQ_INSERT_TAIL(&tp->killlist, pfd, list);
		tp->n_kill++;
	}
	while (tp->n_kill) {
		Lck_Unlock(&tp->mtx);
		(void)usleep(20000);
		Lck_Lock(&tp->mtx);
	}
	Lck_Unlock(&tp->mtx);
	Lck_Delete(&tp->mtx);
	AZ(tp->n_conn);
	AZ(tp->n_kill);

	FREE_OBJ(tp);
}

/*--------------------------------------------------------------------
 * Open a new connection from pool.  This is a distinct function since
 * probing cannot use a recycled connection.
 */

int
VTP_Open(const struct tcp_pool *tp, double tmo, const void **privp)
{

	return (vtp_open(tp, tmo, privp));
}

/*--------------------------------------------------------------------
 * Recycle a connection.
 */

void
VTP_Recycle(const struct worker *wrk, struct pfd **pfdp)
{
	struct pfd *pfd;
	struct tcp_pool *tp;
	int i = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	pfd = *pfdp;
	*pfdp = NULL;
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	tp = pfd->tcp_pool;
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	assert(pfd->state == PFD_STATE_USED);
	assert(pfd->fd > 0);

	Lck_Lock(&tp->mtx);
	tp->n_used--;

	pfd->waited->priv1 = pfd;
	pfd->waited->fd = pfd->fd;
	pfd->waited->idle = VTIM_real();
	pfd->state = PFD_STATE_AVAIL;
	pfd->waited->func = tcp_handle;
	pfd->waited->tmo = &cache_param->backend_idle_timeout;
	if (Wait_Enter(wrk->pool->waiter, pfd->waited)) {
		tp->methods->close(pfd);
		memset(pfd, 0x33, sizeof *pfd);
		free(pfd);
		// XXX: stats
		pfd = NULL;
	} else {
		VTAILQ_INSERT_HEAD(&tp->connlist, pfd, list);
		i++;
	}

	if (pfd != NULL)
		tp->n_conn++;
	Lck_Unlock(&tp->mtx);

	if (i && DO_DEBUG(DBG_VTC_MODE)) {
		/*
		 * In varnishtest we do not have the luxury of using
		 * multiple backend connections, so whenever we end up
		 * in the "pending" case, take a short nap to let the
		 * waiter catch up and put the pfd back into circulations.
		 *
		 * In particular ESI:include related tests suffer random
		 * failures without this.
		 *
		 * In normal operation, the only effect is that we will
		 * have N+1 backend connections rather than N, which is
		 * entirely harmless.
		 */
		(void)usleep(10000);
	}
}

/*--------------------------------------------------------------------
 * Close a connection.
 */

void
VTP_Close(struct pfd **pfdp)
{
	struct pfd *pfd;
	struct tcp_pool *tp;

	pfd = *pfdp;
	*pfdp = NULL;
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	tp = pfd->tcp_pool;
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	assert(pfd->fd > 0);

	Lck_Lock(&tp->mtx);
	assert(pfd->state == PFD_STATE_USED || pfd->state == PFD_STATE_STOLEN);
	tp->n_used--;
	if (pfd->state == PFD_STATE_STOLEN) {
		(void)shutdown(pfd->fd, SHUT_RDWR);
		VTAILQ_REMOVE(&tp->connlist, pfd, list);
		pfd->state = PFD_STATE_CLEANUP;
		VTAILQ_INSERT_HEAD(&tp->killlist, pfd, list);
		tp->n_kill++;
	} else {
		assert(pfd->state == PFD_STATE_USED);
		tp->methods->close(pfd);
		memset(pfd, 0x44, sizeof *pfd);
		free(pfd);
	}
	Lck_Unlock(&tp->mtx);
}

/*--------------------------------------------------------------------
 * Get a connection
 */

struct pfd *
VTP_Get(struct tcp_pool *tp, double tmo, struct worker *wrk,
    unsigned force_fresh)
{
	struct pfd *pfd;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	Lck_Lock(&tp->mtx);
	pfd = VTAILQ_FIRST(&tp->connlist);
	CHECK_OBJ_ORNULL(pfd, PFD_MAGIC);
	if (force_fresh || pfd == NULL || pfd->state == PFD_STATE_STOLEN)
		pfd = NULL;
	else {
		assert(pfd->tcp_pool == tp);
		assert(pfd->state == PFD_STATE_AVAIL);
		VTAILQ_REMOVE(&tp->connlist, pfd, list);
		VTAILQ_INSERT_TAIL(&tp->connlist, pfd, list);
		tp->n_conn--;
		VSC_C_main->backend_reuse++;
		pfd->state = PFD_STATE_STOLEN;
		pfd->cond = &wrk->cond;
	}
	tp->n_used++;			// Opening mostly works
	Lck_Unlock(&tp->mtx);

	if (pfd != NULL)
		return (pfd);

	ALLOC_OBJ(pfd, PFD_MAGIC);
	AN(pfd);
	INIT_OBJ(pfd->waited, WAITED_MAGIC);
	pfd->state = PFD_STATE_USED;
	pfd->tcp_pool = tp;
	pfd->fd = tp->methods->open(tp, tmo, &pfd->priv);
	if (pfd->fd < 0) {
		FREE_OBJ(pfd);
		Lck_Lock(&tp->mtx);
		tp->n_used--;		// Nope, didn't work after all.
		Lck_Unlock(&tp->mtx);
	} else
		VSC_C_main->backend_conn++;

	return (pfd);
}

/*--------------------------------------------------------------------
 */

int
VTP_Wait(struct worker *wrk, struct pfd *pfd, double tmo)
{
	struct tcp_pool *tp;
	int r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	tp = pfd->tcp_pool;
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	assert(pfd->cond == &wrk->cond);
	Lck_Lock(&tp->mtx);
	while (pfd->state == PFD_STATE_STOLEN) {
		r = Lck_CondWait(&wrk->cond, &tp->mtx, tmo);
		if (r != 0) {
			if (r == EINTR)
				continue;
			assert(r == ETIMEDOUT);
			Lck_Unlock(&tp->mtx);
			return (1);
		}
	}
	assert(pfd->state == PFD_STATE_USED);
	pfd->cond = NULL;
	Lck_Unlock(&tp->mtx);

	return (0);
}

/*--------------------------------------------------------------------*/

void
VTP_Init(void)
{
	Lck_New(&tcp_pools_mtx, lck_tcp_pool);
}
