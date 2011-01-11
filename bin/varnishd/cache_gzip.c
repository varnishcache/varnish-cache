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
 * Interaction with the linvgz (zlib) library.
 *
 * The libz library pollutes namespace a LOT when you include the "zlib.h"
 * file so we contain the damage by vectoring all access to libz through
 * this source file.
 *
 * The API defined by this file, will also insulate the rest of the code,
 * should we find a better gzip library at a later date.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include "cache.h"
#include "stevedore.h"

#include "zlib.h"

struct vgz {
	unsigned		magic;
#define VGZ_MAGIC		0x162df0cb
	struct sess 		*sp;
	struct ws		*tmp;
	char			*tmp_snapshot;

	struct ws		*buf;
	size_t			bufsiz;

	z_stream		vz;
};

/*--------------------------------------------------------------------*/

static voidpf
vgz_alloc(voidpf opaque, uInt items, uInt size)
{
	struct vgz *vg;

	CAST_OBJ_NOTNULL(vg, opaque, VGZ_MAGIC);

	return(WS_Alloc(vg->tmp, items * size));
}

static void
vgz_free(voidpf opaque, voidpf address)
{
	struct vgz *vg;

	CAST_OBJ_NOTNULL(vg, opaque, VGZ_MAGIC);
	(void)address;
}

/*--------------------------------------------------------------------*/

struct vgz *
VGZ_NewUnzip(struct sess *sp, struct ws *tmp, struct ws *buf)
{
	struct vgz *vg;
	char *s;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	WS_Assert(tmp);
	WS_Assert(buf);

	s = WS_Snapshot(tmp);
	vg = (void*)WS_Alloc(tmp, sizeof *vg);
	AN(vg);
	memset(vg, 0, sizeof *vg);
	vg->magic = VGZ_MAGIC;
	vg->sp = sp;
	vg->tmp = tmp;
	vg->buf = buf;
	vg->tmp_snapshot = s;

	vg->vz.zalloc = vgz_alloc;
	vg->vz.zfree = vgz_free;
	vg->vz.opaque = vg;

	vg->bufsiz = WS_Reserve(buf, 0);

	assert(Z_OK == inflateInit2(&vg->vz, 31));
	return (vg);
}

/*--------------------------------------------------------------------*/

int
VGZ_Feed(struct vgz *vg, const void *ptr, size_t len)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	AZ(vg->vz.avail_in);
	vg->vz.next_in = TRUST_ME(ptr);
	vg->vz.avail_in = len;

	return (0);
}

/*--------------------------------------------------------------------*/

int
VGZ_Produce(struct vgz *vg, const void **pptr, size_t *plen)
{
	int i;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	*pptr = NULL;
	*plen = 0;
	vg->vz.next_out = (void*)vg->buf->f;
	vg->vz.avail_out = vg->bufsiz;

	i = inflate(&vg->vz, 0);
	if (i == Z_OK || i == Z_STREAM_END) {
		*pptr = vg->buf->f;
		*plen = vg->bufsiz - vg->vz.avail_out;
	}
	if (i == Z_OK)
		return (0);
	if (i == Z_STREAM_END)
		return (1);
	if (i == Z_BUF_ERROR)
		return (2);
	return (-1);
}

/*--------------------------------------------------------------------*/

int
VGZ_Destroy(struct vgz **vg)
{

	CHECK_OBJ_NOTNULL(*vg, VGZ_MAGIC);
	WS_Release((*vg)->buf, 0);
	WS_Reset((*vg)->tmp, (*vg)->tmp_snapshot);
	*vg = NULL;
	return (0);
}

/*--------------------------------------------------------------------
 * VFP_GUNZIP
 *
 * A VFP for gunzip'ing an object as we receive it from the backend
 */

static void __match_proto__()
vfp_gunzip_begin(struct sess *sp, size_t estimate)
{
	(void)estimate;
	sp->wrk->vfp_private = VGZ_NewUnzip(sp, sp->ws, sp->wrk->ws);
}

