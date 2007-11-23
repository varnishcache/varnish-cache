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
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shmlog.h"
#include "cache.h"
#include "vrt.h"

struct bes {
	unsigned		magic;
#define BES_MAGIC		0x015e17ac
	char			*hostname;
	char			*portname;
	char			*ident;
	struct addrinfo		*addr;
	struct addrinfo		*last_addr;
	double			dnsttl;
	double			dnstime;
	unsigned		dnsseq;
	VTAILQ_HEAD(, vbe_conn)	connlist;
};

/*--------------------------------------------------------------------
 * Try to get a socket connected to one of the addresses on the list.
 * We start from the cached "last good" address and try all items on
 * the list exactly once.
 * If a new DNS lookup is made while we try, we start over and try the
 * new list exactly once.
 */

static int
bes_conn_try_list(const struct sess *sp, struct bes *bes)
{
	struct addrinfo *ai, *from;
	int s, loops;
	unsigned myseq;

	CHECK_OBJ_NOTNULL(bes, BES_MAGIC);
	if (bes->addr == NULL)
		return (-1);
	AN(bes->last_addr);

	/* Called with lock held */
	myseq = bes->dnsseq;
	loops = 0;
	ai = from = bes->last_addr;
	while (1) {

		/* NB: releases/acquires lock */
		s = VBE_TryConnect(sp, ai);

		if (s >= 0) { 
			/* Update cached "last good" if still valid */
			if (myseq == bes->dnsseq)
				bes->last_addr = ai;
			return (s);
		}

		if (myseq != bes->dnsseq) {
			/* A DNS-lookup happended, try again from start */
			loops = 0;
			from = bes->last_addr;
			ai = from;
		} else {
			/* Try next one */
			ai = ai->ai_next;
			if (ai == NULL) {
				loops++;
				ai = bes->addr;
			}
		}
		if (loops == 1 && ai == from)
			return (-1);
	}
}

/*--------------------------------------------------------------------*/

static const char *
bes_dns_lookup(struct backend *bp)
{
	struct addrinfo *res, hint, *old;
	struct bes *bes;
	int error;

	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(bes, bp->priv, BES_MAGIC);

	bes->dnstime = TIM_mono();

	/* Let go of lock while we do sleepable stuff */
	UNLOCK(&bp->mtx);

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(bes->hostname,
	    bes->portname == NULL ? "http" : bes->portname,
	    &hint, &res);
	LOCK(&bp->mtx);
	if (error) {
		if (res != NULL)
			freeaddrinfo(res);
		return(gai_strerror(error));
	} 
	bes->dnsseq++;
	old = bes->addr;
	bes->last_addr = res;
	bes->addr = res;
	if (old != NULL)
		freeaddrinfo(old);
	return (NULL);
}

/*--------------------------------------------------------------------*/

static int
bes_conn_try(const struct sess *sp, struct backend *bp)
{
	int s;
	struct bes *bes;

	CAST_OBJ_NOTNULL(bes, bp->priv, BES_MAGIC);

	LOCK(&bp->mtx);

	s = bes_conn_try_list(sp, bes);
	if (s >= 0) {
		bp->refcount++;
		UNLOCK(&bp->mtx);
		return (s);
	}

	if (bes->dnstime + bes->dnsttl >= TIM_mono()) {
		UNLOCK(&bp->mtx);
		return (-1);
	}

	(void)bes_dns_lookup(bp);

	/* And try the entire list */
	s = bes_conn_try_list(sp, bes);
	if (s >= 0) {
		bp->refcount++;
		UNLOCK(&bp->mtx);
		return (s);
	}

	UNLOCK(&bp->mtx);
	return (-1);
}

/* Get a backend connection ------------------------------------------
 *
 * Try all cached backend connections for this backend, and use the
 * first one that is looks like it is still connected.
 * If that fails to get us a connection, create a new one, reusing a
 * connection from the freelist, if possible.
 *
 * This function is slightly complicated by optimizations on besmtx.
 */

static struct vbe_conn *
bes_nextfd(const struct sess *sp)
{
	struct vbe_conn *vc;
	struct backend *bp;
	int reuse = 0;
	struct bes *bes;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	bp = sp->backend;
	CAST_OBJ_NOTNULL(bes, bp->priv, BES_MAGIC);
	while (1) {
		LOCK(&bp->mtx);
		vc = VTAILQ_FIRST(&bes->connlist);
		if (vc != NULL) {
			bp->refcount++;
			assert(vc->backend == bp);
			assert(vc->fd >= 0);
			VTAILQ_REMOVE(&bes->connlist, vc, list);
		}
		UNLOCK(&bp->mtx);
		if (vc == NULL)
			break;

		if (VBE_CheckFd(vc->fd)) {
			/* XXX locking of stats */
			VSL_stats->backend_reuse += reuse;
			VSL_stats->backend_conn++;
			return (vc);
		}
		VBE_ClosedFd(sp->wrk, vc);
	}

	vc = VBE_NewConn();
	assert(vc->fd == -1);
	AZ(vc->backend);
	vc->fd = bes_conn_try(sp, bp);
	if (vc->fd < 0) {
		VBE_ReleaseConn(vc);
		VSL_stats->backend_fail++;
		return (NULL);
	} 
	vc->backend = bp;
	VSL_stats->backend_conn++;
	return (vc);
}

