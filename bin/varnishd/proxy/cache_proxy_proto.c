/*-
 * Copyright (c) 2015 Varnish Software AS
 * Copyright (c) 2018 GANDI SAS
 * All rights reserved.
 *
 * Authors: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *          Emmanuel Hocdet <manu@gandi.net>
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
 */

#include "config.h"

#include <netinet/in.h>
#include <netdb.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_transport.h"
#include "proxy/cache_proxy.h"

#include "vend.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"

// max. PROXY payload length (excl. sig) - XXX parameter?
#define VPX_MAX_LEN 1024

struct vpx_tlv {
	unsigned		magic;
#define VPX_TLV_MAGIC		0xdeb9a4a5
	unsigned		len;
	char			tlv[] v_counted_by_(len);
};

static inline int
vpx_ws_err(const struct req *req)
{
	VSL(SLT_Error, req->sp->vxid, "insufficient workspace");
	return (-1);
}

/**********************************************************************
 * PROXY 1 protocol
 */

static const char vpx1_sig[] = {'P', 'R', 'O', 'X', 'Y'};

static int
vpx_proto1(const struct worker *wrk, const struct req *req)
{
	const char *fld[5];
	int i;
	char *p, *q;
	struct suckaddr *sa;
	ssize_t sz;
	int pfam = -1;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);

	q = memchr(req->htc->rxbuf_b, '\r',
	    req->htc->rxbuf_e - req->htc->rxbuf_b);
	if (q == NULL)
		return (-1);

	*q++ = '\0';
	/* Nuke the CRLF */
	if (q == req->htc->rxbuf_e || *q != '\n')
		return (-1);
	*q++ = '\0';

	/* Split the fields */
	p = req->htc->rxbuf_b;
	for (i = 0; i < 5; i++) {
		p = strchr(p, ' ');
		if (p == NULL) {
			VSL(SLT_ProxyGarbage, req->sp->vxid,
			    "PROXY1: Too few fields");
			return (-1);
		}
		*p++ = '\0';
		fld[i] = p;
	}

	if (strchr(p, ' ')) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: Too many fields");
		return (-1);
	}

	if (!strcmp(fld[0], "TCP4"))
		pfam = PF_INET;
	else if (!strcmp(fld[0], "TCP6"))
		pfam = PF_INET6;
	else {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: Wrong TCP[46] field");
		return (-1);
	}

	if (! SES_Reserve_client_addr(req->sp, &sa, &sz))
		return (vpx_ws_err(req));
	assert (sz == vsa_suckaddr_len);

	if (VSS_ResolveOne(sa, fld[1], fld[3],
	    pfam, SOCK_STREAM, AI_NUMERICHOST | AI_NUMERICSERV) == NULL) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: Cannot resolve source address");
		return (-1);
	}
	if (! SES_Set_String_Attr(req->sp, SA_CLIENT_IP, fld[1]))
		return (vpx_ws_err(req));
	if (! SES_Set_String_Attr(req->sp, SA_CLIENT_PORT, fld[3]))
		return (vpx_ws_err(req));

	if (! SES_Reserve_server_addr(req->sp, &sa, &sz))
		return (vpx_ws_err(req));
	assert (sz == vsa_suckaddr_len);

	if (VSS_ResolveOne(sa, fld[2], fld[4],
	    pfam, SOCK_STREAM, AI_NUMERICHOST | AI_NUMERICSERV) == NULL) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: Cannot resolve destination address");
		return (-1);
	}

	VSL(SLT_Proxy, req->sp->vxid, "1 %s %s %s %s",
	    fld[1], fld[3], fld[2], fld[4]);
	HTC_RxPipeline(req->htc, q);
	return (0);
}

/**********************************************************************
 * PROXY 2 protocol
 */

static const char vpx2_sig[] = {
	'\r', '\n', '\r', '\n', '\0', '\r', '\n',
	'Q', 'U', 'I', 'T', '\n',
};

