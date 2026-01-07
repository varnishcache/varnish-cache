/*-
 * Copyright (c) 2013-2015 Varnish Software AS
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
 * Interaction with the libvgz (zlib) library.
 *
 * The zlib library pollutes namespace a LOT when you include the "vgz.h"
 * (aka "zlib.h") file so we contain the damage by vectoring all access
 * to libz through this source file.
 *
 * The API defined by this file, will also insulate the rest of the code,
 * should we find a better gzip library at a later date.
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "cache_vgz.h"

#include "storage/storage.h"

#include "vend.h"

#include "vgz.h"

struct vgz {
	unsigned		magic;
#define VGZ_MAGIC		0x162df0cb
	enum {VGZ_GZ,VGZ_UN}	dir;
	struct vsl_log		*vsl;
	const char		*id;
	int			last_i;
	enum vgz_flag		flag;

	struct stv_buffer	*stvbuf;
	char			*m_buf;
	ssize_t			m_sz;
	ssize_t			m_len;

	intmax_t		bits;

	z_stream		vz;
};

static const char *
vgz_msg(const struct vgz *vg)
{
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	return (vg->vz.msg ? vg->vz.msg : "(null)");
}

/*--------------------------------------------------------------------
 * Set up a gunzip instance
 */

static struct vgz *
vgz_gunzip(struct vsl_log *vsl, const char *id)
{
	struct vgz *vg;

	ALLOC_OBJ(vg, VGZ_MAGIC);
	AN(vg);
	vg->vsl = vsl;
	vg->id = id;
	vg->dir = VGZ_UN;

	/*
	 * Max memory usage according to zonf.h:
	 *	mem_needed = "a few kb" + (1 << (windowBits))
	 * Since we don't control windowBits, we have to assume
	 * it is 15, so 34-35KB or so.
	 */
	assert(Z_OK == inflateInit2(&vg->vz, 31));
	return (vg);
}

static struct vgz *
VGZ_NewGunzip(struct vsl_log *vsl, const char *id)
{
	VSC_C_main->n_gunzip++;
	return (vgz_gunzip(vsl, id));
}

static struct vgz *
VGZ_NewTestGunzip(struct vsl_log *vsl, const char *id)
{
	VSC_C_main->n_test_gunzip++;
	return (vgz_gunzip(vsl, id));
}

struct vgz *
VGZ_NewGzip(struct vsl_log *vsl, const char *id)
{
	struct vgz *vg;
	int i;

	VSC_C_main->n_gzip++;
	ALLOC_OBJ(vg, VGZ_MAGIC);
	AN(vg);
	vg->vsl = vsl;
	vg->id = id;
	vg->dir = VGZ_GZ;

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
vgz_getmbuf(struct worker *wrk, struct vgz *vg)
{
	size_t sz;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->m_sz);
	AZ(vg->m_len);
	AZ(vg->m_buf);
	AZ(vg->stvbuf);

	vg->stvbuf = STV_AllocBuf(wrk, stv_transient, cache_param->gzip_buffer);
	if (vg->stvbuf == NULL)
		return (-1);
	vg->m_buf = STV_GetBufPtr(vg->stvbuf, &sz);
	vg->m_sz = sz;
	AN(vg->m_buf);
	assert(vg->m_sz > 0);
	return (0);
}

/*--------------------------------------------------------------------*/

void
VGZ_Ibuf(struct vgz *vg, const void *ptr, ssize_t len)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	assert(len >= 0);
	if (len > 0)
		AN(ptr);

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
	AN(ptr);
	assert(len > 0);

	vg->vz.next_out = ptr;
	vg->vz.avail_out = len;
}

int
VGZ_ObufFull(const struct vgz *vg)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	return (vg->vz.avail_out == 0);
}

/*--------------------------------------------------------------------*/

static enum vgzret_e
VGZ_Gunzip(struct vgz *vg, const void **pptr, ssize_t *plen)
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
	VSLb(vg->vsl, SLT_Gzip, "Gunzip error: %d (%s)", i, vgz_msg(vg));
	return (VGZ_ERROR);
}

