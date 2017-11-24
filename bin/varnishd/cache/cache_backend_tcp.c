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
 * These are really a lot more general than just backends, but backends
 * are all we use them for, so they live here for now.
 *
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>

#include "cache.h"

#include "vrt.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

#include "cache_director.h"
#include "cache_backend.h"
#include "cache_pool.h"

struct tcp_pool {
	unsigned		magic;
#define TCP_POOL_MAGIC		0x28b0e42a

	char			*name;
	struct suckaddr		*ip4;
	struct suckaddr		*ip6;

	VTAILQ_ENTRY(tcp_pool)	list;
	int			refcnt;
	struct lock		mtx;

	struct waitfor		waitfor;

	VTAILQ_HEAD(, vbc)	connlist;
	int			n_conn;

	VTAILQ_HEAD(, vbc)	killlist;
	int			n_kill;

	int			n_used;

};

static struct lock		pools_mtx;
static VTAILQ_HEAD(, tcp_pool)	pools = VTAILQ_HEAD_INITIALIZER(pools);

/*--------------------------------------------------------------------
 * Waiter-handler
 */

static void  __match_proto__(waiter_handle_f)
tcp_handle(struct waited *w, enum wait_event ev, double now)
{
	struct vbc *vbc;
	struct tcp_pool *tp;

	CAST_OBJ_NOTNULL(vbc, w->ptr, VBC_MAGIC);
	(void)ev;
	(void)now;
	CHECK_OBJ_NOTNULL(vbc->tcp_pool, TCP_POOL_MAGIC);
	tp = vbc->tcp_pool;

	Lck_Lock(&tp->mtx);

	switch(vbc->state) {
	case VBC_STATE_STOLEN:
		vbc->state = VBC_STATE_USED;
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		AN(vbc->cond);
		AZ(pthread_cond_signal(vbc->cond));
		break;
	case VBC_STATE_AVAIL:
		VTCP_close(&vbc->fd);
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		tp->n_conn--;
		FREE_OBJ(vbc);
		break;
	case VBC_STATE_CLEANUP:
		VTCP_close(&vbc->fd);
		tp->n_kill--;
		VTAILQ_REMOVE(&tp->killlist, vbc, list);
		memset(vbc, 0x11, sizeof *vbc);
		free(vbc);
		break;
	default:
		WRONG("Wrong vbc state");
	}
	Lck_Unlock(&tp->mtx);
}

/*--------------------------------------------------------------------
 * Reference a TCP pool given by {ip4, ip6} pair.  Create if it
 * doesn't exist already.
 */

struct tcp_pool *
VBT_Ref(const struct suckaddr *ip4, const struct suckaddr *ip6)
{
	struct tcp_pool *tp;

	Lck_Lock(&pools_mtx);
	VTAILQ_FOREACH(tp, &pools, list) {
		assert(tp->refcnt > 0);
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
		Lck_Unlock(&pools_mtx);
		return (tp);
	}
	Lck_Unlock(&pools_mtx);

	ALLOC_OBJ(tp, TCP_POOL_MAGIC);
	AN(tp);
	if (ip4 != NULL)
		tp->ip4 = VSA_Clone(ip4);
	if (ip6 != NULL)
		tp->ip6 = VSA_Clone(ip6);
	tp->refcnt = 1;
	Lck_New(&tp->mtx, lck_backend_tcp);
	VTAILQ_INIT(&tp->connlist);
	VTAILQ_INIT(&tp->killlist);
	INIT_OBJ(&tp->waitfor, WAITFOR_MAGIC);
	tp->waitfor.func = tcp_handle;
	tp->waitfor.tmo = &cache_param->backend_idle_timeout;

	Lck_Lock(&pools_mtx);
	VTAILQ_INSERT_HEAD(&pools, tp, list);
	Lck_Unlock(&pools_mtx);

	return (tp);
}

/*--------------------------------------------------------------------
 * Release TCP pool, destroy if last reference.
 */

