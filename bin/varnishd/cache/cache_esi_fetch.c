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
 */

struct vef_priv {
	unsigned		magic;
#define VEF_MAGIC		0xf104b51f
	struct vgz		*vgz;

	ssize_t			tot;
	int			error;

	char			*ibuf;
	char			*ibuf_i;
	char			*ibuf_o;
	ssize_t			ibuf_sz;

	char			*ibuf2;
	ssize_t			ibuf2_sz;
};

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
vfp_esi_bytes_uu(struct worker *wrk, const struct vef_priv *vef,
    struct http_conn *htc, ssize_t bytes)
{
	ssize_t wl;
	struct storage *st;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

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
vfp_esi_bytes_gu(struct worker *wrk, const struct vef_priv *vef,
    struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t wl;
	int i;
	size_t dl;
	const void *dp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	vg = wrk->busyobj->vgz_rx;

	while (bytes > 0) {
		if (VGZ_IbufEmpty(vg) && bytes > 0) {
			wl = vef_read(wrk, htc, vef->ibuf, vef->ibuf_sz, bytes);
			if (wl <= 0)
				return (wl);
			VGZ_Ibuf(vg, vef->ibuf, wl);
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
 * We receive a [un]gzip'ed object, and want to store it gzip'ed.
 *
 * This is rather complicated, because the ESI parser does not
 * spit out all bytes we feed it right away:  Sometimes it needs
 * more input to make up its mind.
 *
 * The inject function feeds uncompressed bytes into the VEP, and
 * takes care to keep any bytes VEP didn't decide on intact until
 * later.
 *
 * The callback is called by VEP to dispose of bytes and report
 * where to find them again later.
 */

static int
vfp_vep_inject(const struct worker *wrk, struct vef_priv *vef, ssize_t wl)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	VEP_Parse(wrk, vef->ibuf_i, wl);
	vef->ibuf_i += wl;
	assert(vef->ibuf_o >= vef->ibuf && vef->ibuf_o <= vef->ibuf_i);
	if (vef->error) {
		errno = vef->error;
		return (-1);
	}
	wl = vef->ibuf_i - vef->ibuf_o;
	if (wl > 0)
		memmove(vef->ibuf, vef->ibuf_o, wl);
	vef->ibuf_o = vef->ibuf;
	vef->ibuf_i = vef->ibuf + wl;
	return (0);
}

static ssize_t
vfp_vep_callback(struct worker *wrk, ssize_t l, enum vgz_flag flg)
{
	struct vef_priv *vef;
	size_t dl;
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

	VGZ_Ibuf(vef->vgz, vef->ibuf_o, l);
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
	assert(VGZ_IbufEmpty(vef->vgz));
	vef->ibuf_o += l;
	if (flg == VGZ_FINISH)
		assert(i == 1);			/* XXX */
	else
		assert(i == 0);			/* XXX */
	return (vef->tot);
}

/*---------------------------------------------------------------------
 * We receive a gunzip'ed object, and want to store it gzip'ed.
 */

static int
vfp_esi_bytes_ug(struct worker *wrk, struct vef_priv *vef,
    struct http_conn *htc, ssize_t bytes)
{
	ssize_t wl;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->busyobj, BUSYOBJ_MAGIC);

	while (bytes > 0) {
		wl = vef->ibuf_sz - (vef->ibuf_i - vef->ibuf);
		wl = vef_read(wrk, htc, vef->ibuf_i, wl, bytes);
		if (wl <= 0)
			return (wl);
		bytes -= wl;
		if (vfp_vep_inject(wrk, vef, wl))
			return (-1);
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it gzip'ed.
 */

static int
vfp_esi_bytes_gg(struct worker *wrk, struct vef_priv *vef,
    struct http_conn *htc, size_t bytes)
{
	ssize_t wl;
	size_t dl;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->busyobj, BUSYOBJ_MAGIC);

	while (bytes > 0) {
		wl = vef_read(wrk, htc, vef->ibuf2, vef->ibuf2_sz, bytes);
		if (wl <= 0)
			return (wl);
		bytes -= wl;

		VGZ_Ibuf(wrk->busyobj->vgz_rx, vef->ibuf2, wl);
		do {
			wl = vef->ibuf_sz - (vef->ibuf_i - vef->ibuf);
			VGZ_Obuf(wrk->busyobj->vgz_rx, vef->ibuf_i, wl);
			i = VGZ_Gunzip(wrk->busyobj->vgz_rx, &dp, &dl);
			/* XXX: check i */
			assert(i >= VGZ_OK);
			if (dl > 0 && vfp_vep_inject(wrk, vef, dl))
				return (-1);
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

	ALLOC_OBJ(vef, VEF_MAGIC);
	XXXAN(vef);
	AZ(bo->vef_priv);
	bo->vef_priv = vef;

	AZ(bo->vgz_rx);
	if (bo->is_gzip && bo->do_gunzip) {
		bo->vgz_rx = VGZ_NewUngzip(wrk, "U F E");
		VEP_Init(wrk, NULL);
		vef->ibuf_sz = cache_param->gzip_buffer;
	} else if (bo->is_gunzip && bo->do_gzip) {
		vef->vgz = VGZ_NewGzip(wrk, "G F E");
		VEP_Init(wrk, vfp_vep_callback);
		vef->ibuf_sz = cache_param->gzip_buffer;
	} else if (bo->is_gzip) {
		bo->vgz_rx = VGZ_NewUngzip(wrk, "U F E");
		vef->vgz = VGZ_NewGzip(wrk, "G F E");
		VEP_Init(wrk, vfp_vep_callback);
		vef->ibuf_sz = cache_param->gzip_buffer;
		vef->ibuf2_sz = cache_param->gzip_buffer;
	} else {
		VEP_Init(wrk, NULL);
	}
	if (vef->ibuf_sz > 0) {
		vef->ibuf = calloc(1L, vef->ibuf_sz);
		XXXAN(vef->ibuf);
		vef->ibuf_i = vef->ibuf;
		vef->ibuf_o = vef->ibuf;
	}
	if (vef->ibuf2_sz > 0) {
		vef->ibuf2 = calloc(1L, vef->ibuf2_sz);
		XXXAN(vef->ibuf2);
	}
	AN(bo->vep);
}

static int __match_proto__()
vfp_esi_bytes(struct worker *wrk, struct http_conn *htc, ssize_t bytes)
{
	struct busyobj *bo;
	struct vef_priv *vef;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	bo = wrk->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	vef = bo->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	AZ(bo->fetch_failed);
	AN(bo->vep);
	assert(&bo->htc == htc);
	if (bo->is_gzip && bo->do_gunzip)
		i = vfp_esi_bytes_gu(wrk, vef, htc, bytes);
	else if (bo->is_gunzip && bo->do_gzip)
		i = vfp_esi_bytes_ug(wrk, vef, htc, bytes);
	else if (bo->is_gzip)
		i = vfp_esi_bytes_gg(wrk, vef, htc, bytes);
	else
		i = vfp_esi_bytes_uu(wrk, vef, htc, bytes);
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
	bo->vef_priv = NULL;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	if (vef->vgz != NULL) {
		VGZ_UpdateObj(vef->vgz, bo->fetch_obj);
		if (VGZ_Destroy(&vef->vgz,  -1) != VGZ_END)
			retval = FetchError(wrk,
			    "ESI+Gzip Failed at the very end");
	}
	if (vef->ibuf != NULL)
		free(vef->ibuf);
	if (vef->ibuf2 != NULL)
		free(vef->ibuf2);
	FREE_OBJ(vef);
	return (retval);
}

struct vfp vfp_esi = {
        .begin  =       vfp_esi_begin,
        .bytes  =       vfp_esi_bytes,
        .end    =       vfp_esi_end,
};
