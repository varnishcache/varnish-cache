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
#include "cache_filter.h"

#include "vtim.h"
#include "cache_esi.h"
#include "vend.h"
#include "vgz.h"

/*--------------------------------------------------------------------*/

static int __match_proto__(vdp_bytes)
ved_vdp_bytes(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct req *preq;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (act == VDP_INIT)
		return (0);
	if (act == VDP_FINI) {
		*priv = NULL;
		return (0);
	}
	CAST_OBJ_NOTNULL(preq, *priv, REQ_MAGIC);
	req->acct.resp_bodybytes += len;
	return (VDP_bytes(preq, act, ptr, len));
}


/*--------------------------------------------------------------------*/

static void
ved_include(struct req *preq, const char *src, const char *host)
{
	struct worker *wrk;
	struct req *req;
	enum req_fsm_nxt s;

	wrk = preq->wrk;

	if (preq->esi_level >= cache_param->max_esi_depth)
		return;

	req = SES_GetReq(wrk, preq->sp);
	req->req_body_status = REQ_BODY_NONE;
	AZ(req->vsl->wid);
	req->vsl->wid = VXID_Get(wrk, VSL_CLIENTMARKER);
	VSLb(req->vsl, SLT_Begin, "req %u esi", VXID(preq->vsl->wid));
	VSLb(preq->vsl, SLT_Link, "req %u esi", VXID(req->vsl->wid));
	req->esi_level = preq->esi_level + 1;

	HTTP_Copy(req->http0, preq->http0);

	req->http0->ws = req->ws;
	req->http0->vsl = req->vsl;
	req->http0->logtag = SLT_ReqMethod;
	req->http0->conds = 0;

	http_SetH(req->http0, HTTP_HDR_URL, src);
	if (host != NULL && *host != '\0')  {
		http_Unset(req->http0, H_Host);
		http_SetHeader(req->http0, host);
	}

	http_ForceField(req->http0, HTTP_HDR_METHOD, "GET");
	http_ForceField(req->http0, HTTP_HDR_PROTO, "HTTP/1.1");

	/* Don't allow Conditions, we can't use a 304 */
	http_Unset(req->http0, H_If_Modified_Since);
	http_Unset(req->http0, H_If_None_Match);

	/* Client content already taken care of */
	http_Unset(req->http0, H_Content_Length);

	/* Reset request to status before we started messing with it */
	HTTP_Copy(req->http, req->http0);

	req->vcl = preq->vcl;
	preq->vcl = NULL;
	req->wrk = preq->wrk;

	/*
	 * XXX: We should decide if we should cache the director
	 * XXX: or not (for session/backend coupling).  Until then
	 * XXX: make sure we don't trip up the check in vcl_recv.
	 */
	req->req_step = R_STP_RECV;
	req->t_req = preq->t_req;
	assert(isnan(req->t_first));
	assert(isnan(req->t_prev));
	req->gzip_resp = preq->gzip_resp;
	req->crc = preq->crc;
	req->l_crc = preq->l_crc;

	VDP_push(req, ved_vdp_bytes, preq, 0);

	THR_SetRequest(req);

	VSLb_ts_req(req, "Start", W_TIM_real(wrk));

	req->ws_req = WS_Snapshot(req->ws);

	while (1) {
		req->wrk = wrk;
		s = CNT_Request(wrk, req);
		if (s == REQ_FSM_DONE)
			break;
		DSL(DBG_WAITINGLIST, req->vsl->wid,
		    "loop waiting for ESI (%d)", (int)s);
		assert(s == REQ_FSM_DISEMBARK);
		AZ(req->wrk);
		(void)usleep(10000);
	}

	CNT_AcctLogCharge(wrk->stats, req);
	VSL_End(req->vsl);

	preq->vcl = req->vcl;
	req->vcl = NULL;

	preq->crc = req->crc;
	preq->l_crc = req->l_crc;

	req->wrk = NULL;

	THR_SetRequest(preq);
	SES_ReleaseReq(req);
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

int __match_proto__(vdp_bytes)
VED_pretend_gzip(struct req *req, enum vdp_action act, void **priv,
    const void *pv, ssize_t l)
{
	uint8_t buf1[5], buf2[5];
	const uint8_t *p;
	uint16_t lx;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	(void)priv;
	if (act == VDP_INIT || act == VDP_FINI)
		return (0);
	p = pv;

	lx = 65535;
	buf1[0] = 0;
	vle16enc(buf1 + 1, lx);
	vle16enc(buf1 + 3, ~lx);

	while (l > 0) {
		if (l >= 65535) {
			lx = 65535;
			if (VDP_bytes(req, VDP_NULL, buf1, sizeof buf1))
				return (-1);
		} else {
			lx = (uint16_t)l;
			buf2[0] = 0;
			vle16enc(buf2 + 1, lx);
			vle16enc(buf2 + 3, ~lx);
			if (VDP_bytes(req, VDP_NULL, buf2, sizeof buf2))
				return (-1);
		}
		if (VDP_bytes(req, VDP_NULL, p, lx))
			return (-1);
		req->crc = crc32(req->crc, p, lx);
		req->l_crc += lx;
		l -= lx;
		p += lx;
	}
	/* buf2 is local, have to flush */
	return (VDP_bytes(req, VDP_FLUSH, NULL, 0));
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
	uint8_t *p, *e, *q, *r;
	ssize_t l, l2, l_icrc = 0;
	uint32_t icrc = 0;
	uint8_t tailbuf[8 + 5];
	int isgzip;
	void *oi;
	enum objiter_status ois;
	void *sp;
	uint8_t *pp;
	ssize_t sl;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	p = ObjGetattr(req->wrk, req->objcore, OA_ESIDATA, &l);
	AN(p);
	assert(l > 0);
	e = p + l;

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
		if (isgzip) {
			assert(sizeof gzip_hdr == 10);
			/* Send out the gzip header */
			(void)VDP_bytes(req, VDP_NULL, gzip_hdr, 10);
			req->l_crc = 0;
			req->gzip_resp = 1;
			req->crc = crc32(0L, Z_NULL, 0);
		}
	}

	oi = ObjIterBegin(req->wrk, req->objcore);
	ois = ObjIter(req->objcore, oi, &sp, &sl);
	assert(ois != OIS_ERROR);
	pp = sp;

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
				if (l2 > sl)
					l2 = sl;
				sl -= l2;
				l -= l2;

				(void)VDP_bytes(req, VDP_NULL, pp, l2);
				pp += l2;
				if (sl == 0) {
					ois = ObjIter(req->objcore, oi,
					    &sp, &sl);
					assert(ois != OIS_ERROR);
					pp = sp;
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
				if (l2 > sl)
					l2 = sl;
				sl -= l2;
				l -= l2;
				pp += l2;
				if (sl == 0) {
					ois = ObjIter(req->objcore, oi,
					    &sp, &sl);
					assert(ois != OIS_ERROR);
					pp = sp;
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
			if (VDP_bytes(req, VDP_FLUSH, NULL, 0)) {
				SES_Close(req->sp, SC_REM_CLOSE);
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

		(void)VDP_bytes(req, VDP_NULL, tailbuf, 13);
	}
	(void)VDP_bytes(req, VDP_FLUSH, NULL, 0);
	ObjIterEnd(req->objcore, &oi);
}

/*---------------------------------------------------------------------
 * Include an object in a gzip'ed ESI object delivery
 */

void
ESI_DeliverChild(struct req *req)
{
	ssize_t start, last, stop, lpad;
	ssize_t l;
	char *p;
	uint32_t icrc;
	uint32_t ilen;
	uint64_t olen;
	uint8_t *dbits;
	uint8_t *pp;
	uint8_t tailbuf[8];
	enum objiter_status ois;
	void *oi;
	void *sp;
	ssize_t sl, ll, dl;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	AN(ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED));

	/*
	 * This is the interesting case: Deliver all the deflate
	 * blocks, stripping the "LAST" bit of the last one and
	 * padding it, as necessary, to a byte boundary.
	 */

	p = ObjGetattr(req->wrk, req->objcore, OA_GZIPBITS, &l);
	AN(p);
	assert(l == 24);
	start = vbe64dec(p);
	last = vbe64dec(p + 8);
	stop = vbe64dec(p + 16);
	olen = ObjGetLen(req->wrk, req->objcore);
	assert(start > 0 && start < olen * 8);
	assert(last > 0 && last < olen * 8);
	assert(stop > 0 && stop < olen * 8);
	assert(last >= start);
	assert(last < stop);

	/* The start bit must be byte aligned. */
	AZ(start & 7);

	/*
	 * XXX: optimize for the case where the 'last'
	 * XXX: bit is in a empty copy block
	 */

	memset(tailbuf, 0xdd, sizeof tailbuf);
	dbits = WS_Alloc(req->ws, 8);
	AN(dbits);
	ll = 0;
	oi = ObjIterBegin(req->wrk, req->objcore);
	do {
		ois = ObjIter(req->objcore, oi, &sp, &sl);
		pp = sp;
		if (sl > 0) {
			/* Skip over the GZIP header */
			dl = start / 8 - ll;
			if (dl > 0) {
				/* Before start, skip */
				if (dl > sl)
					dl = sl;
				ll += dl;
				sl -= dl;
				pp += dl;
			}
		}
		if (sl > 0) {
			/* The main body of the object */
			dl = last / 8 - ll;
			if (dl > 0) {
				if (dl > sl)
					dl = sl;
				if (VDP_bytes(req, VDP_NULL, pp, dl))
					break;
				ll += dl;
				sl -= dl;
				pp += dl;
			}
		}
		if (sl > 0 && ll == last / 8) {
			/* Remove the "LAST" bit */
			dbits[0] = *pp;
			dbits[0] &= ~(1U << (last & 7));
			if (VDP_bytes(req, VDP_NULL, dbits, 1))
				break;
			ll++;
			sl--;
			pp++;
		}
		if (sl > 0) {
			/* Last block */
			dl = stop / 8 - ll;
			if (dl > 0) {
				if (dl > sl)
					dl = sl;
				if (VDP_bytes(req, VDP_NULL, pp, dl))
					break;
				ll += dl;
				sl -= dl;
				pp += dl;
			}
		}
		if (sl > 0 && (stop & 7) && ll == stop / 8) {
			/* Add alignment to byte boundary */
			dbits[1] = *pp;
			ll++;
			sl--;
			pp++;
			switch((int)(stop & 7)) {
			case 1: /*
				 * x000....
				 * 00000000 00000000 11111111 11111111
				 */
			case 3: /*
				 * xxx000..
				 * 00000000 00000000 11111111 11111111
				 */
			case 5: /*
				 * xxxxx000
				 * 00000000 00000000 11111111 11111111
				 */
				dbits[2] = 0x00; dbits[3] = 0x00;
				dbits[4] = 0xff; dbits[5] = 0xff;
				lpad = 5;
				break;
			case 2: /* xx010000 00000100 00000001 00000000 */
				dbits[1] |= 0x08;
				dbits[2] = 0x20;
				dbits[3] = 0x80;
				dbits[4] = 0x00;
				lpad = 4;
				break;
			case 4: /* xxxx0100 00000001 00000000 */
				dbits[1] |= 0x20;
				dbits[2] = 0x80;
				dbits[3] = 0x00;
				lpad = 3;
				break;
			case 6: /* xxxxxx01 00000000 */
				dbits[1] |= 0x80;
				dbits[2] = 0x00;
				lpad = 2;
				break;
			case 7:	/*
				 * xxxxxxx0
				 * 00......
				 * 00000000 00000000 11111111 11111111
				 */
				dbits[2] = 0x00;
				dbits[3] = 0x00; dbits[4] = 0x00;
				dbits[5] = 0xff; dbits[6] = 0xff;
				lpad = 6;
				break;
			case 0: /* xxxxxxxx */
			default:
				WRONG("compiler must be broken");
			}
			if (VDP_bytes(req, VDP_NULL, dbits + 1, lpad))
				break;
		}
		if (sl > 0) {
			/* Recover GZIP tail */
			dl = olen - ll;
			assert(dl >= 0);
			if (dl > sl)
				dl = sl;
			if (dl > 0) {
				assert(dl <= 8);
				l = ll - (olen - 8);
				assert(l >= 0);
				assert(l <= 8);
				assert(l + dl <= 8);
				memcpy(tailbuf + l, pp, dl);
				ll += dl;
				sl -= dl;
				pp += dl;
			}
		}
	} while (ois == OIS_DATA || ois == OIS_STREAM);
	ObjIterEnd(req->objcore, &oi);

	icrc = vle32dec(tailbuf);
	ilen = vle32dec(tailbuf + 4);
	req->crc = crc32_combine(req->crc, icrc, ilen);
	req->l_crc += ilen;
}
