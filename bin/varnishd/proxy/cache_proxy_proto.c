/*-
 * Copyright (c) 2015 Varnish Software AS
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

#include "cache/cache.h"

#include <netinet/in.h>

#include <netdb.h>

#include "cache/cache_transport.h"

#include "vend.h"
#include "vsa.h"
#include "vtcp.h"

/**********************************************************************
 * PROXY 1 protocol
 */

static const char vpx1_sig[] = {'P', 'R', 'O', 'X', 'Y'};

static int
vpx_proto1(const struct worker *wrk, struct req *req)
{
	const char *fld[5];
	int i;
	char *p, *q;
	struct addrinfo hints, *res;
	struct suckaddr *sa;
	int pfam = -1;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);

	q = strchr(req->htc->rxbuf_b, '\r');
	if (q == NULL)
		return (-1);

	*q++ = '\0';
	/* Nuke the CRLF */
	if (*q != '\n')
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
		pfam = AF_INET;
	else if (!strcmp(fld[0], "TCP6"))
		pfam = AF_INET6;
	else {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: Wrong TCP[46] field");
		return (-1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	i = getaddrinfo(fld[1], fld[3], &hints, &res);
	if (i != 0) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: Cannot resolve source address (%s)",
		    gai_strerror(i));
		return (-1);
	}
	AZ(res->ai_next);
	if (res->ai_family != pfam) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: %s got wrong protocol (%d)",
		    fld[0], res->ai_family);
		freeaddrinfo(res);
		return (-1);
	}
	SES_Reserve_client_addr(req->sp, &sa);
	AN(VSA_Build(sa, res->ai_addr, res->ai_addrlen));
	SES_Set_String_Attr(req->sp, SA_CLIENT_IP, fld[1]);
	SES_Set_String_Attr(req->sp, SA_CLIENT_PORT, fld[3]);
	freeaddrinfo(res);

	i = getaddrinfo(fld[2], fld[4], &hints, &res);
	if (i != 0) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: Cannot resolve destination address (%s)",
		    gai_strerror(i));
		return (-1);
	}
	AZ(res->ai_next);
	if (res->ai_family != pfam) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY1: %s got wrong protocol (%d)",
		    fld[0], res->ai_family);
		freeaddrinfo(res);
		return (-1);
	}
	SES_Reserve_server_addr(req->sp, &sa);
	AN(VSA_Build(sa, res->ai_addr, res->ai_addrlen));
	freeaddrinfo(res);

	VSL(SLT_Proxy, req->sp->vxid, "1 %s %s %s %s",
	    fld[1], fld[3], fld[2], fld[4]);
	HTC_RxPipeline(req->htc, q);
	WS_Reset(req->htc->ws, 0);
	return (0);
}

/**********************************************************************
 * PROXY 2 protocol
 */

static const char vpx2_sig[] = {
	'\r', '\n', '\r', '\n', '\0', '\r', '\n',
	'Q', 'U', 'I', 'T', '\n',
};

