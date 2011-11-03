/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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

#include "cache.h"

#include "cache_backend.h"
#include "vtcp.h"

/* Close a connection ------------------------------------------------*/

void
VDI_CloseFd(struct worker *wrk)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(wrk->vbc, VBC_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->vbc->backend, BACKEND_MAGIC);
	assert(wrk->vbc->fd >= 0);

	bp = wrk->vbc->backend;

	WSL(wrk, SLT_BackendClose, wrk->vbc->vsl_id, "%s", bp->display_name);

	/* Checkpoint log to flush all info related to this connection
	   before the OS reuses the FD */
	WSL_Flush(wrk, 0);

	VTCP_close(&wrk->vbc->fd);
	VBE_DropRefConn(bp);
	wrk->vbc->backend = NULL;
	VBE_ReleaseConn(wrk->vbc);
	wrk->vbc = NULL;
	wrk->do_close = 0;
}

/* Recycle a connection ----------------------------------------------*/

void
VDI_RecycleFd(struct worker *wrk)
{
	struct backend *bp;

	CHECK_OBJ_NOTNULL(wrk->vbc, VBC_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->vbc->backend, BACKEND_MAGIC);
	assert(wrk->vbc->fd >= 0);
	AZ(wrk->do_close);

	bp = wrk->vbc->backend;

	WSL(wrk, SLT_BackendReuse, wrk->vbc->vsl_id, "%s", bp->display_name);
	/*
	 * Flush the shmlog, so that another session reusing this backend
	 * will log chronologically later than our use of it.
	 */
	WSL_Flush(wrk, 0);
	Lck_Lock(&bp->mtx);
	VSC_C_main->backend_recycle++;
	VTAILQ_INSERT_HEAD(&bp->connlist, wrk->vbc, list);
	wrk->vbc = NULL;
	VBE_DropRefLocked(bp);
}

/* Get a connection --------------------------------------------------*/

struct vbc *
VDI_GetFd(const struct director *d, struct sess *sp)
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
VDI_Healthy(const struct director *d, const struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	return (d->healthy(d, sp));
}
