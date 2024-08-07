/*-
 * Copyright (c) 2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "vct.h"
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
	int		abrt;

	struct req	*preq;
	struct ecx	*pecx;
	ssize_t		l_crc;
	uint32_t	crc;
};

static int v_matchproto_(vtr_minimal_response_f)
ved_minimal_response(struct req *req, uint16_t status)
{
	(void)req;
	(void)status;
	WRONG("esi:includes should not try minimal responses");
}

static const struct transport VED_transport = {
	.magic =		TRANSPORT_MAGIC,
	.name =			"ESI_INCLUDE",
	.deliver =		ved_deliver,
	.reembark =		ved_reembark,
	.minimal_response =	ved_minimal_response,
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
	PTOK(pthread_cond_signal(&ecx->preq->wrk->cond));
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
	CHECK_OBJ_NOTNULL(preq->top, REQTOP_MAGIC);
	sp = preq->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(ecx, ECX_MAGIC);
	wrk = preq->wrk;

	if (preq->esi_level >= cache_param->max_esi_depth) {
		VSLb(preq->vsl, SLT_VCL_Error,
		    "ESI depth limit reached (param max_esi_depth = %u)",
		    cache_param->max_esi_depth);
		if (ecx->abrt)
			preq->top->topreq->vdc->retval = -1;
		return;
	}

	req = Req_New(sp);
	AN(req);
	THR_SetRequest(req);
	assert(IS_NO_VXID(req->vsl->wid));
	req->vsl->wid = VXID_Get(wrk, VSL_CLIENTMARKER);

	wrk->stats->esi_req++;
	req->esi_level = preq->esi_level + 1;

	VSLb(req->vsl, SLT_Begin, "req %ju esi %u",
	    (uintmax_t)VXID(preq->vsl->wid), req->esi_level);
	VSLb(preq->vsl, SLT_Link, "req %ju esi %u",
	    (uintmax_t)VXID(req->vsl->wid), req->esi_level);

	VSLb_ts_req(req, "Start", W_TIM_real(wrk));

	memset(req->top, 0, sizeof *req->top);
	req->top = preq->top;

	HTTP_Setup(req->http, req->ws, req->vsl, SLT_ReqMethod);
	HTTP_Dup(req->http, preq->http0);

	http_SetH(req->http, HTTP_HDR_URL, src);
	if (host != NULL && *host != '\0')  {
		http_Unset(req->http, H_Host);
		http_SetHeader(req->http, host);
	}

	http_ForceField(req->http, HTTP_HDR_METHOD, "GET");
	http_ForceField(req->http, HTTP_HDR_PROTO, "HTTP/1.1");

	/* Don't allow conditionals, we can't use a 304 */
	http_Unset(req->http, H_If_Modified_Since);
	http_Unset(req->http, H_If_None_Match);

	/* Don't allow Range */
	http_Unset(req->http, H_Range);

	/* Set Accept-Encoding according to what we want */
	if (ecx->isgzip)
		http_ForceHeader(req->http, H_Accept_Encoding, "gzip");
	else
		http_Unset(req->http, H_Accept_Encoding);

	/* Client content already taken care of */
	http_Unset(req->http, H_Content_Length);
	http_Unset(req->http, H_Transfer_Encoding);
	req->req_body_status = BS_NONE;

	AZ(req->vcl);
	AN(req->top);
	if (req->top->vcl0)
		req->vcl = req->top->vcl0;
	else
		req->vcl = preq->vcl;
	VCL_Ref(req->vcl);

	assert(req->req_step == R_STP_TRANSPORT);
	req->t_req = preq->t_req;

	req->transport = &VED_transport;
	req->transport_priv = ecx;

	VCL_TaskEnter(req->privs);

	while (1) {
		CNT_Embark(wrk, req);
		ecx->woken = 0;
		s = CNT_Request(req);
		if (s == REQ_FSM_DONE)
			break;
		DSL(DBG_WAITINGLIST, req->vsl->wid,
		    "waiting for ESI (%d)", (int)s);
		assert(s == REQ_FSM_DISEMBARK);
		Lck_Lock(&sp->mtx);
		if (!ecx->woken)
			(void)Lck_CondWait(&ecx->preq->wrk->cond, &sp->mtx);
		Lck_Unlock(&sp->mtx);
		AZ(req->wrk);
	}

	VCL_Rel(&req->vcl);

	req->wrk = NULL;
	THR_SetRequest(preq);

	Req_Cleanup(sp, wrk, req);
	Req_Release(req);
}

