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

#include <math.h>
#include <poll.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "cache.h"

#include "cache_backend.h"
#include "vrt.h"
#include "vtcp.h"
#include "vsa.h"
#include "waiter/waiter.h"

struct tcp_pool {
	unsigned		magic;
#define TCP_POOL_MAGIC		0x28b0e42a

	char			*name;
	struct suckaddr		*ip4;
	struct suckaddr		*ip6;

	VTAILQ_ENTRY(tcp_pool)	list;
	int			refcnt;
	struct lock		mtx;

	struct waiter		*waiter;
	volatile double		timeout;

	VTAILQ_HEAD(, vbc)	connlist;
	int			n_conn;

	VTAILQ_HEAD(, vbc)	killlist;
	int			n_kill;

	VTAILQ_HEAD(, vbc)	pendlist;
	int			n_pend;

	int			n_used;

};

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
	tp = vbc->backend->tcp_pool;			// NB: Incestous

	Lck_Lock(&tp->mtx);
	switch (vbc->in_waiter) {
	case VBC_W_KILL:
VSL(SLT_Debug, 0, "==========> Handle %s fd %d iw %d ev %d KILL",
    vbc->backend->vcl_name, vbc->fd, vbc->in_waiter, ev);
		assert(vbc->fd < 0);
		tp->n_kill--;
		VTAILQ_REMOVE(&tp->killlist, vbc, list);
		FREE_OBJ(vbc);
		break;
	case VBC_W_PENDING:
VSL(SLT_Debug, 0, "==========> Handle %s fd %d iw %d ev %d PENDING",
    vbc->backend->vcl_name, vbc->fd, vbc->in_waiter, ev);
		vbc->in_waiter = VBC_W_NOWAIT;
		VTAILQ_REMOVE(&tp->pendlist, vbc, list);
		tp->n_pend--;
		break;
	case VBC_W_STOLEN:
VSL(SLT_Debug, 0, "==========> Handle %s fd %d iw %d ev %d STOLEN",
    vbc->backend->vcl_name, vbc->fd, vbc->in_waiter, ev);
		vbc->in_waiter = VBC_W_NOWAIT;
		vbc = NULL;
		break;
	case VBC_W_INWAIT:
VSL(SLT_Debug, 0, "==========> Handle %s fd %d iw %d ev %d INWAIT",
    vbc->backend->vcl_name, vbc->fd, vbc->in_waiter, ev);
		VTCP_close(&vbc->fd);
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		tp->n_conn--;
		FREE_OBJ(vbc);
		break;
	default:
		WRONG("Wrong vbc in_wait state");
	}
	Lck_Unlock(&tp->mtx);
	if (vbc != NULL)
		VBT_Recycle(tp, &vbc);
}

/*--------------------------------------------------------------------
 * Reference a TCP pool given by {name, ip4, ip6} triplet.  Create if
 * it doesn't exist already.
 */

struct tcp_pool *
VBT_Ref(const char *name, const struct suckaddr *ip4,
    const struct suckaddr *ip6)
{
	struct tcp_pool *tp;

	ASSERT_CLI();
	VTAILQ_FOREACH(tp, &pools, list) {
		assert(tp->refcnt > 0);
		if (strcmp(tp->name, name))
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
		return (tp);
	}

	ALLOC_OBJ(tp, TCP_POOL_MAGIC);
	AN(tp);
	REPLACE(tp->name, name);
	if (ip4 != NULL)
		tp->ip4 = VSA_Clone(ip4);
	if (ip6 != NULL)
		tp->ip6 = VSA_Clone(ip6);
	tp->refcnt = 1;
	Lck_New(&tp->mtx, lck_backend);
	VTAILQ_INIT(&tp->connlist);
	VTAILQ_INIT(&tp->killlist);
	VTAILQ_INSERT_HEAD(&pools, tp, list);
	tp->timeout = 60;
	tp->waiter = Wait_New(tcp_handle, &tp->timeout);
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
	assert(tp->refcnt > 0);
	if (--tp->refcnt > 0)
		return;
	AZ(tp->n_used);
	VTAILQ_REMOVE(&pools, tp, list);
	free(tp->name);
	free(tp->ip4);
	free(tp->ip6);
	Lck_Delete(&tp->mtx);
	VTAILQ_FOREACH_SAFE(vbc, &tp->connlist, list, vbc2) {
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		tp->n_conn--;
		vbc->in_waiter = VBC_W_STOLEN;
		VTCP_close(&vbc->fd);
		FREE_OBJ(vbc);
	}
	VTAILQ_FOREACH_SAFE(vbc, &tp->killlist, list, vbc2) {
		VTAILQ_REMOVE(&tp->killlist, vbc, list);
		tp->n_kill--;
		assert(vbc->in_waiter == VBC_W_STOLEN);	// XXX ?
		VTCP_close(&vbc->fd);
		FREE_OBJ(vbc);
	}
	AZ(tp->n_conn);
	AZ(tp->n_kill);
	Wait_Destroy(&tp->waiter);

	FREE_OBJ(tp);
}

