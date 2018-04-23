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

#include "cache_varnishd.h"

#include <stdlib.h>

#include "cache_transport.h"
#include "cache_filter.h"
#include "cache_vgz.h"

#include "vtim.h"
#include "cache_esi.h"
#include "vend.h"
#include "vgz.h"

static vtr_deliver_f ved_deliver;
static vtr_reembark_f ved_reembark;

static const uint8_t gzip_hdr[] = {
	0x1f, 0x8b, 0x08,
	0x00, 0x00, 0x00, 0x00,
	0x00,
	0x02, 0x03
};

struct ecx {
	unsigned	magic;
#define ECX_MAGIC	0x0b0f9163
	const uint8_t	*p;
	const uint8_t	*e;
	int		state;
	ssize_t		l;
	int		isgzip;
	int		woken;

	struct req	*preq;
	ssize_t		l_crc;
	uint32_t	crc;
};

static const struct transport VED_transport = {
	.magic =	TRANSPORT_MAGIC,
	.name =		"ESI_INCLUDE",
	.deliver =	ved_deliver,
	.reembark =	ved_reembark,
};

/*--------------------------------------------------------------------*/

static void v_matchproto_(vtr_reembark_f)
ved_reembark(struct worker *wrk, struct req *req)
{
	struct ecx *ecx;

	(void)wrk;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(ecx, req->transport_priv, ECX_MAGIC);
	Lck_Lock(&req->sp->mtx);
	ecx->woken = 1;
	AZ(pthread_cond_signal(&ecx->preq->wrk->cond));
	Lck_Unlock(&req->sp->mtx);
}

/*--------------------------------------------------------------------*/

static void
ved_include(struct req *preq, const char *src, const char *host,
    struct ecx *ecx)
{
	struct worker *wrk;
	struct sess *sp;
	struct req *req;
	enum req_fsm_nxt s;

	CHECK_OBJ_NOTNULL(preq, REQ_MAGIC);
	sp = preq->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(ecx, ECX_MAGIC);
	wrk = preq->wrk;

	if (preq->esi_level >= cache_param->max_esi_depth)
		return;

	req = Req_New(wrk, sp);
	SES_Ref(sp);
	req->req_body_status = REQ_BODY_NONE;
	AZ(req->vsl->wid);
	req->vsl->wid = VXID_Get(wrk, VSL_CLIENTMARKER);
	VSLb(req->vsl, SLT_Begin, "req %u esi", VXID(preq->vsl->wid));
	VSLb(preq->vsl, SLT_Link, "req %u esi", VXID(req->vsl->wid));
	req->esi_level = preq->esi_level + 1;

	if (preq->esi_level == 0)
		assert(preq->top == preq);
	else
		CHECK_OBJ_NOTNULL(preq->top, REQ_MAGIC);

	req->top = preq->top;

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

	/* Don't allow conditionals, we can't use a 304 */
	http_Unset(req->http0, H_If_Modified_Since);
	http_Unset(req->http0, H_If_None_Match);

	/* Don't allow Range */
	http_Unset(req->http0, H_Range);

	/* Set Accept-Encoding according to what we want */
	http_Unset(req->http0, H_Accept_Encoding);
	if (ecx->isgzip)
		http_ForceHeader(req->http0, H_Accept_Encoding, "gzip");

	/* Client content already taken care of */
	http_Unset(req->http0, H_Content_Length);

	/* Reset request to status before we started messing with it */
	HTTP_Copy(req->http, req->http0);

	AZ(req->vcl);
	req->vcl = preq->vcl;
	preq->vcl = NULL;

	req->req_step = R_STP_RECV;
	req->t_req = preq->t_req;
	assert(isnan(req->t_first));
	assert(isnan(req->t_prev));

	req->transport = &VED_transport;
	req->transport_priv = ecx;

	THR_SetRequest(req);

	VSLb_ts_req(req, "Start", W_TIM_real(wrk));

	req->ws_req = WS_Snapshot(req->ws);

	while (1) {
		req->wrk = wrk;
		ecx->woken = 0;
		s = CNT_Request(wrk, req);
		if (s == REQ_FSM_DONE)
			break;
		DSL(DBG_WAITINGLIST, req->vsl->wid,
		    "loop waiting for ESI (%d)", (int)s);
		assert(s == REQ_FSM_DISEMBARK);
		Lck_Lock(&sp->mtx);
		if (!ecx->woken)
			(void)Lck_CondWait(
			    &ecx->preq->wrk->cond, &sp->mtx, 0);
		Lck_Unlock(&sp->mtx);
		ecx->woken = 0;
		AZ(req->wrk);
	}

