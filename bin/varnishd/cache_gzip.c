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
 * The zlib library pollutes namespace a LOT when you include the "vgz.h"
 * (aka (zlib.h") file so we contain the damage by vectoring all access
 * to libz through this source file.
 *
 * The API defined by this file, will also insulate the rest of the code,
 * should we find a better gzip library at a later date.
 *
 * The absolutely worst case gzip processing path, once we have pipe-lining,
 * will be the following, so we need to be a bit careful with the scratch
 * space we use:
 *
 *	Backend		Tmp	Input	Output
 *         |		----------------------
 *	   v
 *	 gunzip		wrk	stack	?
 *         |
 *	   v
 *	  esi
 *         |
 *	   v
 *	  gzip		wrk	?	storage
 *         |
 *	   v
 *	  cache
 *         |
 *	   v
 *	 gunzip		wrk	storage	stack
 *         |
 *	   v
 *	 client
 *
 * XXXX: The two '?' are obviously the same memory, but I have yet to decide
 * where it goes.   As usual we try to avoid the session->ws if we can but
 * I may have to use that.
 *
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>

#include "svnid.h"
SVNID("$Id$")

#include "vsl.h"
#include "cache.h"
#include "stevedore.h"

#include "vgz.h"

struct vgz {
	unsigned		magic;
#define VGZ_MAGIC		0x162df0cb
	enum {VGZ_GZ,VGZ_UN}	dir;
	struct sess		*sess;
	const char		*id;
	struct ws		*tmp;
	char			*tmp_snapshot;

	struct storage		*obuf;

	z_stream		vz;
};

/*--------------------------------------------------------------------*/

static voidpf
vgz_alloc(voidpf opaque, uInt items, uInt size)
{
	struct vgz *vg;

	CAST_OBJ_NOTNULL(vg, opaque, VGZ_MAGIC);

	return (WS_Alloc(vg->tmp, items * size));
}

static void
vgz_free(voidpf opaque, voidpf address)
{
	struct vgz *vg;

	CAST_OBJ_NOTNULL(vg, opaque, VGZ_MAGIC);
	(void)address;
}

/*--------------------------------------------------------------------
 * Set up a gunzip instance
 */

static struct vgz *
vgz_alloc_vgz(struct sess *sp, const char *id)
{
	struct vgz *vg;
	struct ws *ws = sp->wrk->ws;

	WS_Assert(ws);
	// XXX: we restore workspace in esi:include
	// vg = (void*)WS_Alloc(ws, sizeof *vg);
	ALLOC_OBJ(vg, VGZ_MAGIC);
	AN(vg);
	memset(vg, 0, sizeof *vg);
	vg->magic = VGZ_MAGIC;
	vg->sess = sp;
	vg->id = id;

	switch (params->gzip_tmp_space) {
	case 0:
		/* malloc, the default */
		break;
	case 1:
		vg->tmp = sp->ws;
		vg->tmp_snapshot = WS_Snapshot(vg->tmp);
		vg->vz.zalloc = vgz_alloc;
		vg->vz.zfree = vgz_free;
		vg->vz.opaque = vg;
		break;
	case 2:
		vg->tmp = sp->wrk->ws;
		vg->tmp_snapshot = WS_Snapshot(vg->tmp);
		vg->vz.zalloc = vgz_alloc;
		vg->vz.zfree = vgz_free;
		vg->vz.opaque = vg;
		break;
	default:
		assert(0 == __LINE__);
	}
	return (vg);
}

struct vgz *
VGZ_NewUngzip(struct sess *sp, const char *id)
{
	struct vgz *vg;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vg = vgz_alloc_vgz(sp, id);
	vg->dir = VGZ_UN;
	VSC_main->n_gunzip++;

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
VGZ_NewGzip(struct sess *sp, const char *id)
{
	struct vgz *vg;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vg = vgz_alloc_vgz(sp, id);
	vg->dir = VGZ_GZ;
	VSC_main->n_gzip++;

	/*
	 * From zconf.h:
	 *
	 *	mem_needed = "a few kb"
	 *		+ (1 << (windowBits+2))
	 *		+  (1 << (memLevel+9))
	 *
	 * windowBits [8..15] (-> 1K..128K)
	 * memLevel [1..9] (-> 1K->256K)
	 *
	 * XXX: They probably needs to be params...
	 *
	 * XXX: It may be more efficent to malloc them, rather than have
	 * XXX: too many worker threads grow the stacks.
	 */
	i = deflateInit2(&vg->vz,
	    params->gzip_level,		/* Level */
	    Z_DEFLATED,			/* Method */
	    16 + 8,			/* Window bits (16=gzip + 15) */
	    1,				/* memLevel */
	    Z_DEFAULT_STRATEGY);
	if (i != Z_OK)
		printf("deflateInit2() = %d\n", i);
	assert(Z_OK == i);
	return (vg);
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
VGZ_Obuf(struct vgz *vg, const void *ptr, ssize_t len)
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

/*--------------------------------------------------------------------
 * Keep the outbuffer supplied with storage and file it under the
 * sp->obj as it fills.
 */

int
VGZ_ObufStorage(const struct sess *sp, struct vgz *vg)
{
	struct storage *st;

	st = FetchStorage(sp, 0);
	if (st == NULL)
		return (-1);

	vg->obuf = st;
	VGZ_Obuf(vg, st->ptr + st->len, st->space - st->len);

	return (0);
}

/*--------------------------------------------------------------------*/

int
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
		if (vg->obuf != NULL)
			vg->obuf->len += l;
	}
	if (i == Z_OK)
		return (VGZ_OK);
	if (i == Z_STREAM_END)
		return (VGZ_END);
	if (i == Z_BUF_ERROR)
		return (VGZ_STUCK);
printf("INFLATE=%d (%s)\n", i, vg->vz.msg);
	return (VGZ_ERROR);
}

/*--------------------------------------------------------------------*/

int
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
		if (vg->obuf != NULL)
			vg->obuf->len += l;
	}
	if (i == Z_OK)
		return (0);
	if (i == Z_STREAM_END)
		return (1);
	if (i == Z_BUF_ERROR)
		return (2);
	return (-1);
}