static const uint32_t crctable[256] = {
	0x00000000UL, 0xF26B8303UL, 0xE13B70F7UL, 0x1350F3F4UL,
	0xC79A971FUL, 0x35F1141CUL, 0x26A1E7E8UL, 0xD4CA64EBUL,
	0x8AD958CFUL, 0x78B2DBCCUL, 0x6BE22838UL, 0x9989AB3BUL,
	0x4D43CFD0UL, 0xBF284CD3UL, 0xAC78BF27UL, 0x5E133C24UL,
	0x105EC76FUL, 0xE235446CUL, 0xF165B798UL, 0x030E349BUL,
	0xD7C45070UL, 0x25AFD373UL, 0x36FF2087UL, 0xC494A384UL,
	0x9A879FA0UL, 0x68EC1CA3UL, 0x7BBCEF57UL, 0x89D76C54UL,
	0x5D1D08BFUL, 0xAF768BBCUL, 0xBC267848UL, 0x4E4DFB4BUL,
	0x20BD8EDEUL, 0xD2D60DDDUL, 0xC186FE29UL, 0x33ED7D2AUL,
	0xE72719C1UL, 0x154C9AC2UL, 0x061C6936UL, 0xF477EA35UL,
	0xAA64D611UL, 0x580F5512UL, 0x4B5FA6E6UL, 0xB93425E5UL,
	0x6DFE410EUL, 0x9F95C20DUL, 0x8CC531F9UL, 0x7EAEB2FAUL,
	0x30E349B1UL, 0xC288CAB2UL, 0xD1D83946UL, 0x23B3BA45UL,
	0xF779DEAEUL, 0x05125DADUL, 0x1642AE59UL, 0xE4292D5AUL,
	0xBA3A117EUL, 0x4851927DUL, 0x5B016189UL, 0xA96AE28AUL,
	0x7DA08661UL, 0x8FCB0562UL, 0x9C9BF696UL, 0x6EF07595UL,
	0x417B1DBCUL, 0xB3109EBFUL, 0xA0406D4BUL, 0x522BEE48UL,
	0x86E18AA3UL, 0x748A09A0UL, 0x67DAFA54UL, 0x95B17957UL,
	0xCBA24573UL, 0x39C9C670UL, 0x2A993584UL, 0xD8F2B687UL,
	0x0C38D26CUL, 0xFE53516FUL, 0xED03A29BUL, 0x1F682198UL,
	0x5125DAD3UL, 0xA34E59D0UL, 0xB01EAA24UL, 0x42752927UL,
	0x96BF4DCCUL, 0x64D4CECFUL, 0x77843D3BUL, 0x85EFBE38UL,
	0xDBFC821CUL, 0x2997011FUL, 0x3AC7F2EBUL, 0xC8AC71E8UL,
	0x1C661503UL, 0xEE0D9600UL, 0xFD5D65F4UL, 0x0F36E6F7UL,
	0x61C69362UL, 0x93AD1061UL, 0x80FDE395UL, 0x72966096UL,
	0xA65C047DUL, 0x5437877EUL, 0x4767748AUL, 0xB50CF789UL,
	0xEB1FCBADUL, 0x197448AEUL, 0x0A24BB5AUL, 0xF84F3859UL,
	0x2C855CB2UL, 0xDEEEDFB1UL, 0xCDBE2C45UL, 0x3FD5AF46UL,
	0x7198540DUL, 0x83F3D70EUL, 0x90A324FAUL, 0x62C8A7F9UL,
	0xB602C312UL, 0x44694011UL, 0x5739B3E5UL, 0xA55230E6UL,
	0xFB410CC2UL, 0x092A8FC1UL, 0x1A7A7C35UL, 0xE811FF36UL,
	0x3CDB9BDDUL, 0xCEB018DEUL, 0xDDE0EB2AUL, 0x2F8B6829UL,
	0x82F63B78UL, 0x709DB87BUL, 0x63CD4B8FUL, 0x91A6C88CUL,
	0x456CAC67UL, 0xB7072F64UL, 0xA457DC90UL, 0x563C5F93UL,
	0x082F63B7UL, 0xFA44E0B4UL, 0xE9141340UL, 0x1B7F9043UL,
	0xCFB5F4A8UL, 0x3DDE77ABUL, 0x2E8E845FUL, 0xDCE5075CUL,
	0x92A8FC17UL, 0x60C37F14UL, 0x73938CE0UL, 0x81F80FE3UL,
	0x55326B08UL, 0xA759E80BUL, 0xB4091BFFUL, 0x466298FCUL,
	0x1871A4D8UL, 0xEA1A27DBUL, 0xF94AD42FUL, 0x0B21572CUL,
	0xDFEB33C7UL, 0x2D80B0C4UL, 0x3ED04330UL, 0xCCBBC033UL,
	0xA24BB5A6UL, 0x502036A5UL, 0x4370C551UL, 0xB11B4652UL,
	0x65D122B9UL, 0x97BAA1BAUL, 0x84EA524EUL, 0x7681D14DUL,
	0x2892ED69UL, 0xDAF96E6AUL, 0xC9A99D9EUL, 0x3BC21E9DUL,
	0xEF087A76UL, 0x1D63F975UL, 0x0E330A81UL, 0xFC588982UL,
	0xB21572C9UL, 0x407EF1CAUL, 0x532E023EUL, 0xA145813DUL,
	0x758FE5D6UL, 0x87E466D5UL, 0x94B49521UL, 0x66DF1622UL,
	0x38CC2A06UL, 0xCAA7A905UL, 0xD9F75AF1UL, 0x2B9CD9F2UL,
	0xFF56BD19UL, 0x0D3D3E1AUL, 0x1E6DCDEEUL, 0xEC064EEDUL,
	0xC38D26C4UL, 0x31E6A5C7UL, 0x22B65633UL, 0xD0DDD530UL,
	0x0417B1DBUL, 0xF67C32D8UL, 0xE52CC12CUL, 0x1747422FUL,
	0x49547E0BUL, 0xBB3FFD08UL, 0xA86F0EFCUL, 0x5A048DFFUL,
	0x8ECEE914UL, 0x7CA56A17UL, 0x6FF599E3UL, 0x9D9E1AE0UL,
	0xD3D3E1ABUL, 0x21B862A8UL, 0x32E8915CUL, 0xC083125FUL,
	0x144976B4UL, 0xE622F5B7UL, 0xF5720643UL, 0x07198540UL,
	0x590AB964UL, 0xAB613A67UL, 0xB831C993UL, 0x4A5A4A90UL,
	0x9E902E7BUL, 0x6CFBAD78UL, 0x7FAB5E8CUL, 0x8DC0DD8FUL,
	0xE330A81AUL, 0x115B2B19UL, 0x020BD8EDUL, 0xF0605BEEUL,
	0x24AA3F05UL, 0xD6C1BC06UL, 0xC5914FF2UL, 0x37FACCF1UL,
	0x69E9F0D5UL, 0x9B8273D6UL, 0x88D28022UL, 0x7AB90321UL,
	0xAE7367CAUL, 0x5C18E4C9UL, 0x4F48173DUL, 0xBD23943EUL,
	0xF36E6F75UL, 0x0105EC76UL, 0x12551F82UL, 0xE03E9C81UL,
	0x34F4F86AUL, 0xC69F7B69UL, 0xD5CF889DUL, 0x27A40B9EUL,
	0x79B737BAUL, 0x8BDCB4B9UL, 0x988C474DUL, 0x6AE7C44EUL,
	0xBE2DA0A5UL, 0x4C4623A6UL, 0x5F16D052UL, 0xAD7D5351UL
};

