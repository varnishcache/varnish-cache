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
 * VED - Varnish Esi Delivery
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "cache_esi.h"
#include "vend.h"
#include "vgz.h"

/*--------------------------------------------------------------------*/

static void
ved_include(struct req *req, const char *src, const char *host)
{
	struct object *obj;
	struct worker *wrk;
	char *sp_ws_wm;
	char *wrk_ws_wm;
	unsigned sxid, res_mode;

	wrk = req->sp->wrk;

	if (req->esi_level >= cache_param->max_esi_depth)
		return;
	req->esi_level++;

	(void)WRW_FlushRelease(wrk);

	obj = req->obj;
	req->obj = NULL;
	res_mode = req->res_mode;

	/* Reset request to status before we started messing with it */
	HTTP_Copy(req->http, req->http0);

	/* Take a workspace snapshot */
	sp_ws_wm = WS_Snapshot(req->ws);
	wrk_ws_wm = WS_Snapshot(wrk->aws); /* XXX ? */

	http_SetH(req->http, HTTP_HDR_URL, src);
	if (host != NULL && *host != '\0')  {
		http_Unset(req->http, H_Host);
		http_Unset(req->http, H_If_Modified_Since);
		http_SetHeader(req->http, host);
	}
	/*
	 * XXX: We should decide if we should cache the director
	 * XXX: or not (for session/backend coupling).  Until then
	 * XXX: make sure we don't trip up the check in vcl_recv.
	 */
	req->director = NULL;
	req->sp->step = STP_RECV;
	http_ForceGet(req->http);

	/* Don't do conditionals */
	req->http->conds = 0;
	http_Unset(req->http, H_If_Modified_Since);

	/* Client content already taken care of */
	http_Unset(req->http, H_Content_Length);

	sxid = req->xid;
	while (1) {
		req->sp->wrk = wrk;
		CNT_Session(req->sp);
		if (req->sp->step == STP_DONE)
			break;
		AZ(req->sp->wrk);
		DSL(0x20, SLT_Debug, req->sp->vsl_id, "loop waiting for ESI");
		(void)usleep(10000);
	}
	req->xid = sxid;
	AN(req->sp->wrk);
	assert(req->sp->step == STP_DONE);
	req->esi_level--;
	req->obj = obj;
	req->res_mode = res_mode;

	/* Reset the workspace */
	WS_Reset(req->ws, sp_ws_wm);
	WS_Reset(wrk->aws, wrk_ws_wm);	/* XXX ? */

	WRW_Reserve(req->sp->wrk, &req->sp->fd, req->vsl, req->t_resp);
	if (req->res_mode & RES_CHUNKED)
		WRW_Chunked(req->sp->wrk);
}

/*--------------------------------------------------------------------*/


//#define Debug(fmt, ...) printf(fmt, __VA_ARGS__)
#define Debug(fmt, ...) /**/

static ssize_t
ved_decode_len(uint8_t **pp)
{
	uint8_t *p;
	ssize_t l;

	p = *pp;
	switch (*p & 15) {
	case 1:
		l = p[1];
		p += 2;
		break;
	case 2:
		l = vbe16dec(p + 1);
		p += 3;
		break;
	case 8:
		l = vbe64dec(p + 1);
		p += 9;
		break;
	default:
		printf("Illegal Length %d %d\n", *p, (*p & 15));
		INCOMPL();
	}
	*pp = p;
	assert(l > 0);
	return (l);
}

/*---------------------------------------------------------------------
 * If a gzip'ed ESI object includes a ungzip'ed object, we need to make
 * it looked like a gzip'ed data stream.  The official way to do so would
 * be to fire up libvgz and gzip it, but we don't, we fake it.
 *
 * First, we cannot know if it is ungzip'ed on purpose, the admin may
 * know something we don't.
 *
 * What do you mean "BS ?"
 *
 * All right then...
 *
 * The matter of the fact is that we simply will not fire up a gzip in
 * the output path because it costs too much memory and CPU, so we simply
 * wrap the data in very convenient "gzip copy-blocks" and send it down
 * the stream with a bit more overhead.
 */