/*--------------------------------------------------------------------*/

//#define Debug(fmt, ...) printf(fmt, __VA_ARGS__)
#define Debug(fmt, ...) /**/

static ssize_t
ved_decode_len(struct vsl_log *vsl, const uint8_t **pp)
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
		VSLb(vsl, SLT_Error,
		    "ESI-corruption: Illegal Length %d %d\n", *p, (*p & 15));
		WRONG("ESI-codes: illegal length");
	}
	*pp = p;
	assert(l > 0);
	return (l);
}

/*---------------------------------------------------------------------
 */

static int v_matchproto_(vdp_init_f)
ved_vdp_esi_init(VRT_CTX, struct vdp_ctx *vdc, void **priv, struct objcore *oc)
{
	struct ecx *ecx;
	struct req *req;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(oc, OBJCORE_MAGIC);
	if (oc == NULL || !ObjHasAttr(vdc->wrk, oc, OA_ESIDATA))
		return (1);

	req = vdc->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(priv);
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
	if (req->esi_level > 0) {
		assert(req->transport == &VED_transport);
		CAST_OBJ_NOTNULL(ecx->pecx, req->transport_priv, ECX_MAGIC);
		if (!ecx->pecx->isgzip)
			ecx->pecx = NULL;
	}

	return (0);
}

static int v_matchproto_(vdp_fini_f)
ved_vdp_esi_fini(struct vdp_ctx *vdc, void **priv)
{
	struct ecx *ecx;

	(void)vdc;
	TAKE_OBJ_NOTNULL(ecx, priv, ECX_MAGIC);
	FREE_OBJ(ecx);
	return (0);
}

