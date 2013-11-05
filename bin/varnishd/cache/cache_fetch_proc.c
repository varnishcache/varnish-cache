/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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

int
VFP_Error2(struct busyobj *bo, const char *error, const char *more)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (bo->state < BOS_FAILED) {
		if (more == NULL)
			VSLb(bo->vsl, SLT_FetchError, "%s", error);
		else
			VSLb(bo->vsl, SLT_FetchError, "%s: %s", error, more);
		if (bo->fetch_objcore != NULL)
			HSH_Fail(bo->fetch_objcore);
		VBO_setstate(bo, BOS_FAILED);
	}
	return (-1);
}

int
VFP_Error(struct busyobj *bo, const char *error)
{
	return(VFP_Error2(bo, error, NULL));
}

/*--------------------------------------------------------------------
 * VFP_NOP
 *
 * This fetch-processor does nothing but store the object.
 * It also documents the API
 */

/*--------------------------------------------------------------------
 * VFP_BEGIN
 *
 * Called to set up stuff.
 *
 * 'estimate' is the estimate of the number of bytes we expect to receive,
 * as seen on the socket, or zero if unknown.
 */
static void __match_proto__(vfp_begin_f)
vfp_nop_begin(struct busyobj *bo, size_t estimate)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	if (estimate > 0)
		(void)VFP_GetStorage(bo, estimate);
}

/*--------------------------------------------------------------------
 * VFP_BYTES
 *
 * Process (up to) 'bytes' from the socket.
 *
 * Return -1 on error, issue VFP_Error()
 *	will not be called again, once error happens.
 * Return 0 on EOF on socket even if bytes not reached.
 * Return 1 when 'bytes' have been processed.
 */

static int __match_proto__(vfp_bytes_f)
vfp_nop_bytes(struct busyobj *bo, struct http_conn *htc, ssize_t bytes)
{
	ssize_t l, wl;
	struct storage *st;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	while (bytes > 0) {
		st = VFP_GetStorage(bo, 0);
		if (st == NULL)
			return(-1);
		l = st->space - st->len;
		if (l > bytes)
			l = bytes;
		wl = HTTP1_Read(htc, st->ptr + st->len, l);
		if (wl <= 0)
			return (wl);
		VBO_extend(bo, wl);
		bytes -= wl;
	}
	return (1);
}

/*--------------------------------------------------------------------
 * VFP_END
 *
 * Finish & cleanup
 *
 * Return -1 for error
 * Return 0 for OK
 */

static int __match_proto__(vfp_end_f)
vfp_nop_end(struct busyobj *bo)
{

	(void)bo;
	return (0);
}

struct vfp VFP_nop = {
	.begin	=	vfp_nop_begin,
	.bytes	=	vfp_nop_bytes,
	.end	=	vfp_nop_end,
};

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

	l = fetchfrag;
	if (l == 0)
		l = sz;
	if (l == 0)
		l = cache_param->fetch_chunksize;
	st = STV_alloc(bo, l);
	if (st == NULL) {
		(void)VFP_Error(bo, "Could not get storage");
		return (NULL);
	}
	AZ(st->len);
	Lck_Lock(&bo->mtx);
	VTAILQ_INSERT_TAIL(&obj->store, st, list);
	Lck_Unlock(&bo->mtx);
	return (st);
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
