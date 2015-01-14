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
 * TCP connection pools for backends
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

struct tcp_pool {
	unsigned		magic;
#define TCP_POOL_MAGIC		0x28b0e42a

	char			*name;
	struct suckaddr		*ip4;
	struct suckaddr		*ip6;

	VTAILQ_ENTRY(tcp_pool)	list;
	int			refcnt;
	struct lock		mtx;

	VTAILQ_HEAD(, vbc)	connlist;
	VTAILQ_HEAD(, vbc)	killlist;

};

static VTAILQ_HEAD(, tcp_pool)	pools = VTAILQ_HEAD_INITIALIZER(pools);

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
	VTAILQ_REMOVE(&pools, tp, list);
	free(tp->name);
	free(tp->ip4);
	free(tp->ip6);
	Lck_Delete(&tp->mtx);
	VTAILQ_FOREACH_SAFE(vbc, &tp->connlist, list, vbc2) {
		VTAILQ_REMOVE(&tp->connlist, vbc, list);
		vbc->backend = NULL;
		(void)close(vbc->fd);
		vbc->fd = -1;
		VBE_ReleaseConn(vbc);
	}
	VTAILQ_FOREACH_SAFE(vbc, &tp->killlist, list, vbc2) {
		VTAILQ_REMOVE(&tp->killlist, vbc, list);
		vbc->backend = NULL;
		(void)close(vbc->fd);
		vbc->fd = -1;
		VBE_ReleaseConn(vbc);
	}
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
VBT_Recycle(struct tcp_pool *tp, struct vbc **vbc)
{

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	CHECK_OBJ_NOTNULL((*vbc), VBC_MAGIC);

	Lck_Lock(&tp->mtx);
	VTAILQ_INSERT_HEAD(&tp->connlist, *vbc, list);
	Lck_Unlock(&tp->mtx);
	*vbc = NULL;
}

/*--------------------------------------------------------------------
 * Get a connection
 */

struct vbc *
VBT_Get(struct tcp_pool *tp)
{
	struct vbc *vbc;
	struct pollfd pfd;

	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);

	Lck_Lock(&tp->mtx);
	vbc = VTAILQ_FIRST(&tp->connlist);
	if (vbc != NULL) {
		CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);

		pfd.fd = vbc->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, 0)) {
			/*
			 * If this vbc is dead assume the rest of the list
			 * has also been chopped from the other end.
			 */
			VSC_C_main->backend_toolate++;
			do {
				VTAILQ_REMOVE(&tp->connlist, vbc, list);
#if 0
				VTAILQ_INSERT_TAIL(&tp->killlist, vbc, list);
#else
				vbc->backend = NULL;
				(void)close(vbc->fd);
				vbc->fd = -1;
				VBE_ReleaseConn(vbc);
#endif
				vbc = VTAILQ_FIRST(&tp->connlist);
			} while (vbc != NULL);
		} else {
			VTAILQ_REMOVE(&tp->connlist, vbc, list);
			VSC_C_main->backend_reuse += 1;
		}
	}
	Lck_Unlock(&tp->mtx);
	if (vbc != NULL)
		return (vbc);
	return (NULL);
}