static uint32_t crc32c(const uint8_t *buf, int len)
{
	uint32_t crc = 0xffffffff;
	while (len-- > 0) {
		crc = (crc >> 8) ^ crctable[(crc ^ (*buf++)) & 0xff];
	}
	return (crc ^ 0xffffffff);
}

struct vpx_tlv_iter {
	uint8_t		t;
	void		*p;
	uint16_t	l;
	const char	*e;

	unsigned char	*_p;
	uint16_t	_l;
};

static void
vpx_tlv_iter0(struct vpx_tlv_iter *vpi, void *p, unsigned l)
{

	AN(p);
	assert(l < 65536);
	memset(vpi, 0, sizeof *vpi);
	vpi->_p = p;
	vpi->_l = l;
}

static int
vpx_tlv_itern(struct vpx_tlv_iter *vpi)
{
	if (vpi->_l == 0 || vpi->e != NULL)
		return (0);
	if (vpi->_l < 3) {
		vpi->e = "Dribble bytes";
		return (0);
	}
	vpi->t = *vpi->_p;
	vpi->l = vbe16dec(vpi->_p + 1);
	if (vpi->l + 3 > vpi->_l) {
		vpi->e = "Length Error";
		return (0);
	}
	vpi->p = vpi->_p + 3;
	vpi->_p += 3 + vpi->l;
	vpi->_l -= 3 + vpi->l;
	return (1);
}

