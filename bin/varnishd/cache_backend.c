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
 * Handle backend connections and backend request structures.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <sys/socket.h>

#include "shmlog.h"
#include "cache.h"
#include "cache_backend.h"

/*
 * List of cached vbe_conns, used if enabled in params/heritage
 */
static VTAILQ_HEAD(,vbe_conn) vbe_conns = VTAILQ_HEAD_INITIALIZER(vbe_conns);

/*
 * List of cached bereq's
 */
static VTAILQ_HEAD(,bereq) bereq_head = VTAILQ_HEAD_INITIALIZER(bereq_head);

/*--------------------------------------------------------------------
 * Create default Host: header for backend request
 */
void
VBE_AddHostHeader(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->bereq, BEREQ_MAGIC);
	CHECK_OBJ_NOTNULL(sp->bereq->http, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbe, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbe->backend, BACKEND_MAGIC);
	http_PrintfHeader(sp->wrk, sp->fd, sp->bereq->http,
	    "Host: %s", sp->vbe->backend->hosthdr);
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
VBE_TryConnect(const struct sess *sp, int pf, const struct sockaddr *sa,
    socklen_t salen, const struct backend *bp)
{
	int s, i, tmo;
	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	s = socket(pf, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	tmo = (int)(sp->connect_timeout * 1000);
	if (bp->connect_timeout > 10e-3)
		tmo = (int)(bp->connect_timeout * 1000);

	if (tmo > 0)
		i = TCP_connect(s, sa, salen, tmo);
	else
		i = connect(s, sa, salen);

	if (i != 0) {
		AZ(close(s));
		return (-1);
	}

	TCP_myname(s, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	TCP_name(sa, salen, abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	WSL(sp->wrk, SLT_BackendOpen, s, "%s %s %s %s %s",
	    bp->vcl_name, abuf1, pbuf1, abuf2, pbuf2);

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

	Lck_Lock(&VBE_mtx);
	bereq = VTAILQ_FIRST(&bereq_head);
	if (bereq != NULL)
		VTAILQ_REMOVE(&bereq_head, bereq, list);
	Lck_Unlock(&VBE_mtx);
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
	http_Setup(&bereq->http[0], bereq->ws);
	http_Setup(&bereq->http[1], bereq->ws);
	return (bereq);
}

/*--------------------------------------------------------------------
 * Return a bereq to the stash.
 */

void
VBE_free_bereq(struct bereq **bereqp)
{
	struct bereq *bereq;

	AN(bereqp);
	bereq = *bereqp;
	*bereqp = NULL;

	CHECK_OBJ_NOTNULL(bereq, BEREQ_MAGIC);
	WS_Reset(bereq->ws, NULL);
	Lck_Lock(&VBE_mtx);
	VTAILQ_INSERT_HEAD(&bereq_head, bereq, list);
	Lck_Unlock(&VBE_mtx);
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
		Lck_Lock(&VBE_mtx);
		vc = VTAILQ_FIRST(&vbe_conns);
		if (vc != NULL) {
			VSL_stats->backend_unused--;
			VTAILQ_REMOVE(&vbe_conns, vc, list);
		}
		Lck_Unlock(&VBE_mtx);
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

void
VBE_ReleaseConn(struct vbe_conn *vc)
{

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	assert(vc->backend == NULL);
	assert(vc->fd < 0);

	if (params->cache_vbe_conns) {
		Lck_Lock(&VBE_mtx);
		VTAILQ_INSERT_HEAD(&vbe_conns, vc, list);
		VSL_stats->backend_unused++;
		Lck_Unlock(&VBE_mtx);
	} else {
		VSL_stats->n_vbe_conn--;
		free(vc);
	}
}

/*--------------------------------------------------------------------*/

static int
bes_conn_try(const struct sess *sp, struct backend *bp)
{
	int s;

	Lck_Lock(&bp->mtx);
	bp->refcount++;
	bp->n_conn++;		/* It mostly works */
	Lck_Unlock(&bp->mtx);

	s = -1;
	assert(bp->ipv6 != NULL || bp->ipv4 != NULL);

	/* release lock during stuff that can take a long time */

	if (params->prefer_ipv6 && bp->ipv6 != NULL)
		s = VBE_TryConnect(sp, PF_INET6, bp->ipv6, bp->ipv6len, bp);
	if (s == -1 && bp->ipv4 != NULL)
		s = VBE_TryConnect(sp, PF_INET, bp->ipv4, bp->ipv4len, bp);
	if (s == -1 && !params->prefer_ipv6 && bp->ipv6 != NULL)
		s = VBE_TryConnect(sp, PF_INET6, bp->ipv6, bp->ipv6len, bp);

	if (s < 0) {
		Lck_Lock(&bp->mtx);
		bp->n_conn--;
		bp->refcount--;		/* Only keep ref on success */
		Lck_Unlock(&bp->mtx);
	}
	return (s);
}

/*--------------------------------------------------------------------
 * Get a connection to whatever backend the director think this session
 * should contact.
 */

void
VBE_GetFd(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);

	AN (sp->director->getfd);
	sp->vbe = sp->director->getfd(sp);
}

/*--------------------------------------------------------------------
 * Get a connection to a particular backend.
 */

struct vbe_conn *
VBE_GetVbe(struct sess *sp, struct backend *bp)
{
	struct vbe_conn *vc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);

	/* first look for vbe_conn's we can recycle */
	while (1) {
		Lck_Lock(&bp->mtx);
		vc = VTAILQ_FIRST(&bp->connlist);
		if (vc != NULL) {
			bp->refcount++;
			assert(vc->backend == bp);
			assert(vc->fd >= 0);
			VTAILQ_REMOVE(&bp->connlist, vc, list);
		}
		Lck_Unlock(&bp->mtx);
		if (vc == NULL)
			break;
		if (VBE_CheckFd(vc->fd)) {
			/* XXX locking of stats */
			VSL_stats->backend_reuse += 1;
			VSL_stats->backend_conn++;
			WSP(sp, SLT_Backend, "%d %s %s",
			    vc->fd, sp->director->vcl_name, bp->vcl_name);
			return (vc);
		}
		sp->vbe = vc;
		VBE_ClosedFd(sp);
	}

	if (!bp->healthy) {
		VSL_stats->backend_unhealthy++;
		return (NULL);
	}

	if (bp->max_conn > 0 && bp->n_conn >= bp->max_conn) {
		VSL_stats->backend_busy++;
		return (NULL);
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
	WSP(sp, SLT_Backend, "%d %s %s",
	    vc->fd, sp->director->vcl_name, bp->vcl_name);
	return (vc);
}

/* Close a connection ------------------------------------------------*/

void
VBE_ClosedFd(struct sess *sp)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(sp->vbe, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbe->backend, BACKEND_MAGIC);
	assert(sp->vbe->fd >= 0);

	bp = sp->vbe->backend;

	WSL(sp->wrk, SLT_BackendClose, sp->vbe->fd, "%s", bp->vcl_name);
	TCP_close(&sp->vbe->fd);
	VBE_DropRefConn(bp);
	sp->vbe->backend = NULL;
	VBE_ReleaseConn(sp->vbe);
	sp->vbe = NULL;
}

/* Recycle a connection ----------------------------------------------*/

void
VBE_RecycleFd(struct sess *sp)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(sp->vbe, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbe->backend, BACKEND_MAGIC);
	assert(sp->vbe->fd >= 0);

	bp = sp->vbe->backend;

	WSL(sp->wrk, SLT_BackendReuse, sp->vbe->fd, "%s", bp->vcl_name);
	Lck_Lock(&bp->mtx);
	VSL_stats->backend_recycle++;
	VTAILQ_INSERT_HEAD(&bp->connlist, sp->vbe, list);
	sp->vbe = NULL;
	VBE_DropRefLocked(bp);
}
