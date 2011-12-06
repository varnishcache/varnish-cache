/*-
 * Copyright (c) 2011 Varnish Software AS
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
 * VEF Varnish Esi Fetching
 */

#include "config.h"

#include <stdlib.h>

#include "cache.h"

#include "cache_esi.h"

/*---------------------------------------------------------------------
 * Read some bytes.
 *
 * If the esi_syntax&8 bit is set, we read only a couple of bytes at
 * a time, in order to stress the parse/pending/callback code.
 */

static ssize_t
vef_read(struct worker *wrk, struct http_conn *htc, void *buf, ssize_t buflen,
    ssize_t bytes)
{
	ssize_t d;

	if (buflen < bytes)
		bytes = buflen;
	if (cache_param->esi_syntax & 0x8) {
		d = (random() & 3) + 1;
		if (d < bytes)
			bytes = d;
	}
	return (HTC_Read(wrk, htc, buf, bytes));
}

/*---------------------------------------------------------------------
 * We receive a ungzip'ed object, and want to store it ungzip'ed.
 */

static int
vfp_esi_bytes_uu(struct worker *wrk, struct http_conn *htc, ssize_t bytes)
{
	ssize_t wl;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	while (bytes > 0) {
		st = FetchStorage(wrk, 0);
		if (st == NULL)
			return (-1);
		wl = vef_read(wrk, htc,
		    st->ptr + st->len, st->space - st->len, bytes);
		if (wl <= 0)
			return (wl);
		VEP_Parse(wrk, (const char *)st->ptr + st->len, wl);
		st->len += wl;
		wrk->busyobj->fetch_obj->len += wl;
		bytes -= wl;
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it ungzip'ed.
 */

static int
vfp_esi_bytes_gu(struct worker *wrk, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t wl;
	uint8_t	ibuf[cache_param->gzip_stack_buffer];
	int i;
	size_t dl;
	const void *dp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	vg = wrk->busyobj->vgz_rx;

	while (bytes > 0) {
		if (VGZ_IbufEmpty(vg) && bytes > 0) {
			wl = vef_read(wrk, htc, ibuf, sizeof ibuf, bytes);
			if (wl <= 0)
				return (wl);
			VGZ_Ibuf(vg, ibuf, wl);
			bytes -= wl;
		}
		if (VGZ_ObufStorage(wrk, vg))
			return(-1);
		i = VGZ_Gunzip(vg, &dp, &dl);
		xxxassert(i == VGZ_OK || i == VGZ_END);
		VEP_Parse(wrk, dp, dl);
		wrk->busyobj->fetch_obj->len += dl;
	}
	return (1);
}

/*---------------------------------------------------------------------
 */

struct vef_priv {
	unsigned		magic;
#define VEF_MAGIC		0xf104b51f
	struct vgz		*vgz;

	char			*bufp;
	ssize_t			tot;
	int			error;
	char			pending[20];
	ssize_t			npend;
};

/*---------------------------------------------------------------------
 * We receive a [un]gzip'ed object, and want to store it gzip'ed.
 */

static ssize_t
vfp_vep_callback(struct worker *wrk, ssize_t l, enum vgz_flag flg)
{
	struct vef_priv *vef;
	size_t dl, px;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->busyobj, BUSYOBJ_MAGIC);
	vef = wrk->busyobj->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	assert(l >= 0);

	if (vef->error) {
		vef->tot += l;
		return (vef->tot);
	}

	/*
	 * l == 0 is valid when 'flg' calls for action, but in the
	 * normal case we can just ignore a l==0 request.
	 * (It would cause Z_BUF_ERROR anyway)
	 */
	if (l == 0 && flg == VGZ_NORMAL)
		return (vef->tot);

	do {
		px = vef->npend;
		if (l < px)
			px = l;
		if (px != 0) {
			VGZ_Ibuf(vef->vgz, vef->pending, px);
			l -= px;
		} else {
			VGZ_Ibuf(vef->vgz, vef->bufp, l);
			vef->bufp += l;
			l = 0;
		}
		do {
			if (VGZ_ObufStorage(wrk, vef->vgz)) {
				vef->error = ENOMEM;
				vef->tot += l;
				return (vef->tot);
			}
			i = VGZ_Gzip(vef->vgz, &dp, &dl, flg);
			vef->tot += dl;
			wrk->busyobj->fetch_obj->len += dl;
		} while (!VGZ_IbufEmpty(vef->vgz) ||
		    (flg != VGZ_NORMAL && VGZ_ObufFull(vef->vgz)));
		if (px != 0) {
			memmove(vef->pending, vef->pending + px,
			    vef->npend - px);
			vef->npend -= px;
		}
	} while (l > 0);
	if (flg == VGZ_FINISH)
		assert(i == 1);			/* XXX */
	else
		assert(i == 0);			/* XXX */
	return (vef->tot);
}

static int
vfp_esi_bytes_ug(struct worker *wrk, struct http_conn *htc, ssize_t bytes)
{
	ssize_t wl;
	char ibuf[cache_param->gzip_stack_buffer];
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->busyobj, BUSYOBJ_MAGIC);
	vef = wrk->busyobj->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	while (bytes > 0) {
		wl = vef_read(wrk, htc, ibuf, sizeof ibuf, bytes);
		if (wl <= 0)
			return (wl);
		bytes -= wl;
		vef->bufp = ibuf;
		VEP_Parse(wrk, ibuf, wl);
		assert(vef->bufp >= ibuf && vef->bufp <= ibuf + wl);
		if (vef->error) {
			errno = vef->error;
			return (-1);
		}
		if (vef->bufp < ibuf + wl) {
			wl = (ibuf + wl) - vef->bufp;
			assert(wl + vef->npend < sizeof vef->pending);
			memmove(vef->pending + vef->npend, vef->bufp, wl);
			vef->npend += wl;
		}
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it gzip'ed.
 */

static int
vfp_esi_bytes_gg(struct worker *wrk, struct http_conn *htc, size_t bytes)
{
	ssize_t wl;
	char ibuf[cache_param->gzip_stack_buffer];
	char ibuf2[cache_param->gzip_stack_buffer];
	struct vef_priv *vef;
	size_t dl;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->busyobj, BUSYOBJ_MAGIC);
	vef = wrk->busyobj->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	assert(sizeof ibuf >= 1024);
	ibuf2[0] = 0; /* For Flexelint */

	while (bytes > 0) {
		wl = vef_read(wrk, htc, ibuf, sizeof ibuf, bytes);
		if (wl <= 0)
			return (wl);
		bytes -= wl;

		vef->bufp = ibuf;
		VGZ_Ibuf(wrk->busyobj->vgz_rx, ibuf, wl);
		do {
			VGZ_Obuf(wrk->busyobj->vgz_rx, ibuf2, sizeof ibuf2);
			i = VGZ_Gunzip(wrk->busyobj->vgz_rx, &dp, &dl);
			/* XXX: check i */
			assert(i >= VGZ_OK);
			vef->bufp = ibuf2;
			if (dl > 0)
				VEP_Parse(wrk, ibuf2, dl);
			if (vef->error) {
				errno = vef->error;
				return (-1);
			}
			if (vef->bufp < ibuf2 + dl) {
				dl = (ibuf2 + dl) - vef->bufp;
				assert(dl + vef->npend < sizeof vef->pending);
				memmove(vef->pending + vef->npend,
				    vef->bufp, dl);
				vef->npend += dl;
			}
		} while (!VGZ_IbufEmpty(wrk->busyobj->vgz_rx));
	}
	return (1);
}


/*---------------------------------------------------------------------*/

static void __match_proto__()
vfp_esi_begin(struct worker *wrk, size_t estimate)
{
	struct busyobj *bo;
	struct vef_priv *vef;

	(void)estimate;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	bo = wrk->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AZ(bo->vgz_rx);
	if (bo->is_gzip && bo->do_gunzip) {
		bo->vgz_rx = VGZ_NewUngzip(wrk, "U F E");
		VEP_Init(wrk, NULL);
	} else if (bo->is_gunzip && bo->do_gzip) {
		ALLOC_OBJ(vef, VEF_MAGIC);
		AN(vef);
		vef->vgz = VGZ_NewGzip(wrk, "G F E");
		AZ(bo->vef_priv);
		bo->vef_priv = vef;
		VEP_Init(wrk, vfp_vep_callback);
	} else if (bo->is_gzip) {
		bo->vgz_rx = VGZ_NewUngzip(wrk, "U F E");
		ALLOC_OBJ(vef, VEF_MAGIC);
		AN(vef);
		vef->vgz = VGZ_NewGzip(wrk, "G F E");
		AZ(bo->vef_priv);
		bo->vef_priv = vef;
		VEP_Init(wrk, vfp_vep_callback);
	} else {
		AZ(bo->vef_priv);
		VEP_Init(wrk, NULL);
	}

	AN(bo->vep);
}

static int __match_proto__()
vfp_esi_bytes(struct worker *wrk, struct http_conn *htc, ssize_t bytes)
{
	struct busyobj *bo;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	bo = wrk->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	AZ(bo->fetch_failed);
	AN(bo->vep);
	assert(&bo->htc == htc);
	if (bo->is_gzip && bo->do_gunzip)
		i = vfp_esi_bytes_gu(wrk, htc, bytes);
	else if (bo->is_gunzip && bo->do_gzip)
		i = vfp_esi_bytes_ug(wrk, htc, bytes);
	else if (bo->is_gzip)
		i = vfp_esi_bytes_gg(wrk, htc, bytes);
	else
		i = vfp_esi_bytes_uu(wrk, htc, bytes);
	AN(bo->vep);
	return (i);
}

static int __match_proto__()
vfp_esi_end(struct worker *wrk)
{
	struct vsb *vsb;
	struct vef_priv *vef;
	struct busyobj *bo;
	ssize_t l;
	int retval;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	bo = wrk->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AN(bo->vep);

	retval = bo->fetch_failed;

	if (bo->vgz_rx != NULL && VGZ_Destroy(&bo->vgz_rx, -1) != VGZ_END)
		retval = FetchError(wrk, "Gunzip+ESI Failed at the very end");

	vsb = VEP_Finish(wrk);

	if (vsb != NULL) {
		if (!retval) {
			l = VSB_len(vsb);
			assert(l > 0);
			/* XXX: This is a huge waste of storage... */
			bo->fetch_obj->esidata = STV_alloc(wrk, l);
			if (bo->fetch_obj->esidata != NULL) {
				memcpy(bo->fetch_obj->esidata->ptr,
				    VSB_data(vsb), l);
				bo->fetch_obj->esidata->len = l;
			} else {
				retval = FetchError(wrk,
				    "Could not allocate storage for esidata");
			}
		}
		VSB_delete(vsb);
	}

	vef = bo->vef_priv;
	if (vef != NULL) {
		CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
		bo->vef_priv = NULL;
		VGZ_UpdateObj(vef->vgz, bo->fetch_obj);
		if (VGZ_Destroy(&vef->vgz,  -1) != VGZ_END)
			retval = FetchError(wrk,
			    "ESI+Gzip Failed at the very end");
		FREE_OBJ(vef);
	}
	return (retval);
}

struct vfp vfp_esi = {
        .begin  =       vfp_esi_begin,
        .bytes  =       vfp_esi_bytes,
        .end    =       vfp_esi_end,
};
