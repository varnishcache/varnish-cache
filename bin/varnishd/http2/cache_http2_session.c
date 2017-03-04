/*-
 * Copyright (c) 2016 Varnish Software AS
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

#include <stdio.h>

#include "cache/cache_transport.h"
#include "cache/cache_filter.h"
#include "http2/cache_http2.h"

#include "vend.h"

static const char *
h2_settingname(enum h2setting h2f)
{

	switch(h2f) {
#define H2_SETTINGS(n,v,d) case H2S_##n: return #n;
#include "tbl/h2_settings.h"
#undef H2_SETTINGS
	default:
		return (NULL);
	}
}

static const char h2_resp_101[] =
	"HTTP/1.1 101 Switching Protocols\r\n"
	"Connection: Upgrade\r\n"
	"Upgrade: h2c\r\n"
	"\r\n";

static const char H2_prism[24] = {
	0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54,
	0x54, 0x50, 0x2f, 0x32, 0x2e, 0x30, 0x0d, 0x0a,
	0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a
};

static const uint8_t H2_settings[] = {
	0x00, 0x03,
	0x00, 0x00, 0x00, 0x64,
	0x00, 0x04,
	0x00, 0x00, 0xff, 0xff
};

/**********************************************************************
 * The h2_sess struct needs many of the same things as a request,
 * WS, VSL, HTC &c,  but rather than implement all that stuff over, we
 * grab an actual struct req, and mirror the relevant fields into
 * struct h2_sess.
 * To make things really incestuous, we allocate the h2_sess on
 * the WS of that "Session ReQuest".
 */

static struct h2_sess *
h2_new_sess(const struct worker *wrk, struct sess *sp, struct req *srq)
{
	uintptr_t *up;
	struct h2_sess *h2;

	if (SES_Get_xport_priv(sp, &up)) {
		/* Already reserved if we came via H1 */
		SES_Reserve_xport_priv(sp, &up);
		*up = 0;
	}
	if (*up == 0) {
		if (srq == NULL)
			srq = Req_New(wrk, sp);
		AN(srq);
		h2 = WS_Alloc(srq->ws, sizeof *h2);
		AN(h2);
		INIT_OBJ(h2, H2_SESS_MAGIC);
		h2->srq = srq;
		h2->htc = srq->htc;
		h2->ws = srq->ws;
		h2->vsl = srq->vsl;
		h2->vsl->wid = sp->vxid;
		h2->htc->rfd = &sp->fd;
		h2->sess = sp;
		VTAILQ_INIT(&h2->streams);
#define H2_SETTINGS(n,v,d)					\
		do {						\
			assert(v < H2_SETTINGS_N);		\
			h2->their_settings[v] = d;		\
			h2->our_settings[v] = d;		\
		} while (0);
#include "tbl/h2_settings.h"
#undef H2_SETTINGS

		/* XXX: Lacks a VHT_Fini counterpart. Will leak memory. */
		AZ(VHT_Init(h2->dectbl,
			h2->our_settings[H2S_HEADER_TABLE_SIZE]));

		SES_Reserve_xport_priv(sp, &up);
		*up = (uintptr_t)h2;
	}
	AN(up);
	CAST_OBJ_NOTNULL(h2, (void*)(*up), H2_SESS_MAGIC);
	return (h2);
}

/**********************************************************************
 */

static struct h2_req *
h2_new_req(const struct worker *wrk, struct h2_sess *h2,
    unsigned stream, struct req *req)
{
	struct h2_req *r2;

	Lck_AssertHeld(&h2->sess->mtx);
	if (req == NULL)
		req = Req_New(wrk, h2->sess);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	r2 = WS_Alloc(req->ws, sizeof *r2);
	AN(r2);
	INIT_OBJ(r2, H2_REQ_MAGIC);
	r2->state = H2_S_IDLE;
	r2->h2sess = h2;
	r2->stream = stream;
	r2->req = req;
	req->transport_priv = r2;
	// XXX: ordering ?
	VTAILQ_INSERT_TAIL(&h2->streams, r2, list);
	h2->refcnt++;
	return (r2);
}

static void
h2_del_req(struct worker *wrk, const struct h2_req *r2)
{
	struct h2_sess *h2;
	struct sess *sp;
	struct req *req;
	int r;

	h2 = r2->h2sess;
	sp = h2->sess;
	Lck_Lock(&sp->mtx);
	assert(h2->refcnt > 0);
	r = --h2->refcnt;
	/* XXX: PRIORITY reshuffle */
	VTAILQ_REMOVE(&h2->streams, r2, list);
	Lck_Unlock(&sp->mtx);
	Req_Cleanup(sp, wrk, r2->req);
	Req_Release(r2->req);
	if (r)
		return;

	/* All streams gone, including stream #0, clean up */
	req = h2->srq;
	Req_Cleanup(sp, wrk, req);
	Req_Release(req);
	SES_Delete(sp, SC_RX_JUNK, NAN);
}