static int __match_proto__()
vfp_gunzip_bytes(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	struct vgz *vg;
	struct storage *st;
	ssize_t l, w;
	int i = -100;

	CAST_OBJ_NOTNULL(vg, sp->wrk->vfp_private, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	while (bytes > 0 || vg->vz.avail_in > 0) {
		if (sp->wrk->storage == NULL)
			sp->wrk->storage = STV_alloc(sp,
			    params->fetch_chunksize * 1024LL);
		if (sp->wrk->storage == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		st = sp->wrk->storage;

		vg->vz.next_out = st->ptr + st->len;
		vg->vz.avail_out = st->space - st->len;

		if (vg->vz.avail_in == 0 && bytes > 0) {
			l = vg->bufsiz;
			if (l > bytes)
				l = bytes;
			w = HTC_Read(htc, vg->buf->f, l);
			if (w <= 0)
				return (w);
			vg->vz.next_in = (void*)vg->buf->f;
			vg->vz.avail_in = w;
			bytes -= w;
		}

		i = inflate(&vg->vz, 0);
		assert(i == Z_OK || i == Z_STREAM_END);
		st->len = st->space - vg->vz.avail_out;
		if (st->len == st->space) {
			VTAILQ_INSERT_TAIL(&sp->obj->store,
			    sp->wrk->storage, list);
			sp->obj->len += st->len;
			sp->wrk->storage = NULL;
		}
	}
	if (i == Z_STREAM_END)
		return (1);
	return (-1);
}

static int __match_proto__()
vfp_gunzip_end(struct sess *sp)
{
	struct vgz *vg;
	struct storage *st;

	CAST_OBJ_NOTNULL(vg, sp->wrk->vfp_private, VGZ_MAGIC);
	VGZ_Destroy(&vg);

	st = sp->wrk->storage;
	sp->wrk->storage = NULL;
	if (st == NULL)
		return (0);

	if (st->len == 0) {
		STV_free(st);
		return (0);
	}
	if (st->len < st->space)
		STV_trim(st, st->len);
	sp->obj->len += st->len;
	VTAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	return (0);
}

struct vfp vfp_gunzip = {
        .begin  =       vfp_gunzip_begin,
        .bytes  =       vfp_gunzip_bytes,
        .end    =       vfp_gunzip_end,
};


/*--------------------------------------------------------------------
 * VFP_GZIP
 *
 * A VFP for gzip'ing an object as we receive it from the backend
 */

static void __match_proto__()
vfp_gzip_begin(struct sess *sp, size_t estimate)
{
	struct vgz *vg;
	(void)estimate;

	vg = VGZ_NewUnzip(sp, sp->ws, sp->wrk->ws);
	/* XXX: hack */
	memset(&vg->vz, 0, sizeof vg->vz);
	assert(Z_OK == deflateInit2(&vg->vz,
	    0,
	    Z_DEFLATED,
	    31,
	    9,
	    Z_DEFAULT_STRATEGY));

	sp->wrk->vfp_private = vg;
}

static int __match_proto__()
vfp_gzip_bytes(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	struct vgz *vg;
	struct storage *st;
	ssize_t l, w;
	int i = -100;

	CAST_OBJ_NOTNULL(vg, sp->wrk->vfp_private, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	while (bytes > 0 || vg->vz.avail_in > 0) {
		if (sp->wrk->storage == NULL)
			sp->wrk->storage = STV_alloc(sp,
			    params->fetch_chunksize * 1024LL);
		if (sp->wrk->storage == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		st = sp->wrk->storage;

		vg->vz.next_out = st->ptr + st->len;
		vg->vz.avail_out = st->space - st->len;

		if (vg->vz.avail_in == 0 && bytes > 0) {
			l = vg->bufsiz;
			if (l > bytes)
				l = bytes;
			w = HTC_Read(htc, vg->buf->f, l);
			if (w <= 0)
				return (w);
			vg->vz.next_in = (void*)vg->buf->f;
			vg->vz.avail_in = w;
			bytes -= w;
		}

		i = deflate(&vg->vz, bytes == 0 ? Z_FINISH : 0);
		assert(i == Z_OK || i == Z_STREAM_END);
		st->len = st->space - vg->vz.avail_out;
		if (st->len == st->space) {
			VTAILQ_INSERT_TAIL(&sp->obj->store,
			    sp->wrk->storage, list);
			sp->obj->len += st->len;
			sp->wrk->storage = NULL;
		}
	}
	if (i == Z_STREAM_END)
		return (1);
	return (-1);
}

static int __match_proto__()
vfp_gzip_end(struct sess *sp)
{
	struct vgz *vg;
	struct storage *st;

	CAST_OBJ_NOTNULL(vg, sp->wrk->vfp_private, VGZ_MAGIC);
	VGZ_Destroy(&vg);

	st = sp->wrk->storage;
	sp->wrk->storage = NULL;
	if (st == NULL)
		return (0);

	if (st->len == 0) {
		STV_free(st);
		return (0);
	}
	if (st->len < st->space)
		STV_trim(st, st->len);
	sp->obj->len += st->len;
	VTAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	return (0);
}

struct vfp vfp_gzip = {
        .begin  =       vfp_gzip_begin,
        .bytes  =       vfp_gzip_bytes,
        .end    =       vfp_gzip_end,
};
