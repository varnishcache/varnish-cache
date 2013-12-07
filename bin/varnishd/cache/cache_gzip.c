/*-
 * Copyright (c) 2013 Varnish Software AS
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
 * Interaction with the linvgz (zlib) library.
 *
 * The zlib library pollutes namespace a LOT when you include the "vgz.h"
 * (aka (zlib.h") file so we contain the damage by vectoring all access
 * to libz through this source file.
 *
 * The API defined by this file, will also insulate the rest of the code,
 * should we find a better gzip library at a later date.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "vgz.h"

struct vgz {
	unsigned		magic;
#define VGZ_MAGIC		0x162df0cb
	enum {VGZ_GZ,VGZ_UN}	dir;
	struct vsl_log		*vsl;
	const char		*id;
	int			last_i;
	enum vgz_flag		flag;

	/* Wrw stuff */
	char			*m_buf;
	ssize_t			m_sz;
	ssize_t			m_len;

	z_stream		vz;
};

/*--------------------------------------------------------------------
 * Set up a gunzip instance
 */

static struct vgz *
vgz_alloc_vgz(struct vsl_log *vsl, const char *id)
{
	struct vgz *vg;

	ALLOC_OBJ(vg, VGZ_MAGIC);
	AN(vg);
	vg->vsl = vsl;
	vg->id = id;
	return (vg);
}

struct vgz *
VGZ_NewUngzip(struct vsl_log *vsl, const char *id)
{
	struct vgz *vg;

	vg = vgz_alloc_vgz(vsl, id);
	vg->dir = VGZ_UN;
	VSC_C_main->n_gunzip++;

	/*
	 * Max memory usage according to zonf.h:
	 *	mem_needed = "a few kb" + (1 << (windowBits))
	 * Since we don't control windowBits, we have to assume
	 * it is 15, so 34-35KB or so.
	 */
	assert(Z_OK == inflateInit2(&vg->vz, 31));
	return (vg);
}

struct vgz *
VGZ_NewGzip(struct vsl_log *vsl, const char *id)
{
	struct vgz *vg;
	int i;

	vg = vgz_alloc_vgz(vsl, id);
	vg->dir = VGZ_GZ;
	VSC_C_main->n_gzip++;

	/*
	 * From zconf.h:
	 *
	 *	mem_needed = "a few kb"
	 *		+ (1 << (windowBits+2))
	 *		+  (1 << (memLevel+9))
	 *
	 * windowBits [8..15] (-> 1K..128K)
	 * memLevel [1..9] (-> 1K->256K)
	 */
	i = deflateInit2(&vg->vz,
	    cache_param->gzip_level,		/* Level */
	    Z_DEFLATED,				/* Method */
	    16 + 15,				/* Window bits (16=gzip) */
	    cache_param->gzip_memlevel,		/* memLevel */
	    Z_DEFAULT_STRATEGY);
	assert(Z_OK == i);
	return (vg);
}

/*--------------------------------------------------------------------
 */

static int
vgz_getmbuf(struct vgz *vg)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->m_sz);
	AZ(vg->m_len);
	AZ(vg->m_buf);

	vg->m_sz = cache_param->gzip_buffer;
	vg->m_buf = malloc(vg->m_sz);
	if (vg->m_buf == NULL) {
		vg->m_sz = 0;
		return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

void
VGZ_Ibuf(struct vgz *vg, const void *ptr, ssize_t len)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	AZ(vg->vz.avail_in);
	vg->vz.next_in = TRUST_ME(ptr);
	vg->vz.avail_in = len;
}

int
VGZ_IbufEmpty(const struct vgz *vg)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	return (vg->vz.avail_in == 0);
}

/*--------------------------------------------------------------------*/

void
VGZ_Obuf(struct vgz *vg, void *ptr, ssize_t len)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	vg->vz.next_out = TRUST_ME(ptr);
	vg->vz.avail_out = len;
}

int
VGZ_ObufFull(const struct vgz *vg)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	return (vg->vz.avail_out == 0);
}

/*--------------------------------------------------------------------*/

enum vgzret_e
VGZ_Gunzip(struct vgz *vg, const void **pptr, size_t *plen)
{
	int i;
	ssize_t l;
	const uint8_t *before;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	*pptr = NULL;
	*plen = 0;
	AN(vg->vz.next_out);
	AN(vg->vz.avail_out);
	before = vg->vz.next_out;
	i = inflate(&vg->vz, 0);
	if (i == Z_OK || i == Z_STREAM_END) {
		*pptr = before;
		l = (const uint8_t *)vg->vz.next_out - before;
		*plen = l;
	}
	vg->last_i = i;
	if (i == Z_OK)
		return (VGZ_OK);
	if (i == Z_STREAM_END)
		return (VGZ_END);
	if (i == Z_BUF_ERROR)
		return (VGZ_STUCK);
	VSLb(vg->vsl, SLT_Gzip, "Gunzip error: %d (%s)", i, vg->vz.msg);
	return (VGZ_ERROR);
}