/*--------------------------------------------------------------------
 * Open a new connection from pool.  This is a distinct function since
 * probing cannot use a recycled connection.
 */

int
VBT_Open(struct tcp_pool *tp, double tmo, const struct suckaddr **sa)
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

#include "vtim.h"

void
VBT_Recycle(struct tcp_pool *tp, struct vbc **vbcp)
{
	struct vbc *vbc;
	int i = 0;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	vbc = *vbcp;
	*vbcp = NULL;
	CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);

	Lck_Lock(&tp->mtx);
	assert(vbc->fd > 0);
	tp->n_used--;

VSL(SLT_Debug, 0, "------> Recycle fd %d in_w %d", vbc->fd, vbc->in_waiter);
	switch (vbc->in_waiter) {
	case VBC_W_NEW:
	case VBC_W_NOWAIT:
		vbc->in_waiter = VBC_W_INWAIT;
		vbc->waited->ptr = vbc;
		vbc->waited->fd = vbc->fd;
		vbc->waited->idle = VTIM_real();
VSL(SLT_Debug, 0, "------> Recycle fd %d Enter", vbc->fd);
		if (Wait_Enter(tp->waiter, vbc->waited)) {
			VTCP_close(&vbc->fd);
			FREE_OBJ(vbc);
		} else {
			VTAILQ_INSERT_HEAD(&tp->connlist, vbc, list);
			tp->n_conn++;
			vbc->recycled = 1;
		}
		break;
	case VBC_W_STOLEN:
		/*
		 * We stole the fd from the waiter and it hasn't noticed
		 * this yet.
		 */
VSL(SLT_Debug, 0, "------> Recycle fd %d Still Stolen -> Pending", vbc->fd);
		vbc->in_waiter = VBC_W_PENDING;
		VTAILQ_INSERT_HEAD(&tp->pendlist, vbc, list);
		tp->n_pend++;
		i = 1;
		break;
	default:
		WRONG("Wrong vbc in_wait state");
	}
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

	VTCP_close(&vbc->fd);

	Lck_Lock(&tp->mtx);
	tp->n_used--;
	switch (vbc->in_waiter) {
	case VBC_W_NEW:
	case VBC_W_NOWAIT:
		FREE_OBJ(vbc);
		break;
	case VBC_W_STOLEN:
		vbc->in_waiter = VBC_W_KILL;
		VTAILQ_INSERT_HEAD(&tp->killlist, vbc, list);
		tp->n_kill++;
		break;
	default:
		WRONG("Wrong vbc in_waiter state");
	}
	Lck_Unlock(&tp->mtx);
}

/*--------------------------------------------------------------------
 * Get a connection
 */

struct vbc *
VBT_Get(struct tcp_pool *tp, double tmo)
{
	struct vbc *vbc;
	struct pollfd pfd;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	Lck_Lock(&tp->mtx);
	vbc = VTAILQ_FIRST(&tp->connlist);
	if (vbc != NULL) {
		CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);

		assert(vbc->in_waiter == VBC_W_INWAIT);
VSL(SLT_Debug, 0, "------> Steal fd %d", vbc->fd);
		Wait_Steal(tp->waiter, vbc->waited);
		vbc->in_waiter = VBC_W_STOLEN;
		pfd.fd = vbc->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (0 && poll(&pfd, 1, 0)) {	// XXX
			/*
			 * If this vbc is dead assume the rest of the list
			 * has also been chopped from the other end.
			 * XXX: Not sure if this makes any sense with waiter
			 */
			VSC_C_main->backend_toolate++;
			do {
				VTAILQ_REMOVE(&tp->connlist, vbc, list);
				tp->n_conn--;
				VTCP_close(&vbc->fd);
				vbc->in_waiter = VBC_W_KILL;
				VTAILQ_INSERT_TAIL(&tp->killlist, vbc, list);
				tp->n_kill++;
				vbc = VTAILQ_FIRST(&tp->connlist);
			} while (vbc != NULL);
		} else {
			VTAILQ_REMOVE(&tp->connlist, vbc, list);
			tp->n_conn--;
			VSC_C_main->backend_reuse += 1;
		}
	}
	tp->n_used++;			// Opening mostly works
	Lck_Unlock(&tp->mtx);

	if (vbc != NULL)
		return (vbc);

	ALLOC_OBJ(vbc, VBC_MAGIC);
	AN(vbc);
	INIT_OBJ(vbc->waited, WAITED_MAGIC);
	vbc->in_waiter = VBC_W_NEW;
	if (vbc != NULL) {
		vbc->fd = VBT_Open(tp, tmo, &vbc->addr);
		if (vbc->fd < 0)
			FREE_OBJ(vbc);
	}
	if (vbc == NULL) {
		Lck_Lock(&tp->mtx);
		tp->n_used--;		// Nope, didn't work after all.
		Lck_Unlock(&tp->mtx);
	}
	return (vbc);
}
