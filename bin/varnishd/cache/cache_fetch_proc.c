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
 */

#include "config.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "hash/hash_slinger.h"

#include "cache_backend.h"
#include "vcli_priv.h"

static unsigned fetchfrag;

/*--------------------------------------------------------------------
 * We want to issue the first error we encounter on fetching and
 * supress the rest.  This function does that.
 *
 * Other code is allowed to look at busyobj->fetch_failed to bail out
 *
 * For convenience, always return -1
 */

enum vfp_status
VFP_Error(struct busyobj *bo, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	assert(bo->state >= BOS_REQ_DONE);
	if (!bo->failed) {
		va_start(ap, fmt);
		VSLbv(bo->vsl, SLT_FetchError, fmt, ap);
		va_end(ap);
		bo->failed = 1;
	}
	return (VFP_ERROR);
}

/*--------------------------------------------------------------------
 * Fetch Storage to put object into.
 *
 */

struct storage *
VFP_GetStorage(struct busyobj *bo, ssize_t sz)
{
	ssize_t l;
	struct storage *st;
	struct object *obj;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	obj = bo->fetch_obj;
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	st = VTAILQ_LAST(&obj->store, storagehead);
	if (st != NULL && st->len < st->space)
		return (st);

	AN(bo->stats);
	l = fetchfrag;
	if (l == 0)
		l = sz;
	if (l == 0)
		l = cache_param->fetch_chunksize;
	st = STV_alloc(bo, l);
	if (st == NULL) {
		(void)VFP_Error(bo, "Could not get storage");
	} else {
		AZ(st->len);
		Lck_Lock(&bo->mtx);
		VTAILQ_INSERT_TAIL(&obj->store, st, list);
		Lck_Unlock(&bo->mtx);
	}
	return (st);
}

/**********************************************************************
 */

static void
vfp_suck_fini(struct busyobj *bo)
{
	struct vfp_entry *vfe;

	VTAILQ_FOREACH(vfe, &bo->vfp, list)
		if(vfe->vfp->fini != NULL)
			vfe->vfp->fini(bo, vfe);
}

int
VFP_Open(struct busyobj *bo)
{
	struct vfp_entry *vfe;

	VTAILQ_FOREACH_REVERSE(vfe, &bo->vfp, vfp_entry_s, list) {
		if (vfe->vfp->init == NULL)
			continue;
		vfe->closed = vfe->vfp->init(bo, vfe);
		if (vfe->closed != VFP_OK && vfe->closed != VFP_NULL) {
			(void)VFP_Error(bo,
			    "Fetch filter %s failed to open",
			    vfe->vfp->name);
			vfp_suck_fini(bo);
			return (-1);
		}
	}
	return (0);
}

/**********************************************************************
 * Suck data up from lower levels.
 * Once a layer return non VFP_OK, clean it up and produce the same
 * return value for any subsequent calls.
 */

enum vfp_status
VFP_Suck(struct busyobj *bo, void *p, ssize_t *lp)
{
	enum vfp_status vp;
	struct vfp_entry *vfe;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AN(p);
	AN(lp);
	vfe = bo->vfp_nxt;
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	bo->vfp_nxt = VTAILQ_NEXT(vfe, list);

	if (vfe->closed == VFP_NULL) {
		vp = VFP_Suck(bo, p, lp);
	} else if (vfe->closed == VFP_OK) {
		vp = vfe->vfp->pull(bo, vfe, p, lp);
		if (vp == VFP_END || vp == VFP_ERROR) {
			vfe->closed = vp;
		} else if (vp != VFP_OK)
			(void)VFP_Error(bo, "Fetch filter %s returned %d",
			    vfe->vfp->name, vp);
	} else {
		/* Already closed filter */
		*lp = 0;
		vp = vfe->closed;
	}
	bo->vfp_nxt = vfe;
	return (vp);
}

/*--------------------------------------------------------------------
 */

void
VFP_Fetch_Body(struct busyobj *bo)
{
	ssize_t l;
	enum vfp_status vfps = VFP_ERROR;
	struct storage *st = NULL;
	ssize_t est;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AN(bo->vfp_nxt);

	est = bo->content_length;
	if (est < 0)
		est = 0;

	do {
		if (bo->abandon) {
			/*
			 * A pass object and delivery was terminted
			 * We don't fail the fetch, in order for hit-for-pass
			 * objects to be created.
			 */
			AN(bo->fetch_objcore->flags & OC_F_PASS);
			VSLb(bo->vsl, SLT_FetchError,
			    "Pass delivery abandoned");
			vfps = VFP_END;
			bo->should_close = 1;
			break;
		}
		AZ(bo->failed);
		if (st == NULL) {
			st = VFP_GetStorage(bo, est);
			est = 0;
		}
		if (st == NULL) {
			bo->should_close = 1;
			(void)VFP_Error(bo, "Out of storage");
			break;
		}

		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		assert(st == VTAILQ_LAST(&bo->fetch_obj->store, storagehead));
		l = st->space - st->len;
		AZ(bo->failed);
		vfps = VFP_Suck(bo, st->ptr + st->len, &l);
		if (l > 0 && vfps != VFP_ERROR) {
			AZ(VTAILQ_EMPTY(&bo->fetch_obj->store));
			VBO_extend(bo, l);
		}
		if (st->len == st->space)
			st = NULL;
	} while (vfps == VFP_OK);

	if (vfps == VFP_ERROR) {
		AN(bo->failed);
		(void)VFP_Error(bo, "Fetch Pipeline failed to process");
		bo->should_close = 1;
	}

	vfp_suck_fini(bo);

	if (!bo->do_stream)
		ObjTrimStore(bo->fetch_objcore, bo->stats);
}

void
VFP_Push(struct busyobj *bo, const struct vfp *vfp, intptr_t priv, int top)
{
	struct vfp_entry *vfe;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	vfe = (void*)WS_Alloc(bo->ws, sizeof *vfe);
	AN(vfe);
	vfe->magic = VFP_ENTRY_MAGIC;
	vfe->vfp = vfp;
	vfe->priv2 = priv;
	vfe->closed = VFP_OK;
	if (top)
		VTAILQ_INSERT_HEAD(&bo->vfp, vfe, list);
	else
		VTAILQ_INSERT_TAIL(&bo->vfp, vfe, list);
	if (VTAILQ_FIRST(&bo->vfp) == vfe)
		bo->vfp_nxt = vfe;
}

/*--------------------------------------------------------------------
 * Debugging aids
 */

static void
debug_fragfetch(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	(void)cli;
	fetchfrag = strtoul(av[2], NULL, 0);
}

static struct cli_proto debug_cmds[] = {
	{ "debug.fragfetch", "debug.fragfetch",
		"\tEnable fetch fragmentation\n", 1, 1, "d", debug_fragfetch },
	{ NULL }
};

/*--------------------------------------------------------------------
 *
 */

void
VFP_Init(void)
{

	CLI_AddFuncs(debug_cmds);
}