static int v_matchproto_(vdp_bytes_f)
ved_vdp_esi_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	const uint8_t *q, *r;
	ssize_t l = 0;
	uint32_t icrc = 0;
	uint8_t tailbuf[8 + 5];
	const uint8_t *pp;
	struct ecx *ecx;
	int retval = 0;

	if (act == VDP_END)
		act = VDP_FLUSH;

	AN(priv);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CAST_OBJ_NOTNULL(ecx, *priv, ECX_MAGIC);
	pp = ptr;

	while (1) {
		switch (ecx->state) {
		case 0:
			ecx->p = ObjGetAttr(vdc->wrk, ecx->preq->objcore,
			    OA_ESIDATA, &l);
			AN(ecx->p);
			assert(l > 0);
			ecx->e = ecx->p + l;

			if (*ecx->p == VEC_GZ) {
				if (ecx->pecx == NULL)
					retval = VDP_bytes(vdc, VDP_NULL,
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
				ecx->l = ved_decode_len(vdc->vsl, &ecx->p);
				if (ecx->l < 0)
					return (-1);
				if (ecx->isgzip) {
					assert(*ecx->p == VEC_C1 ||
					    *ecx->p == VEC_C2 ||
					    *ecx->p == VEC_C8);
					l = ved_decode_len(vdc->vsl, &ecx->p);
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
				ecx->l = ved_decode_len(vdc->vsl, &ecx->p);
				if (ecx->l < 0)
					return (-1);
				Debug("SKIP1(%d)\n", (int)ecx->l);
				ecx->state = 4;
				break;
			case VEC_IA:
				ecx->abrt =
				    FEATURE(FEATURE_ESI_INCLUDE_ONERROR);
				/* FALLTHROUGH */
			case VEC_IC:
				ecx->p++;
				q = (void*)strchr((const char*)ecx->p, '\0');
				AN(q);
				q++;
				r = (void*)strchr((const char*)q, '\0');
				AN(r);
				if (VDP_bytes(vdc, VDP_FLUSH, NULL, 0)) {
					ecx->p = ecx->e;
					break;
				}
				Debug("INCL [%s][%s] BEGIN\n", q, ecx->p);
				ved_include(ecx->preq,
				    (const char*)q, (const char*)ecx->p, ecx);
				Debug("INCL [%s][%s] END\n", q, ecx->p);
				ecx->p = r + 1;
				break;
			default:
				VSLb(vdc->vsl, SLT_Error,
				    "ESI corruption line %d 0x%02x [%s]\n",
				    __LINE__, *ecx->p, ecx->p);
				WRONG("ESI-codes: Illegal code");
			}
			break;
		case 2:
			ptr = NULL;
			len = 0;
			if (ecx->isgzip && ecx->pecx == NULL) {
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

				ptr = tailbuf;
				len = 13;
			} else if (ecx->pecx != NULL) {
				ecx->pecx->crc = crc32_combine(ecx->pecx->crc,
				    ecx->crc, ecx->l_crc);
				ecx->pecx->l_crc += ecx->l_crc;
			}
			retval = VDP_bytes(vdc, VDP_END, ptr, len);
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
					retval = VDP_bytes(vdc, act,
					    pp, ecx->l);
				len -= ecx->l;
				pp += ecx->l;
				ecx->state = 1;
				break;
			}
			if (ecx->state == 3 && len > 0)
				retval = VDP_bytes(vdc, act, pp, len);
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
	.init =		ved_vdp_esi_init,
	.bytes =	ved_vdp_esi_bytes,
	.fini =		ved_vdp_esi_fini,
};

/*
 * Account body bytes on req
 * Push bytes to preq
 */
static inline int
ved_bytes(struct ecx *ecx, enum vdp_action act,
    const void *ptr, ssize_t len)
{
	if (act == VDP_END)
		act = VDP_FLUSH;
	return (VDP_bytes(ecx->preq->vdc, act, ptr, len));
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

static int v_matchproto_(vdp_fini_f)
ved_pretend_gzip_fini(struct vdp_ctx *vdc, void **priv)
{
	(void)vdc;
	*priv = NULL;
	return (0);
}

static int v_matchproto_(vdp_bytes_f)
ved_pretend_gzip_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *pv, ssize_t l)
{
	uint8_t buf1[5], buf2[5];
	const uint8_t *p;
	uint16_t lx;
	struct ecx *ecx;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CAST_OBJ_NOTNULL(ecx, *priv, ECX_MAGIC);

	(void)priv;
	if (l == 0)
		return (ved_bytes(ecx, act, pv, l));

	p = pv;

	AN (ecx->isgzip);
	ecx->crc = crc32(ecx->crc, p, l);
	ecx->l_crc += l;

	/*
	 * buf1 can safely be emitted multiple times for objects longer
	 * than 64K-1 bytes.
	 */
	lx = 65535;
	buf1[0] = 0;
	vle16enc(buf1 + 1, lx);
	vle16enc(buf1 + 3, ~lx);

	while (l > 0) {
		if (l >= 65535) {
			lx = 65535;
			if (ved_bytes(ecx, VDP_NULL, buf1, sizeof buf1))
				return (-1);
		} else {
			lx = (uint16_t)l;
			buf2[0] = 0;
			vle16enc(buf2 + 1, lx);
			vle16enc(buf2 + 3, ~lx);
			if (ved_bytes(ecx, VDP_NULL, buf2, sizeof buf2))
				return (-1);
		}
		if (ved_bytes(ecx, VDP_NULL, p, lx))
			return (-1);
		l -= lx;
		p += lx;
	}
	/* buf1 & buf2 are local, so we have to flush */
	return (ved_bytes(ecx, VDP_FLUSH, NULL, 0));
}

static const struct vdp ved_pretend_gz = {
	.name =		"PGZ",
	.bytes =	ved_pretend_gzip_bytes,
	.fini =		ved_pretend_gzip_fini,
};

/*---------------------------------------------------------------------
 * Include a gzip'ed object in a gzip'ed ESI object delivery
 *
 * This is the interesting case: Deliver all the deflate blocks, stripping
 * the "LAST" bit of the last one and padding it, as necessary, to a byte
 * boundary.
 *
 */

struct ved_foo {
	unsigned		magic;
#define VED_FOO_MAGIC		0x6a5a262d
	struct ecx		*ecx;
	struct objcore		*objcore;
	uint64_t		start, last, stop, lpad;
	ssize_t			ll;
	uint64_t		olen;
	uint8_t			dbits[8];
	uint8_t			tailbuf[8];
};

static int v_matchproto_(vdp_init_f)
ved_gzgz_init(VRT_CTX, struct vdp_ctx *vdc, void **priv, struct objcore *oc)
{
	ssize_t l;
	const char *p;
	struct ved_foo *foo;
	struct req *req;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	(void)oc;
	req = vdc->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(foo, *priv, VED_FOO_MAGIC);
	CHECK_OBJ_NOTNULL(foo->objcore, OBJCORE_MAGIC);

	memset(foo->tailbuf, 0xdd, sizeof foo->tailbuf);

	AN(ObjCheckFlag(vdc->wrk, foo->objcore, OF_GZIPED));

	p = ObjGetAttr(vdc->wrk, foo->objcore, OA_GZIPBITS, &l);
	AN(p);
	assert(l == 32);
	foo->start = vbe64dec(p);
	foo->last = vbe64dec(p + 8);
	foo->stop = vbe64dec(p + 16);
	foo->olen = ObjGetLen(vdc->wrk, foo->objcore);
	assert(foo->start > 0 && foo->start < foo->olen * 8);
	assert(foo->last > 0 && foo->last < foo->olen * 8);
	assert(foo->stop > 0 && foo->stop < foo->olen * 8);
	assert(foo->last >= foo->start);
	assert(foo->last < foo->stop);

	/* The start bit must be byte aligned. */
	AZ(foo->start & 7);
	return (0);
}

/*
 * XXX: for act == VDP_END || act == VDP_FLUSH, we send a flush more often than
 * we need. The VDP_END case would trip our "at most one VDP_END call" assertion
 * in VDP_bytes(), but ved_bytes() covers it.
 *
 * To avoid unnecessary chunks downstream, it would be nice to re-structure the
 * code to identify the last block, send VDP_END/VDP_FLUSH for that one and
 * VDP_NULL for anything before it.
 */

static int v_matchproto_(vdp_bytes_f)
ved_gzgz_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct ved_foo *foo;
	const uint8_t *pp;
	ssize_t dl;
	ssize_t l;

	(void)vdc;
	CAST_OBJ_NOTNULL(foo, *priv, VED_FOO_MAGIC);
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
			dl = vmin(dl, len);
			if (ved_bytes(foo->ecx, act, pp, dl))
				return (-1);
			foo->ll += dl;
			len -= dl;
			pp += dl;
		}
	}
	if (len > 0 && foo->ll == foo->last / 8) {
		/* Remove the "LAST" bit */
		foo->dbits[0] = *pp;
		foo->dbits[0] &= ~(1U << (foo->last & 7));
		if (ved_bytes(foo->ecx, act, foo->dbits, 1))
			return (-1);
		foo->ll++;
		len--;
		pp++;
	}
	if (len > 0) {
		/* Last block */
		dl = foo->stop / 8 - foo->ll;
		if (dl > 0) {
			dl = vmin(dl, len);
			if (ved_bytes(foo->ecx, act, pp, dl))
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
		if (ved_bytes(foo->ecx, act, foo->dbits + 1, foo->lpad))
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

static int v_matchproto_(vdp_fini_f)
ved_gzgz_fini(struct vdp_ctx *vdc, void **priv)
{
	uint32_t icrc;
	uint32_t ilen;
	struct ved_foo *foo;

	(void)vdc;
	TAKE_OBJ_NOTNULL(foo, priv, VED_FOO_MAGIC);

	/* XXX
	 * this works due to the esi layering, a VDP pushing bytes from _fini
	 * will otherwise have its own _bytes method called.
	 *
	 * Could rewrite use VDP_END
	 */
	(void)ved_bytes(foo->ecx, VDP_FLUSH, NULL, 0);

	icrc = vle32dec(foo->tailbuf);
	ilen = vle32dec(foo->tailbuf + 4);
	foo->ecx->crc = crc32_combine(foo->ecx->crc, icrc, ilen);
	foo->ecx->l_crc += ilen;

	return (0);
}

static const struct vdp ved_gzgz = {
	.name =		"VZZ",
	.init =		ved_gzgz_init,
	.bytes =	ved_gzgz_bytes,
	.fini =		ved_gzgz_fini,
};

/*--------------------------------------------------------------------
 * Straight through without processing.
 */

static int v_matchproto_(vdp_fini_f)
ved_vdp_fini(struct vdp_ctx *vdc, void **priv)
{
	(void)vdc;
	*priv = NULL;
	return (0);
}

static int v_matchproto_(vdp_bytes_f)
ved_vdp_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct ecx *ecx;

	(void)vdc;
	CAST_OBJ_NOTNULL(ecx, *priv, ECX_MAGIC);
	return (ved_bytes(ecx, act, ptr, len));
}

static const struct vdp ved_ved = {
	.name =		"VED",
	.bytes =	ved_vdp_bytes,
	.fini =		ved_vdp_fini,
};

static void
ved_close(struct req *req, struct boc *boc, int error)
{
	req->acct.resp_bodybytes += VDP_Close(req->vdc, req->objcore, boc);

	if (! error)
		return;
	req->top->topreq->vdc->retval = -1;
	req->top->topreq->doclose = req->doclose;
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(vtr_deliver_f)
ved_deliver(struct req *req, struct boc *boc, int wantbody)
{
	int i = 0;
	const char *p;
	uint16_t status;
	struct ecx *ecx;
	struct ved_foo foo[1];
	struct vrt_ctx ctx[1];

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	CAST_OBJ_NOTNULL(ecx, req->transport_priv, ECX_MAGIC);

	status = req->resp->status % 1000;

	if (FEATURE(FEATURE_ESI_INCLUDE_ONERROR) &&
	    status != 200 && status != 204) {
		ved_close(req, boc, ecx->abrt);
		return;
	}

	if (wantbody == 0) {
		ved_close(req, boc, 0);
		return;
	}

	if (boc == NULL && ObjGetLen(req->wrk, req->objcore) == 0) {
		ved_close(req, boc, 0);
		return;
	}

	if (http_GetHdr(req->resp, H_Content_Encoding, &p))
		i = http_coding_eq(p, gzip);
	if (i)
		i = ObjCheckFlag(req->wrk, req->objcore, OF_GZIPED);

	INIT_OBJ(ctx, VRT_CTX_MAGIC);
	VCL_Req2Ctx(ctx, req);

	if (ecx->isgzip && i && !(req->res_mode & RES_ESI)) {
		/* A gzip'ed include which is not ESI processed */

		/* OA_GZIPBITS are not valid until BOS_FINISHED */
		if (boc != NULL)
			ObjWaitState(req->objcore, BOS_FINISHED);

		if (req->objcore->flags & OC_F_FAILED) {
			/* No way of signalling errors in the middle of
			 * the ESI body. Omit this ESI fragment.
			 * XXX change error argument to 1
			 */
			ved_close(req, boc, 0);
			return;
		}

		INIT_OBJ(foo, VED_FOO_MAGIC);
		foo->ecx = ecx;
		foo->objcore = req->objcore;
		i = VDP_Push(ctx, req->vdc, req->ws, &ved_gzgz, foo);

	} else if (ecx->isgzip && !i) {
		/* Non-Gzip'ed include in gzip'ed parent */
		i = VDP_Push(ctx, req->vdc, req->ws, &ved_pretend_gz, ecx);
	} else {
		/* Anything else goes straight through */
		i = VDP_Push(ctx, req->vdc, req->ws, &ved_ved, ecx);
	}

	if (i == 0) {
		i = VDP_DeliverObj(req->vdc, req->objcore);
	} else {
		VSLb(req->vsl, SLT_Error, "Failure to push ESI processors");
		req->doclose = SC_OVERLOAD;
	}

	if (i && req->doclose == SC_NULL)
		req->doclose = SC_REM_CLOSE;

	ved_close(req, boc, i && ecx->abrt ? 1 : 0);
}
