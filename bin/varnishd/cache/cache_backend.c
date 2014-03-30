/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 * connections happen.  Nonetheless, pretend it is simple by sequestering
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

#define FIND_TMO(tmx, dst, bo, be)					\
	do {								\
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);			\
		dst = bo->tmx;						\
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
vbe_TryConnect(const struct busyobj *bo, int pf,
    const struct suckaddr *sa, const struct vdi_simple *vs)
{
	int s, i, tmo;
	double tmod;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(vs, VDI_SIMPLE_MAGIC);

	s = socket(pf, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	FIND_TMO(connect_timeout, tmod, bo, vs->vrt);

	tmo = (int)(tmod * 1000.0);

	i = VTCP_connect(s, sa, tmo);

	if (i != 0) {
		AZ(close(s));
		return (-1);
	}

	return (s);
}

/*--------------------------------------------------------------------*/

static void
bes_conn_try(struct busyobj *bo, struct vbc *vc, const struct vdi_simple *vs)
{
	int s;
	struct backend *bp = vs->backend;
	char abuf1[VTCP_ADDRBUFSIZE];
	char pbuf1[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(vs, VDI_SIMPLE_MAGIC);

	Lck_Lock(&bp->mtx);
	bp->refcount++;
	bp->n_conn++;		/* It mostly works */
	Lck_Unlock(&bp->mtx);

	s = -1;
	assert(bp->ipv6 != NULL || bp->ipv4 != NULL);

	/* release lock during stuff that can take a long time */

	if (cache_param->prefer_ipv6 && bp->ipv6 != NULL) {
		s = vbe_TryConnect(bo, PF_INET6, bp->ipv6, vs);
		vc->addr = bp->ipv6;
	}
	if (s == -1 && bp->ipv4 != NULL) {
		s = vbe_TryConnect(bo, PF_INET, bp->ipv4, vs);
		vc->addr = bp->ipv4;
	}
	if (s == -1 && !cache_param->prefer_ipv6 && bp->ipv6 != NULL) {
		s = vbe_TryConnect(bo, PF_INET6, bp->ipv6, vs);
		vc->addr = bp->ipv6;
	}

	vc->fd = s;
	if (s < 0) {
		Lck_Lock(&bp->mtx);
		bp->n_conn--;
		bp->refcount--;		/* Only keep ref on success */
		Lck_Unlock(&bp->mtx);
		vc->addr = NULL;
	} else {
		VTCP_myname(s, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
		VSLb(bo->vsl, SLT_BackendOpen, "%d %s %s %s ",
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
 * XXX: as an experiment, make this caching controlled by a parameter
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
 * Test if backend is healthy and report when it last changed
 */

unsigned
VBE_Healthy(const struct backend *backend, double *changed)
{
	CHECK_OBJ_NOTNULL(backend, BACKEND_MAGIC);

	if (changed != NULL)
		*changed = backend->health_changed;

	if (backend->admin_health == ah_probe && !backend->healthy)
		return (0);

	if (backend->admin_health == ah_sick)
		return (0);

	return (1);
}

/*--------------------------------------------------------------------
 * Get a connection to a particular backend.
 */

static struct vbc *
vbe_GetVbe(struct busyobj *bo, struct vdi_simple *vs)
{
	struct vbc *vc;
	struct backend *bp;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
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
			VSLb(bo->vsl, SLT_Backend, "%d %s %s",
			    vc->fd, bo->director->vcl_name,
			    bp->display_name);
			vc->vdis = vs;
			vc->recycled = 1;
			return (vc);
		}
		VSC_C_main->backend_toolate++;
		VSLb(bo->vsl, SLT_BackendClose, "%d %s toolate",
		    vc->fd, bp->display_name);

		/* Checkpoint log to flush all info related to this connection
		   before the OS reuses the FD */
		VSL_Flush(bo->vsl, 0);

		VTCP_close(&vc->fd);
		VBE_DropRefConn(bp, NULL);
		vc->backend = NULL;
		VBE_ReleaseConn(vc);
	}

	if (!VBE_Healthy(bp, NULL)) {
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
	bes_conn_try(bo, vc, vs);
	if (vc->fd < 0) {
		VBE_ReleaseConn(vc);
		VSC_C_main->backend_fail++;
		return (NULL);
	}
	vc->backend = bp;
	VSC_C_main->backend_conn++;
	VSLb(bo->vsl, SLT_Backend, "%d %s %s",
	    vc->fd, bo->director->vcl_name, bp->display_name);
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
vdi_simple_getfd(const struct director *d, struct busyobj *bo)
{
	struct vdi_simple *vs;
	struct vbc *vc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);
	vc = vbe_GetVbe(bo, vs);
	if (vc != NULL) {
		FIND_TMO(first_byte_timeout,
		    vc->first_byte_timeout, bo, vs->vrt);
		FIND_TMO(between_bytes_timeout,
		    vc->between_bytes_timeout, bo, vs->vrt);
	}
	return (vc);
}

static unsigned
vdi_simple_healthy(const struct director *d, double *changed)
{
	struct vdi_simple *vs;
	struct backend *be;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);
	be = vs->backend;
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	return (VBE_Healthy(be, changed));
}

/*--------------------------------------------------------------------*/

void
VRT_fini_dir(struct cli *cli, struct director *d)
{
	struct vdi_simple *vs;

	(void)cli;
	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);

	VBE_DropRefVcl(vs->backend);
	free(vs->dir.vcl_name);
	vs->dir.magic = 0;
	FREE_OBJ(vs);
	d->priv = NULL;
}

void
VRT_init_dir(struct cli *cli, struct director **bp, int idx, const void *priv)
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
