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
#include "cache_director.h"
#include "vrt.h"
#include "vtcp.h"

static struct mempool	*vbcpool;

static unsigned		vbcps = sizeof(struct vbc);

/*--------------------------------------------------------------------
 * The "simple" director really isn't, since thats where all the actual
 * connections happen.  Nonetheless, pretend it is simple by sequestering
 * the directoricity of it under this line.
 */

struct vbe_dir {
	unsigned		magic;
#define VDI_SIMPLE_MAGIC	0x476d25b7
	struct director		dir;
	struct backend		*backend;
	const struct vrt_backend *vrt;
};

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

/*--------------------------------------------------------------------*/

static void
bes_conn_try(struct busyobj *bo, struct vbc *vc, const struct vbe_dir *vs)
{
	int s, msec;
	double tmod;
	struct backend *bp = vs->backend;
	char abuf1[VTCP_ADDRBUFSIZE];
	char pbuf1[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(vs, VDI_SIMPLE_MAGIC);

	Lck_Lock(&bp->mtx);
	bp->refcount++;
	bp->n_conn++;		/* It mostly works */
	bp->vsc->conn++;
	Lck_Unlock(&bp->mtx);

	s = -1;
	assert(bp->ipv6 != NULL || bp->ipv4 != NULL);

	/* release lock during stuff that can take a long time */

	FIND_TMO(connect_timeout, tmod, bo, vs->vrt);
	msec = (int)floor(tmod * 1000.0);

	if (cache_param->prefer_ipv6 && bp->ipv6 != NULL) {
		s = VTCP_connect(bp->ipv6, msec);
		vc->addr = bp->ipv6;
	}
	if (s == -1 && bp->ipv4 != NULL) {
		s = VTCP_connect(bp->ipv4, msec);
		vc->addr = bp->ipv4;
	}
	if (s == -1 && !cache_param->prefer_ipv6 && bp->ipv6 != NULL) {
		s = VTCP_connect(bp->ipv6, msec);
		vc->addr = bp->ipv6;
	}

	vc->fd = s;
	if (s < 0) {
		Lck_Lock(&bp->mtx);
		bp->n_conn--;
		bp->vsc->conn--;
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
vbe_GetVbe(struct busyobj *bo, struct vbe_dir *vs)
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
			    vc->fd, bo->director_resp->vcl_name,
			    bp->display_name);
			vc->vdis = vs;
			vc->recycled = 1;
			return (vc);
		}
		VSC_C_main->backend_toolate++;
		VSLb(bo->vsl, SLT_BackendClose, "%d %s toolate",
		    vc->fd, bp->display_name);

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

	vc = MPL_Get(vbcpool, NULL);
	XXXAN(vc);
	vc->magic = VBC_MAGIC;
	vc->fd = -1;
	bes_conn_try(bo, vc, vs);
	if (vc->fd < 0) {
		VBE_ReleaseConn(vc);
		VSC_C_main->backend_fail++;
		return (NULL);
	}
	vc->backend = bp;
	VSC_C_main->backend_conn++;
	VSLb(bo->vsl, SLT_Backend, "%d %s %s",
	    vc->fd, bo->director_resp->vcl_name, bp->display_name);
	vc->vdis = vs;
	return (vc);
}

/*--------------------------------------------------------------------
 *
 */

void
VBE_UseHealth(const struct director *vdi)
{
	struct vbe_dir *vs;

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
	struct vbe_dir *vs;

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

static int __match_proto__(vdi_getfd_f)
vbe_dir_getfd(const struct director *d, struct busyobj *bo)
{
	struct vbe_dir *vs;
	struct vbc *vc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);

	vc = vbe_GetVbe(bo, vs);
	if (vc == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "no backend connection");
		return (-1);
	}

	vc->backend->vsc->req++;
	if (bo->htc == NULL)
		bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	AN(bo->htc);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);
	bo->htc->vbc = vc;
	FIND_TMO(first_byte_timeout, vc->first_byte_timeout, bo, vs->vrt);
	FIND_TMO(between_bytes_timeout, vc->between_bytes_timeout, bo, vs->vrt);
	return (vc->fd);
}

static unsigned __match_proto__(vdi_healthy_f)
vbe_dir_healthy(const struct director *d, const struct busyobj *bo,
    double *changed)
{
	struct vbe_dir *vs;
	struct backend *be;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);
	be = vs->backend;
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	return (VBE_Healthy(be, changed));
}

