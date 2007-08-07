/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 * $Id$
 *
 * Manage backend connections.
 *
 * XXX: When we switch VCL we can have vbe_conn's dangling from
 * XXX: the backends no longer used.  When the VCL's refcount
 * XXX: drops to zero we should zap them.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

/* A backend IP */

static TAILQ_HEAD(,vbe_conn) vbe_head = TAILQ_HEAD_INITIALIZER(vbe_head);
static TAILQ_HEAD(,bereq) bereq_head = TAILQ_HEAD_INITIALIZER(bereq_head);

static MTX vbemtx;

/*--------------------------------------------------------------------*/

struct bereq *
vbe_new_bereq(void)
{
	struct bereq *bereq;
	volatile unsigned len;

	LOCK(&vbemtx);
	bereq = TAILQ_FIRST(&bereq_head);
	if (bereq != NULL)
		TAILQ_REMOVE(&bereq_head, bereq, list);
	UNLOCK(&vbemtx);
	if (bereq != NULL) {
		CHECK_OBJ(bereq, BEREQ_MAGIC);
	} else {
		len =  params->mem_workspace;
		bereq = calloc(sizeof *bereq + len, 1);
		if (bereq == NULL)
			return (NULL);
		bereq->magic = BEREQ_MAGIC;
		bereq->space = bereq + 1;
		bereq->len = len;
	}
	http_Setup(bereq->http, bereq->space, bereq->len);
	return (bereq);
}

/*--------------------------------------------------------------------*/
/* XXX: no backpressure on pool size */

void
vbe_free_bereq(struct bereq *bereq)
{

	CHECK_OBJ_NOTNULL(bereq, BEREQ_MAGIC);
	LOCK(&vbemtx);
	TAILQ_INSERT_HEAD(&bereq_head, bereq, list);
	UNLOCK(&vbemtx);
}

/*--------------------------------------------------------------------*/

static struct vbe_conn *
vbe_new_conn(void)
{
	struct vbe_conn *vbc;

	vbc = calloc(sizeof *vbc, 1);
	if (vbc == NULL)
		return (NULL);
	VSL_stats->n_vbe_conn++;
	vbc->magic = VBE_CONN_MAGIC;
	vbc->fd = -1;
	return (vbc);
}

/*--------------------------------------------------------------------
 * XXX: There is a race here, we need to lock the replacement of the
 * XXX: resolved addresses, or some other thread might try to access
 * XXX: them while/during/after we changed them.
 * XXX: preferably, we should make a copy to the vbe while we hold a
 * XXX: lock anyway.
 */

static void
vbe_lookup(struct backend *bp)
{
	struct addrinfo *res, hint, *old;
	int error;

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(bp->hostname,
	    bp->portname == NULL ? "http" : bp->portname,
	    &hint, &res);
	bp->dnstime = TIM_mono();
	if (error) {
		if (res != NULL)
			freeaddrinfo(res);
		printf("getaddrinfo: %s\n", gai_strerror(error)); /* XXX */
		return;
	}
	old = bp->addr;
	bp->last_addr = res;
	bp->addr = res;
	if (old != NULL)
		freeaddrinfo(old);
}

/*--------------------------------------------------------------------*/

static int
vbe_sock_conn(const struct addrinfo *ai)
{
	int s;

	s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (s >= 0 && connect(s, ai->ai_addr, ai->ai_addrlen)) {
		AZ(close(s));
		s = -1;
	}
	return (s);
}

/*--------------------------------------------------------------------*/

static int
vbe_conn_try(struct backend *bp, struct addrinfo **pai)
{
	struct addrinfo *ai;
	int s;

	/* First try the cached good address, and any following it */
	for (ai = bp->last_addr; ai != NULL; ai = ai->ai_next) {
		s = vbe_sock_conn(ai);
		if (s >= 0) {
			bp->last_addr = ai;
			*pai = ai;
			return (s);
		}
	}

	/* Then try the list until the cached last good address */
	for (ai = bp->addr; ai != bp->last_addr; ai = ai->ai_next) {
		s = vbe_sock_conn(ai);
		if (s >= 0) {
			bp->last_addr = ai;
			*pai = ai;
			return (s);
		}
	}

	if (bp->dnstime + bp->dnsttl >= TIM_mono())
		return (-1);

	/* Then do another lookup to catch DNS changes */
	vbe_lookup(bp);

	/* And try the entire list */
	for (ai = bp->addr; ai != NULL; ai = ai->ai_next) {
		s = vbe_sock_conn(ai);
		if (s >= 0) {
			bp->last_addr = ai;
			*pai = ai;
			return (s);
		}
	}

	return (-1);
}

