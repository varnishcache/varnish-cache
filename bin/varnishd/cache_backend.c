/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * This is the central switch-board for backend connections and it is
 * slightly complicated by a number of optimizations.
 *
 * The data structures:
 *
 *    A vrt_backend is a definition of a backend in a VCL program.
 *
 *    A backend is a TCP destination, possibly multi-homed and it has a
 *    number of associated properties and statistics.
 *
 *    A vbe_conn is an open TCP connection to a backend.
 *
 *    A bereq is a memory carrier for handling a HTTP transaction with
 *    a backend over a vbe_conn.
 *
 *    A director is a piece of code that selects which backend to use,
 *    by whatever method or metric it chooses.
 *
 * The relationships:
 *
 *    Backends and directors get instantiated when VCL's are loaded,
 *    and this always happen in the CLI thread.
 *
 *    When a VCL tries to instantiate a backend, any existing backend
 *    with the same identity (== definition in VCL) will be used instead
 *    so that vbe_conn's can be reused across VCL changes.
 *
 *    Directors disapper with the VCL that created them.
 *
 *    Backends disappear when their reference count drop to zero.
 *
 *    Backends have their host/port name looked up to addrinfo structures
 *    when they are instantiated, and we just cache that result and cycle
 *    through the entries (for multihomed backends) on failure only.
 *    XXX: add cli command to redo lookup.
 *
 *    bereq is sort of a step-child here, we just manage the pool of them.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <sys/socket.h>
#include <netdb.h>

#include "shmlog.h"
#include "cache.h"
#include "vrt.h"
#include "cli_priv.h"

/* Backend indstance */
struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6

	struct vrt_backend	vrt[1];
	uint32_t		hash;

	VTAILQ_ENTRY(backend)	list;
	int			refcount;
	pthread_mutex_t		mtx;

	struct addrinfo		*ai;
	struct addrinfo		*last_ai;

	VTAILQ_HEAD(, vbe_conn)	connlist;

	int			health;
};

static MTX VBE_mtx;

/*
 * List of cached vbe_conns, used if enabled in params/heritage
 */
static VTAILQ_HEAD(,vbe_conn) vbe_conns = VTAILQ_HEAD_INITIALIZER(vbe_conns);

/*
 * List of cached bereq's
 */
static VTAILQ_HEAD(,bereq) bereq_head = VTAILQ_HEAD_INITIALIZER(bereq_head);

/*
 * The list of backends is not locked, it is only ever accessed from
 * the CLI thread, so there is no need.
 */
static VTAILQ_HEAD(, backend) backends = VTAILQ_HEAD_INITIALIZER(backends);

/*--------------------------------------------------------------------
 * Create default Host: header for backend request
 */
void
VBE_AddHostHeader(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->bereq, BEREQ_MAGIC);
	CHECK_OBJ_NOTNULL(sp->bereq->http, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	http_PrintfHeader(sp->wrk, sp->fd, sp->bereq->http,
	    "Host: %s", sp->backend->vrt->hostname);
}

/*--------------------------------------------------------------------
 * Attempt to connect to a given addrinfo entry.
 *
 * Must be called with locked backend, but will release the backend
 * lock during the slow/sleeping stuff, so that other worker threads
 * can have a go, while we ponder.
 *
 */