#define VPX_TLV_FOREACH(ptr, len, itv)				\
	for (vpx_tlv_iter0(itv, ptr, len);			\
		(vpi->e == NULL) && vpx_tlv_itern(itv);)

int
VPX_tlv(const struct req *req, int typ, const void **dst, int *len)
{
	struct vpx_tlv *tlv;
	struct vpx_tlv_iter vpi[1], vpi2[1];
	uintptr_t *up;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);
	AN(dst);
	AN(len);
	*dst = NULL;
	*len = 0;

	if (SES_Get_proxy_tlv(req->sp, &up) != 0 || *up == 0)
		return (-1);
	CAST_OBJ_NOTNULL(tlv, (void*)(*up), VPX_TLV_MAGIC);

	VPX_TLV_FOREACH(tlv->tlv, tlv->len, vpi) {
		if (vpi->t == typ) {
			*dst = vpi->p;
			*len = vpi->l;
			return (0);
		}
		if (vpi->t != PP2_TYPE_SSL)
			continue;
		VPX_TLV_FOREACH((char*)vpi->p + 5, vpi->l - 5, vpi2) {
			if (vpi2->t == typ) {
				*dst = vpi2->p;
				*len = vpi2->l;
				return (0);
			}
		}
	}
	return (-1);
}

static int
vpx_proto2(const struct worker *wrk, const struct req *req)
{
	uintptr_t *up;
	uint16_t tlv_len;
	const uint8_t *p, *ap, *pp;
	char *d, *tlv_start;
	sa_family_t pfam = 0xff;
	struct suckaddr *sa = NULL;
	ssize_t sz;
	char ha[VTCP_ADDRBUFSIZE];
	char pa[VTCP_PORTBUFSIZE];
	char hb[VTCP_ADDRBUFSIZE];
	char pb[VTCP_PORTBUFSIZE];
	struct vpx_tlv_iter vpi[1], vpi2[1];
	struct vpx_tlv *tlv;
	uint16_t l;
	unsigned hdr_len, flen, alen;
	unsigned const plen = 2, aoff = 16;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);

	assert(req->htc->rxbuf_e - req->htc->rxbuf_b >= 16L);
	l = vbe16dec(req->htc->rxbuf_b + 14);
	assert(l <= VPX_MAX_LEN); // vpx_complete()
	hdr_len = l + 16L;
	assert(req->htc->rxbuf_e >= req->htc->rxbuf_b + hdr_len);
	HTC_RxPipeline(req->htc, req->htc->rxbuf_b + hdr_len);
	p = (const void *)req->htc->rxbuf_b;
	d = req->htc->rxbuf_b + 16L;

	/* Version @12 top half */
	if ((p[12] >> 4) != 2) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY2: bad version (%d)", p[12] >> 4);
		return (-1);
	}

	/* Command @12 bottom half */
	switch (p[12] & 0x0f) {
	case 0x0:
		VSL(SLT_Proxy, req->sp->vxid, "2 local local local local");
		return (0);
	case 0x1:
		/* Proxied connection */
		break;
	default:
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY2: bad command (%d)", p[12] & 0x0f);
		return (-1);
	}

	/* Address family & protocol @13 */
	switch (p[13]) {
	case 0x00:
		/* UNSPEC|UNSPEC, ignore proxy header */
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY2: Ignoring UNSPEC|UNSPEC addresses");
		return (0);
	case 0x11:
		/* IPv4|TCP */
		pfam = AF_INET;
		alen = 4;
		break;
	case 0x21:
		/* IPv6|TCP */
		pfam = AF_INET6;
		alen = 16;
		break;
	default:
		/* Ignore proxy header */
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY2: Ignoring unsupported protocol (0x%02x)", p[13]);
		return (0);
	}

	flen = 2 * alen + 2 * plen;

	if (l < flen) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY2: Ignoring short %s addresses (%u)",
		    pfam == AF_INET ? "IPv4" : "IPv6", l);
		return (0);
	}

	l -= flen;
	d += flen;

	ap = p + aoff;
	pp = ap + 2 * alen;

	/* src/client */
	if (! SES_Reserve_client_addr(req->sp, &sa, &sz))
		return (vpx_ws_err(req));
	assert(sz == vsa_suckaddr_len);
	AN(VSA_BuildFAP(sa, pfam, ap, alen, pp, plen));
	VTCP_name(sa, hb, sizeof hb, pb, sizeof pb);

	ap += alen;
	pp += plen;

	/* dst/server */
	if (! SES_Reserve_server_addr(req->sp, &sa, &sz))
		return (vpx_ws_err(req));
	assert(sz == vsa_suckaddr_len);
	AN(VSA_BuildFAP(sa, pfam, ap, alen, pp, plen));
	VTCP_name(sa, ha, sizeof ha, pa, sizeof pa);

	if (! SES_Set_String_Attr(req->sp, SA_CLIENT_IP, hb))
		return (vpx_ws_err(req));
	if (! SES_Set_String_Attr(req->sp, SA_CLIENT_PORT, pb))
		return (vpx_ws_err(req));

	VSL(SLT_Proxy, req->sp->vxid, "2 %s %s %s %s", hb, pb, ha, pa);

	tlv_start = d;
	tlv_len = l;

	VPX_TLV_FOREACH(d, l, vpi) {
		if (vpi->t == PP2_TYPE_SSL) {
			if (vpi->l < 5) {
				vpi->e = "Length Error";
				break;
			}
			VPX_TLV_FOREACH((char*)vpi->p + 5, vpi->l - 5, vpi2) {
			}
			vpi->e = vpi2->e;
		} else if (vpi->t == PP2_TYPE_CRC32C) {
			uint32_t n_crc32c = vbe32dec(vpi->p);
			vbe32enc(vpi->p, 0);
			if (crc32c(p, hdr_len) != n_crc32c) {
				VSL(SLT_ProxyGarbage, req->sp->vxid,
				    "PROXY2: CRC error");
				return (-1);
			}
		}
	}
	if (vpi->e != NULL) {
		VSL(SLT_ProxyGarbage, req->sp->vxid, "PROXY2: TLV %s", vpi->e);
		return (-1);
	}
	tlv = WS_Alloc(req->sp->ws, sizeof *tlv + tlv_len);
	if (tlv == NULL)
		return (vpx_ws_err(req));
	INIT_OBJ(tlv, VPX_TLV_MAGIC);
	tlv->len = tlv_len;
	memcpy(tlv->tlv, tlv_start, tlv_len);
	if (! SES_Reserve_proxy_tlv(req->sp, &up, &sz))
		return (vpx_ws_err(req));
	assert(sz == sizeof up);
	*up = (uintptr_t)tlv;
	return (0);
}