static int
vpx_proto2(const struct worker *wrk, struct req *req)
{
	int l;
	const uint8_t *p;
	sa_family_t pfam = 0xff;
	struct sockaddr_in sin4;
	struct sockaddr_in6 sin6;
	struct suckaddr *sa = NULL;
	char ha[VTCP_ADDRBUFSIZE];
	char pa[VTCP_PORTBUFSIZE];
	char hb[VTCP_ADDRBUFSIZE];
	char pb[VTCP_PORTBUFSIZE];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);

	assert(req->htc->rxbuf_e - req->htc->rxbuf_b >= 16L);
	l = vbe16dec(req->htc->rxbuf_b + 14);
	assert(req->htc->rxbuf_e - req->htc->rxbuf_b >= 16L + l);
	HTC_RxPipeline(req->htc, req->htc->rxbuf_b + 16L + l);
	WS_Reset(req->ws, 0);
	p = (const void *)req->htc->rxbuf_b;

	/* Version @12 top half */
	if ((p[12] >> 4) != 2) {
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY2: bad version (%d)", p[12] >> 4);
		return (-1);
	}

	/* Command @12 bottom half */
	switch (p[12] & 0x0f) {
	case 0x0:
		/* Local connection from proxy, ignore addresses */
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
		if (l < 12) {
			VSL(SLT_ProxyGarbage, req->sp->vxid,
			    "PROXY2: Ignoring short IPv4 addresses (%d)", l);
			return (0);
		}
		break;
	case 0x21:
		/* IPv6|TCP */
		pfam = AF_INET6;
		if (l < 36) {
			VSL(SLT_ProxyGarbage, req->sp->vxid,
			    "PROXY2: Ignoring short IPv6 addresses (%d)", l);
			return (0);
		}
		break;
	default:
		/* Ignore proxy header */
		VSL(SLT_ProxyGarbage, req->sp->vxid,
		    "PROXY2: Ignoring unsupported protocol (0x%02x)", p[13]);
		return (0);
	}

	switch (pfam) {
	case AF_INET:
		memset(&sin4, 0, sizeof sin4);
		sin4.sin_family = pfam;

		/* dst/server */
		memcpy(&sin4.sin_addr, p + 20, 4);
		memcpy(&sin4.sin_port, p + 26, 2);
		SES_Reserve_server_addr(req->sp, &sa);
		AN(VSA_Build(sa, &sin4, sizeof sin4));
		VTCP_name(sa, ha, sizeof ha, pa, sizeof pa);

		/* src/client */
		memcpy(&sin4.sin_addr, p + 16, 4);
		memcpy(&sin4.sin_port, p + 24, 2);
		SES_Reserve_client_addr(req->sp, &sa);
		AN(VSA_Build(sa, &sin4, sizeof sin4));
		break;
	case AF_INET6:
		memset(&sin6, 0, sizeof sin6);
		sin6.sin6_family = pfam;

		/* dst/server */
		memcpy(&sin6.sin6_addr, p + 32, 16);
		memcpy(&sin6.sin6_port, p + 50, 2);
		SES_Reserve_server_addr(req->sp, &sa);
		AN(VSA_Build(sa, &sin6, sizeof sin6));
		VTCP_name(sa, ha, sizeof ha, pa, sizeof pa);

		/* src/client */
		memcpy(&sin6.sin6_addr, p + 16, 16);
		memcpy(&sin6.sin6_port, p + 48, 2);
		SES_Reserve_client_addr(req->sp, &sa);
		AN(VSA_Build(sa, &sin6, sizeof sin6));
		break;
	default:
		WRONG("Wrong pfam");
	}

	AN(sa);
	VTCP_name(sa, hb, sizeof hb, pb, sizeof pb);
	SES_Set_String_Attr(req->sp, SA_CLIENT_IP, hb);
	SES_Set_String_Attr(req->sp, SA_CLIENT_PORT, pb);

	VSL(SLT_Proxy, req->sp->vxid, "2 %s %s %s %s", hb, pb, ha, pa);
	return (0);
}

/**********************************************************************
 * HTC_Rx completion detector
 */