static int
VBE_TryConnect(const struct sess *sp, const struct addrinfo *ai)
{
	struct sockaddr_storage ss;
	int fam, sockt, proto;
	socklen_t alen;
	int s, i, tmo;
	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);

	/*
	 * ai is only valid with the lock held, so copy out the bits
	 * we need to make the connection
	 */
	fam = ai->ai_family;
	sockt = ai->ai_socktype;
	proto = ai->ai_protocol;
	alen = ai->ai_addrlen;
	assert(alen <= sizeof ss);
	memcpy(&ss, ai->ai_addr, alen);

	/* release lock during stuff that can take a long time */
	UNLOCK(&sp->backend->mtx);

	s = socket(fam, sockt, proto);
	if (s < 0) {
		LOCK(&sp->backend->mtx);
		return (s);
	}

	tmo = params->connect_timeout;
	if (sp->backend->vrt->connect_timeout > 10e-3)
		tmo = sp->backend->vrt->connect_timeout * 1000;

	if (tmo > 0)
		i = TCP_connect(s, (void *)&ss, alen, tmo);
	else
		i = connect(s, (void *)&ss, alen);

	if (i != 0) {
		AZ(close(s));
		LOCK(&sp->backend->mtx);
		return (-1);
	}

	TCP_myname(s, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	TCP_name((void*)&ss, alen, abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	WSL(sp->wrk, SLT_BackendOpen, s, "%s %s %s %s %s",
	    sp->backend->vrt->vcl_name, abuf1, pbuf1, abuf2, pbuf2);

	LOCK(&sp->backend->mtx);
	return (s);
}

/*--------------------------------------------------------------------
 * Check that there is still something at the far end of a given socket.
 * We poll the fd with instant timeout, if there are any events we can't
 * use it (backends are not allowed to pipeline).
 */

static int
VBE_CheckFd(int fd)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	return(poll(&pfd, 1, 0) == 0);
}

/*--------------------------------------------------------------------
 * Get a bereq structure for talking HTTP with the backend.
 * First attempt to pick one from our stash, else make a new.
 *
 * Can fail with NULL.
 */

struct bereq *
VBE_new_bereq(void)
{
	struct bereq *bereq;
	volatile unsigned len;

	LOCK(&VBE_mtx);
	bereq = VTAILQ_FIRST(&bereq_head);
	if (bereq != NULL)
		VTAILQ_REMOVE(&bereq_head, bereq, list);
	UNLOCK(&VBE_mtx);
	if (bereq != NULL) {
		CHECK_OBJ(bereq, BEREQ_MAGIC);
	} else {
		len =  params->sess_workspace;
		bereq = calloc(sizeof *bereq + len, 1);
		if (bereq == NULL)
			return (NULL);
		bereq->magic = BEREQ_MAGIC;
		WS_Init(bereq->ws, "bereq", bereq + 1, len);
		VSL_stats->n_bereq++;
	}
	http_Setup(bereq->http, bereq->ws);
	return (bereq);
}

/*--------------------------------------------------------------------
 * Return a bereq to the stash.
 */

void
VBE_free_bereq(struct bereq *bereq)
{

	CHECK_OBJ_NOTNULL(bereq, BEREQ_MAGIC);
	WS_Reset(bereq->ws, NULL);
	LOCK(&VBE_mtx);
	VTAILQ_INSERT_HEAD(&bereq_head, bereq, list);
	UNLOCK(&VBE_mtx);
}

/*--------------------------------------------------------------------
 * Manage a pool of vbe_conn structures.
 * XXX: as an experiment, make this caching controled by a parameter
 * XXX: so we can see if it has any effect.
 */

static struct vbe_conn *
VBE_NewConn(void)
{
	struct vbe_conn *vc;

	vc = VTAILQ_FIRST(&vbe_conns);
	if (vc != NULL) {
		LOCK(&VBE_mtx);
		vc = VTAILQ_FIRST(&vbe_conns);
		if (vc != NULL) {
			VSL_stats->backend_unused--;
			VTAILQ_REMOVE(&vbe_conns, vc, list);
		}
		UNLOCK(&VBE_mtx);
	}
	if (vc != NULL)
		return (vc);
	vc = calloc(sizeof *vc, 1);
	XXXAN(vc);
	vc->magic = VBE_CONN_MAGIC;
	vc->fd = -1;
	VSL_stats->n_vbe_conn++;
	return (vc);
}

static void
VBE_ReleaseConn(struct vbe_conn *vc)
{

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	assert(vc->backend == NULL);
	assert(vc->fd < 0);

	if (params->cache_vbe_conns) {
		LOCK(&VBE_mtx);
		VTAILQ_INSERT_HEAD(&vbe_conns, vc, list);
		VSL_stats->backend_unused++;
		UNLOCK(&VBE_mtx);
	} else {
		VSL_stats->n_vbe_conn--;
		free(vc);
	}
}

/*--------------------------------------------------------------------
 * Drop a reference to a backend.
 * The last reference must come from the watcher in the CLI thread,
 * as only that thread is allowed to clean up the backend list.
 */

