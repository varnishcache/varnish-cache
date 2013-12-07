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

	struct vep_state	*vep;

	ssize_t			tot;
	int			error;

	char			*ibuf;
	char			*ibuf_i;
	char			*ibuf_o;
	ssize_t			ibuf_sz;
};

static ssize_t
vfp_vep_callback(struct busyobj *bo, void *priv, ssize_t l, enum vgz_flag flg)
{
	struct vef_priv *vef;
	size_t dl;
	const void *dp;
	struct storage *st;
	int i;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(vef, priv, VEF_MAGIC);
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
		st = VFP_GetStorage(bo, 0);
		if (st == NULL) {
			vef->error = ENOMEM;
			vef->tot += l;
			return (vef->tot);
		}
		VGZ_Obuf(vef->vgz, st->ptr + st->len, st->space - st->len);
		i = VGZ_Gzip(vef->vgz, &dp, &dl, flg);
		vef->tot += dl;
		VBO_extend(bo, dl);
	} while (i != VGZ_ERROR &&
	    (!VGZ_IbufEmpty(vef->vgz) || VGZ_ObufFull(vef->vgz)));
	assert(i == VGZ_ERROR || VGZ_IbufEmpty(vef->vgz));
	vef->ibuf_o += l;
	return (vef->tot);
}

static enum vfp_status
vfp_esi_end(struct busyobj *bo, struct vef_priv *vef, enum vfp_status retval)
{
	struct vsb *vsb;
	ssize_t l;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	vsb = VEP_Finish(vef->vep, bo);

	if (vsb != NULL) {
		if (retval == VFP_END) {
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

	if (vef->vgz != NULL) {
		VGZ_UpdateObj(vef->vgz, bo->fetch_obj);
		if (VGZ_Destroy(&vef->vgz) != VGZ_END)
			retval = VFP_Error(bo,
			    "ESI+Gzip Failed at the very end");
	}
	if (vef->ibuf != NULL)
	FREE_OBJ(vef);
	return (retval);
}

enum vfp_status __match_proto__(vfp_pull_f)
vfp_esi_gzip_pull(struct busyobj *bo, void *p, ssize_t *lp, intptr_t *priv)
{
	enum vfp_status vp;
	ssize_t d, l;
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (p == vfp_init) {
		ALLOC_OBJ(vef, VEF_MAGIC);
		XXXAN(vef);
		vef->vgz = VGZ_NewGzip(bo->vsl, "G F E");
		vef->vep = VEP_Init(bo, vfp_vep_callback, vef);
		vef->ibuf_sz = cache_param->gzip_buffer;
		vef->ibuf = calloc(1L, vef->ibuf_sz);
		XXXAN(vef->ibuf);
		vef->ibuf_i = vef->ibuf;
		vef->ibuf_o = vef->ibuf;
		*priv = (uintptr_t)vef;
		return (VFP_OK);
	}
	if (p == vfp_fini) {
		if (*priv)
			(void)vfp_esi_end(bo, (void*)*priv, VFP_ERROR);
		*priv = 0;
		return (VFP_ERROR);
	}
	AN(p);
	AN(lp);
	*lp = 0;
	AN(priv);
	CAST_OBJ_NOTNULL(vef, (void*)*priv, VEF_MAGIC);
	l = vef->ibuf_sz - (vef->ibuf_i - vef->ibuf);
	if (DO_DEBUG(DBG_ESI_CHOP)) {
		d = (random() & 3) + 1;
		if (d < l)
			l = d;
	}
	vp = VFP_Suck(bo, vef->ibuf_i, &l);

	if (l > 0) {
		VEP_Parse(vef->vep, bo, vef->ibuf_i, l);
		vef->ibuf_i += l;
		assert(vef->ibuf_o >= vef->ibuf && vef->ibuf_o <= vef->ibuf_i);
		if (vef->error) {
			errno = vef->error;
			return (VFP_ERROR);
		}
		l = vef->ibuf_i - vef->ibuf_o;
		if (l > 0)
			memmove(vef->ibuf, vef->ibuf_o, l);
		vef->ibuf_o = vef->ibuf;
		vef->ibuf_i = vef->ibuf + l;
	}
	if (vp == VFP_END) {
		vp = vfp_esi_end(bo, vef, vp);
		*priv = 0;
	}
	return (vp);
}

enum vfp_status __match_proto__(vfp_pull_f)
vfp_esi_pull(struct busyobj *bo, void *p, ssize_t *lp, intptr_t *priv)
{
	enum vfp_status vp;
	ssize_t d;
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (p == vfp_init) {
		ALLOC_OBJ(vef, VEF_MAGIC);
		XXXAN(vef);
		vef->vep = VEP_Init(bo, NULL, NULL);
		*priv = (uintptr_t)vef;
		return (VFP_OK);
	}
	if (p == vfp_fini) {
		if (*priv)
			(void)vfp_esi_end(bo, (void*)*priv, VFP_ERROR);
		*priv = 0;
		return (VFP_ERROR);
	}
	AN(p);
	AN(lp);
	AN(priv);
	CAST_OBJ_NOTNULL(vef, (void*)*priv, VEF_MAGIC);
	if (DO_DEBUG(DBG_ESI_CHOP)) {
		d = (random() & 3) + 1;
		if (d < *lp)
			*lp = d;
	}
	vp = VFP_Suck(bo, p, lp);
	if (vp != VFP_ERROR && *lp > 0)
		VEP_Parse(vef->vep, bo, p, *lp);
	if (vp == VFP_END) {
		vp = vfp_esi_end(bo, vef, vp);
		*priv = 0;
	}
	return (vp);
}