static void __match_proto__(vdi_finish_f)
vbe_dir_finish(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	if (bo->htc->vbc == NULL)
		return;
	bp = bo->htc->vbc->backend;
	if (bo->doclose != SC_NULL) {
		VSLb(bo->vsl, SLT_BackendClose, "%d %s", bo->htc->vbc->fd,
		    bp->display_name);
		VTCP_close(&bo->htc->vbc->fd);
		VBE_DropRefConn(bp, &bo->acct);
		bo->htc->vbc->backend = NULL;
		VBE_ReleaseConn(bo->htc->vbc);
	} else {
		VSLb(bo->vsl, SLT_BackendReuse, "%d %s", bo->htc->vbc->fd,
		    bp->display_name);
		Lck_Lock(&bp->mtx);
		VSC_C_main->backend_recycle++;
		VTAILQ_INSERT_HEAD(&bp->connlist, bo->htc->vbc, list);
		VBE_DropRefLocked(bp, &bo->acct);
	}
	bo->htc->vbc = NULL;
	bo->htc = NULL;
}

static int __match_proto__(vdi_gethdrs_f)
vbe_dir_gethdrs(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{
	int i;
	struct vbe_dir *vs;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);

	i = vbe_dir_getfd(d, bo);
	if (i < 0) {
		VSLb(bo->vsl, SLT_FetchError, "no backend connection");
		return (-1);
	}
	AN(bo->htc);

	i = V1F_fetch_hdr(wrk, bo, vs->vrt->hosthdr);
	/*
	 * If we recycle a backend connection, there is a finite chance
	 * that the backend closed it before we get a request to it.
	 * Do a single retry in that case.
	 */
	if (i == 1) {
		vbe_dir_finish(d, wrk, bo);
		AZ(bo->htc);
		VSC_C_main->backend_retry++;
		bo->doclose = SC_NULL;
		i = vbe_dir_getfd(d, bo);
		if (i < 0) {
			VSLb(bo->vsl, SLT_FetchError, "no backend connection");
			bo->htc = NULL;
			return (-1);
		}
		AN(bo->htc);
		i = V1F_fetch_hdr(wrk, bo, vs->vrt->hosthdr);
	}
	if (i != 0) {
		vbe_dir_finish(d, wrk, bo);
		bo->doclose = SC_NULL;
		AZ(bo->htc);
	} else {
		AN(bo->htc->vbc);
	}
	return (i);
}

static int __match_proto__(vdi_getbody_f)
vbe_dir_getbody(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	V1F_Setup_Fetch(bo->vfc, bo->htc);
	return (0);
}

/*--------------------------------------------------------------------*/

void
VRT_fini_vbe(VRT_CTX, struct director *d)
{
	struct vbe_dir *vs;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_SIMPLE_MAGIC);

	VBE_DropRefVcl(vs->backend);
	free(vs->dir.vcl_name);
	vs->dir.magic = 0;
	FREE_OBJ(vs);
	d->priv = NULL;
}

static void
vbe_dir_http1pipe(const struct director *d, struct req *req, struct busyobj *bo)
{
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	i = vbe_dir_getfd(d, bo);
	V1P_Process(req, bo, i);
	vbe_dir_finish(d, bo->wrk, bo);
}

void
VRT_init_vbe(VRT_CTX, struct director **bp, int idx,
    const struct vrt_backend *vrt)
{
	struct vbe_dir *vs;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ALLOC_OBJ(vs, VDI_SIMPLE_MAGIC);
	XXXAN(vs);
	vs->dir.magic = DIRECTOR_MAGIC;
	vs->dir.priv = vs;
	vs->dir.name = "simple";
	REPLACE(vs->dir.vcl_name, vrt->vcl_name);
	vs->dir.http1pipe = vbe_dir_http1pipe;
	vs->dir.healthy = vbe_dir_healthy;
	vs->dir.gethdrs = vbe_dir_gethdrs;
	vs->dir.getbody = vbe_dir_getbody;
	vs->dir.finish = vbe_dir_finish;

	vs->vrt = vrt;

	vs->backend = VBE_AddBackend(NULL, vrt);
	if (vs->vrt->probe != NULL)
		VBP_Insert(vs->backend, vs->vrt->probe, vs->vrt->hosthdr);

	bp[idx] = &vs->dir;
}

void
VBE_Init(void)
{

	vbcpool = MPL_New("vbc", &cache_param->vbc_pool, &vbcps);
	AN(vbcpool);
}
