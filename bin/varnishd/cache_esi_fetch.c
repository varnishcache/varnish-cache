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
#include "zlib.h"
#include "stevedore.h"

/*---------------------------------------------------------------------
 * We receive a ungzip'ed object, and want to store it ungzip'ed.
 */

static int __match_proto__()
vfp_esi_bytes_uu(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	ssize_t l, w;
	struct storage *st;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	while (bytes > 0) {
		if (sp->wrk->storage == NULL) {
			l = params->fetch_chunksize * 1024LL;
			sp->wrk->storage = STV_alloc(sp, l);
		}
		if (sp->wrk->storage == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		st = sp->wrk->storage;
		l = st->space - st->len;
		if (l > bytes)
			l = bytes;
		w = HTC_Read(htc, st->ptr + st->len, l);
		if (w <= 0)
			return (w);
		if (params->esi_syntax & 0x8) {
			ssize_t d;
			for (l = 0; l < w; l += d)  {
				d = (random() & 3) + 1;
				if (l + d >= w)
					d = 1;
				VEP_parse(sp,
				    (const char *)st->ptr + st->len + l, d);
			}
		} else
			VEP_parse(sp, (const char *)st->ptr + st->len, w);
		st->len += w;
		sp->obj->len += w;
		if (st->len == st->space) {
			VTAILQ_INSERT_TAIL(&sp->obj->store,
			    sp->wrk->storage, list);
			sp->wrk->storage = NULL;
			st = NULL;
		}
		bytes -= w;
	}
	return (1);
}

/*---------------------------------------------------------------------
 * We receive a gzip'ed object, and want to store it ungzip'ed.
 */

static int __match_proto__()
vfp_esi_bytes_gu(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	struct vgz *vg;
	ssize_t l, w;
	struct storage *st;
	uint8_t	ibuf[1024 * params->gzip_stack_buffer];
	int i;
	size_t dl;
	const void *dp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vg = sp->wrk->vgz_rx;

	while (bytes > 0) {
		if (sp->wrk->storage == NULL) {
			l = params->fetch_chunksize * 1024LL;
			sp->wrk->storage = STV_alloc(sp, l);
		}
		if (sp->wrk->storage == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		st = sp->wrk->storage;

		VGZ_Obuf(vg, st->ptr + st->len, st->space - st->len);

		if (VGZ_IbufEmpty(vg) && bytes > 0) {
			l = sizeof ibuf;
			if (l > bytes)
				l = bytes;
			w = HTC_Read(htc, ibuf, l);
			if (w <= 0)
				return (w);
			VGZ_Ibuf(vg, ibuf, w);
			bytes -= w;
		}
		i = VGZ_Gunzip(vg, &dp, &dl);
		VEP_parse(sp, dp, dl);
		st->len += dl;
		sp->obj->len += dl;
		if (st->len == st->space) {
			VTAILQ_INSERT_TAIL(&sp->obj->store,
			    sp->wrk->storage, list);
			sp->wrk->storage = NULL;
			st = NULL;
		}
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
};

/*---------------------------------------------------------------------
 * We receive a ungzip'ed object, and want to store it gzip'ed.
 */

#include "vend.h"

static ssize_t
vfp_vep_callback(const struct sess *sp, ssize_t l, enum vgz_flag flg)
{
	struct vef_priv *vef;
	struct storage *st;
	size_t dl;
	const void *dp;
	int i;
	char *p;

printf("ZCB(%jd, %d)\n", l, flg);

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vef = sp->wrk->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
	assert(l >= 0);

	if (vef->error) {
		vef->tot += l;
		return (vef->tot);
	}

	VGZ_Ibuf(vef->vgz, vef->bufp, l);
	do {
		if (sp->wrk->storage == NULL) {
			l = params->fetch_chunksize * 1024LL;
			sp->wrk->storage = STV_alloc(sp, l);
		}
		if (sp->wrk->storage == NULL) {
			vef->error = ENOMEM;
			vef->tot += l;
			return (vef->tot);
		}

		st = sp->wrk->storage;
		VGZ_Obuf(vef->vgz, st->ptr + st->len, st->space - st->len);
		p = (void*)(st->ptr + st->len);
		i = VGZ_Gzip(vef->vgz, &dp, &dl, flg);
printf("GZI = %d %jd\n", i, dl);
		vef->tot += dl;
		st->len += dl;
		if (st->len == st->space) {
			VTAILQ_INSERT_TAIL(&sp->obj->store,
			    sp->wrk->storage, list);
			sp->wrk->storage = NULL;
			st = NULL;
		}
	} while (!VGZ_IbufEmpty(vef->vgz));
	vef->bufp += l;
if (flg == VGZ_FINISH)
	assert(i == 1);			/* XXX */
else
	assert(i == 0);			/* XXX */
printf("ZCB = %jd\n", vef->tot);
fflush(stdout);
usleep(100);
	return (vef->tot);
}

static int __match_proto__()
vfp_esi_bytes_ug(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	ssize_t l, w;
	char ibuf[1024 * params->gzip_stack_buffer];
	struct vef_priv *vef;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vef = sp->wrk->vef_priv;
	CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);

	while (bytes > 0) {
		if (sp->wrk->storage == NULL) {
			l = params->fetch_chunksize * 1024LL;
			sp->wrk->storage = STV_alloc(sp, l);
		}
		if (sp->wrk->storage == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		l = sizeof ibuf;
		if (l > bytes)
			l = bytes;
		w = HTC_Read(htc, ibuf, l);
		if (w <= 0) {
printf("RT %jd\n", w);
			return (w);
}
		bytes -= w;
		vef->bufp = ibuf;
		VEP_parse(sp, ibuf, l);
		if (vef->error) {
			// errno = vef->error;
			return (-1);
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
	return (vfp_esi_bytes_uu(sp, htc, bytes));
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
		VEP_Init(sp, vfp_vep_callback);
		vef = (void*)WS_Alloc(sp->ws, sizeof *vef);
		AN(vef);
		memset(vef, 0, sizeof *vef);
		vef->magic = VEF_MAGIC;
		vef->vgz = VGZ_NewGzip(sp, sp->ws);
		AZ(sp->wrk->vef_priv);
		sp->wrk->vef_priv = vef;
	} else {
		VEP_Init(sp, NULL);
	}

	(void)estimate;
}

static int __match_proto__()
vfp_esi_bytes(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
printf("BYTES = %jd\n", bytes);
	if (sp->wrk->is_gzip && sp->wrk->do_gunzip)
		i = vfp_esi_bytes_gu(sp, htc, bytes);
	else if (sp->wrk->is_gunzip && sp->wrk->do_gzip)
		i = vfp_esi_bytes_ug(sp, htc, bytes);
	else if (sp->wrk->is_gzip)
		i = vfp_esi_bytes_gg(sp, htc, bytes);
	else
		i = vfp_esi_bytes_uu(sp, htc, bytes);
printf("BYTES = %d\n", i);
	return (i);
}

static int __match_proto__()
vfp_esi_end(struct sess *sp)
{
	struct storage *st;
	struct vsb *vsb;
	struct vef_priv *vef;
	ssize_t l;

printf("END\n");
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

	st = sp->wrk->storage;
	sp->wrk->storage = NULL;

	if (sp->wrk->vef_priv != NULL) {
		vef = sp->wrk->vef_priv;
		sp->wrk->vef_priv = NULL;
		CHECK_OBJ_NOTNULL(vef, VEF_MAGIC);
		XXXAZ(vef->error);
printf("TOT %jd\n", vef->tot);
sp->obj->len = vef->tot;
	}

	if (st == NULL)
		return (0);

	if (st->len == 0) {
		STV_free(st);
		return (0);
	}
	if (st->len < st->space)
		STV_trim(st, st->len);
	VTAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	return (0);
}

struct vfp vfp_esi = {
        .begin  =       vfp_esi_begin,
        .bytes  =       vfp_esi_bytes,
        .end    =       vfp_esi_end,
};