static void
ved_pretend_gzip(struct req *req, const uint8_t *p, ssize_t l)
{
	uint8_t buf1[5], buf2[5];
	uint16_t lx;

	lx = 65535;
	buf1[0] = 0;
	vle16enc(buf1 + 1, lx);
	vle16enc(buf1 + 3, ~lx);

	while (l > 0) {
		if (l >= 65535) {
			lx = 65535;
			(void)WRW_Write(req->sp->wrk, buf1, sizeof buf1);
		} else {
			lx = (uint16_t)l;
			buf2[0] = 0;
			vle16enc(buf2 + 1, lx);
			vle16enc(buf2 + 3, ~lx);
			(void)WRW_Write(req->sp->wrk, buf2, sizeof buf2);
		}
		(void)WRW_Write(req->sp->wrk, p, lx);
		req->crc = crc32(req->crc, p, lx);
		req->l_crc += lx;
		l -= lx;
		p += lx;
	}
	/* buf2 is local, have to flush */
	(void)WRW_Flush(req->sp->wrk);
}

/*---------------------------------------------------------------------
 */

static const uint8_t gzip_hdr[] = {
	0x1f, 0x8b, 0x08,
	0x00, 0x00, 0x00, 0x00,
	0x00,
	0x02, 0x03
};

void
ESI_Deliver(struct req *req)
{
	struct storage *st;
	uint8_t *p, *e, *q, *r;
	unsigned off;
	ssize_t l, l2, l_icrc = 0;
	uint32_t icrc = 0;
	uint8_t tailbuf[8 + 5];
	int isgzip;
	struct vgz *vgz = NULL;
	size_t dl;
	const void *dp;
	int i;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	st = req->obj->esidata;
	AN(st);

	p = st->ptr;
	e = st->ptr + st->len;

	if (*p == VEC_GZ) {
		isgzip = 1;
		p++;
	} else {
		isgzip = 0;
	}

	if (req->esi_level == 0) {
		/*
		 * Only the top level document gets to decide this.
		 */
		req->gzip_resp = 0;
		if (isgzip && !(req->res_mode & RES_GUNZIP)) {
			assert(sizeof gzip_hdr == 10);
			/* Send out the gzip header */
			(void)WRW_Write(req->sp->wrk, gzip_hdr, 10);
			req->l_crc = 0;
			req->gzip_resp = 1;
			req->crc = crc32(0L, Z_NULL, 0);
		}
	}

	if (isgzip && !req->gzip_resp) {
		vgz = VGZ_NewUngzip(req->vsl, "U D E");
		AZ(VGZ_WrwInit(vgz));

		/* Feed a gzip header to gunzip to make it happy */
		VGZ_Ibuf(vgz, gzip_hdr, sizeof gzip_hdr);
		i = VGZ_Gunzip(vgz, &dp, &dl);
		assert(i == VGZ_OK);
		assert(VGZ_IbufEmpty(vgz));
		assert(dl == 0);
	}

	st = VTAILQ_FIRST(&req->obj->store);
	off = 0;

	while (p < e) {
		switch (*p) {
		case VEC_V1:
		case VEC_V2:
		case VEC_V8:
			l = ved_decode_len(&p);
			if (isgzip) {
				assert(*p == VEC_C1 || *p == VEC_C2 ||
				    *p == VEC_C8);
				l_icrc = ved_decode_len(&p);
				icrc = vbe32dec(p);
				p += 4;
				if (req->gzip_resp) {
					req->crc = crc32_combine(
					    req->crc, icrc, l_icrc);
					req->l_crc += l_icrc;
				}
			}
			/*
			 * There is no guarantee that the 'l' bytes are all
			 * in the same storage segment, so loop over storage
			 * until we have processed them all.
			 */
			while (l > 0) {
				l2 = l;
				if (l2 > st->len - off)
					l2 = st->len - off;
				l -= l2;

				if (req->gzip_resp && isgzip) {
					/*
					 * We have a gzip'ed VEC and delivers
					 * a gzip'ed ESI response.
					 */
					(void)WRW_Write(req->sp->wrk,
					    st->ptr + off, l2);
				} else if (req->gzip_resp) {
					/*
					 * A gzip'ed ESI response, but the VEC
					 * was not gzip'ed.
					 */
					ved_pretend_gzip(req,
					    st->ptr + off, l2);
				} else if (isgzip) {
					/*
					 * A gzip'ed VEC, but ungzip'ed ESI
					 * response
					 */
					AN(vgz);
					i = VGZ_WrwGunzip(req->sp->wrk, vgz,
						st->ptr + off, l2);
					if (WRW_Error(req->sp->wrk)) {
						SES_Close(req->sp,
						    "remote closed");
						p = e;
						break;
					}
					assert(i == VGZ_OK || i == VGZ_END);
				} else {
					/*
					 * Ungzip'ed VEC, ungzip'ed ESI response
					 */
					(void)WRW_Write(req->sp->wrk,
					    st->ptr + off, l2);
				}
				off += l2;
				if (off == st->len) {
					st = VTAILQ_NEXT(st, list);
					off = 0;
				}
			}
			break;
		case VEC_S1:
		case VEC_S2:
		case VEC_S8:
			l = ved_decode_len(&p);
			Debug("SKIP1(%d)\n", (int)l);
			/*
			 * There is no guarantee that the 'l' bytes are all
			 * in the same storage segment, so loop over storage
			 * until we have processed them all.
			 */
			while (l > 0) {
				l2 = l;
				if (l2 > st->len - off)
					l2 = st->len - off;
				l -= l2;
				off += l2;
				if (off == st->len) {
					st = VTAILQ_NEXT(st, list);
					off = 0;
				}
			}
			break;
		case VEC_INCL:
			p++;
			q = (void*)strchr((const char*)p, '\0');
			AN(q);
			q++;
			r = (void*)strchr((const char*)q, '\0');
			AN(r);
			if (vgz != NULL)
				VGZ_WrwFlush(req->sp->wrk, vgz);
			if (WRW_Flush(req->sp->wrk)) {
				SES_Close(req->sp, "remote closed");
				p = e;
				break;
			}
			Debug("INCL [%s][%s] BEGIN\n", q, p);
			ved_include(req, (const char*)q, (const char*)p);
			Debug("INCL [%s][%s] END\n", q, p);
			p = r + 1;
			break;
		default:
			printf("XXXX 0x%02x [%s]\n", *p, p);
			INCOMPL();
		}
	}
	if (vgz != NULL) {
		VGZ_WrwFlush(req->sp->wrk, vgz);
		(void)VGZ_Destroy(&vgz);
	}
	if (req->gzip_resp && req->esi_level == 0) {
		/* Emit a gzip literal block with finish bit set */
		tailbuf[0] = 0x01;
		tailbuf[1] = 0x00;
		tailbuf[2] = 0x00;
		tailbuf[3] = 0xff;
		tailbuf[4] = 0xff;

		/* Emit CRC32 */
		vle32enc(tailbuf + 5, req->crc);

		/* MOD(2^32) length */
		vle32enc(tailbuf + 9, req->l_crc);

		(void)WRW_Write(req->sp->wrk, tailbuf, 13);
	}
	(void)WRW_Flush(req->sp->wrk);
}

