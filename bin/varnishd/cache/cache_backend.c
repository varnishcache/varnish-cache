/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * The director implementation for VCL backends.
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache.h"

#include "cache_backend.h"
#include "cache_director.h"
#include "vrt.h"
#include "vtcp.h"

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
 * Get a connection to the backend
 */

static int __match_proto__(vdi_getfd_f)
vbe_dir_getfd(const struct director *d, struct busyobj *bo)
{
	struct vbc *vc;
	struct backend *bp;
	double tmod;
	const struct vrt_backend *vrt;
	char abuf1[VTCP_ADDRBUFSIZE], abuf2[VTCP_ADDRBUFSIZE];
	char pbuf1[VTCP_PORTBUFSIZE], pbuf2[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(bp, d->priv, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(vrt, d->priv2, VRT_BACKEND_MAGIC);

	if (!VBE_Healthy(bp, NULL)) {
		// XXX: per backend stats ?
		VSC_C_main->backend_unhealthy++;
		return (-1);
	}

	if (vrt->max_connections > 0 &&
	    bp->n_conn >= vrt->max_connections) {
		// XXX: per backend stats ?
		VSC_C_main->backend_busy++;
		return (-1);
	}

	FIND_TMO(connect_timeout, tmod, bo, vrt);
	vc = VBT_Get(bp->tcp_pool, tmod);
	if (vc == NULL) {
		// XXX: Per backend stats ?
		VSC_C_main->backend_fail++;
		VSLb(bo->vsl, SLT_FetchError, "no backend connection");
		return (-1);
	}

	assert(vc->fd >= 0);
	vc->backend = bp;
	AN(vc->addr);

	Lck_Lock(&bp->mtx);
	bp->refcount++;
	bp->n_conn++;
	bp->vsc->conn++;
	Lck_Unlock(&bp->mtx);

	VTCP_myname(vc->fd, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	VTCP_hisname(vc->fd, abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	VSLb(bo->vsl, SLT_BackendOpen, "%d %s %s %s %s %s",
	    vc->fd, bp->display_name, abuf2, pbuf2, abuf1, pbuf1);

	vc->backend->vsc->req++;
	if (bo->htc == NULL)
		bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	AN(bo->htc);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);
	bo->htc->vbc = vc;
	bo->htc->fd = vc->fd;
	FIND_TMO(first_byte_timeout,
	    bo->htc->first_byte_timeout, bo, vrt);
	FIND_TMO(between_bytes_timeout,
	    bo->htc->between_bytes_timeout, bo, vrt);
	return (vc->fd);
}

static unsigned __match_proto__(vdi_healthy_f)
vbe_dir_healthy(const struct director *d, const struct busyobj *bo,
    double *changed)
{
	struct backend *be;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_ORNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);
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
		VBT_Close(bp->tcp_pool, &bo->htc->vbc);
		Lck_Lock(&bp->mtx);
	} else {
		VSLb(bo->vsl, SLT_BackendReuse, "%d %s", bo->htc->vbc->fd,
		    bp->display_name);
		Lck_Lock(&bp->mtx);
		VSC_C_main->backend_recycle++;
		VBT_Recycle(bp->tcp_pool, &bo->htc->vbc);
	}
#define ACCT(foo)	bp->vsc->foo += bo->acct.foo;
#include "tbl/acct_fields_bereq.h"
#undef ACCT
	Lck_Unlock(&bp->mtx);
	bo->htc->vbc = NULL;
	bo->htc = NULL;
}

static int __match_proto__(vdi_gethdrs_f)
vbe_dir_gethdrs(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{
	int i;
	const struct vrt_backend *vrt;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(vrt, d->priv2, VRT_BACKEND_MAGIC);

	i = vbe_dir_getfd(d, bo);
	if (i < 0) {
		VSLb(bo->vsl, SLT_FetchError, "no backend connection");
		return (-1);
	}
	AN(bo->htc);

	i = V1F_fetch_hdr(wrk, bo, vrt->hosthdr);
	/*
	 * If we recycle a backend connection, there is a finite chance
	 * that the backend closed it before we get a request to it.
	 * Do a single retry in that case.
	 */
	if (i == 1 && bo->htc->vbc->recycled) {
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
		i = V1F_fetch_hdr(wrk, bo, vrt->hosthdr);
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
	CHECK_OBJ_NOTNULL(bo->vfc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);

	V1F_Setup_Fetch(bo->vfc, bo->htc);
	return (0);
}

static const struct suckaddr * __match_proto__(vdi_getip_f)
vbe_dir_getip(const struct director *d, struct worker *wrk,
    struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc->vbc, VBC_MAGIC);

	return (bo->htc->vbc->addr);
}

/*--------------------------------------------------------------------*/

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

/*--------------------------------------------------------------------*/

void
VRT_init_vbe(VRT_CTX, struct director **dp, const struct vrt_backend *vrt)
{
	struct director *d;
	struct backend *be;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(dp);
	AZ(*dp);
	CHECK_OBJ_NOTNULL(vrt, VRT_BACKEND_MAGIC);

	be = VBE_AddBackend(vrt);
	AN(be);
	ALLOC_OBJ(d, DIRECTOR_MAGIC);
	XXXAN(d);
	d->priv = be;
	d->priv2 = vrt;
	d->name = "backend";
	REPLACE(d->vcl_name, vrt->vcl_name);
	d->http1pipe = vbe_dir_http1pipe;
	d->healthy = vbe_dir_healthy;
	d->gethdrs = vbe_dir_gethdrs;
	d->getbody = vbe_dir_getbody;
	d->getip = vbe_dir_getip;
	d->finish = vbe_dir_finish;

	if (vrt->probe != NULL)
		VBP_Insert(be, vrt->probe, vrt->hosthdr);

	*dp = d;
}

void
VRT_use_vbe(VRT_CTX, const struct director *d, const struct vrt_backend *vrt)
{
	struct backend *be;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(vrt, VRT_BACKEND_MAGIC);
	assert(d->priv2 == vrt);

	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);

	if (vrt->probe == NULL)
		return;

	VBP_Use(be, vrt->probe);
}

void
VRT_fini_vbe(VRT_CTX, struct director **dp, const struct vrt_backend *vrt)
{
	struct director *d;
	struct backend *be;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(dp);
	AN(*dp);
	CHECK_OBJ_NOTNULL(vrt, VRT_BACKEND_MAGIC);

	d = *dp;
	*dp = NULL;
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	assert(d->priv2 == vrt);
	CAST_OBJ_NOTNULL(be, d->priv, BACKEND_MAGIC);

	if (vrt->probe != NULL)
		VBP_Remove(be, vrt->probe);

	VBE_Drop(be);
	free(d->vcl_name);
	FREE_OBJ(d);
}