/*--------------------------------------------------------------------*/

enum vgzret_e
VGZ_Gzip(struct vgz *vg, const void **pptr, size_t *plen, enum vgz_flag flags)
{
	int i;
	int zflg;
	ssize_t l;
	const uint8_t *before;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	*pptr = NULL;
	*plen = 0;
	AN(vg->vz.next_out);
	AN(vg->vz.avail_out);
	before = vg->vz.next_out;
	switch(flags) {
	case VGZ_NORMAL:	zflg = Z_NO_FLUSH; break;
	case VGZ_ALIGN:		zflg = Z_SYNC_FLUSH; break;
	case VGZ_RESET:		zflg = Z_FULL_FLUSH; break;
	case VGZ_FINISH:	zflg = Z_FINISH; break;
	default:		INCOMPL();
	}
	i = deflate(&vg->vz, zflg);
	if (i == Z_OK || i == Z_STREAM_END) {
		*pptr = before;
		l = (const uint8_t *)vg->vz.next_out - before;
		*plen = l;
	}
	vg->last_i = i;
	if (i == Z_OK)
		return (VGZ_OK);
	if (i == Z_STREAM_END)
		return (VGZ_END);
	if (i == Z_BUF_ERROR)
		return (VGZ_STUCK);
	VSLb(vg->vsl, SLT_Gzip, "Gzip error: %d (%s)", i, vg->vz.msg);
	return (VGZ_ERROR);
}

/*--------------------------------------------------------------------
 */

int
VGZ_WrwInit(struct vgz *vg)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	if (vgz_getmbuf(vg))
		return (-1);

	VGZ_Obuf(vg, vg->m_buf, vg->m_sz);
	return (0);
}

/*--------------------------------------------------------------------
 * VDP for gunzip'ing
 */

int __match_proto__(vdp_bytes)
VDP_gunzip(struct req *req, enum vdp_action act, const void *ptr, ssize_t len)
{
	enum vgzret_e vr;
	size_t dl;
	const void *dp;
	struct worker *wrk;
	struct vgz *vg;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	vg = req->vgz;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AN(vg->m_buf);

	if (len == 0) {
		AN(act > VDP_NULL);
		return (VDP_bytes(req, act, vg->m_buf, vg->m_len));
	}

	VGZ_Ibuf(vg, ptr, len);
	do {
		if (vg->m_len == vg->m_sz)
			vr = VGZ_STUCK;
		else {
			vr = VGZ_Gunzip(vg, &dp, &dl);
			vg->m_len += dl;
		}
		if (vr < VGZ_OK)
			return (-1);
		if (vg->m_len == vg->m_sz || vr == VGZ_STUCK) {
			if (VDP_bytes(req, VDP_FLUSH, vg->m_buf, vg->m_len))
				return (-1);
			vg->m_len = 0;
			VGZ_Obuf(vg, vg->m_buf, vg->m_sz);
		}
	} while (!VGZ_IbufEmpty(vg));
	assert(vr == VGZ_STUCK || vr == VGZ_OK || vr == VGZ_END);
	return (0);
}

/*--------------------------------------------------------------------
 * Gunzip ibuf into outb, if it runs full, emit it with WRW.
 * Leave flushing to caller, more data may be coming.
 */

enum vgzret_e
VGZ_WrwGunzip(struct req *req, struct vgz *vg, const void *ibuf,
    ssize_t ibufl)
{
	enum vgzret_e vr;
	size_t dl;
	const void *dp;
	struct worker *wrk;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AN(vg->m_buf);
	VGZ_Ibuf(vg, ibuf, ibufl);
	if (ibufl == 0)
		return (VGZ_OK);
	do {
		if (vg->m_len == vg->m_sz)
			vr = VGZ_STUCK;
		else {
			vr = VGZ_Gunzip(vg, &dp, &dl);
			vg->m_len += dl;
		}
		if (vr < VGZ_OK)
			return (vr);
		if (vg->m_len == vg->m_sz || vr == VGZ_STUCK) {
			(void)VDP_bytes(req, VDP_FLUSH, vg->m_buf, vg->m_len);
			vg->m_len = 0;
			VGZ_Obuf(vg, vg->m_buf, vg->m_sz);
		}
	} while (!VGZ_IbufEmpty(vg));
	if (vr == VGZ_STUCK)
		vr = VGZ_OK;
	return (vr);
}

