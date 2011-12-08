/*-
 * Copyright (c) 2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "cache.h"

struct vbo {
	unsigned		magic;
#define VBO_MAGIC		0xde3d8223
	struct lock		mtx;
	unsigned		refcount;
	struct busyobj		bo;
};

static struct lock vbo_mtx;
static struct vbo *nvbo;

void
VBO_Init(void)
{
	Lck_New(&vbo_mtx, lck_busyobj);
	nvbo = NULL;
}

/*--------------------------------------------------------------------
 * BusyObj handling
 */

static struct vbo *
vbo_New(void)
{
	struct vbo *vbo;

	ALLOC_OBJ(vbo, VBO_MAGIC);
	AN(vbo);
	Lck_New(&vbo->mtx, lck_busyobj);
	return (vbo);
}

void
VBO_Free(struct vbo **vbop)
{
	struct vbo *vbo;

	AN(vbop);
	vbo = *vbop;
	*vbop = NULL;
	CHECK_OBJ_NOTNULL(vbo, VBO_MAGIC);
	AZ(vbo->refcount);
	Lck_Delete(&vbo->mtx);
	FREE_OBJ(vbo);
}

struct busyobj *
VBO_GetBusyObj(struct worker *wrk)
{
	struct vbo *vbo = NULL;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	if (wrk->nvbo != NULL) {
		vbo = wrk->nvbo;
		wrk->nvbo = NULL;
	}

	if (vbo == NULL) {
		Lck_Lock(&vbo_mtx);

		vbo = nvbo;
		nvbo = NULL;

		if (vbo == NULL)
			VSC_C_main->busyobj_alloc++;

		Lck_Unlock(&vbo_mtx);
	}

	if (vbo == NULL)
		vbo = vbo_New();

	CHECK_OBJ_NOTNULL(vbo, VBO_MAGIC);
	AZ(vbo->refcount);
	AZ(vbo->bo.magic);
	vbo->refcount = 1;
	vbo->bo.magic = BUSYOBJ_MAGIC;
	vbo->bo.vbo = vbo;
	vbo->bo.beresp = wrk->x_beresp;
	vbo->bo.bereq = wrk->x_bereq;
	return (&vbo->bo);
}

void
VBO_RefBusyObj(const struct busyobj *busyobj)
{
	struct vbo *vbo;

	CHECK_OBJ_NOTNULL(busyobj, BUSYOBJ_MAGIC);
	vbo = busyobj->vbo;
	CHECK_OBJ_NOTNULL(vbo, VBO_MAGIC);
	Lck_Lock(&vbo->mtx);
	assert(vbo->refcount > 0);
	vbo->refcount++;
	Lck_Unlock(&vbo->mtx);
}

void
VBO_DerefBusyObj(struct worker *wrk, struct busyobj **pbo)
{
	struct busyobj *bo;
	struct vbo *vbo;
	unsigned r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(pbo);
	bo = *pbo;
	*pbo = NULL;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	vbo = bo->vbo;
	CHECK_OBJ_NOTNULL(vbo, VBO_MAGIC);
	Lck_Lock(&vbo->mtx);
	assert(vbo->refcount > 0);
	r = --vbo->refcount;
	Lck_Unlock(&vbo->mtx);

	if (r == 0) {
		/* XXX: Sanity checks & cleanup */
		memset(&vbo->bo, 0, sizeof vbo->bo);

		if (cache_param->bo_cache && wrk->nvbo == NULL) {
			wrk->nvbo = vbo;
		} else {
			Lck_Lock(&vbo_mtx);
			if (nvbo == NULL) {
				nvbo = vbo;
				vbo = NULL;
			} else
				VSC_C_main->busyobj_free++;
			Lck_Unlock(&vbo_mtx);

			if (vbo != NULL)
				VBO_Free(&vbo);
		}
	}
}