static enum htc_status_e __match_proto__(htc_complete_f)
vpx_complete(struct http_conn *htc)
{
	int i, l, j;
	char *p, *q;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);

	assert(htc->rxbuf_e >= htc->rxbuf_b);
	assert(htc->rxbuf_e <= htc->ws->r);

	l = htc->rxbuf_e - htc->rxbuf_b;
	p = htc->rxbuf_b;
	j = 0x3;
	for (i = 0; i < l; i++) {
		if (i < sizeof vpx1_sig && p[i] != vpx1_sig[i])
			j &= ~1;
		if (i < sizeof vpx2_sig && p[i] != vpx2_sig[i])
			j &= ~2;
		if (j == 0)
			return (HTC_S_JUNK);
		if (j == 1 && i == sizeof vpx1_sig) {
			assert (htc->rxbuf_e < htc->ws->r);
			*htc->rxbuf_e = '\0';
			q = strchr(p + i, '\n');
			if (q != NULL && (q - htc->rxbuf_b) > 107)
				return (HTC_S_OVERFLOW);
			if (q == NULL)
				return (HTC_S_MORE);
			return (HTC_S_COMPLETE);
		}
		if (j == 2 && i == sizeof vpx2_sig) {
			if (l < 16)
				return (HTC_S_MORE);
			j = vbe16dec(p + 14);
			if (l < 16 + j)
				return (HTC_S_MORE);
			return (HTC_S_COMPLETE);
		}
	}
	return (HTC_S_MORE);
}

static void __match_proto__(task_func_t)
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
	hs = HTC_RxStuff(req->htc, vpx_complete,
	    NULL, NULL, NAN, sp->t_idle + cache_param->timeout_idle,
	    1024);			// XXX ?
	if (hs != HTC_S_COMPLETE) {
		Req_Release(req);
		SES_Delete(sp, SC_RX_JUNK, NAN);
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

void
VPX_Send_Proxy(int fd, int version, const struct sess *sp)
{
	struct vsb *vsb, *vsb2;
	const char *p1, *p2;
	struct suckaddr *sac, *sas;
	char ha[VTCP_ADDRBUFSIZE];
	char pa[VTCP_PORTBUFSIZE];
	int proto;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(version == 1 || version == 2);
	vsb = VSB_new_auto();
	AN(vsb);

	AZ(SES_Get_server_addr(sp, &sas));
	AN(sas);
	proto = VSA_Get_Proto(sas);
	assert(proto == PF_INET6 || proto == PF_INET);

	if (version == 1) {
		VSB_bcat(vsb, vpx1_sig, sizeof(vpx1_sig));
		p1 = SES_Get_String_Attr(sp, SA_CLIENT_IP);
		AN(p1);
		p2 = SES_Get_String_Attr(sp, SA_CLIENT_PORT);
		AN(p2);
		VTCP_name(sas, ha, sizeof ha, pa, sizeof pa);
		if (proto == PF_INET6)
			VSB_printf(vsb, " TCP6 ");
		else if (proto == PF_INET)
			VSB_printf(vsb, " TCP4 ");
		VSB_printf(vsb, "%s %s %s %s\r\n", p1, ha, p2, pa);
	} else if (version == 2) {
		AZ(SES_Get_client_addr(sp, &sac));
		AN(sac);

		VSB_bcat(vsb, vpx2_sig, sizeof(vpx2_sig));
		VSB_putc(vsb, 0x21);
		if (proto == PF_INET6) {
			VSB_putc(vsb, 0x21);
			VSB_putc(vsb, 0x00);
			VSB_putc(vsb, 0x24);
		} else if (proto == PF_INET) {
			VSB_putc(vsb, 0x11);
			VSB_putc(vsb, 0x00);
			VSB_putc(vsb, 0x0c);
		}
		vpx_enc_addr(vsb, proto, sac);
		vpx_enc_addr(vsb, proto, sas);
		vpx_enc_port(vsb, sac);
		vpx_enc_port(vsb, sas);
	} else
		WRONG("Wrong proxy version");

	AZ(VSB_finish(vsb));
	(void)write(fd, VSB_data(vsb), VSB_len(vsb));
	vsb2 = VSB_new_auto();
	AN(vsb2);
	VSB_quote(vsb2, VSB_data(vsb), VSB_len(vsb),
	    version == 2 ? VSB_QUOTE_HEX : 0);
	AZ(VSB_finish(vsb2));
	VSL(SLT_Debug, 999, "PROXY_HDR %s", VSB_data(vsb2));
	VSB_delete(vsb);
	VSB_delete(vsb2);
}