static void
VBE_DropRefLocked(struct backend *b)
{
	int i;
	struct vbe_conn *vbe, *vbe2;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	assert(b->refcount > 0);

	i = --b->refcount;
	UNLOCK(&b->mtx);
	if (i > 0)
		return;

	ASSERT_CLI();
	VTAILQ_REMOVE(&backends, b, list);
	VTAILQ_FOREACH_SAFE(vbe, &b->connlist, list, vbe2) {
		VTAILQ_REMOVE(&b->connlist, vbe, list);
		if (vbe->fd >= 0)
			AZ(close(vbe->fd));
		VBE_ReleaseConn(vbe);
	}
	free(TRUST_ME(b->vrt->ident));
	free(TRUST_ME(b->vrt->hostname));
	free(TRUST_ME(b->vrt->portname));
	b->magic = 0;
	free(b);
	VSL_stats->n_backend--;
}

void
VBE_DropRef(struct backend *b)
{

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	LOCK(&b->mtx);
	VBE_DropRefLocked(b);
}

/*--------------------------------------------------------------------
 * Try to get a socket connected to one of the addresses on the list.
 * We start from the cached "last good" address and try all items on
 * the list exactly once.
 *
 * Called with backend mutex held, but will release/acquire it.
 *
 * XXX: Not ready for DNS re-lookups
 */

static int
bes_conn_try_list(const struct sess *sp, struct backend *bp)
{
	struct addrinfo *ai, *from;
	int s, loops;

	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);

	/* No addrinfo, no connection */
	if (bp->ai == NULL)
		return (-1);
	AN(bp->last_ai);

	/* Called with lock held */
	loops = 0;
	ai = from = bp->last_ai;
	while (loops == 0 || ai != from) {

		/* NB: releases/acquires backend lock */
		s = VBE_TryConnect(sp, ai);

		if (s >= 0) { 
			bp->last_ai = ai;
			return (s);
		}

		/* Try next one */
		ai = ai->ai_next;
		if (ai == NULL) {
			loops++;
			ai = bp->ai;
		}
	}
	/* We have tried them all, fail */
	return (-1);
}


/*--------------------------------------------------------------------*/

static int
bes_conn_try(const struct sess *sp, struct backend *bp)
{
	int s;

	LOCK(&bp->mtx);
	bp->refcount++;
	s = bes_conn_try_list(sp, bp);	/* releases/acquires backend lock */
	if (s < 0)
		bp->refcount--;		/* Only keep ref on success */
	UNLOCK(&bp->mtx);
	return (s);
}

/*--------------------------------------------------------------------*/

void
VBE_SelectBackend(struct sess *sp)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	bp = sp->director->choose(sp);
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	sp->backend = bp;
}

/*--------------------------------------------------------------------*/

