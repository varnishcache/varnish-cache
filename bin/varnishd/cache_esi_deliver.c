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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id")

#include <stdlib.h>
#include <stdio.h>

#include "cache.h"
#include "cache_esi.h"
#include "vend.h"
#include "vct.h"
#include "zlib.h"
#include "stevedore.h"

/*--------------------------------------------------------------------*/

static void
ESI_Include(struct sess *sp, const char *src, const char *host)
{
	struct object *obj;
	struct worker *w;
	char *ws_wm;
	unsigned sxid, res_mode;

	w = sp->wrk;

	if (WRW_Flush(w)) {
		vca_close_session(sp, "remote closed");
		return;
	}

	AZ(WRW_FlushRelease(w));

	sp->esi_level++;
	obj = sp->obj;
	sp->obj = NULL;
	res_mode = sp->wrk->res_mode;

	/* Reset request to status before we started messing with it */
	HTTP_Copy(sp->http, sp->http0);

	/* Take a workspace snapshot */
	ws_wm = WS_Snapshot(sp->ws);

	http_SetH(sp->http, HTTP_HDR_URL, src);
	if (host != NULL && *host != '\0')  {
		http_Unset(sp->http, H_Host);
		http_Unset(sp->http, H_If_Modified_Since);
		http_SetHeader(w, sp->fd, sp->http, host);
	}
	/*
	 * XXX: We should decide if we should cache the director
	 * XXX: or not (for session/backend coupling).  Until then
	 * XXX: make sure we don't trip up the check in vcl_recv.
	 */
	sp->director = NULL;
	sp->step = STP_RECV;
	http_ForceGet(sp->http);

	/* Don't do conditionals */
	sp->http->conds = 0;
	http_Unset(sp->http, H_If_Modified_Since);

	/* Client content already taken care of */
	http_Unset(sp->http, H_Content_Length);

	sxid = sp->xid;
	while (1) {
		sp->wrk = w;
		CNT_Session(sp);
		if (sp->step == STP_DONE)
			break;
		AZ(sp->wrk);
		WSL_Flush(w, 0);
		DSL(0x20, SLT_Debug, sp->id, "loop waiting for ESI");
		(void)usleep(10000);
	}
	sp->xid = sxid;
	AN(sp->wrk);
	assert(sp->step == STP_DONE);
	sp->esi_level--;
	sp->obj = obj;
	sp->wrk->res_mode = res_mode;

	/* Reset the workspace */
	WS_Reset(sp->ws, ws_wm);

	WRW_Reserve(sp->wrk, &sp->fd);
}

/*--------------------------------------------------------------------*/


//#define Debug(fmt, ...) printf(fmt, __VA_ARGS__)
#define Debug(fmt, ...) /**/

static void
esi_sendchunk(const struct sess *sp, const void *cb, ssize_t cl,
    const void *ptr, ssize_t l)
{

	Debug("VER(%d) %d\n", (int)l, (int)(cb-ce));
	assert(l > 0);
	if (sp->wrk->res_mode & RES_CHUNKED)
		(void)WRW_Write(sp->wrk, cb, cl);
	(void)WRW_Write(sp->wrk, ptr, l);
	if (sp->wrk->res_mode & RES_CHUNKED)
		(void)WRW_Write(sp->wrk, "\r\n", -1);
}

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

void
ESI_Deliver(struct sess *sp)
{
	struct storage *st;
	uint8_t *p, *e, *q, *r;
	unsigned off;
	ssize_t l;
	uint32_t crc, crc_ref;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	st = sp->obj->esidata;
	AN(st);
	p = st->ptr;
	e = st->ptr + st->len;

	st = VTAILQ_FIRST(&sp->obj->store);
	off = 0;

	while (p < e) {
		switch (*p) {
		case VEC_V1:
		case VEC_V2:
		case VEC_V8:
			l = ved_decode_len(&p);
			crc = vbe32dec(p);
			p += 4;
			q = (void*)strchr((const char*)p, '\0');
			assert (q > p);
			crc_ref = crc32(0L, Z_NULL, 0);
			crc_ref = crc32(crc_ref, st->ptr + off, l);
			if (crc_ref != crc) {
				printf("CRC Mismatch %08x %08x\n", crc_ref, crc);
			}
			xxxassert(crc_ref == crc);
			esi_sendchunk(sp, p, q - p, st->ptr + off, l);
			off += l;
			p = q + 1;
			break;
		case VEC_S1:
		case VEC_S2:
		case VEC_S8:
			l = ved_decode_len(&p);
			Debug("SKIP1(%d)\n", (int)l);
			off += l;
			break;
		case VEC_INCL:
			p++;
			q = (void*)strchr((const char*)p, '\0');
			AN(q);
			q++;
			r = (void*)strchr((const char*)q, '\0');
			AN(r);
			Debug("INCL [%s][%s] BEGIN\n", q, p);
			ESI_Include(sp, (const char*)q, (const char*)p);
			Debug("INCL [%s][%s] END\n", q, p);
			p = r + 1;
			break;
		default:
			Debug("XXXX 0x%02x [%s]\n", *p, p);
			INCOMPL();
		}
	}
	(void)WRW_Flush(sp->wrk);
}