/* set vz pointers for in and out iovecs */
static inline void
vgz_iovec_update(struct vgz *vg, const struct iovec *in, const struct iovec *buf)
{
	/* in: either fully consumed or the same */
	assert(vg->vz.avail_in == 0 || vg->vz.next_in == TRUST_ME(in->iov_base));
	vg->vz.next_in = TRUST_ME(in->iov_base);
	vg->vz.avail_in = in->iov_len;
	vg->vz.next_out = TRUST_ME(buf->iov_base);
	vg->vz.avail_out = buf->iov_len;
}

static enum vgzret_e
vgz_gunzip_iovec(struct vgz *vg, struct iovec *in, struct iovec *buf, struct iovec *out)
{
	int i;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AN(in && buf && out);
	vgz_iovec_update(vg, in, buf);

	i = inflate(&vg->vz, 0);
	if (i == Z_OK || i == Z_STREAM_END) {
		iovec_collect(buf, out, pdiff(buf->iov_base, vg->vz.next_out));
		in->iov_base = TRUST_ME(vg->vz.next_in);
		in->iov_len = vg->vz.avail_in;
	}
	vg->last_i = i;
	if (i == Z_OK)
		return (VGZ_OK);
	if (i == Z_STREAM_END)
		return (VGZ_END);
	if (i == Z_BUF_ERROR)
		return (VGZ_STUCK);
	VSLb(vg->vsl, SLT_Gzip, "Gunzip error: %d (%s)", i, vgz_msg(vg));
	return (VGZ_ERROR);
}

/*--------------------------------------------------------------------*/

enum vgzret_e
VGZ_Gzip(struct vgz *vg, const void **pptr, ssize_t *plen, enum vgz_flag flags)
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
	switch (flags) {
	case VGZ_NORMAL:	zflg = Z_NO_FLUSH; break;
	case VGZ_ALIGN:		zflg = Z_SYNC_FLUSH; break;
	case VGZ_RESET:		zflg = Z_FULL_FLUSH; break;
	case VGZ_FINISH:	zflg = Z_FINISH; break;
	default:		WRONG("Invalid VGZ flag");
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
	VSLb(vg->vsl, SLT_Gzip, "Gzip error: %d (%s)", i, vgz_msg(vg));
	return (VGZ_ERROR);
}

/*--------------------------------------------------------------------
 * VDP for gunzip'ing
 */

// common for traditional interface and vdpio
static int vdp_gunzip_init_common(struct vdp_ctx *vdc);

static int v_matchproto_(vdp_init_f)
vdp_gunzip_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{
	struct vgz *vg;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	vg = VGZ_NewGunzip(vdc->vsl, "U D -");
	AN(vg);
	if (vgz_getmbuf(vdc->wrk, vg)) {
		(void)VGZ_Destroy(vdc->wrk, &vg);
		return (-1);
	}

	VGZ_Obuf(vg, vg->m_buf, vg->m_sz);
	*priv = vg;
	return (vdp_gunzip_init_common(vdc));
}

static int
vdp_gunzip_init_common(struct vdp_ctx *vdc)
{
	struct boc *boc;
	enum boc_state_e bos;
	const char *p;
	ssize_t dl;
	uint64_t u;
	http_Unset(vdc->hp, H_Content_Encoding);

	*vdc->clen = -1;

	if (vdc->oc == NULL)
		return (0);

	boc = HSH_RefBoc(vdc->oc);
	if (boc != NULL) {
		CHECK_OBJ(boc, BOC_MAGIC);
		bos = boc->state;
		HSH_DerefBoc(vdc->wrk, vdc->oc);
		if (bos < BOS_FINISHED)
			return (0); /* OA_GZIPBITS is not stable yet */
	}

	p = ObjGetAttr(vdc->wrk, vdc->oc, OA_GZIPBITS, &dl);
	if (p != NULL && dl == 32) {
		u = vbe64dec(p + 24);
		if (u != 0)
			*vdc->clen = u;
	}
	return (0);
}

static int v_matchproto_(vdp_fini_f)
vdp_gunzip_fini(struct vdp_ctx *vdc, void **priv)
{
	struct vgz *vg;

	(void)vdc;
	TAKE_OBJ_NOTNULL(vg, priv, VGZ_MAGIC);
	AN(vg->m_buf);
	(void)VGZ_Destroy(vdc->wrk, &vg);
	return (0);
}

