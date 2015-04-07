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

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#include "../cache/cache.h"

#include "vend.h"
#include "vsa.h"

static const char vpx2_sig[] = {
	'\r', '\n', '\r', '\n', '\0', '\r', '\n',
	'Q', 'U', 'I', 'T', '\n',
};

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
	int pfam = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	VSL(SLT_Debug, req->sp->fd, "PROXY1");

	q = strchr(req->htc->rxbuf_b, '\r');
	AN(q);

	*q++ = '\0';
	/* Nuke the CRNL */
	if (*q != '\n')
		return (-1);
	*q++ = '\0';

	/* Split the fields */
	p = req->htc->rxbuf_b;
	for (i = 0; i < 5; i++) {
		p = strchr(p, ' ');
		if (p == NULL) {
			VSLb(req->vsl, SLT_ProxyGarbage,
			    "PROXY1: Too few fields");
			return (-1);
		}
		*p++ = '\0';
		fld[i] = p;
	}

	if (strchr(p, ' ')) {
		VSLb(req->vsl, SLT_ProxyGarbage,
		    "PROXY1: Too many fields");
		return (-1);
	}

	VSL(SLT_Debug, req->sp->fd, "PROXY1 <%s> <%s> <%s> <%s> <%s>",
	    fld[0], fld[1], fld[2], fld[3], fld[4]);

	if (!strcmp(fld[0], "TCP4"))
		pfam = AF_INET;
	else if (!strcmp(fld[0], "TCP6"))
		pfam = AF_INET6;
	else {
		VSLb(req->vsl, SLT_ProxyGarbage,
		    "PROXY1: Wrong TCP[46] field");
		return (-1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	i = getaddrinfo(fld[1], fld[2], &hints, &res);
	if (i != 0) {
		VSLb(req->vsl, SLT_ProxyGarbage,
		    "PROXY1: Cannot resolve source address (%s)",
		    gai_strerror(i));
		return (-1);
	}
	AZ(res->ai_next);
	if (res->ai_family != pfam) {
		VSLb(req->vsl, SLT_ProxyGarbage,
		    "PROXY1: %s got wrong protocol (%d)",
		    fld[0], res->ai_family);
		freeaddrinfo(res);
		return (-1);
	}
	SES_Reserve_client_addr(req->sp, &sa);
	AN(VSA_Build(sa, res->ai_addr, res->ai_addrlen));
	SES_Set_String_Attr(req->sp, SA_CLIENT_IP, fld[1]);
	SES_Set_String_Attr(req->sp, SA_CLIENT_PORT, fld[2]);
	freeaddrinfo(res);

	i = getaddrinfo(fld[3], fld[4], &hints, &res);
	if (i != 0) {
		VSLb(req->vsl, SLT_ProxyGarbage,
		    "PROXY1: Cannot resolve destination address (%s)",
		    gai_strerror(i));
		return (-1);
	}
	AZ(res->ai_next);
	if (res->ai_family != pfam) {
		VSLb(req->vsl, SLT_ProxyGarbage,
		    "PROXY1: %s got wrong protocol (%d)",
		    fld[0], res->ai_family);
		freeaddrinfo(res);
		return (-1);
	}
	SES_Reserve_server_addr(req->sp, &sa);
	AN(VSA_Build(sa, res->ai_addr, res->ai_addrlen));
	freeaddrinfo(res);

	req->htc->pipeline_b = q;
	return (0);
}

static int
vpx_proto2(const struct worker *wrk, struct req *req)
{
	int l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	VSL(SLT_Debug, req->sp->fd, "PROXY2");

	assert(req->htc->rxbuf_e - req->htc->rxbuf_b >= 16);
	l = vbe16dec(req->htc->rxbuf_b + 14);
	req->htc->pipeline_b = req->htc->rxbuf_b + 16 + l;
	return (0);
}

static enum htc_status_e __match_proto__(htc_complete_f)
vpx_complete(struct http_conn *htc)
{
	int i, l, j;
	char *p;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AZ(htc->pipeline_b);
	AZ(htc->pipeline_e);

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
			if (strchr(p + i, '\n') == NULL)
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


void __match_proto__(task_func_t)
VPX_Proto_Sess(struct worker *wrk, void *priv)
{
	struct req *req;
	struct sess *sp;
	enum htc_status_e hs;
	char *p;
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, priv, REQ_MAGIC);
	sp = req->sp;

	/* Per specifiction */
	assert(sizeof vpx1_sig == 5);
	assert(sizeof vpx2_sig == 12);

	hs = SES_RxReq(wrk, req, vpx_complete);
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

	if (req->htc->rxbuf_e ==  req->htc->pipeline_b)
		req->htc->pipeline_b = NULL;
	else
		req->htc->pipeline_e = req->htc->rxbuf_e;
	WS_Release(req->htc->ws, 0);
	SES_RxReInit(req->htc);
	req->t_req = NAN;
	req->t_first = NAN;
	req->sp->sess_step = S_STP_H1NEWREQ;
	wrk->task.func = SES_Proto_Req;
	wrk->task.priv = req;
	return;
}