/**********************************************************************
 * HTC_Rx completion detector
 */

static enum htc_status_e v_matchproto_(htc_complete_f)
vpx_complete(struct http_conn *htc)
{
	size_t z, l;
	uint16_t j;
	char *p, *q;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(WS_Reservation(htc->ws));
	assert(pdiff(htc->rxbuf_b, htc->rxbuf_e) <= WS_ReservationSize(htc->ws));

	l = htc->rxbuf_e - htc->rxbuf_b;
	p = htc->rxbuf_b;
	j = 0x3;
	for (z = 0; z < l; z++) {
		if (z < sizeof vpx1_sig && p[z] != vpx1_sig[z])
			j &= ~1;
		if (z < sizeof vpx2_sig && p[z] != vpx2_sig[z])
			j &= ~2;
		if (j == 0)
			return (HTC_S_JUNK);
		if (j == 1 && z == sizeof vpx1_sig) {
			q = memchr(p + z, '\n', htc->rxbuf_e - (p + z));
			if (q != NULL && (q - htc->rxbuf_b) > 107)
				return (HTC_S_OVERFLOW);
			if (q == NULL)
				return (HTC_S_MORE);
			return (HTC_S_COMPLETE);
		}
		if (j == 2 && z == sizeof vpx2_sig) {
			if (l < 16)
				return (HTC_S_MORE);
			j = vbe16dec(p + 14);
			if (j > VPX_MAX_LEN)
				return (HTC_S_OVERFLOW);
			if (l < 16L + j)
				return (HTC_S_MORE);
			return (HTC_S_COMPLETE);
		}
	}
	return (HTC_S_MORE);
}