static int v_matchproto_(vdp_bytes_f)
vdp_gunzip_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	enum vgzret_e vr;
	ssize_t dl;
	const void *dp;
	struct vgz *vg;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->wrk, WORKER_MAGIC);
	(void)act;

	CAST_OBJ_NOTNULL(vg, *priv, VGZ_MAGIC);
	AN(vg->m_buf);

	if (len == 0)
		return (0);

	VGZ_Ibuf(vg, ptr, len);
	do {
		vr = VGZ_Gunzip(vg, &dp, &dl);
		if (vr == VGZ_END && !VGZ_IbufEmpty(vg)) {
			VSLb(vg->vsl, SLT_Gzip, "G(un)zip error: %d (%s)",
			     vr, "junk after VGZ_END");
			return (-1);
		}
		vg->m_len += dl;
		if (vr < VGZ_OK)
			return (-1);
		// END or STUCK
		if (vg->m_len == vg->m_sz || vr != VGZ_OK) {
			if (VDP_bytes(vdc, vr == VGZ_END ? VDP_END : VDP_FLUSH,
			    vg->m_buf, vg->m_len))
				return (vdc->retval);
			vg->m_len = 0;
			VGZ_Obuf(vg, vg->m_buf, vg->m_sz);
		}
	} while (!VGZ_IbufEmpty(vg));
	assert(vr == VGZ_STUCK || vr == VGZ_OK || vr == VGZ_END);
	return (0);
}

#ifdef LATER
/*
 * XXX does it make sense to work on more than one buffer?
 */
static int v_matchproto_(vdpio_init_f)
vdpio_gunzip_init(VRT_CTX, struct vdp_ctx *vdc, void **priv, int capacity)
{
	struct vgz *vg;
	struct vscarab *in;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);
	assert(capacity >= 1);

	if (capacity < 4)
		capacity = 4;

	in = WS_Alloc(ctx->ws, VSCARAB_SIZE(capacity));
	if (in == NULL)
		return (-1);

	vg = VGZ_NewGunzip(vdc->vsl, "U D -");
	if (vg == NULL)
		return (-1);

	AZ(vg->m_buf);
	vg->m_buf = (void *)in;

	*priv = vg;
	AZ(vdp_gunzip_init_common(vdc));
	return (1);
}
#endif

static int v_matchproto_(vdpio_init_f)
vdpio_gunzip_upgrade(VRT_CTX, struct vdp_ctx *vdc, void **priv, int capacity)
{
	struct vgz *vg;
	struct vscarab *in;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	AN(priv);
	assert(capacity >= 1);

	if (capacity < 4)
		capacity = 4;

	/* done in vdp_gunzip_init */
	CAST_OBJ_NOTNULL(vg, *priv, VGZ_MAGIC);

	in = WS_Alloc(ctx->ws, VSCARAB_SIZE(capacity));
	if (in == NULL)
		return (-1);
	VSCARAB_INIT(in, capacity);

	// XXX duplicate work - remove when completing transition to VAI
	AN(vg->m_buf);
	AN(vg->stvbuf);
	STV_FreeBuf(vdc->wrk, &vg->stvbuf);
	vg->stvbuf = NULL;
	vg->m_buf = (void *)in;

	return (1);
}