static int
vbe_connect(struct sess *sp, struct backend *bp)
{
	int s;
	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];
	struct addrinfo *ai;

	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	AN(bp->hostname);

	s = vbe_conn_try(bp, &ai);
	if (s < 0)
		return (s);

	TCP_myname(s, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	TCP_name(ai->ai_addr, ai->ai_addrlen,
	    abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	WSL(sp->wrk, SLT_BackendOpen, s, "%s %s %s %s %s",
	    bp->vcl_name, abuf1, pbuf1, abuf2, pbuf2);
	return (s);
}

/* Get a backend connection ------------------------------------------
 *
 * Try all cached backend connections for this backend, and use the
 * first one that is looks like it is still connected.
 * If that fails to get us a connection, create a new one, reusing a
 * connection from the freelist, if possible.
 *
 * This function is slightly complicated by optimizations on vbemtx.
 */

static struct vbe_conn *
vbe_nextfd(struct sess *sp)
{
	struct vbe_conn *vc, *vc2;
	struct pollfd pfd;
	struct backend *bp;
	int reuse = 0;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	bp = sp->backend;
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	vc2 = NULL;
	while (1) {
		LOCK(&vbemtx);
		vc = TAILQ_FIRST(&bp->connlist);
		if (vc != NULL) {
			assert(vc->backend == bp);
			assert(vc->fd >= 0);
			TAILQ_REMOVE(&bp->connlist, vc, list);
		} else {
			vc2 = TAILQ_FIRST(&vbe_head);
			if (vc2 != NULL) {
				VSL_stats->backend_unused--;
				TAILQ_REMOVE(&vbe_head, vc2, list);
			}
		}
		UNLOCK(&vbemtx);
		if (vc == NULL)
			break;

		/* Test the connection for remote close before we use it */
		pfd.fd = vc->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (!poll(&pfd, 1, 0)) {
			reuse = 1;
			break;
		}
		VBE_ClosedFd(sp->wrk, vc, 0);
	}

	if (vc == NULL) {
		if (vc2 == NULL)
			vc = vbe_new_conn();
		else
			vc = vc2;
		if (vc != NULL) {
			assert(vc->fd == -1);
			AZ(vc->backend);
			vc->fd = vbe_connect(sp, bp);
			if (vc->fd < 0) {
				LOCK(&vbemtx);
				TAILQ_INSERT_HEAD(&vbe_head, vc, list);
				VSL_stats->backend_unused++;
				UNLOCK(&vbemtx);
				vc = NULL;
			} else {
				vc->backend = bp;
			}
		}
	}
	LOCK(&vbemtx);
	if (vc != NULL ) {
		VSL_stats->backend_reuse += reuse;
		VSL_stats->backend_conn++;
	} else {
		VSL_stats->backend_fail++;
	}
	UNLOCK(&vbemtx);
	if (vc != NULL ) {
		WSL(sp->wrk, SLT_BackendXID, vc->fd, "%u", sp->xid);
		assert(vc->fd >= 0);
		assert(vc->backend == bp);
	}
	return (vc);
}

/*--------------------------------------------------------------------*/

struct vbe_conn *
VBE_GetFd(struct sess *sp)
{
	struct vbe_conn *vc;
	unsigned n;

	for (n = 1; n < 5; n++) {
		vc = vbe_nextfd(sp);
		if (vc != NULL) {
			WSL(sp->wrk, SLT_Backend, sp->fd, "%d %s", vc->fd,
			    sp->backend->vcl_name);
			return (vc);
		}
		usleep(100000 * n);
	}
	return (NULL);
}

/* Close a connection ------------------------------------------------*/

void
VBE_ClosedFd(struct worker *w, struct vbe_conn *vc, int already)
{

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	assert(vc->fd >= 0);
	AN(vc->backend);
	WSL(w, SLT_BackendClose, vc->fd, "%s", vc->backend->vcl_name);
	if (!already)
		AZ(close(vc->fd));
	vc->fd = -1;
	vc->backend = NULL;
	LOCK(&vbemtx);
	TAILQ_INSERT_HEAD(&vbe_head, vc, list);
	VSL_stats->backend_unused++;
	UNLOCK(&vbemtx);
}

/* Recycle a connection ----------------------------------------------*/

void
VBE_RecycleFd(struct worker *w, struct vbe_conn *vc)
{

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	assert(vc->fd >= 0);
	AN(vc->backend);
	WSL(w, SLT_BackendReuse, vc->fd, "%s", vc->backend->vcl_name);
	LOCK(&vbemtx);
	VSL_stats->backend_recycle++;
	TAILQ_INSERT_HEAD(&vc->backend->connlist, vc, list);
	UNLOCK(&vbemtx);
}

/*--------------------------------------------------------------------*/

void
VBE_Init(void)
{

	MTX_INIT(&vbemtx);
}