void
VBT_Rel(struct tcp_pool **tpp)
{
	struct tcp_pool *tp;
	struct vbc *vbc, *vbc2;

	AN(tpp);
	tp = *tpp;
	*tpp = NULL;
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	Lck_Lock(&pools_mtx);
	assert(tp->refcnt > 0);
	if (--tp->refcnt > 0) {
		Lck_Unlock(&pools_mtx);
		return;
	}
	AZ(tp->n_used);
	VTAILQ_REMOVE(&pools, tp, list);
	Lck_Unlock(&pools_mtx);

	free(tp->name);
	free(tp->ip4);
	free(tp->ip6);
	Lck_Lock(&tp->mtx);
	VTAILQ_FOREACH_SAFE(vbc, &tp->connlist, list, vbc2) {
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		tp->n_conn--;
		assert(vbc->state == VBC_STATE_AVAIL);
		vbc->state = VBC_STATE_CLEANUP;
		(void)shutdown(vbc->fd, SHUT_WR);
		VTAILQ_INSERT_TAIL(&tp->killlist, vbc, list);
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
VBT_Open(const struct tcp_pool *tp, double tmo, const struct suckaddr **sa)
{
	int s;
	int msec;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	msec = (int)floor(tmo * 1000.0);
	if (cache_param->prefer_ipv6) {
		*sa = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
		if (s >= 0)
			return(s);
	}
	*sa = tp->ip4;
	s = VTCP_connect(tp->ip4, msec);
	if (s < 0 && !cache_param->prefer_ipv6) {
		*sa = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
	}
	return(s);
}

/*--------------------------------------------------------------------
 * Recycle a connection.
 */

void
VBT_Recycle(const struct worker *wrk, struct tcp_pool *tp, struct vbc **vbcp)
{
	struct vbc *vbc;
	int i = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	vbc = *vbcp;
	*vbcp = NULL;
	CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);

	assert(vbc->state == VBC_STATE_USED);
	assert(vbc->fd > 0);

	Lck_Lock(&tp->mtx);
	tp->n_used--;

	vbc->waited->ptr = vbc;
	vbc->waited->fd = vbc->fd;
	vbc->waited->idle = VTIM_real();
	vbc->state = VBC_STATE_AVAIL;
	vbc->waited->waitfor =  &tp->waitfor;
	if (Wait_Enter(wrk->pool->waiter, vbc->waited)) {
		VTCP_close(&vbc->fd);
		memset(vbc, 0x33, sizeof *vbc);
		free(vbc);
		// XXX: stats
		vbc = NULL;
	} else {
		VTAILQ_INSERT_HEAD(&tp->connlist, vbc, list);
		i++;
	}

	if (vbc != NULL)
		tp->n_conn++;
	Lck_Unlock(&tp->mtx);

	if (i && DO_DEBUG(DBG_VTC_MODE)) {
		/*
		 * In varnishtest we do not have the luxury of using
		 * multiple backend connections, so whenever we end up
		 * in the "pending" case, take a short nap to let the
		 * waiter catch up and put the vbc back into circulations.
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
VBT_Close(struct tcp_pool *tp, struct vbc **vbcp)
{
	struct vbc *vbc;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	vbc = *vbcp;
	*vbcp = NULL;
	CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);

	assert(vbc->fd > 0);

	Lck_Lock(&tp->mtx);
	assert(vbc->state == VBC_STATE_USED || vbc->state == VBC_STATE_STOLEN);
	tp->n_used--;
	if (vbc->state == VBC_STATE_STOLEN) {
		(void)shutdown(vbc->fd, SHUT_RDWR);
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		vbc->state = VBC_STATE_CLEANUP;
		VTAILQ_INSERT_HEAD(&tp->killlist, vbc, list);
		tp->n_kill++;
	} else {
		assert(vbc->state == VBC_STATE_USED);
		VTCP_close(&vbc->fd);
		memset(vbc, 0x44, sizeof *vbc);
		free(vbc);
	}
	Lck_Unlock(&tp->mtx);
}

/*--------------------------------------------------------------------
 * Get a connection
 */

struct vbc *
VBT_Get(struct tcp_pool *tp, double tmo, const struct backend *be,
    struct worker *wrk, unsigned force_fresh)
{
	struct vbc *vbc;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	Lck_Lock(&tp->mtx);
	vbc = VTAILQ_FIRST(&tp->connlist);
	CHECK_OBJ_ORNULL(vbc, VBC_MAGIC);
	if (force_fresh || vbc == NULL || vbc->state == VBC_STATE_STOLEN)
		vbc = NULL;
	else {
		assert(vbc->tcp_pool == tp);
		assert(vbc->state == VBC_STATE_AVAIL);
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		VTAILQ_INSERT_TAIL(&tp->connlist, vbc, list);
		tp->n_conn--;
		VSC_C_main->backend_reuse += 1;
		vbc->state = VBC_STATE_STOLEN;
		vbc->cond = &wrk->cond;
	}
	tp->n_used++;			// Opening mostly works
	Lck_Unlock(&tp->mtx);

	if (vbc != NULL)
		return (vbc);

	ALLOC_OBJ(vbc, VBC_MAGIC);
	AN(vbc);
	INIT_OBJ(vbc->waited, WAITED_MAGIC);
	vbc->state = VBC_STATE_USED;
	vbc->tcp_pool = tp;
	vbc->fd = VBT_Open(tp, tmo, &vbc->addr);
	if (vbc->fd < 0)
		FREE_OBJ(vbc);
	if (vbc == NULL) {
		Lck_Lock(&tp->mtx);
		tp->n_used--;		// Nope, didn't work after all.
		Lck_Unlock(&tp->mtx);
	} else
		VSC_C_main->backend_conn++;

	return (vbc);
}

/*--------------------------------------------------------------------
 */

int
VBT_Wait(struct worker *wrk, struct vbc *vbc, double tmo)
{
	struct tcp_pool *tp;
	int r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);
	tp = vbc->tcp_pool;
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	assert(vbc->cond == &wrk->cond);
	Lck_Lock(&tp->mtx);

	while (vbc->state == VBC_STATE_STOLEN) {
		r = Lck_CondWait(&wrk->cond, &tp->mtx, tmo);
		if (r != 0) {
			if (r == EINTR)
				continue;
			assert(r == ETIMEDOUT);
			Lck_Unlock(&tp->mtx);
			return (1);
		}
	}

	assert(vbc->state == VBC_STATE_USED);
	vbc->cond = NULL;
	Lck_Unlock(&tp->mtx);

	return (0);
}

/*--------------------------------------------------------------------*/

void
VBT_Init(void)
{
	Lck_New(&pools_mtx, lck_backend);
}
