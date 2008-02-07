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
 * Manage backend connections and requests.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <sys/socket.h>
#include <netdb.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"
#include "vrt.h"
#include "cli_priv.h"

/* Backend indstance */
struct backend {
	unsigned		magic;
#define BACKEND_MAGIC		0x64c4c7c6
	char			*vcl_name;

	struct vrt_backend	vrt[1];

	VTAILQ_ENTRY(backend)	list;
	int			refcount;
	pthread_mutex_t		mtx;

	struct addrinfo		*ai;
	struct addrinfo		*last_ai;

	VTAILQ_HEAD(, vbe_conn)	connlist;

	int			health;
	double			last_check;
};

static VTAILQ_HEAD(,bereq) bereq_head = VTAILQ_HEAD_INITIALIZER(bereq_head);
static VTAILQ_HEAD(,vbe_conn) vbe_head = VTAILQ_HEAD_INITIALIZER(vbe_head);

static MTX VBE_mtx;

struct backendlist backendlist = VTAILQ_HEAD_INITIALIZER(backendlist);

/*--------------------------------------------------------------------
 * Attempt to connect to a given addrinfo entry.
 *
 * Must be called with locked backend, but will release the backend
 * lock during the slow/sleeping stuff, so that other worker threads
 * can have a go, while we ponder.
 *
 */

int
VBE_TryConnect(const struct sess *sp, const struct addrinfo *ai)
{
	struct sockaddr_storage ss;
	int fam, sockt, proto;
	socklen_t alen;
	int s;
	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];

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

	if (connect(s, (void *)&ss, alen) != 0) {
		AZ(close(s));
		LOCK(&sp->backend->mtx);
		return (-1);
	}

	TCP_myname(s, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	TCP_name((void*)&ss, alen,
	    abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	WSL(sp->wrk, SLT_BackendOpen, s, "%s %s %s %s %s",
	    sp->backend->vcl_name, abuf1, pbuf1, abuf2, pbuf2);

	LOCK(&sp->backend->mtx);
	return (s);
}

/*--------------------------------------------------------------------
 * Check that there is still something at the far end of a given fd.
 * We poll the fd with instant timeout, if there are any events we can't
 * use it (backends are not allowed to pipeline).
 */

int
VBE_CheckFd(int fd)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	return(poll(&pfd, 1, 0) == 0);
}

/*--------------------------------------------------------------------
 * Get a http structure for talking to the backend.
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
		len =  params->mem_workspace;
		bereq = calloc(sizeof *bereq + len, 1);
		if (bereq == NULL)
			return (NULL);
		bereq->magic = BEREQ_MAGIC;
		WS_Init(bereq->ws, "bereq", bereq + 1, len);
	}
	http_Setup(bereq->http, bereq->ws);
	return (bereq);
}

/*--------------------------------------------------------------------*/
/* XXX: no backpressure on pool size */

void
VBE_free_bereq(struct bereq *bereq)
{

	CHECK_OBJ_NOTNULL(bereq, BEREQ_MAGIC);
	WS_Reset(bereq->ws, NULL);
	LOCK(&VBE_mtx);
	VTAILQ_INSERT_HEAD(&bereq_head, bereq, list);
	UNLOCK(&VBE_mtx);
}

/*--------------------------------------------------------------------*/

struct vbe_conn *
VBE_NewConn(void)
{
	struct vbe_conn *vc;

	vc = VTAILQ_FIRST(&vbe_head);
	if (vc != NULL) {
		LOCK(&VBE_mtx);
		vc = VTAILQ_FIRST(&vbe_head);
		if (vc != NULL) {
			VSL_stats->backend_unused--;
			VTAILQ_REMOVE(&vbe_head, vc, list);
		} else {
			VSL_stats->n_vbe_conn++;
		}
		UNLOCK(&VBE_mtx);
	}
	if (vc != NULL)
		return (vc);

	vc = calloc(sizeof *vc, 1);
	XXXAN(vc);
	vc->magic = VBE_CONN_MAGIC;
	vc->fd = -1;
	return (vc);
}