static int v_matchproto_(vdpio_lease_f)
vdpio_gunzip_lease(struct vdp_ctx *vdc, struct vdp_entry *this, struct vscarab *out)
{
	struct vscarab *in;
	enum vgzret_e vr;
	struct vgz *vg;
	struct viov *v, *b, *o;
	int r;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(this, VDP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(vg, this->priv, VGZ_MAGIC);
	CAST_OBJ_NOTNULL(in, (void*)vg->m_buf, VSCARAB_MAGIC);

	this->calls++;

	if (out->used == out->capacity)
		return (0);

	if (in->used < in->capacity && (in->flags & VSCARAB_F_END) == 0)
		r = vdpio_pull(vdc, this, in);
	else
		r = 0;

	if (in->used == 0) {
		out->flags = in->flags & VSCARAB_F_END;
		return (r);
	}

	// XXX more than one buffer?
	VSCARAB_LOCAL(buf, 1);
	b = VSCARAB_GET(buf);
	AN(b);
	b->iov.iov_len = cache_param->gzip_buffer;
	r = ObjVAIbuffer(vdc->wrk, vdc->vai_hdl, buf);
	if (r < 0)
		return (out->used ? 0 : r);

	o = VSCARAB_GET(out);
	AN(o);
	r = 0;

	VSCARAB_FOREACH(v, in) {
		this->bytes_in += v->iov.iov_len;
		vr = vgz_gunzip_iovec(vg, &v->iov, &b->iov, &o->iov);
		if (vr == VGZ_END && v->iov.iov_len > 0) {
			VSLb(vg->vsl, SLT_Gzip, "G(un)zip error: %d (%s)",
			     vr, "junk after VGZ_END");
			r = -EMSGSIZE;
			break;
		}
		if (vr < VGZ_OK)
			break;

		if (b->iov.iov_len == 0 || vr != VGZ_OK) {
			r = 1;
			break;
		}
	}

	if (r <= 0) {
		o->iov.iov_base = NULL;
		o->iov.iov_len = 0;
		vdpio_return_lease(vdc, b->lease);
		return (r);
	}

	o->lease = b->lease;
	b->lease = 0;

	vdpio_consolidate_vscarab(vdc, in);
	if (in->used == 0)
		out->flags = in->flags & VSCARAB_F_END;

	return (r);
}

static void v_matchproto_(vdpio_fini_f)
vdpio_gunzip_fini(struct vdp_ctx *vdc, void **priv)
{
	struct vscarab *in;
	struct vgz *vg;

	(void)vdc;
	TAKE_OBJ_NOTNULL(vg, priv, VGZ_MAGIC);
	CAST_OBJ_NOTNULL(in, (void *)vg->m_buf, VSCARAB_MAGIC);
	vg->m_buf = NULL;

	(void)VGZ_Destroy(vdc->wrk, &vg);
}

const struct vdp VDP_gunzip = {
	.name =		"gunzip",
	.init =		vdp_gunzip_init,
	.bytes =	vdp_gunzip_bytes,
	.fini =		vdp_gunzip_fini,

#ifdef LATER
	.io_init =	vdpio_gunzip_init,
#endif
	.io_upgrade =	vdpio_gunzip_upgrade,
	.io_lease =	vdpio_gunzip_lease,
	.io_fini =	vdpio_gunzip_fini,
};

/*--------------------------------------------------------------------*/

void
VGZ_UpdateObj(const struct vfp_ctx *vc, struct vgz *vg, enum vgzret_e e)
{
	char *p;
	intmax_t ii;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	if (e < VGZ_OK)
		return;
	ii = vg->vz.start_bit + vg->vz.last_bit + vg->vz.stop_bit;
	if (e != VGZ_END && ii == vg->bits)
		return;
	vg->bits = ii;
	p = ObjSetAttr(vc->wrk, vc->oc, OA_GZIPBITS, 32, NULL);
	AN(p);
	vbe64enc(p, vg->vz.start_bit);
	vbe64enc(p + 8, vg->vz.last_bit);
	vbe64enc(p + 16, vg->vz.stop_bit);
	if (e != VGZ_END)
		return;
	if (vg->dir == VGZ_GZ)
		vbe64enc(p + 24, vg->vz.total_in);
	if (vg->dir == VGZ_UN)
		vbe64enc(p + 24, vg->vz.total_out);
}

/*--------------------------------------------------------------------
 */

enum vgzret_e
VGZ_Destroy(struct worker *wrk, struct vgz **vgp)
{
	struct vgz *vg;
	enum vgzret_e vr;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(vg, vgp, VGZ_MAGIC);
	AN(vg->id);
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
	if (vg->m_buf != NULL) {
		AN(vg->stvbuf);
		STV_FreeBuf(wrk, &vg->stvbuf);
	}
	AZ(vg->stvbuf);
	if (i == Z_OK)
		vr = VGZ_OK;
	else if (i == Z_STREAM_END)
		vr = VGZ_END;
	else if (i == Z_BUF_ERROR)
		vr = VGZ_STUCK;
	else {
		VSLb(vg->vsl, SLT_Gzip, "G(un)zip error: %d (%s)",
		    i, vgz_msg(vg));
		vr = VGZ_ERROR;
	}
	FREE_OBJ(vg);
	return (vr);
}

/*--------------------------------------------------------------------*/

static enum vfp_status v_matchproto_(vfp_init_f)
vfp_gzip_init(VRT_CTX, struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct vgz *vg;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);

	/*
	 * G(un)zip makes no sence on partial responses, but since
	 * it is an pure 1:1 transform, we can just ignore it.
	 */
	if (http_GetStatus(vc->resp) == 206)
		return (VFP_NULL);

	if (vfe->vfp == &VFP_gzip) {
		if (http_GetHdr(vc->resp, H_Content_Encoding, NULL))
			return (VFP_NULL);
		vg = VGZ_NewGzip(vc->wrk->vsl, vfe->vfp->priv1);
		vc->obj_flags |= OF_GZIPED | OF_CHGCE;
	} else {
		if (!http_HdrIs(vc->resp, H_Content_Encoding, "gzip"))
			return (VFP_NULL);
		if (vfe->vfp == &VFP_gunzip) {
			vg = VGZ_NewGunzip(vc->wrk->vsl, vfe->vfp->priv1);
			vc->obj_flags &= ~OF_GZIPED;
			vc->obj_flags |= OF_CHGCE;
		} else {
			vg = VGZ_NewTestGunzip(vc->wrk->vsl, vfe->vfp->priv1);
			vc->obj_flags |= OF_GZIPED;
		}
	}
	AN(vg);
	vfe->priv1 = vg;
	if (vgz_getmbuf(vc->wrk, vg))
		return (VFP_ERROR);
	VGZ_Ibuf(vg, vg->m_buf, 0);
	AZ(vg->m_len);

	if (vfe->vfp == &VFP_gunzip || vfe->vfp == &VFP_gzip) {
		http_Unset(vc->resp, H_Content_Encoding);
		http_Unset(vc->resp, H_Content_Length);
		RFC2616_Weaken_Etag(vc->resp);
	}

	if (vfe->vfp == &VFP_gzip)
		http_SetHeader(vc->resp, "Content-Encoding: gzip");

	if (vfe->vfp == &VFP_gzip || vfe->vfp == &VFP_testgunzip)
		RFC2616_Vary_AE(vc->resp);

	return (VFP_OK);
}

