/*-
 * Copyright (c) 2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_vgz.h"

#include "cache_esi.h"

#include "vrnd.h"

/*---------------------------------------------------------------------
 */

struct vef_priv {
	unsigned		magic;
#define VEF_MAGIC		0xf104b51f
	int			error;
	ssize_t			tot;

	struct vgz		*vgz;

	struct vep_state	*vep;

	char			*ibuf;
	char			*ibuf_i;
	char			*ibuf_o;
	ssize_t			ibuf_sz;
};

static ssize_t
vfp_vep_callback(struct vfp_ctx *vc, void *priv, ssize_t l, enum vgz_flag flg)
{
	struct vef_priv *vef;
	ssize_t dl;
	const void *dp;
	uint8_t *ptr;
	enum vgzret_e i;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
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
		dl = 0;
		if (VFP_GetStorage(vc, &dl, &ptr) != VFP_OK) {
			vef->error = ENOMEM;
			vef->tot += l;
			return (vef->tot);
		}
		VGZ_Obuf(vef->vgz, ptr, dl);
		i = VGZ_Gzip(vef->vgz, &dp, &dl, flg);
		VGZ_UpdateObj(vc, vef->vgz, i);
		if (dl > 0) {
			vef->tot += dl;
			VFP_Extend(vc, dl, VFP_OK);
		}
	} while (i != VGZ_ERROR &&
	    (!VGZ_IbufEmpty(vef->vgz) || VGZ_ObufFull(vef->vgz)));
	assert(i == VGZ_ERROR || VGZ_IbufEmpty(vef->vgz));
	vef->ibuf_o += l;
	return (vef->tot);
}

static enum vfp_status
vfp_esi_end(struct vfp_ctx *vc, struct vef_priv *vef,
    enum vfp_status retval)
{
	struct vsb *vsb;
	ssize_t l;
	void *p;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	if (retval == VFP_ERROR) {
		if (vef->error == 0)
			vef->error = errno ? errno : EINVAL;
	} else {
		assert(retval == VFP_END);
	}

	vsb = VEP_Finish(vef->vep);

	if (vsb != NULL) {
		if (retval == VFP_END) {
			l = VSB_len(vsb);
			assert(l > 0);
			p = ObjSetAttr(vc->wrk, vc->oc,
			    OA_ESIDATA, l, VSB_data(vsb));
			if (p == NULL) {
				retval = VFP_Error(vc,
				    "Could not allocate storage for esidata");
			}
		}
		VSB_destroy(&vsb);
	}

	if (vef->vgz != NULL) {
		if (retval == VFP_END)
			VGZ_UpdateObj(vc, vef->vgz, VGZ_END);
		if (VGZ_Destroy(vc->wrk, &vef->vgz) != VGZ_END)
			retval = VFP_Error(vc,
			    "ESI+Gzip Failed at the very end");
	}
	if (vef->ibuf != NULL)
		free(vef->ibuf);
	FREE_OBJ(vef);
	return (retval);
}

static enum vfp_status v_matchproto_(vfp_init_f)
vfp_esi_gzip_init(VRT_CTX, struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc->req, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	if (http_GetStatus(vc->resp) == 206) {
		VSLb(vc->wrk->vsl, SLT_VCL_Error,
		    "Attempted ESI on partial (206) response");
		return (VFP_ERROR);
	}
	ALLOC_OBJ(vef, VEF_MAGIC);
	if (vef == NULL)
		return (VFP_ERROR);
	vc->obj_flags |= OF_GZIPED | OF_CHGCE | OF_ESIPROC;
	vef->vep = VEP_Init(vc, vc->req, vfp_vep_callback, vef);
	if (vef->vep == NULL) {
		FREE_OBJ(vef);
		return (VFP_ERROR);
	}
	vef->vgz = VGZ_NewGzip(vc->wrk->vsl, "G F E");
	AN(vef->vgz);

	vef->ibuf_sz = cache_param->gzip_buffer;
	vef->ibuf = calloc(1L, vef->ibuf_sz);
	if (vef->ibuf == NULL)
		return (vfp_esi_end(vc, vef, VFP_ERROR));
	vef->ibuf_i = vef->ibuf;
	vef->ibuf_o = vef->ibuf;
	vfe->priv1 = vef;

	RFC2616_Weaken_Etag(vc->resp);
	http_Unset(vc->resp, H_Content_Length);
	http_Unset(vc->resp, H_Content_Encoding);
	http_SetHeader(vc->resp, "Content-Encoding: gzip");

	RFC2616_Vary_AE(vc->resp);

	return (VFP_OK);
}

