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

#include "cache.h"

#include "cache_backend.h"	// for w->vbc
#include "vgz.h"

struct vgz {
	unsigned		magic;
#define VGZ_MAGIC		0x162df0cb
	enum {VGZ_GZ,VGZ_UN}	dir;
	struct worker		*wrk;
	int			vsl_id;
	const char		*id;
	struct ws		*tmp;
	char			*tmp_snapshot;
	const char		*error;

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
vgz_alloc_vgz(struct worker *wrk, int vsl_id, const char *id)
{
	struct vgz *vg;
	struct ws *ws;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ws = wrk->ws;
	WS_Assert(ws);
	// XXX: we restore workspace in esi:include
	// vg = (void*)WS_Alloc(ws, sizeof *vg);
	ALLOC_OBJ(vg, VGZ_MAGIC);
	AN(vg);
	memset(vg, 0, sizeof *vg);
	vg->magic = VGZ_MAGIC;
	vg->wrk = wrk;
	vg->vsl_id = vsl_id;
	vg->id = id;

	switch (params->gzip_tmp_space) {
	case 0:
	case 1:
		/* malloc, the default */
		break;
	case 2:
		vg->tmp = wrk->ws;
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
VGZ_NewUngzip(struct worker *wrk, int vsl_id, const char *id)
{
	struct vgz *vg;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	vg = vgz_alloc_vgz(wrk, vsl_id, id);
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
VGZ_NewGzip(struct worker *wrk, int vsl_id, const char *id)
{
	struct vgz *vg;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	vg = vgz_alloc_vgz(wrk, vsl_id, id);
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
	 *
	 * XXX: They probably needs to be params...
	 *
	 * XXX: It may be more efficent to malloc them, rather than have
	 * XXX: too many worker threads grow the stacks.
	 */
	i = deflateInit2(&vg->vz,
	    params->gzip_level,		/* Level */
	    Z_DEFLATED,			/* Method */
	    16 + params->gzip_window,	/* Window bits (16=gzip + 15) */
	    params->gzip_memlevel,	/* memLevel */
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

/*--------------------------------------------------------------------
 * Keep the outbuffer supplied with storage and file it under the
 * sp->obj as it fills.
 */

int
VGZ_ObufStorage(struct worker *w, struct vgz *vg)
{
	struct storage *st;

	st = FetchStorage(w, 0);
	if (st == NULL) {
		vg->error = "Could not get ObufStorage";
		return (-1);
	}

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
	VSL(SLT_Debug, 0, "Unknown INFLATE=%d (%s)\n", i, vg->vz.msg);
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
VGZ_WrwGunzip(struct worker *w, struct vgz *vg, const void *ibuf,
    ssize_t ibufl, char *obuf, ssize_t obufl, ssize_t *obufp)
{
	int i;
	size_t dl;
	const void *dp;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	assert(obufl > 16);
	VGZ_Ibuf(vg, ibuf, ibufl);
	if (ibufl == 0)
		return (VGZ_OK);
	VGZ_Obuf(vg, obuf + *obufp, obufl - *obufp);
	do {
		if (obufl == *obufp)
			i = VGZ_STUCK;
		else {
			i = VGZ_Gunzip(vg, &dp, &dl);
			*obufp += dl;
		}
		if (i < VGZ_OK) {
			/* XXX: VSL ? */
			return (-1);
		}
		if (obufl == *obufp || i == VGZ_STUCK) {
			w->acct_tmp.bodybytes += *obufp;
			(void)WRW_Write(w, obuf, *obufp);
			(void)WRW_Flush(w);
			*obufp = 0;
			VGZ_Obuf(vg, obuf + *obufp, obufl - *obufp);
		}
	} while (!VGZ_IbufEmpty(vg));
	if (i == VGZ_STUCK)
		i = VGZ_OK;
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
	const char *err;

	vg = *vgp;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	*vgp = NULL;

	WSL(vg->wrk, SLT_Gzip, vg->vsl_id, "%s %jd %jd %jd %jd %jd",
	    vg->id,
	    (intmax_t)vg->vz.total_in,
	    (intmax_t)vg->vz.total_out,
	    (intmax_t)vg->vz.start_bit,
	    (intmax_t)vg->vz.last_bit,
	    (intmax_t)vg->vz.stop_bit);
	err = vg->error;
	if (vg->tmp != NULL)
		WS_Reset(vg->tmp, vg->tmp_snapshot);
	if (vg->dir == VGZ_GZ)
		assert(deflateEnd(&vg->vz) == 0 || err != NULL);
	else
		assert(inflateEnd(&vg->vz) == 0 || err != NULL);
	FREE_OBJ(vg);
}

/*--------------------------------------------------------------------
 * VFP_GUNZIP
 *
 * A VFP for gunzip'ing an object as we receive it from the backend
 */

static void __match_proto__()
vfp_gunzip_begin(struct worker *w, size_t estimate)
{
	(void)estimate;
	AZ(w->vgz_rx);
	w->vgz_rx = VGZ_NewUngzip(w, w->vbc->vsl_id, "U F -");
}

static int __match_proto__()
vfp_gunzip_bytes(struct worker *w, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t l, wl;
	int i = -100;
	uint8_t	ibuf[params->gzip_stack_buffer];
	size_t dl;
	const void *dp;

	vg = w->vgz_rx;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	while (bytes > 0 || vg->vz.avail_in > 0) {
		if (vg->vz.avail_in == 0 && bytes > 0) {
			l = sizeof ibuf;
			if (l > bytes)
				l = bytes;
			wl = HTC_Read(htc, ibuf, l);
			if (wl <= 0)
				return (wl);
			VGZ_Ibuf(vg, ibuf, wl);
			bytes -= wl;
		}

		if (VGZ_ObufStorage(w, vg)) {
			htc->error = "Could not get storage";
			return (-1);
		}
		i = VGZ_Gunzip(vg, &dp, &dl);
		assert(i == VGZ_OK || i == VGZ_END);
		w->fetch_obj->len += dl;
		if (w->do_stream)
			RES_StreamPoll(w);
	}
	if (i == Z_OK || i == Z_STREAM_END)
		return (1);
	htc->error = "See other message";
	WSLB(w, SLT_FetchError, "Gunzip trouble (%d)", i);
	return (-1);
}

static int __match_proto__()
vfp_gunzip_end(struct worker *w)
{
	struct vgz *vg;

	vg = w->vgz_rx;
	w->vgz_rx = NULL;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	VGZ_Destroy(&vg);
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
vfp_gzip_begin(struct worker *w, size_t estimate)
{
	(void)estimate;

	AZ(w->vgz_rx);
	w->vgz_rx = VGZ_NewGzip(w, w->vbc->vsl_id, "G F -");
}

static int __match_proto__()
vfp_gzip_bytes(struct worker *w, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t l, wl;
	int i = -100;
	uint8_t ibuf[params->gzip_stack_buffer];
	size_t dl;
	const void *dp;

	vg = w->vgz_rx;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	while (bytes > 0 || !VGZ_IbufEmpty(vg)) {
		if (VGZ_IbufEmpty(vg) && bytes > 0) {
			l = sizeof ibuf;
			if (l > bytes)
				l = bytes;
			wl = HTC_Read(htc, ibuf, l);
			if (wl <= 0)
				return (wl);
			VGZ_Ibuf(vg, ibuf, wl);
			bytes -= wl;
		}
		if (VGZ_ObufStorage(w, vg)) {
			htc->error = "Could not get storage";
			return (-1);
		}
		i = VGZ_Gzip(vg, &dp, &dl, VGZ_NORMAL);
		assert(i == Z_OK);
		w->fetch_obj->len += dl;
		if (w->do_stream)
			RES_StreamPoll(w);
	}
	return (1);
}

static int __match_proto__()
vfp_gzip_end(struct worker *w)
{
	struct vgz *vg;
	size_t dl;
	const void *dp;
	int i;

	vg = w->vgz_rx;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	w->vgz_rx = NULL;
	if (vg->error == NULL) {
		do {
			VGZ_Ibuf(vg, "", 0);
			if (VGZ_ObufStorage(w, vg))
				return (-1);
			i = VGZ_Gzip(vg, &dp, &dl, VGZ_FINISH);
			w->fetch_obj->len += dl;
		} while (i != Z_STREAM_END);
		if (w->do_stream)
			RES_StreamPoll(w);
		VGZ_UpdateObj(vg, w->fetch_obj);
	}
	VGZ_Destroy(&vg);
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
vfp_testgzip_begin(struct worker *w, size_t estimate)
{
	(void)estimate;
	w->vgz_rx = VGZ_NewUngzip(w, w->vbc->vsl_id, "u F -");
	CHECK_OBJ_NOTNULL(w->vgz_rx, VGZ_MAGIC);
}

static int __match_proto__()
vfp_testgzip_bytes(struct worker *w, struct http_conn *htc, ssize_t bytes)
{
	struct vgz *vg;
	ssize_t l, wl;
	int i = -100;
	uint8_t	obuf[params->gzip_stack_buffer];
	size_t dl;
	const void *dp;
	struct storage *st;

	vg = w->vgz_rx;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	while (bytes > 0) {
		st = FetchStorage(w, 0);
		if (st == NULL) {
			htc->error = "Could not get storage";
			vg->error = htc->error;
			return (-1);
		}
		l = st->space - st->len;
		if (l > bytes)
			l = bytes;
		wl = HTC_Read(htc, st->ptr + st->len, l);
		if (wl <= 0)
			return (wl);
		bytes -= wl;
		VGZ_Ibuf(vg, st->ptr + st->len, wl);
		st->len += wl;
		w->fetch_obj->len += wl;
		if (w->do_stream)
			RES_StreamPoll(w);

		while (!VGZ_IbufEmpty(vg)) {
			VGZ_Obuf(vg, obuf, sizeof obuf);
			i = VGZ_Gunzip(vg, &dp, &dl);
			if (i == VGZ_END && !VGZ_IbufEmpty(vg)) {
				htc->error = "Junk after gzip data";
				return (-1);
			}
			if (i != VGZ_OK && i != VGZ_END) {
				htc->error = "See other message";
				WSLB(w, SLT_FetchError,
				    "Invalid Gzip data: %s", vg->vz.msg);
				return (-1);
			}
		}
	}
	if (i == VGZ_OK || i == VGZ_END)
		return (1);
	htc->error = "See other message";
	WSLB(w, SLT_FetchError, "Gunzip trouble (%d)", i);
	return (-1);
}

static int __match_proto__()
vfp_testgzip_end(struct worker *w)
{
	struct vgz *vg;

	vg = w->vgz_rx;
	w->vgz_rx = NULL;
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	VGZ_UpdateObj(vg, w->fetch_obj);
	VGZ_Destroy(&vg);
	return (0);
}

struct vfp vfp_testgzip = {
        .begin  =       vfp_testgzip_begin,
        .bytes  =       vfp_testgzip_bytes,
        .end    =       vfp_testgzip_end,
};