	VRTPRIV_dynamic_kill(req->privs, (uintptr_t)req);

	AZ(preq->vcl);
	preq->vcl = req->vcl;
	req->vcl = NULL;

	req->wrk = NULL;
	THR_SetRequest(preq);

	Req_AcctLogCharge(wrk->stats, req);
	Req_Release(req);
	SES_Rel(sp);
}

/*--------------------------------------------------------------------*/

//#define Debug(fmt, ...) printf(fmt, __VA_ARGS__)
#define Debug(fmt, ...) /**/

static ssize_t
ved_decode_len(struct req *req, const uint8_t **pp)
{
	const uint8_t *p;
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
		VSLb(req->vsl, SLT_Error,
		    "ESI-corruption: Illegal Length %d %d\n", *p, (*p & 15));
		WRONG("ESI-codes: illegal length");
	}
	*pp = p;
	assert(l > 0);
	return (l);
}

/*---------------------------------------------------------------------
 */

static int v_matchproto_(vdp_bytes)
ved_vdp(struct req *req, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	uint8_t *q, *r;
	ssize_t l = 0;
	uint32_t icrc = 0;
	uint8_t tailbuf[8 + 5];
	const uint8_t *pp;
	struct ecx *ecx, *pecx = NULL;
	int retval = 0;

	if (act == VDP_INIT) {
		AZ(*priv);
		ALLOC_OBJ(ecx, ECX_MAGIC);
		AN(ecx);
		assert(sizeof gzip_hdr == 10);
		ecx->preq = req;
		*priv = ecx;
		RFC2616_Weaken_Etag(req->resp);
		req->res_mode |= RES_ESI;
		if (req->resp_len != 0)
			req->resp_len = -1;
		return (0);
	}
	CAST_OBJ_NOTNULL(ecx, *priv, ECX_MAGIC);
	if (act == VDP_FINI) {
		FREE_OBJ(ecx);
		*priv = NULL;
		return (0);
	}
	pp = ptr;

	if (req->esi_level > 0) {
		assert(req->transport == &VED_transport);
		CAST_OBJ_NOTNULL(pecx, req->transport_priv, ECX_MAGIC);
		if (!pecx->isgzip)
			pecx = NULL;
	}

	while (1) {
		switch (ecx->state) {
		case 0:
			ecx->p = ObjGetAttr(req->wrk, req->objcore,
			    OA_ESIDATA, &l);
			AN(ecx->p);
			assert(l > 0);
			ecx->e = ecx->p + l;

			if (*ecx->p == VEC_GZ) {
				if (pecx == NULL)
					retval = VDP_bytes(req, VDP_NULL,
					    gzip_hdr, 10);
				ecx->l_crc = 0;
				ecx->crc = crc32(0L, Z_NULL, 0);
				ecx->isgzip = 1;
				ecx->p++;
			}
			ecx->state = 1;
			break;
		case 1:
			if (ecx->p >= ecx->e) {
				ecx->state = 2;
				break;
			}
			switch (*ecx->p) {
			case VEC_V1:
			case VEC_V2:
			case VEC_V8:
				ecx->l = ved_decode_len(req, &ecx->p);
				if (ecx->l < 0)
					return (-1);
				if (ecx->isgzip) {
					assert(*ecx->p == VEC_C1 ||
					    *ecx->p == VEC_C2 ||
					    *ecx->p == VEC_C8);
					l = ved_decode_len(req, &ecx->p);
					if (l < 0)
						return (-1);
					icrc = vbe32dec(ecx->p);
					ecx->p += 4;
					ecx->crc = crc32_combine(
					    ecx->crc, icrc, l);
					ecx->l_crc += l;
				}
				ecx->state = 3;
				break;
			case VEC_S1:
			case VEC_S2:
			case VEC_S8:
				ecx->l = ved_decode_len(req, &ecx->p);
				if (ecx->l < 0)
					return (-1);
				Debug("SKIP1(%d)\n", (int)ecx->l);
				ecx->state = 4;
				break;
			case VEC_INCL:
				ecx->p++;
				q = (void*)strchr((const char*)ecx->p, '\0');
				AN(q);
				q++;
				r = (void*)strchr((const char*)q, '\0');
				AN(r);
				if (VDP_bytes(req, VDP_FLUSH, NULL, 0)) {
					ecx->p = ecx->e;
					break;
				}
				Debug("INCL [%s][%s] BEGIN\n", q, ecx->p);
				ved_include(req,
				    (const char*)q, (const char*)ecx->p, ecx);
				Debug("INCL [%s][%s] END\n", q, ecx->p);
				ecx->p = r + 1;
				break;
			default:
				VSLb(req->vsl, SLT_Error,
				    "ESI corruption line %d 0x%02x [%s]\n",
				    __LINE__, *ecx->p, ecx->p);
				WRONG("ESI-codes: Illegal code");
			}
			break;
		case 2:
			if (ecx->isgzip && pecx == NULL) {
				/*
				 * We are bytealigned here, so simply emit
				 * a gzip literal block with finish bit set.
				 */
				tailbuf[0] = 0x01;
				tailbuf[1] = 0x00;
				tailbuf[2] = 0x00;
				tailbuf[3] = 0xff;
				tailbuf[4] = 0xff;

				/* Emit CRC32 */
				vle32enc(tailbuf + 5, ecx->crc);

				/* MOD(2^32) length */
				vle32enc(tailbuf + 9, ecx->l_crc);

				(void)VDP_bytes(req, VDP_NULL, tailbuf, 13);
			} else if (pecx != NULL) {
				pecx->crc = crc32_combine(pecx->crc,
				    ecx->crc, ecx->l_crc);
				pecx->l_crc += ecx->l_crc;
			}
			retval = VDP_bytes(req, VDP_FLUSH, NULL, 0);
			ecx->state = 99;
			return (retval);
		case 3:
		case 4:
			/*
			 * There is no guarantee that the 'l' bytes are all
			 * in the same storage segment, so loop over storage
			 * until we have processed them all.
			 */
			if (ecx->l <= len) {
				if (ecx->state == 3)
					retval = VDP_bytes(req, act,
					    pp, ecx->l);
				len -= ecx->l;
				pp += ecx->l;
				ecx->state = 1;
				break;
			}
			if (ecx->state == 3 && len > 0)
				retval = VDP_bytes(req, act, pp, len);
			ecx->l -= len;
			return (retval);
		case 99:
			/*
			 * VEP does not account for the PAD+CRC+LEN
			 * so we can see up to approx 15 bytes here.
			 */
			return (retval);
		default:
			WRONG("FOO");
			break;
		}
		if (retval)
			return (retval);
	}
}

