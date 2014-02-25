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

char vfp_init[] = "<init>";
char vfp_fini[] = "<fini>";

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

static enum vfp_status
vfp_call(struct busyobj *bo, int nbr, void *p, ssize_t *lp)
{
	AN(bo->vfps[nbr]);
	return (bo->vfps[nbr](bo, p, lp, &bo->vfps_priv[nbr]));
}

static void
vfp_suck_fini(struct busyobj *bo)
{
	int i;

	for (i = 0; i < bo->vfp_nxt; i++) {
		if(bo->vfps[i] != NULL)
			(void)vfp_call(bo, i, vfp_fini, NULL);
	}
}

static enum vfp_status
vfp_suck_init(struct busyobj *bo)
{
	enum vfp_status retval = VFP_ERROR;
	int i;

	for (i = 0; i < bo->vfp_nxt; i++) {
		retval = vfp_call(bo, i, vfp_init, NULL);
		if (retval != VFP_OK) {
			vfp_suck_fini(bo);
			break;
		}
	}
	return (retval);
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

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AN(p);
	AN(lp);
	assert(bo->vfp_nxt > 0);
	bo->vfp_nxt--;
	if (bo->vfps[bo->vfp_nxt] == NULL) {
		*lp = 0;
		vp = (enum vfp_status)bo->vfps_priv[bo->vfp_nxt];
	} else {
		vp = vfp_call(bo, bo->vfp_nxt, p, lp);
		if (vp != VFP_OK) {
			(void)vfp_call(bo, bo->vfp_nxt, vfp_fini, NULL);
			bo->vfps[bo->vfp_nxt] = NULL;
			bo->vfps_priv[bo->vfp_nxt] = vp;
		}
	}
	bo->vfp_nxt++;
	return (vp);
}

/*--------------------------------------------------------------------
 */

void
VFP_Fetch_Body(struct busyobj *bo, ssize_t est)
{
	ssize_t l;
	enum vfp_status vfps = VFP_ERROR;
	struct storage *st = NULL;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AN(bo->vfp_nxt);

	if (est < 0)
		est = 0;

	if (vfp_suck_init(bo) != VFP_OK) {
		(void)VFP_Error(bo, "Fetch Pipeline failed to initialize");
		bo->should_close = 1;
		return;
	}

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
			assert(!VTAILQ_EMPTY(&bo->fetch_obj->store));
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

	/*
	 * Trim or delete the last segment, if any
	 */

	st = VTAILQ_LAST(&bo->fetch_obj->store, storagehead);
	/* XXX: Temporary:  Only trim if we are not streaming */
	if (st != NULL && !bo->do_stream) {
		/* None of this this is safe under streaming */
		if (st->len == 0) {
			VTAILQ_REMOVE(&bo->fetch_obj->store, st, list);
			STV_free(st);
		} else if (st->len < st->space) {
			STV_trim(st, st->len, 1);
		}
	}
}

void
VFP_Push(struct busyobj *bo, vfp_pull_f *func, intptr_t priv)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	bo->vfps_priv[bo->vfp_nxt] = priv;
	bo->vfps[bo->vfp_nxt] = func;
	bo->vfp_nxt++;
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