static void v_matchproto_(task_func_t)
vpx_new_session(struct worker *wrk, void *arg)
{
	struct req *req;
	struct sess *sp;
	enum htc_status_e hs;
	char *p;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	/* Per specification */
	assert(sizeof vpx1_sig == 5);
	assert(sizeof vpx2_sig == 12);

	HTC_RxInit(req->htc, req->ws);
	hs = HTC_RxStuff(req->htc, vpx_complete, NULL, NULL, NAN,
	    sp->t_idle + cache_param->timeout_idle, NAN, VPX_MAX_LEN);
	if (hs != HTC_S_COMPLETE) {
		Req_Release(req);
		SES_DeleteHS(sp, hs, NAN);
		return;
	}
	p = req->htc->rxbuf_b;
	if (p[0] == vpx1_sig[0])
		i = vpx_proto1(wrk, req);
	else if (p[0] == vpx2_sig[0])
		i = vpx_proto2(wrk, req);
	else
		WRONG("proxy sig mismatch");

	if (i) {
		Req_Release(req);
		SES_Delete(sp, SC_RX_JUNK, NAN);
		return;
	}

	SES_SetTransport(wrk, sp, req, &HTTP1_transport);
}

struct transport PROXY_transport = {
	.name =			"PROXY",
	.proto_ident =		"PROXY",
	.magic =		TRANSPORT_MAGIC,
	.new_session =		vpx_new_session,
};

static void
vpx_enc_addr(struct vsb *vsb, int proto, const struct suckaddr *s)
{
	const struct sockaddr_in *sin4;
	const struct sockaddr_in6 *sin6;
	socklen_t sl;

	if (proto == PF_INET6) {
		sin6 = VSA_Get_Sockaddr(s, &sl);	//lint !e826
		AN(sin6);
		assert(sl >= sizeof *sin6);
		VSB_bcat(vsb, &sin6->sin6_addr, sizeof(sin6->sin6_addr));
	} else {
		sin4 = VSA_Get_Sockaddr(s, &sl);	//lint !e826
		AN(sin4);
		assert(sl >= sizeof *sin4);
		VSB_bcat(vsb, &sin4->sin_addr, sizeof(sin4->sin_addr));
	}
}

static void
vpx_enc_port(struct vsb *vsb, const struct suckaddr *s)
{
	uint8_t b[2];

	vbe16enc(b, (uint16_t)VSA_Port(s));
	VSB_bcat(vsb, b, sizeof(b));
}

static void
vpx_enc_authority(struct vsb *vsb, const char *authority, size_t l_authority)
{
	uint16_t l;

	AN(vsb);

	if (l_authority == 0)
		return;
	AN(authority);
	AN(*authority);

	VSB_putc(vsb, PP2_TYPE_AUTHORITY);
	vbe16enc(&l, l_authority);
	VSB_bcat(vsb, &l, sizeof(l));
	VSB_cat(vsb, authority);
}

/* short path for stringified addresses from session attributes */
static void
vpx_format_proxy_v1(struct vsb *vsb, int proto,
    const char *cip,  const char *cport,
    const char *sip,  const char *sport)
{
	AN(vsb);
	AN(cip);
	AN(cport);
	AN(sip);
	AN(sport);

	VSB_bcat(vsb, vpx1_sig, sizeof(vpx1_sig));

	if (proto == PF_INET6)
		VSB_cat(vsb, " TCP6 ");
	else if (proto == PF_INET)
		VSB_cat(vsb, " TCP4 ");
	else
		WRONG("Wrong proxy v1 proto");

	VSB_printf(vsb, "%s %s %s %s\r\n", cip, sip, cport, sport);

	AZ(VSB_finish(vsb));
}

