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

#include "cache/cache_varnishd.h"

#include <errno.h>
#include <stdio.h>

#include "cache/cache_transport.h"
#include "http2/cache_http2.h"

#include "vend.h"
#include "vtim.h"

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

static size_t
h2_enc_settings(const struct h2_settings *h2s, uint8_t *buf, size_t n)
{
	uint8_t *b;
	size_t len = 0;

	b = buf;
#define H2_SETTING(U,l,v,d,...)				\
	if (h2s->l != d) {				\
		len += 6;				\
		assert(len <= n);			\
		vbe16enc(b, v);				\
		b += 2;					\
		vbe32enc(b, h2s->l);			\
		b += 4;					\
	}
#include "tbl/h2_settings.h"
	return (len);
}

static const struct h2_settings H2_proto_settings = {
#define H2_SETTING(U,l,v,d,...) . l = d,
#include "tbl/h2_settings.h"
};

static void
h2_local_settings(struct h2_settings *h2s)
{
	*h2s = H2_proto_settings;
#define H2_SETTINGS_PARAM_ONLY
#define H2_SETTING(U, l, ...)			\
	h2s->l = cache_param->h2_##l;
#include "tbl/h2_settings.h"
#undef H2_SETTINGS_PARAM_ONLY
}

/**********************************************************************
 * The h2_sess struct needs many of the same things as a request,
 * WS, VSL, HTC &c,  but rather than implement all that stuff over, we
 * grab an actual struct req, and mirror the relevant fields into
 * struct h2_sess.
 */

static struct h2_sess *
h2_init_sess(const struct worker *wrk, struct sess *sp,
    struct h2_sess *h2s, struct req *srq)
{
	uintptr_t *up;
	struct h2_sess *h2;

	if (SES_Get_proto_priv(sp, &up)) {
		/* Already reserved if we came via H1 */
		SES_Reserve_proto_priv(sp, &up);
		*up = 0;
	}
	if (*up == 0) {
		if (srq == NULL)
			srq = Req_New(wrk, sp);
		AN(srq);
		h2 = h2s;
		AN(h2);
		INIT_OBJ(h2, H2_SESS_MAGIC);
		h2->srq = srq;
		h2->htc = srq->htc;
		h2->ws = srq->ws;
		h2->vsl = srq->vsl;
		VSL_Flush(h2->vsl, 0);
		h2->vsl->wid = sp->vxid;
		h2->htc->rfd = &sp->fd;
		h2->sess = sp;
		h2->rxthr = pthread_self();
		VTAILQ_INIT(&h2->streams);
		VTAILQ_INIT(&h2->txqueue);
		h2_local_settings(&h2->local_settings);
		h2->remote_settings = H2_proto_settings;

		AZ(VHT_Init(h2->dectbl,
			h2->local_settings.header_table_size));

		SES_Reserve_proto_priv(sp, &up);
		*up = (uintptr_t)h2;
	}
	AN(up);
	CAST_OBJ_NOTNULL(h2, (void*)(*up), H2_SESS_MAGIC);
	return (h2);
}

static void
h2_del_sess(struct worker *wrk, struct h2_sess *h2, enum sess_close reason)
{
	struct sess *sp;
	struct req *req;

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	AZ(h2->refcnt);
	assert(VTAILQ_EMPTY(&h2->streams));

	VHT_Fini(h2->dectbl);
	req = h2->srq;
	AZ(req->ws->r);
	sp = h2->sess;
	Req_Cleanup(sp, wrk, req);
	Req_Release(req);
	SES_Delete(sp, reason, NAN);
}

/**********************************************************************/

enum htc_status_e v_matchproto_(htc_complete_f)
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
	if (!http_GetHdr(req->http, H_HTTP2_Settings, &p))
		return (-1);
	AN(p);
	VSLb(req->vsl, SLT_Debug, "H2CS %s", p);

	n = 0;
	x = 0;
	up = u;
	for (;*p; p++) {
		q = strchr(s, *p);
		if (q == NULL)
			return (-1);
		i = q - s;
		assert(i >= 0 && i <= 64);
		x <<= 6;
		x |= i;
		n += 6;
		if (n < 8)
			continue;
		*up++ = (uint8_t)(x >> (n - 8));
		n -= 8;
		if (up == u + sizeof u) {
			AZ(n);
			if (h2_set_setting(h2, (void*)u))
				return (-1);
			up = u;
		}
	}
	if (up != u)
		return (-1);
	return (0);
}