struct vbe_conn *
VBE_GetFd(struct sess *sp)
{
	struct backend *bp;
	struct vbe_conn *vc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	bp = sp->backend;

	/* first look for vbe_conn's we can recycle */
	while (1) {
		LOCK(&bp->mtx);
		vc = VTAILQ_FIRST(&bp->connlist);
		if (vc != NULL) {
			bp->refcount++;
			assert(vc->backend == bp);
			assert(vc->fd >= 0);
			VTAILQ_REMOVE(&bp->connlist, vc, list);
		}
		UNLOCK(&bp->mtx);
		if (vc == NULL)
			break;
		if (VBE_CheckFd(vc->fd)) {
			/* XXX locking of stats */
			VSL_stats->backend_reuse += 1;
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

/* Close a connection ------------------------------------------------*/

void
VBE_ClosedFd(struct worker *w, struct vbe_conn *vc)
{
	struct backend *b;
	int i;

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	b = vc->backend;
	assert(vc->fd >= 0);
	WSL(w, SLT_BackendClose, vc->fd, "%s", vc->backend->vrt->vcl_name);
	i = close(vc->fd);
	assert(i == 0 || errno == ECONNRESET || errno == ENOTCONN);
	vc->fd = -1;
	VBE_DropRef(vc->backend);
	vc->backend = NULL;
	VBE_ReleaseConn(vc);
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
}

/* Recycle a connection ----------------------------------------------*/

void
VBE_RecycleFd(struct worker *w, struct vbe_conn *vc)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	assert(vc->fd >= 0);
	bp = vc->backend;
	WSL(w, SLT_BackendReuse, vc->fd, "%s", vc->backend->vrt->vcl_name);
	LOCK(&vc->backend->mtx);
	VSL_stats->backend_recycle++;
	VTAILQ_INSERT_HEAD(&bp->connlist, vc, list);
	VBE_DropRefLocked(vc->backend);
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
}

/* Update health ----------------------------------------------------*/
/* See cache_backend_random.c and/or cache_backend_round_robin.c for
 * details and comments about this function. 
 */
void
VBE_UpdateHealth(const struct sess *sp, const struct vbe_conn *vc, int a)
{
	(void)sp;
	(void)vc;
	(void)a;
#if 0
	INCOMPL();
	struct backend *b;

	if (vc != NULL) {
		CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
		CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
		b = vc->backend;
	}
	else {
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
		b = sp->backend;
	}
	AN(b->method);
	if(b->method->updatehealth != NULL)
		b->method->updatehealth(sp, vc, a);
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
#endif
}

/*--------------------------------------------------------------------
 * DNS lookup of backend host/port
 */

static void
vbe_dns_lookup(struct cli *cli, struct backend *bp)
{
	int error;
	struct addrinfo *res, hint, *old;

	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(bp->vrt->hostname, bp->vrt->portname,
	    &hint, &res);
	if (error) {
		if (res != NULL)
			freeaddrinfo(res);
		/*
		 * We cannot point to the source code any more, it may
		 * be long gone from memory.   We already checked over in
		 * the VCL compiler, so this is only relevant for refreshes.
		 * XXX: which we do when exactly ?
		 */
		cli_out(cli, "DNS(/hosts) lookup failed for (%s/%s): %s",
		    bp->vrt->hostname, bp->vrt->portname, gai_strerror(error));
		return;
	}
	LOCK(&bp->mtx);
	old = bp->ai;
	bp->ai = res;
	bp->last_ai = res;
	UNLOCK(&bp->mtx);
	if (old != NULL)
		freeaddrinfo(old);
}

/*--------------------------------------------------------------------
 * Add a backend/director instance when loading a VCL.
 * If an existing backend is matched, grab a refcount and return.
 * Else create a new backend structure with reference initialized to one.
 */

struct backend *
VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb)
{
	struct backend *b;
	uint32_t u;

	AN(vb->hostname);
	AN(vb->portname);
	AN(vb->ident);
	(void)cli;
	ASSERT_CLI();
	u = crc32_l(vb->ident, strlen(vb->ident));
	VTAILQ_FOREACH(b, &backends, list) {
		CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
		if (u != b->hash)
			continue;
		if (strcmp(b->vrt->ident, vb->ident))
			continue;
		b->refcount++;
		return (b);
	}

	ALLOC_OBJ(b, BACKEND_MAGIC);
	XXXAN(b);
	b->magic = BACKEND_MAGIC;

	VTAILQ_INIT(&b->connlist);
	b->hash = u;

	/*
	 * This backend may live longer than the VCL that instantiated it
	 * so we cannot simply reference the VCL's copy of things.
	 */
	REPLACE(b->vrt->ident, vb->ident);
	REPLACE(b->vrt->hostname, vb->hostname);
	REPLACE(b->vrt->portname, vb->portname);
	REPLACE(b->vrt->vcl_name, vb->vcl_name);

	b->vrt->connect_timeout = vb->connect_timeout;

	MTX_INIT(&b->mtx);
	b->refcount = 1;

	vbe_dns_lookup(cli, b);

	VTAILQ_INSERT_TAIL(&backends, b, list);
	VSL_stats->n_backend++;
	return (b);
}


/*--------------------------------------------------------------------*/

void
VRT_fini_dir(struct cli *cli, struct director *b)
{

	ASSERT_CLI();
#if 0
	VBE_DropRef(b);	
#else
	(void)b;
	(void)cli;
#endif
}
/*--------------------------------------------------------------------*/

void
VBE_Init(void)
{

	MTX_INIT(&VBE_mtx);
}
