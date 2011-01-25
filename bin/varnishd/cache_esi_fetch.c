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

#include "svnid.h"
SVNID("$Id")

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "cache_esi.h"
#include "vct.h"
#include "vgz.h"
#include "stevedore.h"

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
	if (params->esi_syntax & 0x8) {
		d = (random() & 3) + 1;
		if (d < bytes)
			bytes = d;
	}
	return (HTC_Read(htc, buf, bytes));
}

/*---------------------------------------------------------------------
 * We receive a ungzip'ed object, and want to store it ungzip'ed.
 */

static int __match_proto__()
vfp_esi_bytes_uu(struct sess *sp, struct http_conn *htc, ssize_t bytes)
{
	ssize_t w;
	struct storage *st;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	while (bytes > 0) {
		if (FetchStorage(sp))
			return (-1);
		st = sp->wrk->storage;
		w = vef_read(htc,
		    st->ptr + st->len, st->space - st->len, bytes);
		if (w <= 0)
			return (w);
		VEP_parse(sp, (const char *)st->ptr + st->len, w);
		st->len += w;
		sp->obj->len += w;
		bytes -= w;
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it ungzip'ed.
 */

static int __match_proto__()
vfp_esi_bytes_gu(struct sess *sp, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t w;
	uint8_t	ibuf[params->gzip_stack_buffer];
	int i;
	size_t dl;
	const void *dp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vg = sp->wrk->vgz_rx;

	while (bytes > 0) {
		if (VGZ_IbufEmpty(vg) && bytes > 0) {
			w = vef_read(htc, ibuf, sizeof ibuf, bytes);
			if (w <= 0)
				return (w);
			VGZ_Ibuf(vg, ibuf, w);
			bytes -= w;
		}
		if (VGZ_ObufStorage(sp, vg))
			return (-1);
		i = VGZ_Gunzip(vg, &dp, &dl);
		xxxassert(i == Z_OK || i == Z_STREAM_END);
		VEP_parse(sp, dp, dl);
		sp->obj->len += dl;
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
 * We receive a ungzip'ed object, and want to store it gzip'ed.
 */

static ssize_t
vfp_vep_callback(const struct sess *sp, ssize_t l, enum vgz_flag flg)
{
	struct vef_priv *vef;
	size_t dl, px;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vef = sp->wrk->vef_priv;
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
			if (VGZ_ObufStorage(sp, vef->vgz)) {
				vef->error = errno;
				vef->tot += l;
				return (vef->tot);
			}
			i = VGZ_Gzip(vef->vgz, &dp, &dl, flg);
			vef->tot += dl;
			sp->obj->len += dl;
		} while (!VGZ_IbufEmpty(vef->vgz));
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

static int __match_proto__()
vfp_esi_bytes_ug(struct sess *sp, struct http_conn *htc, ssize_t bytes)
{
	ssize_t w;
	char ibuf[params->gzip_stack_buffer];
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vef = sp->wrk->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	while (bytes > 0) {
		w = vef_read(htc, ibuf, sizeof ibuf, bytes);
		if (w <= 0)
			return (w);
		bytes -= w;
		vef->bufp = ibuf;
		VEP_parse(sp, ibuf, w);
		if (vef->error) {
			errno = vef->error;
			return (-1);
		}
		if (vef->bufp < ibuf + w) {
			w = (ibuf + w) - vef->bufp;
			assert(w + vef->npend < sizeof vef->pending);
			memmove(vef->pending + vef->npend, vef->bufp, w);
			vef->npend += w;
		}
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it gzip'ed.
 */

static int __match_proto__()
vfp_esi_bytes_gg(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	ssize_t w;
	char ibuf[params->gzip_stack_buffer];
	char ibuf2[params->gzip_stack_buffer];
	struct vef_priv *vef;
	size_t dl;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vef = sp->wrk->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	assert(sizeof ibuf >= 1024);
	ibuf2[0] = 0; /* For Flexelint */

	while (bytes > 0) {
		w = vef_read(htc, ibuf, sizeof ibuf, bytes);
		if (w <= 0)
			return (w);
		bytes -= w;
		
		vef->bufp = ibuf;
		VGZ_Ibuf(sp->wrk->vgz_rx, ibuf, w);
		do {
			VGZ_Obuf(sp->wrk->vgz_rx, ibuf2, sizeof ibuf2);
			i = VGZ_Gunzip(sp->wrk->vgz_rx, &dp, &dl);
			/* XXX: check i */
			assert(i >= 0);
			vef->bufp = ibuf2;
			if (dl > 0)
				VEP_parse(sp, ibuf2, dl);
			if (vef->error) {
				errno = vef->error;
				return (-1);
			}
			if (vef->bufp < ibuf2 + dl) {
				dl = (ibuf2 + dl) - vef->bufp;
				assert(dl + vef->npend < sizeof vef->pending);
				memmove(vef->pending + vef->npend, vef->bufp, dl);
				vef->npend += dl;
			}
		} while (!VGZ_IbufEmpty(sp->wrk->vgz_rx));
	}
	return (1);
}


/*---------------------------------------------------------------------*/

static void __match_proto__()
vfp_esi_begin(struct sess *sp, size_t estimate)
{
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	/* XXX: snapshot WS's ? We'll need the space */

	if (sp->wrk->is_gzip && sp->wrk->do_gunzip) {
		sp->wrk->vgz_rx = VGZ_NewUngzip(sp, sp->ws);
		VEP_Init(sp, NULL);
	} else if (sp->wrk->is_gunzip && sp->wrk->do_gzip) {
		vef = (void*)WS_Alloc(sp->ws, sizeof *vef);
		AN(vef);
		memset(vef, 0, sizeof *vef);
		vef->magic = VEF_MAGIC;
		vef->vgz = VGZ_NewGzip(sp, sp->ws);
		AZ(sp->wrk->vef_priv);
		sp->wrk->vef_priv = vef;
		VEP_Init(sp, vfp_vep_callback);
	} else if (sp->wrk->is_gzip) {
		sp->wrk->vgz_rx = VGZ_NewUngzip(sp, sp->ws);
		vef = (void*)WS_Alloc(sp->ws, sizeof *vef);
		AN(vef);
		memset(vef, 0, sizeof *vef);
		vef->magic = VEF_MAGIC;
		vef->vgz = VGZ_NewGzip(sp, sp->ws);
		AZ(sp->wrk->vef_priv);
		sp->wrk->vef_priv = vef;
		VEP_Init(sp, vfp_vep_callback);
	} else {
		VEP_Init(sp, NULL);
	}

	(void)estimate;
}

static int __match_proto__()
vfp_esi_bytes(struct sess *sp, struct http_conn *htc, ssize_t bytes)
{
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->wrk->is_gzip && sp->wrk->do_gunzip)
		i = vfp_esi_bytes_gu(sp, htc, bytes);
	else if (sp->wrk->is_gunzip && sp->wrk->do_gzip)
		i = vfp_esi_bytes_ug(sp, htc, bytes);
	else if (sp->wrk->is_gzip)
		i = vfp_esi_bytes_gg(sp, htc, bytes);
	else
		i = vfp_esi_bytes_uu(sp, htc, bytes);
	return (i);
}

static int __match_proto__()
vfp_esi_end(struct sess *sp)
{
	struct vsb *vsb;
	struct vef_priv *vef;
	ssize_t l;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	if (sp->wrk->is_gzip && sp->wrk->do_gunzip)
		VGZ_Destroy(&sp->wrk->vgz_rx);

	vsb = VEP_Finish(sp);

	if (vsb != NULL) {
		l = vsb_len(vsb);
		assert(l > 0);
		/* XXX: This is a huge waste of storage... */
		sp->obj->esidata = STV_alloc(sp, l);
		AN(sp->obj->esidata);
		memcpy(sp->obj->esidata->ptr, vsb_data(vsb), l);
		sp->obj->esidata->len = l;
		vsb_delete(vsb);
	}

	if (sp->wrk->vef_priv != NULL) {
		vef = sp->wrk->vef_priv;
		VGZ_UpdateObj(vef->vgz, sp->obj);
		sp->wrk->vef_priv = NULL;
		CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
		XXXAZ(vef->error);
		sp->obj->gziped = 1;
	} else {
		sp->obj->gziped = 0;
	}
	return (0);
}

struct vfp vfp_esi = {
        .begin  =       vfp_esi_begin,
        .bytes  =       vfp_esi_bytes,
        .end    =       vfp_esi_end,
};