static enum vfp_status v_matchproto_(vfp_pull_f)
vfp_esi_gzip_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
   ssize_t *lp)
{
	enum vfp_status vp;
	ssize_t d, l;
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(vef, vfe->priv1, VEF_MAGIC);
	AN(p);
	AN(lp);
	*lp = 0;
	l = vef->ibuf_sz - (vef->ibuf_i - vef->ibuf);
	if (DO_DEBUG(DBG_ESI_CHOP)) {
		d = (VRND_RandomTestable() & 3) + 1;
		if (d < l)
			l = d;
	}
	vp = VFP_Suck(vc, vef->ibuf_i, &l);

	if (l > 0) {
		VEP_Parse(vef->vep, vef->ibuf_i, l);
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
		vp = vfp_esi_end(vc, vef, vp);
		vfe->priv1 = NULL;
	}
	return (vp);
}

static enum vfp_status v_matchproto_(vfp_init_f)
vfp_esi_init(VRT_CTX, struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct vef_priv *vef;
	struct vep_state *vep;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc->req, HTTP_MAGIC);
	if (http_GetStatus(vc->resp) == 206) {
		VSLb(vc->wrk->vsl, SLT_VCL_Error,
		    "Attempted ESI on partial (206) response");
		return (VFP_ERROR);
	}
	vep = VEP_Init(vc, vc->req, NULL, NULL);
	if (vep == NULL)
		return (VFP_ERROR);
	ALLOC_OBJ(vef, VEF_MAGIC);
	if (vef == NULL)
		return (VFP_ERROR);
	vc->obj_flags |= OF_ESIPROC;
	vef->vep = vep;
	vfe->priv1 = vef;
	return (VFP_OK);
}

static enum vfp_status v_matchproto_(vfp_pull_f)
vfp_esi_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p, ssize_t *lp)
{
	enum vfp_status vp;
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(vef, vfe->priv1, VEF_MAGIC);
	AN(p);
	AN(lp);
	if (DO_DEBUG(DBG_ESI_CHOP)) {
		*lp = vmin_t(size_t, *lp, (VRND_RandomTestable() & 3) + 1);
	}
	vp = VFP_Suck(vc, p, lp);
	if (vp != VFP_ERROR && *lp > 0)
		VEP_Parse(vef->vep, p, *lp);
	if (vp == VFP_END) {
		vp = vfp_esi_end(vc, vef, vp);
		vfe->priv1 = NULL;
	}
	return (vp);
}

static void v_matchproto_(vfp_fini_f)
vfp_esi_fini(struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);

	if (vfe->priv1 == NULL)
		return;

	if (vc->oc->stobj->stevedore == NULL)
		errno = ENOMEM;

	(void)vfp_esi_end(vc, vfe->priv1, VFP_ERROR);
	vfe->priv1 = NULL;
}

const struct vfp VFP_esi = {
	.name = "esi",
	.init = vfp_esi_init,
	.pull = vfp_esi_pull,
	.fini = vfp_esi_fini,
};

const struct vfp VFP_esi_gzip = {
	.name = "esi_gzip",
	.init = vfp_esi_gzip_init,
	.pull = vfp_esi_gzip_pull,
	.fini = vfp_esi_fini,
};
