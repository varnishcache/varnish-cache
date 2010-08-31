/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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
 * Handle backend connections and backend request structures.
 *
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <sys/socket.h>

#include "shmlog.h"
#include "cache.h"
#include "cache_backend.h"
#include "vrt.h"

/*
 * List of cached vbe_conns, used if enabled in params/heritage
 */
static VTAILQ_HEAD(,vbe_conn) vbe_conns = VTAILQ_HEAD_INITIALIZER(vbe_conns);

/*--------------------------------------------------------------------
 * Create default Host: header for backend request
 */
void
VBE_AddHostHeader(const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbe, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbe->backend, BACKEND_MAGIC);
	http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->bereq,
	    "Host: %s", sp->vbe->backend->hosthdr);
}

/* Private interface from backend_cfg.c */
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
		Lck_Lock(&VBE_mtx);
		VSL_stats->n_vbe_conn--;
		Lck_Unlock(&VBE_mtx);
		free(vc);
	}
}

#define FIND_TMO(tmx, dst, sp, be)		\
	do {					\
		dst = sp->wrk->tmx;		\
		if (dst == 0.0)			\
			dst = be->tmx;		\
		if (dst == 0.0)			\
			dst = params->tmx;	\
	} while (0)


/*--------------------------------------------------------------------
 * Attempt to connect to a given addrinfo entry.
 *
 * Must be called with locked backend, but will release the backend
 * lock during the slow/sleeping stuff, so that other worker threads
 * can have a go, while we ponder.
 *
 */

static int
vbe_TryConnect(const struct sess *sp, int pf, const struct sockaddr *sa,
    socklen_t salen, const struct backend *bp)
{
	int s, i, tmo;
	double tmod;
	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	s = socket(pf, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	FIND_TMO(connect_timeout, tmod, sp, bp);

	tmo = (int)(tmod * 1000.0);

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
		s = vbe_TryConnect(sp, PF_INET6, bp->ipv6, bp->ipv6len, bp);
	if (s == -1 && bp->ipv4 != NULL)
		s = vbe_TryConnect(sp, PF_INET, bp->ipv4, bp->ipv4len, bp);
	if (s == -1 && !params->prefer_ipv6 && bp->ipv6 != NULL)
		s = vbe_TryConnect(sp, PF_INET6, bp->ipv6, bp->ipv6len, bp);

	if (s < 0) {
		Lck_Lock(&bp->mtx);
		bp->n_conn--;
		bp->refcount--;		/* Only keep ref on success */
		Lck_Unlock(&bp->mtx);
	}
	return (s);
}

/*--------------------------------------------------------------------
 * Check that there is still something at the far end of a given socket.
 * We poll the fd with instant timeout, if there are any events we can't
 * use it (backends are not allowed to pipeline).
 */

static int
vbe_CheckFd(int fd)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	return(poll(&pfd, 1, 0) == 0);
}

/*--------------------------------------------------------------------
 * Manage a pool of vbe_conn structures.
 * XXX: as an experiment, make this caching controled by a parameter
 * XXX: so we can see if it has any effect.
 */

static struct vbe_conn *
vbe_NewConn(void)
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
	Lck_Lock(&VBE_mtx);
	VSL_stats->n_vbe_conn++;
	Lck_Unlock(&VBE_mtx);
	return (vc);
}


/*--------------------------------------------------------------------
 * It evaluates if a backend is healthy _for_a_specific_object_.
 * That means that it relies on sp->objhead. This is mainly for saint-mode,
 * but also takes backend->healthy into account. If
 * params->saintmode_threshold is 0, this is basically just a test of
 * backend->healthy.
 *
 * The threshold has to be evaluated _after_ the timeout check, otherwise
 * items would never time out once the threshold is reached.
 */

static unsigned int
vbe_Healthy(double now, uintptr_t target, struct backend *backend)
{
	struct trouble *tr;
	struct trouble *tr2;
	struct trouble *old;
	unsigned i = 0, retval;
	unsigned int threshold;

	CHECK_OBJ_NOTNULL(backend, BACKEND_MAGIC);

	if (!backend->healthy)
		return (0);

	/* VRT/VCC sets threshold to UINT_MAX to mark that it's not
	 * specified by VCL (thus use param).
	 */
	if (backend->saintmode_threshold == UINT_MAX)
		threshold = params->saintmode_threshold;
	else
		threshold = backend->saintmode_threshold;

	/* Saintmode is disabled */
	if (threshold == 0)
		return (1);

	/* No need to test if we don't have an object head to test against.
	 * FIXME: Should check the magic too, but probably not assert?
	 */
	if (target == 0)
		return (1);

	old = NULL;
	retval = 1;
	Lck_Lock(&backend->mtx);
	VTAILQ_FOREACH_SAFE(tr, &backend->troublelist, list, tr2) {
		CHECK_OBJ_NOTNULL(tr, TROUBLE_MAGIC);

		if (tr->timeout < now) {
			VTAILQ_REMOVE(&backend->troublelist, tr, list);
			old = tr;
			retval = 1;
			break;
		}

		if (tr->target == target) {
			retval = 0;
			break;
		}

		/* If the threshold is at 1, a single entry on the list
		 * will disable the backend. Since 0 is disable, ++i
		 * instead of i++ to allow this behavior.
		 */
		if (++i >= threshold) {
			retval = 0;
			break;
		}
	}
	Lck_Unlock(&backend->mtx);

	if (old != NULL)
		FREE_OBJ(old);

	return (retval);
}