/*--------------------------------------------------------------------
 * Gunzip ibuf into outb, if it runs full, emit it with WRW.
 * Leave flushing to caller, more data may be coming.
 */

int
VGZ_WrwGunzip(struct sess *sp, struct vgz *vg, void *ibuf, ssize_t ibufl,
    char *obuf, ssize_t obufl, ssize_t *obufp)
{
	int i;
	size_t dl;
	const void *dp;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	VGZ_Ibuf(vg, ibuf, ibufl);
	VGZ_Obuf(vg, obuf + *obufp, ibufl - *obufp);
	do {
		i = VGZ_Gunzip(vg, &dp, &dl);
		if (i < VGZ_OK) {
			/* XXX: VSL ? */
			return (-1);
		}
		*obufp += dl;
		if (obufl == *obufp || i == VGZ_STUCK) {
			WRW_Write(sp->wrk, obuf, *obufp);
			if (WRW_Flush(sp->wrk))
				return (-1);
			*obufp = 0;
			VGZ_Obuf(vg, obuf + *obufp, ibufl - *obufp);
		}
	} while (!VGZ_IbufEmpty(vg));
	return (i);
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

/*--------------------------------------------------------------------*/

void
VGZ_Destroy(struct vgz **vgp)
{
	struct vgz *vg;

	vg = *vgp;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	*vgp = NULL;

	WSP(vg->sess, SLT_Gzip, "%s %jd %jd %jd %jd %jd",
	    vg->id,
	    (intmax_t)vg->vz.total_in,
	    (intmax_t)vg->vz.total_out,
	    (intmax_t)vg->vz.start_bit,
	    (intmax_t)vg->vz.last_bit,
	    (intmax_t)vg->vz.stop_bit);
	if (vg->tmp != NULL)
		WS_Reset(vg->tmp, vg->tmp_snapshot);
	if (vg->dir == VGZ_GZ)
		AZ(deflateEnd(&vg->vz));
	else
		AZ(inflateEnd(&vg->vz));
	FREE_OBJ(vg);
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
	AZ(sp->wrk->vgz_rx);
	sp->wrk->vgz_rx = VGZ_NewUngzip(sp, "U F -");
}

static int __match_proto__()
vfp_gunzip_bytes(struct sess *sp, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t l, w;
	int i = -100;
	uint8_t	ibuf[params->gzip_stack_buffer];
	size_t dl;
	const void *dp;

	vg = sp->wrk->vgz_rx;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	while (bytes > 0 || vg->vz.avail_in > 0) {
		if (vg->vz.avail_in == 0 && bytes > 0) {
			l = sizeof ibuf;
			if (l > bytes)
				l = bytes;
			w = HTC_Read(htc, ibuf, l);
			if (w <= 0)
				return (w);
			VGZ_Ibuf(vg, ibuf, w);
			bytes -= w;
		}

		if (VGZ_ObufStorage(sp, vg))
			return (-1);
		i = VGZ_Gunzip(vg, &dp, &dl);
		assert(i == VGZ_OK || i == VGZ_END);
		sp->obj->len += dl;
	}
	if (i == Z_OK || i == Z_STREAM_END)
		return (1);
	return (-1);
}

static int __match_proto__()
vfp_gunzip_end(struct sess *sp)
{
	struct vgz *vg;

	vg = sp->wrk->vgz_rx;
	sp->wrk->vgz_rx = NULL;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	VGZ_Destroy(&vg);
	sp->obj->gziped = 0;
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
	(void)estimate;

	AZ(sp->wrk->vgz_rx);
	sp->wrk->vgz_rx = VGZ_NewGzip(sp, "G F -");
}

static int __match_proto__()
vfp_gzip_bytes(struct sess *sp, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t l, w;
	int i = -100;
	uint8_t ibuf[params->gzip_stack_buffer];
	size_t dl;
	const void *dp;

	vg = sp->wrk->vgz_rx;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	while (bytes > 0 || !VGZ_IbufEmpty(vg)) {
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
		if (VGZ_ObufStorage(sp, vg))
			return (-1);
		i = VGZ_Gzip(vg, &dp, &dl, VGZ_NORMAL);
		assert(i == Z_OK);
		sp->obj->len += dl;
	}
	return (1);
}

static int __match_proto__()
vfp_gzip_end(struct sess *sp)
{
	struct vgz *vg;
	size_t dl;
	const void *dp;
	int i;

	vg = sp->wrk->vgz_rx;
	sp->wrk->vgz_rx = NULL;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	do {
		VGZ_Ibuf(vg, "", 0);
		if (VGZ_ObufStorage(sp, vg))
			return (-1);
		i = VGZ_Gzip(vg, &dp, &dl, VGZ_FINISH);
		sp->obj->len += dl;
	} while (i != Z_STREAM_END);
	VGZ_UpdateObj(vg, sp->obj);
	VGZ_Destroy(&vg);
	sp->obj->gziped = 1;
	return (0);
}

struct vfp vfp_gzip = {
        .begin  =       vfp_gzip_begin,
        .bytes  =       vfp_gzip_bytes,
        .end    =       vfp_gzip_end,
};

/*--------------------------------------------------------------------
 * VFP_TESTGZIP
 *
 * A VFP for testing that received gzip data is valid, and for
 * collecting the magic bits while we're at it.
 */

static void __match_proto__()
vfp_testgzip_begin(struct sess *sp, size_t estimate)
{
	(void)estimate;
	sp->wrk->vgz_rx = VGZ_NewUngzip(sp, "u F -");
}

static int __match_proto__()
vfp_testgzip_bytes(struct sess *sp, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t l, w;
	int i = -100;
	uint8_t	ibuf[params->gzip_stack_buffer];
	size_t dl;
	const void *dp;
	struct storage *st;

	vg = sp->wrk->vgz_rx;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	while (bytes > 0) {
		st = FetchStorage(sp, 0);
		if (st == NULL)
			return (-1);
		l = st->space - st->len;
		if (l > bytes)
			l = bytes;
		w = HTC_Read(htc, st->ptr + st->len, l);
		if (w <= 0)
			return (w);
		bytes -= w;
		VGZ_Ibuf(vg, st->ptr + st->len, w);
		st->len += w;
		sp->obj->len += w;

		while (!VGZ_IbufEmpty(vg)) {
			VGZ_Obuf(vg, ibuf, sizeof ibuf);
			i = VGZ_Gunzip(vg, &dp, &dl);
			if (i != VGZ_OK && i != VGZ_END) {
				WSP(sp, SLT_FetchError,
				    "Invalid Gzip data: %s", vg->vz.msg);
				return (-1);
			}
		}
	}
	if (i == Z_STREAM_END)
		return (1);
	WSP(sp, SLT_FetchError, "Incomplete Gzip data (not STREAM_END)");
	return (-1);
}

static int __match_proto__()
vfp_testgzip_end(struct sess *sp)
{
	struct vgz *vg;

	vg = sp->wrk->vgz_rx;
	sp->wrk->vgz_rx = NULL;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	VGZ_UpdateObj(vg, sp->obj);
	VGZ_Destroy(&vg);
	sp->obj->gziped = 1;
	return (0);
}

struct vfp vfp_testgzip = {
        .begin  =       vfp_testgzip_begin,
        .bytes  =       vfp_testgzip_bytes,
        .end    =       vfp_testgzip_end,
};


