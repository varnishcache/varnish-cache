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
 * Abstract backend API
 *
 */

#include "config.h"

#include "cache.h"

#include "cache_backend.h"
#include "vtcp.h"

/* Close a connection ------------------------------------------------*/

void
VDI_CloseFd(struct vbc **vbp, const struct acct_bereq *acct_bereq)
{
	struct backend *bp;
	struct vbc *vc;

	AN(vbp);
	vc = *vbp;
	*vbp = NULL;
	CHECK_OBJ_NOTNULL(vc, VBC_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	assert(vc->fd >= 0);

	bp = vc->backend;

	VSLb(vc->vsl, SLT_BackendClose, "%d %s", vc->fd, bp->display_name);

	vc->vsl = NULL;
	VTCP_close(&vc->fd);
	VBE_DropRefConn(bp, acct_bereq);
	vc->backend = NULL;
	VBE_ReleaseConn(vc);
}

/* Recycle a connection ----------------------------------------------*/

void
VDI_RecycleFd(struct vbc **vbp, const struct acct_bereq *acct_bereq)
{
	struct backend *bp;
	struct vbc *vc;

	AN(vbp);
	vc = *vbp;
	*vbp = NULL;
	CHECK_OBJ_NOTNULL(vc, VBC_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	assert(vc->fd >= 0);

	bp = vc->backend;

	VSLb(vc->vsl, SLT_BackendReuse, "%d %s", vc->fd, bp->display_name);

	vc->vsl = NULL;

	Lck_Lock(&bp->mtx);
	VSC_C_main->backend_recycle++;
	VTAILQ_INSERT_HEAD(&bp->connlist, vc, list);
	VBE_DropRefLocked(bp, acct_bereq);
}

/* Resolve director --------------------------------------------------*/

static const struct director *
vdi_resolve(struct worker *wrk, struct busyobj *bo, const struct director *d)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	if (d == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "No backend");
		return (NULL);
	}

	while (d != NULL && d->resolve != NULL) {
		CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
		d = d->resolve(d, wrk, bo);
	}
	CHECK_OBJ_ORNULL(d, DIRECTOR_MAGIC);
	if (d == NULL)
		VSLb(bo->vsl, SLT_FetchError, "Backend selection failed");
	bo->director_resp = d;
	return (d);
}

/* Get a set of response headers -------------------------------------*/

int
VDI_GetHdr(struct worker *wrk, struct busyobj *bo)
{

	const struct director *d;

	d = vdi_resolve(wrk, bo, bo->director_req);
	if (d == NULL)
		return (-1);

	AN(d->gethdrs);
	return (d->gethdrs(d, wrk, bo));
}

/* Get a connection --------------------------------------------------*/

struct vbc *
VDI_GetFd(const struct director *d, struct worker *wrk, struct busyobj *bo)
{
	struct vbc *vc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);

	d = vdi_resolve(wrk, bo, d);
	if (d == NULL)
		return (NULL);

	AN(d->getfd);
	vc = d->getfd(d, bo);
	if (vc != NULL)
		vc->vsl = bo->vsl;
	return (vc);
}

/* Check health ------------------------------------------------------
 */

int
VDI_Healthy(const struct director *d)
{

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	AN(d->healthy);
	return (d->healthy(d, NULL));
}
