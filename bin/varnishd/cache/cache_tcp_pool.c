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

struct tcp_pool {
	unsigned		magic;
#define TCP_POOL_MAGIC		0x28b0e42a

	const void		*id;
	struct suckaddr		*ip4;
	struct suckaddr		*ip6;

	VTAILQ_ENTRY(tcp_pool)	list;
	int			refcnt;
	struct lock		mtx;

	VTAILQ_HEAD(, vtp)	connlist;
	int			n_conn;

	VTAILQ_HEAD(, vtp)	killlist;
	int			n_kill;

	int			n_used;
};

static struct lock		tcp_pools_mtx;
static VTAILQ_HEAD(, tcp_pool)	tcp_pools = VTAILQ_HEAD_INITIALIZER(tcp_pools);

/*--------------------------------------------------------------------
 * Waiter-handler
 */

static void  v_matchproto_(waiter_handle_f)
tcp_handle(struct waited *w, enum wait_event ev, double now)
{
	struct vtp *vtp;
	struct tcp_pool *tp;

	CAST_OBJ_NOTNULL(vtp, w->priv1, VTP_MAGIC);
	(void)ev;
	(void)now;
	CHECK_OBJ_NOTNULL(vtp->tcp_pool, TCP_POOL_MAGIC);
	tp = vtp->tcp_pool;

	Lck_Lock(&tp->mtx);

	switch (vtp->state) {
	case VTP_STATE_STOLEN:
		vtp->state = VTP_STATE_USED;
		VTAILQ_REMOVE(&tp->connlist, vtp, list);
		AN(vtp->cond);
		AZ(pthread_cond_signal(vtp->cond));
		break;
	case VTP_STATE_AVAIL:
		VTCP_close(&vtp->fd);
		VTAILQ_REMOVE(&tp->connlist, vtp, list);
		tp->n_conn--;
		FREE_OBJ(vtp);
		break;
	case VTP_STATE_CLEANUP:
		VTCP_close(&vtp->fd);
		tp->n_kill--;
		VTAILQ_REMOVE(&tp->killlist, vtp, list);
		memset(vtp, 0x11, sizeof *vtp);
		free(vtp);
		break;
	default:
		WRONG("Wrong vtp state");
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
	struct vtp *vtp, *vtp2;

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
	VTAILQ_FOREACH_SAFE(vtp, &tp->connlist, list, vtp2) {
		VTAILQ_REMOVE(&tp->connlist, vtp, list);
		tp->n_conn--;
		assert(vtp->state == VTP_STATE_AVAIL);
		vtp->state = VTP_STATE_CLEANUP;
		(void)shutdown(vtp->fd, SHUT_WR);
		VTAILQ_INSERT_TAIL(&tp->killlist, vtp, list);
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
VTP_Open(const struct tcp_pool *tp, double tmo, const struct suckaddr **sa)
{
	int s;
	int msec;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	msec = (int)floor(tmo * 1000.0);
	if (cache_param->prefer_ipv6) {
		*sa = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
		if (s >= 0)
			return (s);
	}
	*sa = tp->ip4;
	s = VTCP_connect(tp->ip4, msec);
	if (s >= 0)
		return (s);
	if (!cache_param->prefer_ipv6) {
		*sa = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
	}
	return (s);
}

/*--------------------------------------------------------------------
 * Recycle a connection.
 */

void
VTP_Recycle(const struct worker *wrk, struct vtp **vtpp)
{
	struct vtp *vtp;
	struct tcp_pool *tp;
	int i = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	vtp = *vtpp;
	*vtpp = NULL;
	CHECK_OBJ_NOTNULL(vtp, VTP_MAGIC);
	tp = vtp->tcp_pool;
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	assert(vtp->state == VTP_STATE_USED);
	assert(vtp->fd > 0);

	Lck_Lock(&tp->mtx);
	tp->n_used--;

	vtp->waited->priv1 = vtp;
	vtp->waited->fd = vtp->fd;
	vtp->waited->idle = VTIM_real();
	vtp->state = VTP_STATE_AVAIL;
	vtp->waited->func = tcp_handle;
	vtp->waited->tmo = &cache_param->backend_idle_timeout;
	if (Wait_Enter(wrk->pool->waiter, vtp->waited)) {
		VTCP_close(&vtp->fd);
		memset(vtp, 0x33, sizeof *vtp);
		free(vtp);
		// XXX: stats
		vtp = NULL;
	} else {
		VTAILQ_INSERT_HEAD(&tp->connlist, vtp, list);
		i++;
	}

	if (vtp != NULL)
		tp->n_conn++;
	Lck_Unlock(&tp->mtx);

	if (i && DO_DEBUG(DBG_VTC_MODE)) {
		/*
		 * In varnishtest we do not have the luxury of using
		 * multiple backend connections, so whenever we end up
		 * in the "pending" case, take a short nap to let the
		 * waiter catch up and put the vtp back into circulations.
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
VTP_Close(struct vtp **vtpp)
{
	struct vtp *vtp;
	struct tcp_pool *tp;

	vtp = *vtpp;
	*vtpp = NULL;
	CHECK_OBJ_NOTNULL(vtp, VTP_MAGIC);
	tp = vtp->tcp_pool;
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	assert(vtp->fd > 0);

	Lck_Lock(&tp->mtx);
	assert(vtp->state == VTP_STATE_USED || vtp->state == VTP_STATE_STOLEN);
	tp->n_used--;
	if (vtp->state == VTP_STATE_STOLEN) {
		(void)shutdown(vtp->fd, SHUT_RDWR);
		VTAILQ_REMOVE(&tp->connlist, vtp, list);
		vtp->state = VTP_STATE_CLEANUP;
		VTAILQ_INSERT_HEAD(&tp->killlist, vtp, list);
		tp->n_kill++;
	} else {
		assert(vtp->state == VTP_STATE_USED);
		VTCP_close(&vtp->fd);
		memset(vtp, 0x44, sizeof *vtp);
		free(vtp);
	}
	Lck_Unlock(&tp->mtx);
}

/*--------------------------------------------------------------------
 * Get a connection
 */

struct vtp *
VTP_Get(struct tcp_pool *tp, double tmo, struct worker *wrk,
    unsigned force_fresh)
{
	struct vtp *vtp;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	Lck_Lock(&tp->mtx);
	vtp = VTAILQ_FIRST(&tp->connlist);
	CHECK_OBJ_ORNULL(vtp, VTP_MAGIC);
	if (force_fresh || vtp == NULL || vtp->state == VTP_STATE_STOLEN)
		vtp = NULL;
	else {
		assert(vtp->tcp_pool == tp);
		assert(vtp->state == VTP_STATE_AVAIL);
		VTAILQ_REMOVE(&tp->connlist, vtp, list);
		VTAILQ_INSERT_TAIL(&tp->connlist, vtp, list);
		tp->n_conn--;
		VSC_C_main->backend_reuse++;
		vtp->state = VTP_STATE_STOLEN;
		vtp->cond = &wrk->cond;
	}
	tp->n_used++;			// Opening mostly works
	Lck_Unlock(&tp->mtx);

	if (vtp != NULL)
		return (vtp);

	ALLOC_OBJ(vtp, VTP_MAGIC);
	AN(vtp);
	INIT_OBJ(vtp->waited, WAITED_MAGIC);
	vtp->state = VTP_STATE_USED;
	vtp->tcp_pool = tp;
	vtp->fd = VTP_Open(tp, tmo, &vtp->addr);
	if (vtp->fd < 0) {
		FREE_OBJ(vtp);
		Lck_Lock(&tp->mtx);
		tp->n_used--;		// Nope, didn't work after all.
		Lck_Unlock(&tp->mtx);
	} else
		VSC_C_main->backend_conn++;

	return (vtp);
}

/*--------------------------------------------------------------------
 */

int
VTP_Wait(struct worker *wrk, struct vtp *vtp, double tmo)
{
	struct tcp_pool *tp;
	int r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vtp, VTP_MAGIC);
	tp = vtp->tcp_pool;
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	assert(vtp->cond == &wrk->cond);
	Lck_Lock(&tp->mtx);
	while (vtp->state == VTP_STATE_STOLEN) {
		r = Lck_CondWait(&wrk->cond, &tp->mtx, tmo);
		if (r != 0) {
			if (r == EINTR)
				continue;
			assert(r == ETIMEDOUT);
			Lck_Unlock(&tp->mtx);
			return (1);
		}
	}
	assert(vtp->state == VTP_STATE_USED);
	vtp->cond = NULL;
	Lck_Unlock(&tp->mtx);

	return (0);
}

/*--------------------------------------------------------------------*/

void
VTP_Init(void)
{
	Lck_New(&tcp_pools_mtx, lck_tcp_pool);
}
