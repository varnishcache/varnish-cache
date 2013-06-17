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
vef_read(struct http_conn *htc, void *buf, ssize_t buflen, ssize_t bytes)
{
	ssize_t d;

	if (buflen < bytes)
		bytes = buflen;
	if (cache_param->esi_syntax & 0x8) {
		d = (random() & 3) + 1;
		if (d < bytes)
			bytes = d;
	}
	return (htc->read(htc, buf, bytes));
}

/*---------------------------------------------------------------------
 * We receive a ungzip'ed object, and want to store it ungzip'ed.
 */

static int
vfp_esi_bytes_uu(struct busyobj *bo, const struct vef_priv *vef,
    struct http_conn *htc, ssize_t bytes)
{
	ssize_t wl;
	struct storage *st;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	while (bytes > 0) {
		st = VFP_GetStorage(bo, 0);
		if (st == NULL)
			return (-1);
		wl = vef_read(htc,
		    st->ptr + st->len, st->space - st->len, bytes);
		if (wl <= 0)
			return (wl);
		VEP_Parse(bo, (const char *)st->ptr + st->len, wl);
		st->len += wl;
		VBO_extend(bo, wl);
		bytes -= wl;
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it ungzip'ed.
 */

static int
vfp_esi_bytes_gu(struct busyobj *bo, const struct vef_priv *vef,
    struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t wl;
	enum vgzret_e vr;
	size_t dl;
	const void *dp;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	vg = bo->vgz_rx;

	while (bytes > 0) {
		if (VGZ_IbufEmpty(vg) && bytes > 0) {
			wl = vef_read(htc, vef->ibuf, vef->ibuf_sz, bytes);
			if (wl <= 0)
				return (wl);
			VGZ_Ibuf(vg, vef->ibuf, wl);
			bytes -= wl;
		}
		if (VGZ_ObufStorage(bo, vg))
			return(-1);
		vr = VGZ_Gunzip(vg, &dp, &dl);
		if (vr < VGZ_OK)
			return (-1);
		if (dl > 0) {
			VEP_Parse(bo, dp, dl);
			VBO_extend(bo, dl);
		}
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
vfp_vep_inject(const struct busyobj *bo, struct vef_priv *vef, ssize_t wl)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	VEP_Parse(bo, vef->ibuf_i, wl);
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
vfp_vep_callback(struct busyobj *bo, ssize_t l, enum vgz_flag flg)
{
	struct vef_priv *vef;
	size_t dl;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	vef = bo->vef_priv;
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
		if (VGZ_ObufStorage(bo, vef->vgz)) {
			vef->error = ENOMEM;
			vef->tot += l;
			return (vef->tot);
		}
		i = VGZ_Gzip(vef->vgz, &dp, &dl, flg);
		vef->tot += dl;
		VBO_extend(bo, dl);
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
vfp_esi_bytes_ug(const struct busyobj *bo, struct vef_priv *vef,
    struct http_conn *htc, ssize_t bytes)
{
	ssize_t wl;

	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	while (bytes > 0) {
		wl = vef->ibuf_sz - (vef->ibuf_i - vef->ibuf);
		wl = vef_read(htc, vef->ibuf_i, wl, bytes);
		if (wl <= 0)
			return (wl);
		bytes -= wl;
		if (vfp_vep_inject(bo, vef, wl))
			return (-1);
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it gzip'ed.
 */

static int
vfp_esi_bytes_gg(const struct busyobj *bo, struct vef_priv *vef,
    struct http_conn *htc, size_t bytes)
{
	ssize_t wl;
	size_t dl;
	const void *dp;
	enum vgzret_e vr;

	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	while (bytes > 0) {
		wl = vef_read(htc, vef->ibuf2, vef->ibuf2_sz, bytes);
		if (wl <= 0)
			return (wl);
		bytes -= wl;

		VGZ_Ibuf(bo->vgz_rx, vef->ibuf2, wl);
		do {
			wl = vef->ibuf_sz - (vef->ibuf_i - vef->ibuf);
			VGZ_Obuf(bo->vgz_rx, vef->ibuf_i, wl);
			vr = VGZ_Gunzip(bo->vgz_rx, &dp, &dl);
			if (vr < VGZ_OK)
				return (-1);
			if (dl > 0 && vfp_vep_inject(bo, vef, dl))
				return (-1);
		} while (!VGZ_IbufEmpty(bo->vgz_rx));
	}
	return (1);
}

/*---------------------------------------------------------------------*/

static void __match_proto__(vfp_begin_f)
vfp_esi_begin(void *priv, size_t estimate)
{
	struct busyobj *bo;
	struct vef_priv *vef;

	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);
	(void)estimate;

	ALLOC_OBJ(vef, VEF_MAGIC);
	XXXAN(vef);
	AZ(bo->vef_priv);
	bo->vef_priv = vef;

	AZ(bo->vgz_rx);
	if (bo->is_gzip && bo->do_gunzip) {
		bo->vgz_rx = VGZ_NewUngzip(bo->vsl, "U F E");
		VEP_Init(bo, NULL);
		vef->ibuf_sz = cache_param->gzip_buffer;
	} else if (bo->is_gunzip && bo->do_gzip) {
		vef->vgz = VGZ_NewGzip(bo->vsl, "G F E");
		VEP_Init(bo, vfp_vep_callback);
		vef->ibuf_sz = cache_param->gzip_buffer;
	} else if (bo->is_gzip) {
		bo->vgz_rx = VGZ_NewUngzip(bo->vsl, "U F E");
		vef->vgz = VGZ_NewGzip(bo->vsl, "G F E");
		VEP_Init(bo, vfp_vep_callback);
		vef->ibuf_sz = cache_param->gzip_buffer;
		vef->ibuf2_sz = cache_param->gzip_buffer;
	} else {
		VEP_Init(bo, NULL);
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

static int __match_proto__(vfp_bytes_f)
vfp_esi_bytes(void *priv, struct http_conn *htc, ssize_t bytes)
{
	struct busyobj *bo;
	struct vef_priv *vef;
	int i;

	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);
	vef = bo->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	AN(bo->vep);
	assert(&bo->htc == htc);
	if (bo->is_gzip && bo->do_gunzip)
		i = vfp_esi_bytes_gu(bo, vef, htc, bytes);
	else if (bo->is_gunzip && bo->do_gzip)
		i = vfp_esi_bytes_ug(bo, vef, htc, bytes);
	else if (bo->is_gzip)
		i = vfp_esi_bytes_gg(bo, vef, htc, bytes);
	else
		i = vfp_esi_bytes_uu(bo, vef, htc, bytes);
	AN(bo->vep);
	return (i);
}

static int __match_proto__(vfp_end_f)
vfp_esi_end(void *priv)
{
	struct busyobj *bo;
	struct vsb *vsb;
	struct vef_priv *vef;
	ssize_t l;
	int retval = 0;

	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);
	AN(bo->vep);

	if (bo->state == BOS_FAILED)
		retval = -1;

	if (bo->vgz_rx != NULL && VGZ_Destroy(&bo->vgz_rx) != VGZ_END)
		retval = VFP_Error(bo, "Gunzip+ESI Failed at the very end");

	vsb = VEP_Finish(bo);

	if (vsb != NULL) {
		if (!retval) {
			l = VSB_len(vsb);
			assert(l > 0);
			/* XXX: This is a huge waste of storage... */
			bo->fetch_obj->esidata = STV_alloc(bo, l);
			if (bo->fetch_obj->esidata != NULL) {
				memcpy(bo->fetch_obj->esidata->ptr,
				    VSB_data(vsb), l);
				bo->fetch_obj->esidata->len = l;
			} else {
				retval = VFP_Error(bo,
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
		if (VGZ_Destroy(&vef->vgz) != VGZ_END)
			retval = VFP_Error(bo,
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