/*--------------------------------------------------------------------
 * VFP_GUNZIP
 *
 * A VFP for gunzip'ing an object as we receive it from the backend
 */

static enum vfp_status v_matchproto_(vfp_pull_f)
vfp_gunzip_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	ssize_t l;
	struct vgz *vg;
	enum vgzret_e vr = VGZ_ERROR;
	const void *dp;
	ssize_t dl;
	enum vfp_status vp = VFP_OK;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(vg, vfe->priv1, VGZ_MAGIC);
	AN(p);
	AN(lp);
	l = *lp;
	*lp = 0;
	VGZ_Obuf(vg, p, l);
	do {
		if (VGZ_IbufEmpty(vg)) {
			l = vg->m_sz;
			vp = VFP_Suck(vc, vg->m_buf, &l);
			if (vp == VFP_ERROR)
				return (vp);
			VGZ_Ibuf(vg, vg->m_buf, l);
		}
		if (!VGZ_IbufEmpty(vg) || vp == VFP_END) {
			vr = VGZ_Gunzip(vg, &dp, &dl);
			if (vr == VGZ_END && !VGZ_IbufEmpty(vg))
				return (VFP_Error(vc, "Junk after gzip data"));
			if (vr < VGZ_OK)
				return (VFP_Error(vc,
				    "Invalid Gzip data: %s", vgz_msg(vg)));
			if (dl > 0) {
				*lp = dl;
				assert(dp == p);
				return (VFP_OK);
			}
		}
		AN(VGZ_IbufEmpty(vg));
	} while (vp == VFP_OK);
	if (vr != VGZ_END)
		return (VFP_Error(vc, "Gunzip error at the very end"));
	return (vp);
}


/*--------------------------------------------------------------------
 * VFP_GZIP
 *
 * A VFP for gzip'ing an object as we receive it from the backend
 */

