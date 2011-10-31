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
vef_read(struct worker *w, struct http_conn *htc, void *buf, ssize_t buflen,
    ssize_t bytes)
{
	ssize_t d;

	if (buflen < bytes)
		bytes = buflen;
	if (params->esi_syntax & 0x8) {
		d = (random() & 3) + 1;
		if (d < bytes)
			bytes = d;
	}
	return (HTC_Read(w, htc, buf, bytes));
}

/*---------------------------------------------------------------------
 * We receive a ungzip'ed object, and want to store it ungzip'ed.
 */

static int
vfp_esi_bytes_uu(struct worker *w, struct http_conn *htc, ssize_t bytes)
{
	ssize_t wl;
	struct storage *st;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

	while (bytes > 0) {
		st = FetchStorage(w, 0);
		if (st == NULL)
			return (-1);
		wl = vef_read(w, htc,
		    st->ptr + st->len, st->space - st->len, bytes);
		if (wl <= 0)
			return (wl);
		VEP_Parse(w, (const char *)st->ptr + st->len, wl);
		st->len += wl;
		w->fetch_obj->len += wl;
		bytes -= wl;
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it ungzip'ed.
 */

static int
vfp_esi_bytes_gu(struct worker *w, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t wl;
	uint8_t	ibuf[params->gzip_stack_buffer];
	int i;
	size_t dl;
	const void *dp;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	vg = w->vgz_rx;

	while (bytes > 0) {
		if (VGZ_IbufEmpty(vg) && bytes > 0) {
			wl = vef_read(w, htc, ibuf, sizeof ibuf, bytes);
			if (wl <= 0)
				return (wl);
			VGZ_Ibuf(vg, ibuf, wl);
			bytes -= wl;
		}
		if (VGZ_ObufStorage(w, vg))
			return(-1);
		i = VGZ_Gunzip(vg, &dp, &dl);
		xxxassert(i == VGZ_OK || i == VGZ_END);
		VEP_Parse(w, dp, dl);
		w->fetch_obj->len += dl;
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
vfp_vep_callback(struct worker *w, ssize_t l, enum vgz_flag flg)
{
	struct vef_priv *vef;
	size_t dl, px;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	vef = w->vef_priv;
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
			if (VGZ_ObufStorage(w, vef->vgz)) {
				vef->error = ENOMEM;
				vef->tot += l;
				return (vef->tot);
			}
			i = VGZ_Gzip(vef->vgz, &dp, &dl, flg);
			vef->tot += dl;
			w->fetch_obj->len += dl;
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
vfp_esi_bytes_ug(struct worker *w, struct http_conn *htc, ssize_t bytes)
{
	ssize_t wl;
	char ibuf[params->gzip_stack_buffer];
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	vef = w->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	while (bytes > 0) {
		wl = vef_read(w, htc, ibuf, sizeof ibuf, bytes);
		if (wl <= 0)
			return (wl);
		bytes -= wl;
		vef->bufp = ibuf;
		VEP_Parse(w, ibuf, wl);
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
vfp_esi_bytes_gg(struct worker *w, struct http_conn *htc, size_t bytes)
{
	ssize_t wl;
	char ibuf[params->gzip_stack_buffer];
	char ibuf2[params->gzip_stack_buffer];
	struct vef_priv *vef;
	size_t dl;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	vef = w->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	assert(sizeof ibuf >= 1024);
	ibuf2[0] = 0; /* For Flexelint */

	while (bytes > 0) {
		wl = vef_read(w, htc, ibuf, sizeof ibuf, bytes);
		if (wl <= 0)
			return (wl);
		bytes -= wl;

		vef->bufp = ibuf;
		VGZ_Ibuf(w->vgz_rx, ibuf, wl);
		do {
			VGZ_Obuf(w->vgz_rx, ibuf2, sizeof ibuf2);
			i = VGZ_Gunzip(w->vgz_rx, &dp, &dl);
			/* XXX: check i */
			assert(i >= VGZ_OK);
			vef->bufp = ibuf2;
			if (dl > 0)
				VEP_Parse(w, ibuf2, dl);
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
		} while (!VGZ_IbufEmpty(w->vgz_rx));
	}
	return (1);
}


/*---------------------------------------------------------------------*/

static void __match_proto__()
vfp_esi_begin(struct worker *w, size_t estimate)
{
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

	AZ(w->vgz_rx);
	if (w->is_gzip && w->do_gunzip) {
		w->vgz_rx = VGZ_NewUngzip(w, "U F E");
		VEP_Init(w, NULL);
	} else if (w->is_gunzip && w->do_gzip) {
		ALLOC_OBJ(vef, VEF_MAGIC);
		AN(vef);
		vef->vgz = VGZ_NewGzip(w, "G F E");
		AZ(w->vef_priv);
		w->vef_priv = vef;
		VEP_Init(w, vfp_vep_callback);
	} else if (w->is_gzip) {
		w->vgz_rx = VGZ_NewUngzip(w, "U F E");
		ALLOC_OBJ(vef, VEF_MAGIC);
		AN(vef);
		vef->vgz = VGZ_NewGzip(w, "G F E");
		AZ(w->vef_priv);
		w->vef_priv = vef;
		VEP_Init(w, vfp_vep_callback);
	} else {
		AZ(w->vef_priv);
		VEP_Init(w, NULL);
	}

	(void)estimate;
	AN(w->vep);
}

static int __match_proto__()
vfp_esi_bytes(struct worker *w, struct http_conn *htc, ssize_t bytes)
{
	int i;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AZ(w->fetch_failed);
	AN(w->vep);
	assert(w->htc == htc);
	if (w->is_gzip && w->do_gunzip)
		i = vfp_esi_bytes_gu(w, htc, bytes);
	else if (w->is_gunzip && w->do_gzip)
		i = vfp_esi_bytes_ug(w, htc, bytes);
	else if (w->is_gzip)
		i = vfp_esi_bytes_gg(w, htc, bytes);
	else
		i = vfp_esi_bytes_uu(w, htc, bytes);
	AN(w->vep);
	return (i);
}

static int __match_proto__()
vfp_esi_end(struct worker *w)
{
	struct vsb *vsb;
	struct vef_priv *vef;
	ssize_t l;
	int retval;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(w->vep);

	retval = w->fetch_failed;

	if (w->vgz_rx != NULL && VGZ_Destroy(&w->vgz_rx, -1) != VGZ_END)
		retval = FetchError(w,
		    "Gunzip+ESI Failed at the very end");

	vsb = VEP_Finish(w);

	if (vsb != NULL) {
		if (!retval) {
			l = VSB_len(vsb);
			assert(l > 0);
			/* XXX: This is a huge waste of storage... */
			w->fetch_obj->esidata = STV_alloc(w, l);
			if (w->fetch_obj->esidata != NULL) {
				memcpy(w->fetch_obj->esidata->ptr,
				    VSB_data(vsb), l);
				w->fetch_obj->esidata->len = l;
			} else {
				retval = FetchError(w,
				    "Could not allocate storage for esidata");
			}
		}
		VSB_delete(vsb);
	}

	if (w->vef_priv != NULL) {
		vef = w->vef_priv;
		CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
		w->vef_priv = NULL;
		VGZ_UpdateObj(vef->vgz, w->fetch_obj);
		if (VGZ_Destroy(&vef->vgz,  -1) != VGZ_END)
			retval = FetchError(w, 
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