/*--------------------------------------------------------------------
 * Get a connection to a particular backend.
 */

static struct vbe_conn *
vbe_GetVbe(struct sess *sp, struct backend *bp)
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
		if (vbe_CheckFd(vc->fd)) {
			/* XXX locking of stats */
			VSL_stats->backend_reuse += 1;
			WSP(sp, SLT_Backend, "%d %s %s",
			    vc->fd, sp->director->vcl_name, bp->vcl_name);
			return (vc);
		}
		VSL_stats->backend_toolate++;
		sp->vbe = vc;
		VBE_ClosedFd(sp);
	}

	if (!vbe_Healthy(sp->t_req, (uintptr_t)sp->objhead, bp)) {
		VSL_stats->backend_unhealthy++;
		return (NULL);
	}

	if (bp->max_conn > 0 && bp->n_conn >= bp->max_conn) {
		VSL_stats->backend_busy++;
		return (NULL);
	}

	vc = vbe_NewConn();
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

	/* Checkpoint log to flush all info related to this connection
	   before the OS reuses the FD */
	WSL_Flush(sp->wrk, 0);

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
	/*
	 * Flush the shmlog, so that another session reusing this backend
	 * will log chronologically later than our use of it.
	 */
	WSL_Flush(sp->wrk, 0);
	Lck_Lock(&bp->mtx);
	VSL_stats->backend_recycle++;
	VTAILQ_INSERT_HEAD(&bp->connlist, sp->vbe, list);
	sp->vbe = NULL;
	VBE_DropRefLocked(bp);
}

/* Get a connection --------------------------------------------------*/

struct vbe_conn *
VBE_GetFd(const struct director *d, struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (d == NULL)
		d = sp->director;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	return (d->getfd(d, sp));
}

/* Check health ------------------------------------------------------
 *
 * The target is really an objhead pointer, but since it can not be
 * dereferenced during health-checks, we pass it as uintptr_t, which
 * hopefully will make people investigate, before mucking about with it.
 */

int
VBE_Healthy_sp(const struct sess *sp, const struct director *d)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	return (d->healthy(sp->t_req, d, (uintptr_t)sp->objhead));
}

int
VBE_Healthy(double now, const struct director *d, uintptr_t target)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	return (d->healthy(now, d, target));
}

/*--------------------------------------------------------------------
 * The "simple" director really isn't, since thats where all the actual
 * connections happen.  Nontheless, pretend it is simple by sequestering
 * the directoricity of it under this line.
 */

struct vdi_simple {
	unsigned		magic;
#define VDI_SIMPLE_MAGIC	0x476d25b7
	struct director		dir;
	struct backend		*backend;
};

static struct vbe_conn *
vdi_simple_getfd(const struct director *d, struct sess *sp)
{
	struct vdi_simple *vs;
	struct vbe_conn *vc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);
	vc = vbe_GetVbe(sp, vs->backend);
	if (vc != NULL) {
		FIND_TMO(first_byte_timeout,
		    vc->first_byte_timeout, sp, vc->backend);
		FIND_TMO(between_bytes_timeout,
		    vc->between_bytes_timeout, sp, vc->backend);
	}
	return (vc);
}

static unsigned
vdi_simple_healthy(double now, const struct director *d, uintptr_t target)
{
	struct vdi_simple *vs;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);
	return (vbe_Healthy(now, target, vs->backend));
}

/*lint -e{818} not const-able */
static void
vdi_simple_fini(struct director *d)
{
	struct vdi_simple *vs;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);

	VBE_DropRef(vs->backend);
	free(vs->dir.vcl_name);
	vs->dir.magic = 0;
	FREE_OBJ(vs);
}

void
VRT_init_dir_simple(struct cli *cli, struct director **bp, int idx,
    const void *priv)
{
	const struct vrt_backend *t;
	struct vdi_simple *vs;

	ASSERT_CLI();
	(void)cli;
	t = priv;

	ALLOC_OBJ(vs, VDI_SIMPLE_MAGIC);
	XXXAN(vs);
	vs->dir.magic = DIRECTOR_MAGIC;
	vs->dir.priv = vs;
	vs->dir.name = "simple";
	REPLACE(vs->dir.vcl_name, t->vcl_name);
	vs->dir.getfd = vdi_simple_getfd;
	vs->dir.fini = vdi_simple_fini;
	vs->dir.healthy = vdi_simple_healthy;

	vs->backend = VBE_AddBackend(cli, t);

	bp[idx] = &vs->dir;
}