static enum vfp_status v_matchproto_(vfp_pull_f)
vfp_gzip_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	ssize_t l;
	struct vgz *vg;
	enum vgzret_e vr = VGZ_ERROR;
	const void *dp;
	ssize_t dl;
	enum vfp_status vp = VFP_ERROR;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(vg, vfe->priv1, VGZ_MAGIC);
	AN(p);
	AN(lp);
	l = *lp;
	*lp = 0;
	VGZ_Obuf(vg, p, l);
	do {
		if (VGZ_IbufEmpty(vg)) {
			l = vg->m_sz;
			vp = VFP_Suck(vc, vg->m_buf, &l);
			if (vp == VFP_ERROR)
				break;
			if (vp == VFP_END)
				vg->flag = VGZ_FINISH;
			VGZ_Ibuf(vg, vg->m_buf, l);
		}
		if (!VGZ_IbufEmpty(vg) || vg->flag == VGZ_FINISH) {
			vr = VGZ_Gzip(vg, &dp, &dl, vg->flag);
			if (vr < VGZ_OK)
				return (VFP_Error(vc, "Gzip failed"));
			if (dl > 0) {
				VGZ_UpdateObj(vc, vg, vr);
				*lp = dl;
				assert(dp == p);
				if (vr != VGZ_END || !VGZ_IbufEmpty(vg))
					return (VFP_OK);
			}
		}
		AN(VGZ_IbufEmpty(vg));
	} while (vg->flag != VGZ_FINISH);

	if (vr != VGZ_END)
		return (VFP_Error(vc, "Gzip failed"));
	VGZ_UpdateObj(vc, vg, VGZ_END);
	return (VFP_END);
}

/*--------------------------------------------------------------------
 * VFP_TESTGZIP
 *
 * A VFP for testing that received gzip data is valid, and for
 * collecting the magic bits while we're at it.
 */

static enum vfp_status v_matchproto_(vfp_pull_f)
vfp_testgunzip_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	struct vgz *vg;
	enum vgzret_e vr = VGZ_ERROR;
	const void *dp;
	ssize_t dl;
	enum vfp_status vp;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(vg, vfe->priv1, VGZ_MAGIC);
	AN(p);
	AN(lp);
	vp = VFP_Suck(vc, p, lp);
	if (vp == VFP_ERROR)
		return (vp);
	if (*lp > 0 || vp == VFP_END) {
		VGZ_Ibuf(vg, p, *lp);
		do {
			VGZ_Obuf(vg, vg->m_buf, vg->m_sz);
			vr = VGZ_Gunzip(vg, &dp, &dl);
			if (vr == VGZ_END && !VGZ_IbufEmpty(vg))
				return (VFP_Error(vc, "Junk after gzip data"));
			if (vr < VGZ_OK)
				return (VFP_Error(vc,
				    "Invalid Gzip data: %s", vgz_msg(vg)));
		} while (!VGZ_IbufEmpty(vg));
	}
	VGZ_UpdateObj(vc, vg, vr);
	if (vp == VFP_END && vr != VGZ_END)
		return (VFP_Error(vc, "tGunzip failed"));
	return (vp);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(vfp_fini_f)
vfp_gzip_fini(struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct vgz *vg;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);

	if (vfe->priv1 != NULL) {
		TAKE_OBJ_NOTNULL(vg, &vfe->priv1, VGZ_MAGIC);
		(void)VGZ_Destroy(vc->wrk, &vg);
	}
}

/*--------------------------------------------------------------------*/

const struct vfp VFP_gunzip = {
	.name = "gunzip",
	.init = vfp_gzip_init,
	.pull = vfp_gunzip_pull,
	.fini = vfp_gzip_fini,
	.priv1 = "U F -",
};

const struct vfp VFP_gzip = {
	.name = "gzip",
	.init = vfp_gzip_init,
	.pull = vfp_gzip_pull,
	.fini = vfp_gzip_fini,
	.priv1 = "G F -",
};

const struct vfp VFP_testgunzip = {
	.name = "testgunzip",
	.init = vfp_gzip_init,
	.pull = vfp_testgunzip_pull,
	.fini = vfp_gzip_fini,
	.priv1 = "u F -",
};
