/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <poll.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "cache.h"

#include "cache_backend.h"
#include "vrt.h"
#include "vtcp.h"

static struct mempool	*vbcpool;

static unsigned		vbcps = sizeof(struct vbc);

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
	const struct vrt_backend *vrt;
};

/*--------------------------------------------------------------------
 * Create default Host: header for backend request
 */
void
VDI_AddHostHeader(struct http *hp, const struct vbc *vbc)
{

	CHECK_OBJ_NOTNULL(vbc, VBC_MAGIC);
	CHECK_OBJ_NOTNULL(vbc->vdis, VDI_SIMPLE_MAGIC);
	http_PrintfHeader(hp,
	    "Host: %s", vbc->vdis->vrt->hosthdr);
}

/*--------------------------------------------------------------------*/

/* Private interface from backend_cfg.c */
void
VBE_ReleaseConn(struct vbc *vc)
{

	CHECK_OBJ_NOTNULL(vc, VBC_MAGIC);
	assert(vc->backend == NULL);
	assert(vc->fd < 0);
	MPL_Free(vbcpool, vc);
}

#define FIND_TMO(tmx, dst, req, be)					\
	do {								\
		CHECK_OBJ_NOTNULL(req->busyobj, BUSYOBJ_MAGIC);		\
		dst = req->busyobj->tmx;				\
		if (dst == 0.0)						\
			dst = be->tmx;					\
		if (dst == 0.0)						\
			dst = cache_param->tmx;				\
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
vbe_TryConnect(const struct req *req, int pf, const struct sockaddr_storage *sa,
    socklen_t salen, const struct vdi_simple *vs)
{
	int s, i, tmo;
	double tmod;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(vs, VDI_SIMPLE_MAGIC);

	s = socket(pf, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	FIND_TMO(connect_timeout, tmod, req, vs->vrt);

	tmo = (int)(tmod * 1000.0);

	i = VTCP_connect(s, sa, salen, tmo);

	if (i != 0) {
		AZ(close(s));
		return (-1);
	}

	return (s);
}

/*--------------------------------------------------------------------*/

static void
bes_conn_try(struct req *req, struct vbc *vc, const struct vdi_simple *vs)
{
	int s;
	struct backend *bp = vs->backend;
	char abuf1[VTCP_ADDRBUFSIZE];
	char pbuf1[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(vs, VDI_SIMPLE_MAGIC);

	Lck_Lock(&bp->mtx);
	bp->refcount++;
	bp->n_conn++;		/* It mostly works */
	Lck_Unlock(&bp->mtx);

	s = -1;
	assert(bp->ipv6 != NULL || bp->ipv4 != NULL);

	/* release lock during stuff that can take a long time */

	if (cache_param->prefer_ipv6 && bp->ipv6 != NULL) {
		s = vbe_TryConnect(req, PF_INET6, bp->ipv6, bp->ipv6len, vs);
		vc->addr = bp->ipv6;
		vc->addrlen = bp->ipv6len;
	}
	if (s == -1 && bp->ipv4 != NULL) {
		s = vbe_TryConnect(req, PF_INET, bp->ipv4, bp->ipv4len, vs);
		vc->addr = bp->ipv4;
		vc->addrlen = bp->ipv4len;
	}
	if (s == -1 && !cache_param->prefer_ipv6 && bp->ipv6 != NULL) {
		s = vbe_TryConnect(req, PF_INET6, bp->ipv6, bp->ipv6len, vs);
		vc->addr = bp->ipv6;
		vc->addrlen = bp->ipv6len;
	}

	vc->fd = s;
	if (s < 0) {
		Lck_Lock(&bp->mtx);
		bp->n_conn--;
		bp->refcount--;		/* Only keep ref on success */
		Lck_Unlock(&bp->mtx);
		vc->addr = NULL;
		vc->addrlen = 0;
	} else {
		VTCP_myname(s, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
		VSLb(req->vsl, SLT_BackendOpen, "%d %s %s %s ",
		    vc->fd, vs->backend->display_name, abuf1, pbuf1);
	}

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
 * Manage a pool of vbc structures.
 * XXX: as an experiment, make this caching controled by a parameter
 * XXX: so we can see if it has any effect.
 */

static struct vbc *
vbe_NewConn(void)
{
	struct vbc *vc;

	vc = MPL_Get(vbcpool, NULL);
	XXXAN(vc);
	vc->magic = VBC_MAGIC;
	vc->fd = -1;
	return (vc);
}

/*--------------------------------------------------------------------
 * It evaluates if a backend is healthy _for_a_specific_object_.
 * That means that it relies on req->objcore->objhead. This is mainly for
 * saint-mode, but also takes backend->healthy into account. If
 * cache_param->saintmode_threshold is 0, this is basically just a test of
 * backend->healthy.
 *
 * The threshold has to be evaluated _after_ the timeout check, otherwise
 * items would never time out once the threshold is reached.
 */

static unsigned int
vbe_Healthy(const struct vdi_simple *vs, const struct req *req)
{
	struct trouble *tr;
	struct trouble *tr2;
	struct trouble *old;
	unsigned i = 0, retval;
	unsigned int threshold;
	struct backend *backend;
	double now;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(vs, VDI_SIMPLE_MAGIC);
	backend = vs->backend;
	CHECK_OBJ_NOTNULL(backend, BACKEND_MAGIC);

	if (backend->admin_health == ah_probe && !backend->healthy)
		return (0);

	if (backend->admin_health == ah_sick)
		return (0);

	/* VRT/VCC sets threshold to UINT_MAX to mark that it's not
	 * specified by VCL (thus use param).
	 */
	threshold = vs->vrt->saintmode_threshold;
	if (threshold == UINT_MAX)
		threshold = cache_param->saintmode_threshold;

	if (backend->admin_health == ah_healthy)
		threshold = UINT_MAX;

	/* Saintmode is disabled, or list is empty */
	if (threshold == 0 || VTAILQ_EMPTY(&backend->troublelist))
		return (1);

	now = req->t_req;

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

		if (!memcmp(tr->digest, req->digest, sizeof tr->digest)) {
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

static struct vbc *
vbe_GetVbe(struct req *req, struct vdi_simple *vs)
{
	struct vbc *vc;
	struct backend *bp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(vs, VDI_SIMPLE_MAGIC);
	bp = vs->backend;
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);

	/* first look for vbc's we can recycle */
	while (1) {
		Lck_Lock(&bp->mtx);
		vc = VTAILQ_FIRST(&bp->connlist);
		if (vc != NULL) {
			bp->refcount++;
			assert(vc->backend == bp);
			assert(vc->fd >= 0);
			AN(vc->addr);
			VTAILQ_REMOVE(&bp->connlist, vc, list);
		}
		Lck_Unlock(&bp->mtx);
		if (vc == NULL)
			break;
		if (vbe_CheckFd(vc->fd)) {
			/* XXX locking of stats */
			VSC_C_main->backend_reuse += 1;
			VSLb(req->vsl, SLT_Backend, "%d %s %s",
			    vc->fd, req->director->vcl_name,
			    bp->display_name);
			vc->vdis = vs;
			vc->recycled = 1;
			return (vc);
		}
		VSC_C_main->backend_toolate++;
		VSLb(req->vsl, SLT_BackendClose, "%d %s toolate",
		    vc->fd, bp->display_name);

		/* Checkpoint log to flush all info related to this connection
		   before the OS reuses the FD */
		VSL_Flush(req->vsl, 0);

		VTCP_close(&vc->fd);
		VBE_DropRefConn(bp);
		vc->backend = NULL;
		VBE_ReleaseConn(vc);
	}

	if (!vbe_Healthy(vs, req)) {
		VSC_C_main->backend_unhealthy++;
		return (NULL);
	}

	if (vs->vrt->max_connections > 0 &&
	    bp->n_conn >= vs->vrt->max_connections) {
		VSC_C_main->backend_busy++;
		return (NULL);
	}

	vc = vbe_NewConn();
	assert(vc->fd == -1);
	AZ(vc->backend);
	bes_conn_try(req, vc, vs);
	if (vc->fd < 0) {
		VBE_ReleaseConn(vc);
		VSC_C_main->backend_fail++;
		return (NULL);
	}
	vc->backend = bp;
	VSC_C_main->backend_conn++;
	VSLb(req->vsl, SLT_Backend, "%d %s %s",
	    vc->fd, req->director->vcl_name, bp->display_name);
	vc->vdis = vs;
	return (vc);
}

/*--------------------------------------------------------------------
 *
 */

void
VBE_UseHealth(const struct director *vdi)
{
	struct vdi_simple *vs;

	ASSERT_CLI();

	if (strcmp(vdi->name, "simple"))
		return;
	CAST_OBJ_NOTNULL(vs, vdi->priv, VDI_SIMPLE_MAGIC);
	if (vs->vrt->probe == NULL)
		return;
	VBP_Use(vs->backend, vs->vrt->probe);
}

/*--------------------------------------------------------------------
 *
 */

void
VBE_DiscardHealth(const struct director *vdi)
{
	struct vdi_simple *vs;

	ASSERT_CLI();

	if (strcmp(vdi->name, "simple"))
		return;
	CAST_OBJ_NOTNULL(vs, vdi->priv, VDI_SIMPLE_MAGIC);
	if (vs->vrt->probe == NULL)
		return;
	VBP_Remove(vs->backend, vs->vrt->probe);
}

/*--------------------------------------------------------------------
 *
 */

static struct vbc * __match_proto__(vdi_getfd_f)
vdi_simple_getfd(const struct director *d, struct req *req)
{
	struct vdi_simple *vs;
	struct vbc *vc;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);
	vc = vbe_GetVbe(req, vs);
	if (vc != NULL) {
		FIND_TMO(first_byte_timeout,
		    vc->first_byte_timeout, req, vs->vrt);
		FIND_TMO(between_bytes_timeout,
		    vc->between_bytes_timeout, req, vs->vrt);
	}
	return (vc);
}

static unsigned
vdi_simple_healthy(const struct director *d, const struct req *req)
{
	struct vdi_simple *vs;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);
	return (vbe_Healthy(vs, req));
}

static void
vdi_simple_fini(const struct director *d)
{
	struct vdi_simple *vs;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);

	VBE_DropRefVcl(vs->backend);
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

	vs->vrt = t;

	vs->backend = VBE_AddBackend(cli, t);
	if (vs->vrt->probe != NULL)
		VBP_Insert(vs->backend, vs->vrt->probe, vs->vrt->hosthdr);

	bp[idx] = &vs->dir;
}

void
VDI_Init(void)
{

	vbcpool = MPL_New("vbc", &cache_param->vbc_pool, &vbcps);
	AN(vbcpool);
}
