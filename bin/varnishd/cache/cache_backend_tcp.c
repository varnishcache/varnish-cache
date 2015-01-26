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
#include "vtim.h"


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
	AN(vbc->in_waiter);
VSL(SLT_Debug, 0, "------> Handler fd %d in_w %d state %d ev %d stolen %d", vbc->fd, vbc->in_waiter, vbc->state, ev, vbc->stolen);

	switch(vbc->state) {
	case VBC_STATE_AVAIL:
		if (ev != WAITER_ACTION || !vbc->stolen) {
VSL(SLT_Debug, 0, "------> Handler avail + !action -> close");
			VTCP_close(&vbc->fd);
			VTAILQ_REMOVE(&tp->connlist, vbc, list);
			tp->n_conn--;
			FREE_OBJ(vbc);
		} else {
VSL(SLT_Debug, 0, "------> Handler avail + action -> re-wait");
			vbc->stolen = 0;
			if (Wait_Enter(tp->waiter, vbc->waited)) {
VSL(SLT_Debug, 0, "------> Handler avail + !timeout -> re-wait failed");
				VTCP_close(&vbc->fd);
				VTAILQ_REMOVE(&tp->connlist, vbc, list);
				tp->n_conn--;
				FREE_OBJ(vbc);
			}
		}
		break;
	case VBC_STATE_USED:
VSL(SLT_Debug, 0, "------> Handler used");
		vbc->in_waiter = 0;
		break;
	case VBC_STATE_CLEANUP:
VSL(SLT_Debug, 0, "------> Handler cleanup");
		assert(vbc->fd < 0);
		tp->n_kill--;
		VTAILQ_REMOVE(&tp->killlist, vbc, list);
		FREE_OBJ(vbc);
		break;
	default:
		WRONG("Wrong vbc state");
	}
	Lck_Unlock(&tp->mtx);
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
	Lck_Lock(&tp->mtx);
	VTAILQ_FOREACH_SAFE(vbc, &tp->connlist, list, vbc2) {
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		tp->n_conn--;
		vbc->state = VBC_STATE_CLEANUP;
		VTCP_close(&vbc->fd);
		if (vbc->in_waiter) {
			VTAILQ_INSERT_TAIL(&tp->killlist, vbc, list);
			tp->n_kill++;
		} else {
			FREE_OBJ(vbc);
		}
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

void
VBT_Recycle(struct tcp_pool *tp, struct vbc **vbcp)
{
	struct vbc *vbc;
	int i = 0;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	vbc = *vbcp;
	*vbcp = NULL;
	CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);

	assert(vbc->state == VBC_STATE_USED);
	assert(vbc->fd > 0);

	Lck_Lock(&tp->mtx);
	tp->n_used--;

VSL(SLT_Debug, 0, "------> Recycle fd %d in_w %d", vbc->fd, vbc->in_waiter);

	if (!vbc->in_waiter) {
		vbc->in_waiter = 1;
		vbc->waited->ptr = vbc;
		vbc->waited->fd = vbc->fd;
		vbc->waited->idle = VTIM_real();
VSL(SLT_Debug, 0, "------> Recycle fd %d Wait_Enter", vbc->fd);
		if (Wait_Enter(tp->waiter, vbc->waited)) {
			VTCP_close(&vbc->fd);
			FREE_OBJ(vbc);
		}
		i = 1;
	}

	if (vbc != NULL) {
		vbc->state = VBC_STATE_AVAIL;
		vbc->stolen = 1;
		VTAILQ_INSERT_HEAD(&tp->connlist, vbc, list);
		tp->n_conn++;
		vbc->recycled = 1;
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

	assert(vbc->state == VBC_STATE_USED);
	assert(vbc->fd > 0);
VSL(SLT_Debug, 0, "------> Close fd %d in_w %d", vbc->fd, vbc->in_waiter);

	Lck_Lock(&tp->mtx);
	VTCP_close(&vbc->fd);
	tp->n_used--;
	if (vbc->in_waiter) {
		vbc->state = VBC_STATE_CLEANUP;
		VTAILQ_INSERT_HEAD(&tp->killlist, vbc, list);
		tp->n_kill++;
	} else {
		FREE_OBJ(vbc);
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

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	Lck_Lock(&tp->mtx);
	vbc = VTAILQ_FIRST(&tp->connlist);
	if (vbc != NULL) {
		CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);

		assert(vbc->state == VBC_STATE_AVAIL);
VSL(SLT_Debug, 0, "------> Steal fd %d", vbc->fd);

		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		tp->n_conn--;
		VSC_C_main->backend_reuse += 1;
		vbc->state = VBC_STATE_USED;
	}
	tp->n_used++;			// Opening mostly works
	Lck_Unlock(&tp->mtx);

	if (vbc != NULL)
		return (vbc);

	ALLOC_OBJ(vbc, VBC_MAGIC);
	AN(vbc);
	INIT_OBJ(vbc->waited, WAITED_MAGIC);
	vbc->state = VBC_STATE_USED;
	vbc->fd = VBT_Open(tp, tmo, &vbc->addr);
	if (vbc->fd < 0)
		FREE_OBJ(vbc);
	if (vbc == NULL) {
VSL(SLT_Debug, 0, "------> No new fd");
		Lck_Lock(&tp->mtx);
		tp->n_used--;		// Nope, didn't work after all.
		Lck_Unlock(&tp->mtx);
	} else
VSL(SLT_Debug, 0, "------> New fd %d", vbc->fd);
	return (vbc);
}