/**********************************************************************
 * Update and VSL a single SETTING rx'ed from the other side
 * 'd' must point to six bytes.
 */

static void
h2_setting(struct h2_sess *h2, const uint8_t *d)
{
	uint16_t x;
	uint32_t y;
	const char *n;
	char nb[8];

	x = vbe16dec(d);
	y = vbe32dec(d + 2);
	n = h2_settingname((enum h2setting)x);
	if (n == NULL) {
		bprintf(nb, "0x%04x", x);
		n = nb;
	}
	VSLb(h2->vsl, SLT_Debug, "H2SETTING %s 0x%08x", n, y);
	if (x > 0 && x < H2_SETTINGS_N)
		h2->their_settings[x] = y;
}

/**********************************************************************
 * Incoming HEADERS, this is where the partys at...
 */

static void __match_proto__(task_func_t)
h2_do_req(struct worker *wrk, void *priv)
{
	struct req *req;
	struct h2_req *r2;

	CAST_OBJ_NOTNULL(req, priv, REQ_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	THR_SetRequest(req);
	if (!CNT_GotReq(wrk, req))
		assert(CNT_Request(wrk, req) != REQ_FSM_DISEMBARK);
	THR_SetRequest(NULL);
	VSL(SLT_Debug, 0, "H2REQ CNT done");
	/* XXX clean up req */
	r2->state = H2_S_CLOSED;
	h2_del_req(wrk, r2);
}

/**********************************************************************/

enum htc_status_e __match_proto__(htc_complete_f)
H2_prism_complete(struct http_conn *htc)
{
	int l;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	l = htc->rxbuf_e - htc->rxbuf_b;
	if (l >= sizeof(H2_prism) &&
	    !memcmp(htc->rxbuf_b, H2_prism, sizeof(H2_prism)))
		return (HTC_S_COMPLETE);
	if (l < sizeof(H2_prism) && !memcmp(htc->rxbuf_b, H2_prism, l))
		return (HTC_S_MORE);
	return (HTC_S_JUNK);
}


/**********************************************************************
 * Deal with the base64url (NB: ...url!) encoded SETTINGS in the H1 req
 * of a H2C upgrade.
 */

static int
h2_b64url_settings(struct h2_sess *h2, struct req *req)
{
	const char *p, *q;
	uint8_t u[6], *up;
	unsigned x;
	int i, n;
	static const char s[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz"
	    "0123456789"
	    "-_=";

	/*
	 * If there is trouble with this, we could reject the upgrade
	 * but putting this on the H1 side is just plain wrong...
	 */
	AN(http_GetHdr(req->http, H_HTTP2_Settings, &p));
	if (p == NULL)
		return (-1);
	VSLb(req->vsl, SLT_Debug, "H2CS %s", p);

	n = 0;
	x = 0;
	up = u;
	for (;*p; p++) {
		q = strchr(s, *p);
		if (q == NULL)
			return (-1);
		i = q - s;
		assert(i >= 0 && i <= 63);
		x <<= 6;
		x |= i;
		n += 6;
		if (n < 8)
			continue;
		*up++ = (uint8_t)(x >> (n - 8));
		n -= 8;
		if (up == u + sizeof u) {
			AZ(n);
			h2_setting(h2, (void*)u);
			up = u;
		}
	}
	if (up != u)
		return (-1);
	return (0);
}

/**********************************************************************/

static int
h2_new_pu_session(struct worker *wrk, const struct h2_sess *h2)
{
	enum htc_status_e hs;

	(void)wrk;

	hs = H2_prism_complete(h2->htc);
	if (hs == HTC_S_MORE) {
		VSLb(h2->vsl, SLT_Debug, "Short pu PRISM");
		return (0);
	}
	if (hs != HTC_S_COMPLETE) {
		VSLb(h2->vsl, SLT_Debug, "Wrong pu PRISM");
		return (0);
	}
	HTC_RxPipeline(h2->htc, h2->htc->rxbuf_b + sizeof(H2_prism));
	HTC_RxInit(h2->htc, wrk->aws);

	VSLb(h2->vsl, SLT_Debug, "H2: Got pu PRISM");
	return (1);
}

/**********************************************************************/

static int
h2_new_ou_session(struct worker *wrk, struct h2_sess *h2,
    struct req *req)
{
	ssize_t sz;
	enum htc_status_e hs;

	sz = write(h2->sess->fd, h2_resp_101, strlen(h2_resp_101));
	assert(sz == strlen(h2_resp_101));

	AZ(h2_b64url_settings(h2, req));
	http_Unset(req->http, H_Upgrade);
	http_Unset(req->http, H_HTTP2_Settings);

	/* Steal pipelined read-ahead, if any */
	h2->htc->pipeline_b = req->htc->pipeline_b;
	h2->htc->pipeline_e = req->htc->pipeline_e;
	req->htc->pipeline_b = NULL;
	req->htc->pipeline_e = NULL;
	/* XXX: This call may assert on buffer overflow if the pipelined
	   data exceeds the available space in the aws workspace. What to
	   do about the overflowing data is an open issue. */
	HTC_RxInit(h2->htc, wrk->aws);

	/* Start req thread */
	(void)h2_new_req(wrk, h2, 1, req);
	req->req_step = R_STP_RECV;
	req->transport = &H2_transport;
	req->task.func = h2_do_req;
	req->task.priv = req;
	req->err_code = 0;
	http_SetH(req->http, HTTP_HDR_PROTO, "HTTP/2.0");
	XXXAZ(Pool_Task(wrk->pool, &req->task, TASK_QUEUE_REQ));

	/* Wait for PRISM response */
	hs = HTC_RxStuff(h2->htc, H2_prism_complete,
	    NULL, NULL, NAN, h2->sess->t_idle + cache_param->timeout_idle, 256);
	if (hs != HTC_S_COMPLETE) {
		/* XXX clean up req thread */
		VSLb(h2->vsl, SLT_Debug, "H2: No OU PRISM (hs=%d)", hs);
		Req_Release(req);
		Lck_Unlock(&h2->sess->mtx);
		SES_Delete(h2->sess, SC_RX_JUNK, NAN);
		return (0);
	}
	HTC_RxPipeline(h2->htc, h2->htc->rxbuf_b + sizeof(H2_prism));
	HTC_RxInit(h2->htc, wrk->aws);
	VSLb(h2->vsl, SLT_Debug, "H2: Got PRISM");
	return (1);
}

static void __match_proto__(task_func_t)
h2_new_session(struct worker *wrk, void *arg)
{
	struct req *req;
	struct sess *sp;
	struct h2_sess *h2;
	struct h2_req *r2, *r22;
	uintptr_t wsp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	assert(req->transport == &H2_transport);

	wsp = WS_Snapshot(wrk->aws);

	switch(req->err_code) {
	case 0:
		/* Direct H2 connection (via Proxy) */
		h2 = h2_new_sess(wrk, sp, req);
		Lck_Lock(&h2->sess->mtx);
		(void)h2_new_req(wrk, h2, 0, NULL);
		break;
	case 1:
		/* Prior Knowledge H1->H2 upgrade */
		h2 = h2_new_sess(wrk, sp, req);
		Lck_Lock(&h2->sess->mtx);
		(void)h2_new_req(wrk, h2, 0, NULL);

		if (!h2_new_pu_session(wrk, h2))
			return;
		break;
	case 2:
		/* Optimistic H1->H2 upgrade */
		h2 = h2_new_sess(wrk, sp, NULL);
		Lck_Lock(&h2->sess->mtx);
		(void)h2_new_req(wrk, h2, 0, NULL);

		if (!h2_new_ou_session(wrk, h2, req))
			return;
		break;
	default:
		WRONG("Bad req->err_code");
	}

	THR_SetRequest(h2->srq);

	H2_Send_Frame(wrk, h2,
	    H2_FRAME_SETTINGS, H2FF_NONE, sizeof H2_settings, 0, H2_settings);

	/* and off we go... */
	h2->cond = &wrk->cond;
	Lck_Unlock(&h2->sess->mtx);

	while (h2_rxframe(wrk, h2)) {
		WS_Reset(wrk->aws, wsp);
		HTC_RxInit(h2->htc, wrk->aws);
	}

	/* Delete all idle streams */
	VTAILQ_FOREACH_SAFE(r2, &h2->streams, list, r22) {
		if (r2->state == H2_S_IDLE)
			h2_del_req(wrk, r2);
	}
	h2->cond = NULL;
}

struct transport H2_transport = {
	.name =			"H2",
	.magic =		TRANSPORT_MAGIC,
	.new_session =		h2_new_session,
	.sess_panic =		h2_sess_panic,
	.deliver =		h2_deliver,
	.req_body =		h2_req_body,
	.minimal_response =	h2_minimal_response,
};