/*--------------------------------------------------------------------*/

static struct vbe_conn *
bes_GetFd(const struct sess *sp)
{
	struct vbe_conn *vc;
	unsigned n;
	for (n = 1; n < 5; n++) {
		vc = bes_nextfd(sp);
		if (vc == NULL) {
			AZ(usleep(100000 * n));
			continue;
		}
		assert(vc->fd >= 0);
		assert(vc->backend == sp->backend);
		WSL(sp->wrk, SLT_BackendXID, vc->fd, "%u", sp->xid);
		WSP(sp, SLT_Backend, "%d %s", vc->fd, sp->backend->vcl_name);
		return (vc);
	}
	return (NULL);
}

/* Close a connection ------------------------------------------------*/

static void
bes_ClosedFd(struct worker *w, struct vbe_conn *vc)
{
	int i;

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	assert(vc->fd >= 0);
	WSL(w, SLT_BackendClose, vc->fd, "%s", vc->backend->vcl_name);
	i = close(vc->fd);
	assert(i == 0 || errno == ECONNRESET || errno == ENOTCONN);
	vc->fd = -1;
	VBE_DropRef(vc->backend);
	vc->backend = NULL;
	VBE_ReleaseConn(vc);
}

/* Recycle a connection ----------------------------------------------*/

static void
bes_RecycleFd(struct worker *w, struct vbe_conn *vc)
{
	struct bes *bes;

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(bes, vc->backend->priv, BES_MAGIC);

	assert(vc->fd >= 0);
	WSL(w, SLT_BackendReuse, vc->fd, "%s", vc->backend->vcl_name);
	LOCK(&vc->backend->mtx);
	VSL_stats->backend_recycle++;
	VTAILQ_INSERT_HEAD(&bes->connlist, vc, list);
	VBE_DropRefLocked(vc->backend);
}

/*--------------------------------------------------------------------*/

static void
bes_Cleanup(const struct backend *b)
{
	struct bes *bes;
	struct vbe_conn *vbe;

	assert(b->refcount == 0);
	CAST_OBJ_NOTNULL(bes, b->priv, BES_MAGIC);
	free(bes->portname);
	free(bes->hostname);
	freeaddrinfo(bes->addr);
	while (1) {
		vbe = VTAILQ_FIRST(&bes->connlist);
		if (vbe == NULL)
			break;
		VTAILQ_REMOVE(&bes->connlist, vbe, list);
		if (vbe->fd >= 0)
			AZ(close(vbe->fd));
		FREE_OBJ(vbe);
	}
	FREE_OBJ(bes);
}

/*--------------------------------------------------------------------*/

static const char *
bes_GetHostname(const struct backend *b)
{
	struct bes *bes;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(bes, b->priv, BES_MAGIC);
	return (bes->hostname);
}

/*--------------------------------------------------------------------*/

struct backend_method backend_method_simple = {
	.name =			"simple",
	.getfd =		bes_GetFd,
	.close =		bes_ClosedFd,
	.recycle =		bes_RecycleFd,
	.gethostname =		bes_GetHostname,
	.cleanup =		bes_Cleanup,
};

/*--------------------------------------------------------------------*/

void
VRT_init_simple_backend(struct backend **bp, const struct vrt_simple_backend *t)
{
	struct backend *b;
	struct bes *bes;
	const char *p;
	
	/*
	 * Scan existing backends to see if we can recycle one of them.
	 */
	VTAILQ_FOREACH(b, &backendlist, list) {
		CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
		if (b->method != &backend_method_simple)
			continue;
		CAST_OBJ_NOTNULL(bes, b->priv, BES_MAGIC);
		if (strcmp(bes->ident, t->ident))
			continue;
		b->refcount++;
		*bp = b;
		return;
	}

	b = VBE_NewBackend(&backend_method_simple);

	bes = calloc(sizeof *bes, 1);
	XXXAN(bes);
	bes->magic = BES_MAGIC;

	b->priv = bes;

	bes->dnsttl = 300;

	AN(t->ident);
	REPLACE(bes->ident, t->ident);

	AN(t->name);
	REPLACE(b->vcl_name, t->name);

	AN(t->port);
	REPLACE(bes->portname, t->port);

	AN(t->host);
	REPLACE(bes->hostname, t->host);

	/*
	 * The VCL compiler already did a lookup, but we'll do another one
	 * here, just in case...
	 */
	LOCK(&b->mtx);
	p = bes_dns_lookup(b);
	UNLOCK(&b->mtx);
	if (p != NULL)
		printf("Warning: could not lookup backend %s (%s:%s): %s",
		    t->name, t->host, t->port, p);

	*bp = b;
}