static void
vpx_format_proxy_v2(struct vsb *vsb, int proto,
    const struct suckaddr *sac, const struct suckaddr *sas,
    const char *authority)
{
	size_t l_authority = 0;
	uint16_t l_tlv = 0, l;

	AN(vsb);
	AN(sac);
	AN(sas);

	if (authority != NULL && *authority != '\0') {
		l_authority = strlen(authority);
		/* 3 bytes in the TLV before the authority string */
		assert(3 + l_authority <= UINT16_MAX);
		l_tlv = 3 + l_authority;
	}

	VSB_bcat(vsb, vpx2_sig, sizeof(vpx2_sig));
	VSB_putc(vsb, 0x21);
	if (proto == PF_INET6) {
		VSB_putc(vsb, 0x21);
		vbe16enc(&l, 0x24 + l_tlv);
		VSB_bcat(vsb, &l, sizeof(l));
	} else if (proto == PF_INET) {
		VSB_putc(vsb, 0x11);
		vbe16enc(&l, 0x0c + l_tlv);
		VSB_bcat(vsb, &l, sizeof(l));
	} else {
		WRONG("Wrong proxy v2 proto");
	}
	vpx_enc_addr(vsb, proto, sac);
	vpx_enc_addr(vsb, proto, sas);
	vpx_enc_port(vsb, sac);
	vpx_enc_port(vsb, sas);
	vpx_enc_authority(vsb, authority, l_authority);
	AZ(VSB_finish(vsb));
}

void
VPX_Format_Proxy(struct vsb *vsb, int version,
    const struct suckaddr *sac, const struct suckaddr *sas,
    const char *authority)
{
	int proto;
	char hac[VTCP_ADDRBUFSIZE];
	char pac[VTCP_PORTBUFSIZE];
	char has[VTCP_ADDRBUFSIZE];
	char pas[VTCP_PORTBUFSIZE];

	AN(vsb);
	AN(sac);
	AN(sas);

	assert(version == 1 || version == 2);

	proto = VSA_Get_Proto(sas);
	assert(proto == VSA_Get_Proto(sac));

	if (version == 1) {
		VTCP_name(sac, hac, sizeof hac, pac, sizeof pac);
		VTCP_name(sas, has, sizeof has, pas, sizeof pas);
		vpx_format_proxy_v1(vsb, proto, hac, pac, has, pas);
	} else if (version == 2) {
		vpx_format_proxy_v2(vsb, proto, sac, sas, authority);
	} else
		WRONG("Wrong proxy version");
}

#define PXY_BUFSZ						\
	(sizeof(vpx1_sig) + 4 /* TCPx */ +			\
	2 * VTCP_ADDRBUFSIZE + 2 * VTCP_PORTBUFSIZE +		\
	6 /* spaces, CRLF */ + 16 /* safety */ )

int
VPX_Send_Proxy(int fd, int version, const struct sess *sp)
{
	struct vsb vsb[1], *vsb2;
	struct suckaddr *sac, *sas;
	char ha[VTCP_ADDRBUFSIZE];
	char pa[VTCP_PORTBUFSIZE];
	char buf[PXY_BUFSZ];
	int proto, r;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(version == 1 || version == 2);
	AN(VSB_init(vsb, buf, sizeof buf));

	AZ(SES_Get_server_addr(sp, &sas));
	AN(sas);
	proto = VSA_Get_Proto(sas);

	if (version == 1) {
		VTCP_name(sas, ha, sizeof ha, pa, sizeof pa);
		vpx_format_proxy_v1(vsb, proto,
		    SES_Get_String_Attr(sp, SA_CLIENT_IP),
		    SES_Get_String_Attr(sp, SA_CLIENT_PORT),
		    ha, pa);
	} else if (version == 2) {
		AZ(SES_Get_client_addr(sp, &sac));
		AN(sac);
		vpx_format_proxy_v2(vsb, proto, sac, sas, NULL);
	} else
		WRONG("Wrong proxy version");

	r = write(fd, VSB_data(vsb), VSB_len(vsb));
	VTCP_Assert(r);

	if (DO_DEBUG(DBG_PROXY_ERROR)) {
		errno = EINTR;
		return (-1);
	}

	if (!DO_DEBUG(DBG_PROTOCOL))
		return (r);

	vsb2 = VSB_new_auto();
	AN(vsb2);
	VSB_quote(vsb2, VSB_data(vsb), VSB_len(vsb),
	    version == 2 ? VSB_QUOTE_HEX : 0);
	AZ(VSB_finish(vsb2));
	VSL(SLT_Debug, NO_VXID, "PROXY_HDR %s", VSB_data(vsb2));
	VSB_destroy(&vsb2);
	VSB_fini(vsb);
	return (r);
}

#undef PXY_BUFSZ