/*--------------------------------------------------------------------*/

void
VGZ_WrwFlush(struct req *req, struct vgz *vg)
{
	struct worker *wrk;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	if (vg->m_len ==  0)
		return;

	(void)VDP_bytes(req, VDP_FLUSH, vg->m_buf, vg->m_len);
	vg->m_len = 0;
	VGZ_Obuf(vg, vg->m_buf, vg->m_sz);
}

/*--------------------------------------------------------------------*/

void
VGZ_UpdateObj(const struct vgz *vg, struct object *obj)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	obj->gzip_start	= vg->vz.start_bit;
	obj->gzip_last	= vg->vz.last_bit;
	obj->gzip_stop	= vg->vz.stop_bit;
}

/*--------------------------------------------------------------------
 */

enum vgzret_e
VGZ_Destroy(struct vgz **vgp)
{
	struct vgz *vg;
	enum vgzret_e vr;
	int i;

	vg = *vgp;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	*vgp = NULL;

	VSLb(vg->vsl, SLT_Gzip, "%s %jd %jd %jd %jd %jd",
	    vg->id,
	    (intmax_t)vg->vz.total_in,
	    (intmax_t)vg->vz.total_out,
	    (intmax_t)vg->vz.start_bit,
	    (intmax_t)vg->vz.last_bit,
	    (intmax_t)vg->vz.stop_bit);
	if (vg->dir == VGZ_GZ)
		i = deflateEnd(&vg->vz);
	else
		i = inflateEnd(&vg->vz);
	if (vg->last_i == Z_STREAM_END && i == Z_OK)
		i = Z_STREAM_END;
	if (vg->m_buf)
		free(vg->m_buf);
	if (i == Z_OK)
		vr = VGZ_OK;
	else if (i == Z_STREAM_END)
		vr = VGZ_END;
	else if (i == Z_BUF_ERROR)
		vr = VGZ_STUCK;
	else {
		VSLb(vg->vsl, SLT_Gzip, "G(un)zip error: %d (%s)",
		    i, vg->vz.msg);
		vr = VGZ_ERROR;
	}
	FREE_OBJ(vg);
	return (vr);
}

/*--------------------------------------------------------------------
 * VFP_GUNZIP
 *
 * A VFP for gunzip'ing an object as we receive it from the backend
 */

enum vfp_status __match_proto__(vfp_pull_f)
vfp_gunzip_pull(struct busyobj *bo, void *p, ssize_t *lp, intptr_t *priv)
{
        ssize_t l;
	struct vgz *vg;
	enum vgzret_e vr = VGZ_ERROR;
	const void *dp;
	size_t dl;
	enum vfp_status vp = VFP_OK;

        CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (p == vfp_init) {
		vg = VGZ_NewUngzip(bo->vsl, "U F -");
		XXXAZ(vgz_getmbuf(vg));
		*priv = (uintptr_t)vg;
		VGZ_Ibuf(vg, vg->m_buf, 0);
		AZ(vg->m_len);
		return (VFP_OK);
	}
	if (p == vfp_fini) {
		if (*priv != 0) {
			CAST_OBJ_NOTNULL(vg, (void*)(*priv), VGZ_MAGIC);
			*priv = 0;
			(void)VGZ_Destroy(&vg);
		}
		*priv = 0;
		return (VFP_ERROR);
	}
        AN(p);
        AN(lp);
        AN(priv);
	CAST_OBJ_NOTNULL(vg, (void*)(*priv), VGZ_MAGIC);
	l = *lp;
	*lp = 0;
	VGZ_Obuf(vg, p, l);
	do {
		if (VGZ_IbufEmpty(vg)) {
			l = vg->m_sz;
			vp = VFP_Suck(bo, vg->m_buf, &l);
			if (vp == VFP_ERROR)
				return (vp);
			VGZ_Ibuf(vg, vg->m_buf, l);
		}
		if (!VGZ_IbufEmpty(vg) || vp == VFP_END) {
			vr = VGZ_Gunzip(vg, &dp, &dl);
			if (vr == VGZ_END && !VGZ_IbufEmpty(vg))
				return(VFP_Error(bo, "Junk after gzip data"));
			if (vr < VGZ_OK)
				return (VFP_Error(bo,
				    "Invalid Gzip data: %s", vg->vz.msg));
			if (dl > 0) {
				*lp = dl;
				assert(dp == p);
				return (VFP_OK);
			}
		}
		AN(VGZ_IbufEmpty(vg));
	} while (vp == VFP_OK);
	if (vr != VGZ_END)
		return(VFP_Error(bo, "Gunzip error at the very end"));
	return (vp);
}