/*--------------------------------------------------------------------*/

void
VBE_ReleaseConn(struct vbe_conn *vc)
{

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	assert(vc->backend == NULL);
	assert(vc->fd < 0);
	LOCK(&VBE_mtx);
	VTAILQ_INSERT_HEAD(&vbe_head, vc, list);
	VSL_stats->backend_unused++;
	UNLOCK(&VBE_mtx);
}


/*--------------------------------------------------------------------*/

void
VBE_DropRefLocked(struct backend *b)
{
	int i;
	struct vbe_conn *vbe, *vbe2;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	i = --b->refcount;
	UNLOCK(&b->mtx);
	if (i)
		return;

	ASSERT_CLI();	/* XXX: ?? */
	VTAILQ_REMOVE(&backendlist, b, list);
	VTAILQ_FOREACH_SAFE(vbe, &b->connlist, list, vbe2) {
		VTAILQ_REMOVE(&b->connlist, vbe, list);
		if (vbe->fd >= 0)
			AZ(close(vbe->fd));
		FREE_OBJ(vbe);
	}
	free(TRUST_ME(b->vrt->ident));
	free(TRUST_ME(b->vrt->hostname));
	free(TRUST_ME(b->vrt->portname));
	b->magic = 0;
	free(b);
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
 * If a new DNS lookup is made while we try, we start over and try the
 * new list exactly once.
 */

static int
bes_conn_try_list(const struct sess *sp, struct backend *bp)
{
	struct addrinfo *ai, *from;
	int s, loops;

	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	if (bp->last_ai == NULL)
		return (-1);
	AN(bp->ai);

	/* Called with lock held */
	loops = 0;
	ai = from = bp->last_ai;
	while (1) {

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
		if (loops == 1 && ai == from)
			return (-1);
	}
}


/*--------------------------------------------------------------------*/

static int
bes_conn_try(const struct sess *sp, struct backend *bp)
{
	int s;

	LOCK(&bp->mtx);

	s = bes_conn_try_list(sp, bp);
	if (s >= 0) {
		bp->refcount++;
		UNLOCK(&bp->mtx);
		return (s);
	}
	UNLOCK(&bp->mtx);
	return (-1);
}

/*--------------------------------------------------------------------*/

struct vbe_conn *
VBE_GetFd(struct sess *sp)
{
	struct backend *bp;
	struct vbe_conn *vc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	bp = sp->director->choose(sp);
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	sp->backend = bp;

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
	WSL(w, SLT_BackendClose, vc->fd, "%s", vc->backend->vcl_name);
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
	WSL(w, SLT_BackendReuse, vc->fd, "%s", vc->backend->vcl_name);
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
 * If an existing backend is matched, grab a refcount and return one.
 * Else create a new backend structure with reference initialized to one
 * and return zero.
 */

struct backend *
VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb)
{
	struct backend *b;

	AN(vb->hostname);
	AN(vb->portname);
	AN(vb->ident);
	(void)cli;
	ASSERT_CLI();
	VTAILQ_FOREACH(b, &backendlist, list) {
		CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
		if (strcmp(b->vrt->ident, vb->ident))
			continue;
		b->refcount++;
		return (b);
	}

	b = calloc(sizeof *b, 1);
	XXXAN(b);
	b->magic = BACKEND_MAGIC;
	VTAILQ_INIT(&b->connlist);

	memcpy(b->vrt, vb, sizeof *vb);
	/*
	 * This backend may live longer than the VCL that instantiated it
	 * so we cannot simply reference the VCL's copy of the strings.
	 */
	b->vrt->ident = strdup(vb->ident);
	XXXAN(b->vrt->ident);
	b->vrt->hostname = strdup(vb->hostname);
	XXXAN(b->vrt->hostname);
	b->vrt->portname = strdup(vb->portname);
	XXXAN(b->vrt->portname);

	MTX_INIT(&b->mtx);
	b->refcount = 1;

	vbe_dns_lookup(cli, b);

	b->last_check = TIM_mono();

	VTAILQ_INSERT_TAIL(&backendlist, b, list);
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