/**********************************************************************/

static int
h2_ou_rel(struct worker *wrk, struct req *req)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN (req->vcl);
	VCL_Rel(&req->vcl);
	Req_AcctLogCharge(wrk->stats, req);
	Req_Release(req);
	return (0);
}

static int
h2_ou_session(struct worker *wrk, struct h2_sess *h2,
    struct req *req)
{
	ssize_t sz;
	enum htc_status_e hs;
	struct h2_req *r2;

	if (h2_b64url_settings(h2, req)) {
		VSLb(h2->vsl, SLT_Debug, "H2: Bad HTTP-Settings");
		return (h2_ou_rel(wrk, req));
	}

	sz = write(h2->sess->fd, h2_resp_101, strlen(h2_resp_101));
	if (sz != strlen(h2_resp_101)) {
		VSLb(h2->vsl, SLT_Debug, "H2: Upgrade: Error writing 101"
		    " response: %s\n", strerror(errno));
		return (h2_ou_rel(wrk, req));
	}

	http_Unset(req->http, H_Upgrade);
	http_Unset(req->http, H_HTTP2_Settings);

	/* Steal pipelined read-ahead, if any */
	h2->htc->pipeline_b = req->htc->pipeline_b;
	h2->htc->pipeline_e = req->htc->pipeline_e;
	req->htc->pipeline_b = NULL;
	req->htc->pipeline_e = NULL;
	/* XXX: This call may assert on buffer overflow if the pipelined
	   data exceeds the available space in the ws workspace. What to
	   do about the overflowing data is an open issue. */
	HTC_RxInit(h2->htc, h2->ws);

	/* Start req thread */
	r2 = h2_new_req(wrk, h2, 1, req);
	req->transport = &H2_transport;
	req->req_step = R_STP_TRANSPORT;
	req->task.func = h2_do_req;
	req->task.priv = req;
	r2->scheduled = 1;
	req->err_code = 0;
	http_SetH(req->http, HTTP_HDR_PROTO, "HTTP/2.0");

	/* Wait for PRISM response */
	hs = HTC_RxStuff(h2->htc, H2_prism_complete,
	    NULL, NULL, NAN, h2->sess->t_idle + cache_param->timeout_idle,
	    sizeof H2_prism);
	if (hs != HTC_S_COMPLETE) {
		VSLb(h2->vsl, SLT_Debug, "H2: No/Bad OU PRISM (hs=%d)", hs);
		r2->scheduled = 0;
		h2_del_req(wrk, r2);
		return (0);
	}
	XXXAZ(Pool_Task(wrk->pool, &req->task, TASK_QUEUE_REQ));
	return (1);
}

/**********************************************************************
 */

#define H2_PU_MARKER	1
#define H2_OU_MARKER	2

void
H2_PU_Sess(struct worker *wrk, struct sess *sp, struct req *req)
{
	VSLb(req->vsl, SLT_Debug, "H2 Prior Knowledge Upgrade");
	req->err_code = H2_PU_MARKER;
	SES_SetTransport(wrk, sp, req, &H2_transport);
}

void
H2_OU_Sess(struct worker *wrk, struct sess *sp, struct req *req)
{
	VSLb(req->vsl, SLT_Debug, "H2 Optimistic Upgrade");
	req->err_code = H2_OU_MARKER;
	SES_SetTransport(wrk, sp, req, &H2_transport);
}