/*--------------------------------------------------------------------
 * VFP_GZIP
 *
 * A VFP for gzip'ing an object as we receive it from the backend
 */

enum vfp_status __match_proto__(vfp_pull_f)
vfp_gzip_pull(struct busyobj *bo, void *p, ssize_t *lp, intptr_t *priv)
{
        ssize_t l;
	struct vgz *vg;
	enum vgzret_e vr = VGZ_ERROR;
	const void *dp;
	size_t dl;
	enum vfp_status vp = VFP_ERROR;

        CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (p == vfp_init) {
		vg = VGZ_NewGzip(bo->vsl, "G F -");
		XXXAZ(vgz_getmbuf(vg));
		*priv = (uintptr_t)vg;
		VGZ_Ibuf(vg, vg->m_buf, 0);
		AZ(vg->m_len);
		vg->flag = VGZ_NORMAL;
		return (VFP_OK);
	}
	if (p == vfp_fini) {
		if (*priv != 0) {
			CAST_OBJ_NOTNULL(vg, (void*)(*priv), VGZ_MAGIC);
			*priv = 0;
			(void)VGZ_Destroy(&vg);
		}
		return (VFP_ERROR);
	}
        AN(p);
        AN(lp);
        AN(priv);
	CAST_OBJ_NOTNULL(vg, (void*)(*priv), VGZ_MAGIC);
	l = *lp;
	*lp = 0;
	VGZ_Obuf(vg, p, l);
	do {
		if (VGZ_IbufEmpty(vg)) {
			l = vg->m_sz;
			vp = VFP_Suck(bo, vg->m_buf, &l);
			if (vp == VFP_ERROR)
				break;
			if (vp == VFP_END)
				vg->flag = VGZ_FINISH;
			VGZ_Ibuf(vg, vg->m_buf, l);
		}
		if (!VGZ_IbufEmpty(vg) || vg->flag == VGZ_FINISH) {
			vr = VGZ_Gzip(vg, &dp, &dl, vg->flag);
			if (vr < VGZ_OK)
				return (VFP_Error(bo, "Gzip failed"));
			if (dl > 0) {
				*lp = dl;
				assert(dp == p);
				return (VFP_OK);
			}
		}
		AN(VGZ_IbufEmpty(vg));
	} while (vg->flag != VGZ_FINISH);

	if (vr != VGZ_END)
		return (VFP_Error(bo, "Gzip failed"));
	VGZ_UpdateObj(vg, bo->fetch_obj);
	return (VFP_END);
}

/*--------------------------------------------------------------------
 * VFP_TESTGZIP
 *
 * A VFP for testing that received gzip data is valid, and for
 * collecting the magic bits while we're at it.
 */

enum vfp_status __match_proto__(vfp_pull_f)
vfp_testgunzip_pull(struct busyobj *bo, void *p, ssize_t *lp, intptr_t *priv)
{
	struct vgz *vg;
	enum vgzret_e vr = VGZ_ERROR;
	const void *dp;
	size_t dl;
	enum vfp_status vp;

        CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (p == vfp_init) {
		vg = VGZ_NewUngzip(bo->vsl, "u F -");
		XXXAZ(vgz_getmbuf(vg));
		*priv = (uintptr_t)vg;
		AZ(vg->m_len);
		return (VFP_OK);
	}
	if (p == vfp_fini) {
		if (*priv != 0) {
			CAST_OBJ_NOTNULL(vg, (void*)(*priv), VGZ_MAGIC);
			*priv = 0;
			(void)VGZ_Destroy(&vg);
		}
		return (VFP_ERROR);
	}
        AN(p);
        AN(lp);
        AN(priv);
	CAST_OBJ_NOTNULL(vg, (void*)(*priv), VGZ_MAGIC);
	vp = VFP_Suck(bo, p, lp);
	if (vp == VFP_ERROR)
		return (vp);
	if (*lp > 0 || vp == VFP_END) {
		VGZ_Ibuf(vg, p, *lp);
		do {
			VGZ_Obuf(vg, vg->m_buf, vg->m_sz);
			vr = VGZ_Gunzip(vg, &dp, &dl);
			if (vr == VGZ_END && !VGZ_IbufEmpty(vg))
				return(VFP_Error(bo, "Junk after gzip data"));
			if (vr < VGZ_OK)
				return (VFP_Error(bo,
				    "Invalid Gzip data: %s", vg->vz.msg));
		} while (!VGZ_IbufEmpty(vg));
	}
	if (vp == VFP_END) {
		if (vr != VGZ_END)
			return (VFP_Error(bo, "tGunzip failed"));
		VGZ_UpdateObj(vg, bo->fetch_obj);
	}
	return (vp);
}