const struct vdp VDP_esi = {
	.name =		"esi",
	.func =		ved_vdp,
};

/*
 * Account body bytes on req
 * Push bytes to preq
 */
static inline int
ved_bytes(struct req *req, struct req *preq, enum vdp_action act,
    const void *ptr, ssize_t len)
{
	req->acct.resp_bodybytes += len;
	return (VDP_bytes(preq, act, ptr, len));
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

static int v_matchproto_(vdp_bytes)
ved_pretend_gzip(struct req *req, enum vdp_action act, void **priv,
    const void *pv, ssize_t l)
{
	uint8_t buf1[5], buf2[5];
	const uint8_t *p;
	uint16_t lx;
	struct ecx *ecx;
	struct req *preq;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(ecx, *priv, ECX_MAGIC);
	preq = ecx->preq;

	(void)priv;
	if (act == VDP_INIT)
		return (0);
	if (act == VDP_FINI) {
		*priv = NULL;
		return (0);
	}
	if (l == 0)
		return (ved_bytes(req, ecx->preq, act, pv, l));

	p = pv;

	AN (ecx->isgzip);
	ecx->crc = crc32(ecx->crc, p, l);
	ecx->l_crc += l;

	lx = 65535;
	buf1[0] = 0;
	vle16enc(buf1 + 1, lx);
	vle16enc(buf1 + 3, ~lx);

	while (l > 0) {
		if (l >= 65535) {
			lx = 65535;
			if (ved_bytes(req, preq, VDP_NULL, buf1, sizeof buf1))
				return (-1);
		} else {
			lx = (uint16_t)l;
			buf2[0] = 0;
			vle16enc(buf2 + 1, lx);
			vle16enc(buf2 + 3, ~lx);
			if (ved_bytes(req, preq, VDP_NULL, buf2, sizeof buf2))
				return (-1);
		}
		if (ved_bytes(req, preq, VDP_NULL, p, lx))
			return (-1);
		l -= lx;
		p += lx;
	}
	/* buf2 is local, have to flush */
	return (ved_bytes(req, preq, VDP_FLUSH, NULL, 0));
}

static const struct vdp ved_vdp_pgz = {
	.name =		"PGZ",
	.func =		ved_pretend_gzip,
};

/*---------------------------------------------------------------------
 * Include an object in a gzip'ed ESI object delivery
 *
 * This is not written as a VDP (yet) because it relies on the
 * OA_GZIPBITS which only becomes available when the input side
 * has fully digested the object and located the magic bit positions.
 *
 * We can improve this two ways.
 *
 * One is to run a gunzip instance here, to find the stopbit ourselves,
 * but that would be double work, in particular when passing a gziped
 * object, where we would have two null-gunzips.
 *
 * The other is to have the input side guarantee that OA_GZIPBITS::stopbit
 * always is committed before the chunk of data containing it.  We would
 * be required to poll OA_GZIPBITS on every chunk presented, but that is
 * much cheaper than running a gunzip instance.
 */

struct ved_foo {
	unsigned		magic;
#define VED_FOO_MAGIC		0x6a5a262d
	struct req		*req;
	struct req		*preq;
	ssize_t start, last, stop, lpad;
	ssize_t ll;
	uint64_t olen;
	uint8_t *dbits;
	uint8_t tailbuf[8];
};

static int
ved_objiterate(void *priv, int flush, const void *ptr, ssize_t len)
{
	struct ved_foo *foo;
	const uint8_t *pp;
	ssize_t dl;
	ssize_t l;

	CAST_OBJ_NOTNULL(foo, priv, VED_FOO_MAGIC);
	(void)flush;
	pp = ptr;
	if (len > 0) {
		/* Skip over the GZIP header */
		dl = foo->start / 8 - foo->ll;
		if (dl > 0) {
			/* Before foo.start, skip */
			if (dl > len)
				dl = len;
			foo->ll += dl;
			len -= dl;
			pp += dl;
		}
	}
	if (len > 0) {
		/* The main body of the object */
		dl = foo->last / 8 - foo->ll;
		if (dl > 0) {
			if (dl > len)
				dl = len;
			if (ved_bytes(foo->req, foo->preq, VDP_NULL, pp, dl))
				return(-1);
			foo->ll += dl;
			len -= dl;
			pp += dl;
		}
	}
	if (len > 0 && foo->ll == foo->last / 8) {
		/* Remove the "LAST" bit */
		foo->dbits[0] = *pp;
		foo->dbits[0] &= ~(1U << (foo->last & 7));
		if (ved_bytes(foo->req, foo->preq, VDP_NULL, foo->dbits, 1))
			return (-1);
		foo->ll++;
		len--;
		pp++;
	}
	if (len > 0) {
		/* Last block */
		dl = foo->stop / 8 - foo->ll;
		if (dl > 0) {
			if (dl > len)
				dl = len;
			if (ved_bytes(foo->req, foo->preq, VDP_NULL, pp, dl))
				return (-1);
			foo->ll += dl;
			len -= dl;
			pp += dl;
		}
	}
	if (len > 0 && (foo->stop & 7) && foo->ll == foo->stop / 8) {
		/* Add alignment to byte boundary */
		foo->dbits[1] = *pp;
		foo->ll++;
		len--;
		pp++;
		switch ((int)(foo->stop & 7)) {
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
			foo->dbits[2] = 0x00; foo->dbits[3] = 0x00;
			foo->dbits[4] = 0xff; foo->dbits[5] = 0xff;
			foo->lpad = 5;
			break;
		case 2: /* xx010000 00000100 00000001 00000000 */
			foo->dbits[1] |= 0x08;
			foo->dbits[2] = 0x20;
			foo->dbits[3] = 0x80;
			foo->dbits[4] = 0x00;
			foo->lpad = 4;
			break;
		case 4: /* xxxx0100 00000001 00000000 */
			foo->dbits[1] |= 0x20;
			foo->dbits[2] = 0x80;
			foo->dbits[3] = 0x00;
			foo->lpad = 3;
			break;
		case 6: /* xxxxxx01 00000000 */
			foo->dbits[1] |= 0x80;
			foo->dbits[2] = 0x00;
			foo->lpad = 2;
			break;
		case 7:	/*
			 * xxxxxxx0
			 * 00......
			 * 00000000 00000000 11111111 11111111
			 */
			foo->dbits[2] = 0x00;
			foo->dbits[3] = 0x00; foo->dbits[4] = 0x00;
			foo->dbits[5] = 0xff; foo->dbits[6] = 0xff;
			foo->lpad = 6;
			break;
		case 0: /* xxxxxxxx */
		default:
			WRONG("compiler must be broken");
		}
		if (ved_bytes(foo->req, foo->preq,
		    VDP_NULL, foo->dbits + 1, foo->lpad))
			return (-1);
	}
	if (len > 0) {
		/* Recover GZIP tail */
		dl = foo->olen - foo->ll;
		assert(dl >= 0);
		if (dl > len)
			dl = len;
		if (dl > 0) {
			assert(dl <= 8);
			l = foo->ll - (foo->olen - 8);
			assert(l >= 0);
			assert(l <= 8);
			assert(l + dl <= 8);
			memcpy(foo->tailbuf + l, pp, dl);
			foo->ll += dl;
			len -= dl;
		}
	}
	assert(len == 0);
	return (0);
}

static void
ved_stripgzip(struct req *req, const struct boc *boc)
{
	ssize_t l;
	const char *p;
	uint32_t icrc;
	uint32_t ilen;
	uint8_t *dbits;
	struct ecx *ecx;
	struct ved_foo foo;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(ecx, req->transport_priv, ECX_MAGIC);

	INIT_OBJ(&foo, VED_FOO_MAGIC);
	foo.req = req;
	foo.preq = ecx->preq;
	memset(foo.tailbuf, 0xdd, sizeof foo.tailbuf);

	/* OA_GZIPBITS is not valid until BOS_FINISHED */
	if (boc != NULL)
		ObjWaitState(req->objcore, BOS_FINISHED);
	if (req->objcore->flags & OC_F_FAILED) {
		/* No way of signalling errors in the middle of
		   the ESI body. Omit this ESI fragment. */
		return;
	}

	AN(ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED));

	/*
	 * This is the interesting case: Deliver all the deflate
	 * blocks, stripping the "LAST" bit of the last one and
	 * padding it, as necessary, to a byte boundary.
	 */

	p = ObjGetAttr(req->wrk, req->objcore, OA_GZIPBITS, &l);
	AN(p);
	assert(l == 32);
	foo.start = vbe64dec(p);
	foo.last = vbe64dec(p + 8);
	foo.stop = vbe64dec(p + 16);
	foo.olen = ObjGetLen(req->wrk, req->objcore);
	assert(foo.start > 0 && foo.start < foo.olen * 8);
	assert(foo.last > 0 && foo.last < foo.olen * 8);
	assert(foo.stop > 0 && foo.stop < foo.olen * 8);
	assert(foo.last >= foo.start);
	assert(foo.last < foo.stop);

	/* The start bit must be byte aligned. */
	AZ(foo.start & 7);

	dbits = WS_Alloc(req->ws, 8);
	AN(dbits);
	foo.dbits = dbits;
	(void)ObjIterate(req->wrk, req->objcore, &foo, ved_objiterate, 0);
	/* XXX: error check ?? */
	(void)ved_bytes(req, foo.preq, VDP_FLUSH, NULL, 0);

	icrc = vle32dec(foo.tailbuf);
	ilen = vle32dec(foo.tailbuf + 4);

	ecx->crc = crc32_combine(ecx->crc, icrc, ilen);
	ecx->l_crc += ilen;
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(vdp_bytes)
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
	return (ved_bytes(req, preq, act, ptr, len));
}

static const struct vdp ved_ved = {
	.name =		"VED",
	.func =		ved_vdp_bytes,
};

/*--------------------------------------------------------------------*/

static void v_matchproto_(vtr_deliver_f)
ved_deliver(struct req *req, struct boc *boc, int wantbody)
{
	int i;
	struct ecx *ecx;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	CAST_OBJ_NOTNULL(ecx, req->transport_priv, ECX_MAGIC);

	if (wantbody == 0)
		return;

	if (boc == NULL && ObjGetLen(req->wrk, req->objcore) == 0)
		return;

	req->res_mode |= RES_ESI_CHILD;
	i = ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED);
	if (ecx->isgzip && i && !(req->res_mode & RES_ESI)) {
		ved_stripgzip(req, boc);
	} else {
		if (ecx->isgzip && !i)
			(void)VDP_push(req, &ved_vdp_pgz, ecx, 1);
		else
			(void)VDP_push(req, &ved_ved, ecx->preq, 1);
		(void)VDP_DeliverObj(req);
		(void)VDP_bytes(req, VDP_FLUSH, NULL, 0);
	}
	VDP_close(req);
}