static void v_matchproto_(task_func_t)
h2_new_session(struct worker *wrk, void *arg)
{
	struct req *req;
	struct sess *sp;
	struct h2_sess h2s;
	struct h2_sess *h2;
	struct h2_req *r2, *r22;
	int again;
	uint8_t settings[48];
	size_t l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	assert(req->transport == &H2_transport);

	assert (req->err_code == H2_PU_MARKER || req->err_code == H2_OU_MARKER);

	h2 = h2_init_sess(wrk, sp, &h2s,
	    req->err_code == H2_PU_MARKER ? req : NULL);
	h2->req0 = h2_new_req(wrk, h2, 0, NULL);
	AZ(h2->htc->priv);
	h2->htc->priv = h2;

	if (req->err_code == H2_OU_MARKER && !h2_ou_session(wrk, h2, req)) {
		assert(h2->refcnt == 1);
		h2_del_req(wrk, h2->req0);
		h2_del_sess(wrk, h2, SC_RX_JUNK);
		return;
	}
	assert(HTC_S_COMPLETE == H2_prism_complete(h2->htc));
	HTC_RxPipeline(h2->htc, h2->htc->rxbuf_b + sizeof(H2_prism));
	WS_Reset(h2->ws, 0);
	HTC_RxInit(h2->htc, h2->ws);
	AN(h2->ws->r);
	VSLb(h2->vsl, SLT_Debug, "H2: Got pu PRISM");

	THR_SetRequest(h2->srq);
	AN(h2->ws->r);

	l = h2_enc_settings(&h2->local_settings, settings, sizeof (settings));
	AN(h2->ws->r);
	H2_Send_Get(wrk, h2, h2->req0);
	AN(h2->ws->r);
	H2_Send_Frame(wrk, h2,
	    H2_F_SETTINGS, H2FF_NONE, l, 0, settings);
	AN(h2->ws->r);
	H2_Send_Rel(h2, h2->req0);
	AN(h2->ws->r);

	/* and off we go... */
	h2->cond = &wrk->cond;

	while (h2_rxframe(wrk, h2)) {
		WS_Reset(h2->ws, 0);
		HTC_RxInit(h2->htc, h2->ws);
		if (WS_Overflowed(h2->ws)) {
			VSLb(h2->vsl, SLT_Debug, "H2: Empty Rx Workspace");
			h2->error = H2CE_INTERNAL_ERROR;
			break;
		}
		AN(h2->ws->r);
	}

	AN(h2->error);

	/* Delete all idle streams */
	VSLb(h2->vsl, SLT_Debug, "H2 CLEANUP %s", h2->error->name);
	Lck_Lock(&h2->sess->mtx);
	VTAILQ_FOREACH(r2, &h2->streams, list) {
		if (r2->error == 0)
			r2->error = h2->error;
		if (r2->cond != NULL)
			AZ(pthread_cond_signal(r2->cond));
	}
	AZ(pthread_cond_broadcast(h2->cond));
	Lck_Unlock(&h2->sess->mtx);
	while (1) {
		again = 0;
		VTAILQ_FOREACH_SAFE(r2, &h2->streams, list, r22) {
			if (r2 != h2->req0) {
				h2_kill_req(wrk, h2, r2, h2->error);
				again++;
			}
		}
		if (!again)
			break;
		Lck_Lock(&h2->sess->mtx);
		VTAILQ_FOREACH(r2, &h2->streams, list)
			VSLb(h2->vsl, SLT_Debug, "ST %u %d",
			    r2->stream, r2->state);
		(void)Lck_CondWait(h2->cond, &h2->sess->mtx, VTIM_real() + .1);
		Lck_Unlock(&h2->sess->mtx);
	}
	h2->cond = NULL;
	assert(h2->refcnt == 1);
	h2_del_req(wrk, h2->req0);
	/* TODO: proper sess close reason */
	h2_del_sess(wrk, h2, SC_RX_JUNK);
}

static void v_matchproto_(vtr_reembark_f)
h2_reembark(struct worker *wrk, struct req *req)
{
	struct sess *sp;
	struct h2_req *r2;

	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(req->transport == &H2_transport);

	if (!DO_DEBUG(DBG_FAILRESCHED) &&
	    !SES_Reschedule_Req(req, TASK_QUEUE_STR))
		return;

	/* Couldn't schedule, ditch */
	wrk->stats->busy_wakeup--;
	wrk->stats->busy_killed++;
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	VSLb(req->vsl, SLT_Error, "Fail to reschedule req from waiting list");
	h2_cleanup_waiting(wrk, r2);
}


struct transport H2_transport = {
	.name =			"H2",
	.magic =		TRANSPORT_MAGIC,
	.deliver =		h2_deliver,
	.minimal_response =	h2_minimal_response,
	.new_session =		h2_new_session,
	.reembark =		h2_reembark,
	.req_body =		h2_req_body,
	.req_fail =		h2_req_fail,
	.sess_panic =		h2_sess_panic,
};