/*---------------------------------------------------------------------
 * Include an object in a gzip'ed ESI object delivery
 */

static uint8_t
ved_deliver_byterange(const struct req *req, ssize_t low, ssize_t high)
{
	struct storage *st;
	ssize_t l, lx;
	u_char *p;

	lx = 0;
	VTAILQ_FOREACH(st, &req->obj->store, list) {
		p = st->ptr;
		l = st->len;
		if (lx + l < low) {
			lx += l;
			continue;
		}
		if (lx == high)
			return (p[0]);
		assert(lx < high);
		if (lx < low) {
			p += (low - lx);
			l -= (low - lx);
			lx = low;
		}
		if (lx + l >= high)
			l = high - lx;
		assert(lx >= low && lx + l <= high);
		if (l != 0)
			(void)WRW_Write(req->sp->wrk, p, l);
		if (p + l < st->ptr + st->len)
			return(p[l]);
		lx += l;
	}
	INCOMPL();
}

void
ESI_DeliverChild(struct req *req)
{
	struct storage *st;
	struct object *obj;
	ssize_t start, last, stop, lpad;
	u_char cc;
	uint32_t icrc;
	uint32_t ilen;
	uint8_t *dbits;
	int i, j;
	uint8_t tailbuf[8];

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (!req->obj->gziped) {
		VTAILQ_FOREACH(st, &req->obj->store, list)
			ved_pretend_gzip(req, st->ptr, st->len);
		return;
	}
	/*
	 * This is the interesting case: Deliver all the deflate
	 * blocks, stripping the "LAST" bit of the last one and
	 * padding it, as necessary, to a byte boundary.
	 */

	dbits = (void*)WS_Alloc(req->ws, 8);
	AN(dbits);
	obj = req->obj;
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	start = obj->gzip_start;
	last = obj->gzip_last;
	stop = obj->gzip_stop;
	assert(start > 0 && start < obj->len * 8);
	assert(last > 0 && last < obj->len * 8);
	assert(stop > 0 && stop < obj->len * 8);
	assert(last >= start);
	assert(last < stop);

	/* The start bit must be byte aligned. */
	AZ(start & 7);

	/*
	 * XXX: optimize for the case where the 'last'
	 * XXX: bit is in a empty copy block
	 */
	*dbits = ved_deliver_byterange(req, start/8, last/8);
	*dbits &= ~(1U << (last & 7));
	(void)WRW_Write(req->sp->wrk, dbits, 1);
	cc = ved_deliver_byterange(req, 1 + last/8, stop/8);
	switch((int)(stop & 7)) {
	case 0: /* xxxxxxxx */
		/* I think we have an off by one here, but that's OK */
		lpad = 0;
		break;
	case 1: /* x000.... 00000000 00000000 11111111 11111111 */
	case 3: /* xxx000.. 00000000 00000000 11111111 11111111 */
	case 5: /* xxxxx000 00000000 00000000 11111111 11111111 */
		dbits[1] = cc | 0x00;
		dbits[2] = 0x00; dbits[3] = 0x00;
	        dbits[4] = 0xff; dbits[5] = 0xff;
		lpad = 5;
		break;
	case 2: /* xx010000 00000100 00000001 00000000 */
		dbits[1] = cc | 0x08;
		dbits[2] = 0x20;
		dbits[3] = 0x80;
		dbits[4] = 0x00;
		lpad = 4;
		break;
	case 4: /* xxxx0100 00000001 00000000 */
		dbits[1] = cc | 0x20;
		dbits[2] = 0x80;
		dbits[3] = 0x00;
		lpad = 3;
		break;
	case 6: /* xxxxxx01 00000000 */
		dbits[1] = cc | 0x80;
		dbits[2] = 0x00;
		lpad = 2;
		break;
	case 7:	/* xxxxxxx0 00...... 00000000 00000000 11111111 11111111 */
		dbits[1] = cc | 0x00;
		dbits[2] = 0x00;
		dbits[3] = 0x00; dbits[4] = 0x00;
	        dbits[5] = 0xff; dbits[6] = 0xff;
		lpad = 6;
		break;
	default:
		INCOMPL();
	}
	if (lpad > 0)
		(void)WRW_Write(req->sp->wrk, dbits + 1, lpad);

	/* We need the entire tail, but it may not be in one storage segment */
	st = VTAILQ_LAST(&req->obj->store, storagehead);
	for (i = sizeof tailbuf; i > 0; i -= j) {
		j = st->len;
		if (j > i)
			j = i;
		memcpy(tailbuf + i - j, st->ptr + st->len - j, j);
		st = VTAILQ_PREV(st, storagehead, list);
		assert(i == j || st != NULL);
	}

	icrc = vle32dec(tailbuf);
	ilen = vle32dec(tailbuf + 4);
	req->crc = crc32_combine(req->crc, icrc, ilen);
	req->l_crc += ilen;
}
