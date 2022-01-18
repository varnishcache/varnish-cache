/*-
 * Copyright (c) 2016-2019 Varnish Software AS
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
 */

#include "config.h"

#include "cache/cache_varnishd.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache/cache_transport.h"
#include "cache/cache_filter.h"
#include "http2/cache_http2.h"
#include "cache/cache_objhead.h"
#include "storage/storage.h"

#include "vend.h"
#include "vtcp.h"
#include "vtim.h"

#define H2EC1(U,v,r,d) const struct h2_error_s H2CE_##U[1] = {{#U,d,v,0,1,r}};
#define H2EC2(U,v,r,d) const struct h2_error_s H2SE_##U[1] = {{#U,d,v,1,0,r}};
#define H2EC3(U,v,r,d) H2EC1(U,v,r,d) H2EC2(U,v,r,d)
#define H2_ERROR(NAME, val, sc, reason, desc) H2EC##sc(NAME, val, reason, desc)
#include "tbl/h2_error.h"
#undef H2EC1
#undef H2EC2
#undef H2EC3

static const struct h2_error_s H2NN_ERROR[1] = {{
	"UNKNOWN_ERROR",
	"Unknown error number",
	0xffffffff,
	1,
	1,
	SC_RX_JUNK
}};

enum h2frame {
#define H2_FRAME(l,u,t,f,...)	H2F_##u = t,
#include "tbl/h2_frames.h"
};

static const char *
h2_framename(enum h2frame h2f)
{

	switch (h2f) {
#define H2_FRAME(l,u,t,f,...)	case H2F_##u: return (#u);
#include "tbl/h2_frames.h"
	default:
		return (NULL);
	}
}

#define H2_FRAME_FLAGS(l,u,v)	const uint8_t H2FF_##u = v;
#include "tbl/h2_frames.h"

/**********************************************************************
 */

static const h2_error stream_errors[] = {
#define H2EC1(U,v,r,d)
#define H2EC2(U,v,r,d) [v] = H2SE_##U,
#define H2EC3(U,v,r,d) H2EC1(U,v,r,d) H2EC2(U,v,r,d)
#define H2_ERROR(NAME, val, sc, reason, desc) H2EC##sc(NAME, val, reason, desc)
#include "tbl/h2_error.h"
#undef H2EC1
#undef H2EC2
#undef H2EC3
};

#define NSTREAMERRORS (sizeof(stream_errors)/sizeof(stream_errors[0]))

static h2_error
h2_streamerror(uint32_t u)
{
	if (u < NSTREAMERRORS && stream_errors[u] != NULL)
		return (stream_errors[u]);
	else
		return (H2NN_ERROR);
}

/**********************************************************************
 */

static const h2_error conn_errors[] = {
#define H2EC1(U,v,r,d) [v] = H2CE_##U,
#define H2EC2(U,v,r,d)
#define H2EC3(U,v,r,d) H2EC1(U,v,r,d) H2EC2(U,v,r,d)
#define H2_ERROR(NAME, val, sc, reason, desc) H2EC##sc(NAME, val, reason, desc)
#include "tbl/h2_error.h"
#undef H2EC1
#undef H2EC2
#undef H2EC3
};

#define NCONNERRORS (sizeof(conn_errors)/sizeof(conn_errors[0]))

static h2_error
h2_connectionerror(uint32_t u)
{
	if (u < NCONNERRORS && conn_errors[u] != NULL)
		return (conn_errors[u]);
	else
		return (H2NN_ERROR);
}

/**********************************************************************/

struct h2_req *
h2_new_req(struct h2_sess *h2, unsigned stream, struct req *req)
{
	struct h2_req *r2;

	ASSERT_RXTHR(h2);
	if (req == NULL)
		req = Req_New(h2->sess);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	r2 = WS_Alloc(req->ws, sizeof *r2);
	AN(r2);
	INIT_OBJ(r2, H2_REQ_MAGIC);
	r2->state = H2_S_IDLE;
	r2->h2sess = h2;
	r2->stream = stream;
	r2->req = req;
	if (stream)
		r2->counted = 1;
	r2->r_window = h2->local_settings.initial_window_size;
	r2->t_window = h2->remote_settings.initial_window_size;
	req->transport_priv = r2;
	Lck_Lock(&h2->sess->mtx);
	if (stream)
		h2->open_streams++;
	VTAILQ_INSERT_TAIL(&h2->streams, r2, list);
	Lck_Unlock(&h2->sess->mtx);
	h2->refcnt++;
	return (r2);
}

void
h2_del_req(struct worker *wrk, struct h2_req *r2)
{
	struct h2_sess *h2;
	struct sess *sp;
	struct stv_buffer *stvbuf;

	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	AZ(r2->scheduled);
	h2 = r2->h2sess;
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	ASSERT_RXTHR(h2);
	sp = h2->sess;
	Lck_Lock(&sp->mtx);
	assert(h2->refcnt > 0);
	--h2->refcnt;
	/* XXX: PRIORITY reshuffle */
	VTAILQ_REMOVE(&h2->streams, r2, list);
	Lck_Unlock(&sp->mtx);

	assert(!WS_IsReserved(r2->req->ws));
	AZ(r2->req->ws->r);

	CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);
	if (r2->rxbuf) {
		stvbuf = r2->rxbuf->stvbuf;
		r2->rxbuf = NULL;
		STV_FreeBuf(wrk, &stvbuf);
		AZ(stvbuf);
	}

	Req_Cleanup(sp, wrk, r2->req);
	Req_Release(r2->req);
}

void
h2_kill_req(struct worker *wrk, struct h2_sess *h2,
    struct h2_req *r2, h2_error h2e)
{

	ASSERT_RXTHR(h2);
	AN(h2e);
	Lck_Lock(&h2->sess->mtx);
	VSLb(h2->vsl, SLT_Debug, "KILL st=%u state=%d sched=%d",
	    r2->stream, r2->state, r2->scheduled);
	if (r2->counted) {
		assert(h2->open_streams > 0);
		h2->open_streams--;
		r2->counted = 0;
	}
	if (r2->error == NULL)
		r2->error = h2e;
	if (r2->scheduled) {
		if (r2->cond != NULL)
			PTOK(pthread_cond_signal(r2->cond));
		r2 = NULL;
	} else {
		if (r2->state == H2_S_OPEN && h2->new_req == r2->req)
			(void)h2h_decode_fini(h2);
	}
	Lck_Unlock(&h2->sess->mtx);
	if (r2 != NULL)
		h2_del_req(wrk, r2);
}

/**********************************************************************/

static void
h2_vsl_frame(const struct h2_sess *h2, const void *ptr, size_t len)
{
	const uint8_t *b;
	struct vsb *vsb;
	const char *p;
	unsigned u;

	if (VSL_tag_is_masked(SLT_H2RxHdr) &&
	    VSL_tag_is_masked(SLT_H2RxBody))
		return;

	AN(ptr);
	assert(len >= 9);
	b = ptr;

	vsb = VSB_new_auto();
	AN(vsb);
	p = h2_framename((enum h2frame)b[3]);
	if (p != NULL)
		VSB_cat(vsb, p);
	else
		VSB_quote(vsb, b + 3, 1, VSB_QUOTE_HEX);

	u = vbe32dec(b) >> 8;
	VSB_printf(vsb, "[%u] ", u);
	VSB_quote(vsb, b + 4, 1, VSB_QUOTE_HEX);
	VSB_putc(vsb, ' ');
	VSB_quote(vsb, b + 5, 4, VSB_QUOTE_HEX);
	if (u > 0) {
		VSB_putc(vsb, ' ');
		VSB_quote(vsb, b + 9, len - 9, VSB_QUOTE_HEX);
	}
	AZ(VSB_finish(vsb));
	Lck_Lock(&h2->sess->mtx);
	VSLb_bin(h2->vsl, SLT_H2RxHdr, 9, b);
	if (len > 9)
		VSLb_bin(h2->vsl, SLT_H2RxBody, len - 9, b + 9);

	VSLb(h2->vsl, SLT_Debug, "H2RXF %s", VSB_data(vsb));
	Lck_Unlock(&h2->sess->mtx);
	VSB_destroy(&vsb);
}


/**********************************************************************
 */

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_ping(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	assert(r2 == h2->req0);

	if (h2->rxf_len != 8)				// rfc7540,l,2364,2366
		return (H2CE_FRAME_SIZE_ERROR);
	AZ(h2->rxf_stream);				// rfc7540,l,2359,2362
	if (h2->rxf_flags != 0)				// We never send pings
		return (H2SE_PROTOCOL_ERROR);
	H2_Send_Get(wrk, h2, r2);
	H2_Send_Frame(wrk, h2,
	    H2_F_PING, H2FF_PING_ACK, 8, 0, h2->rxf_data);
	H2_Send_Rel(h2, r2);
	return (0);
}

/**********************************************************************
 */

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_push_promise(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_ORNULL(r2, H2_REQ_MAGIC);
	// rfc7540,l,2262,2267
	return (H2CE_PROTOCOL_ERROR);
}

/**********************************************************************
 */

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_rst_stream(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_ORNULL(r2, H2_REQ_MAGIC);

	if (h2->rxf_len != 4)			// rfc7540,l,2003,2004
		return (H2CE_FRAME_SIZE_ERROR);
	if (r2 == NULL)
		return (0);
	h2_kill_req(wrk, h2, r2, h2_streamerror(vbe32dec(h2->rxf_data)));
	return (0);
}

/**********************************************************************
 */

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_goaway(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	assert(r2 == h2->req0);

	h2->goaway = 1;
	h2->goaway_last_stream = vbe32dec(h2->rxf_data);
	h2->error = h2_connectionerror(vbe32dec(h2->rxf_data + 4));
	Lck_Lock(&h2->sess->mtx);
	VSLb(h2->vsl, SLT_Debug, "GOAWAY %s", h2->error->name);
	Lck_Unlock(&h2->sess->mtx);
	return (h2->error);
}

static void
h2_tx_goaway(struct worker *wrk, struct h2_sess *h2, h2_error h2e)
{
	char b[8];

	ASSERT_RXTHR(h2);
	AN(h2e);

	if (h2->goaway)
		return;

	h2->goaway = 1;
	vbe32enc(b, h2->highest_stream);
	vbe32enc(b + 4, h2e->val);
	H2_Send_Get(wrk, h2, h2->req0);
	H2_Send_Frame(wrk, h2, H2_F_GOAWAY, 0, 8, 0, b);
	H2_Send_Rel(h2, h2->req0);
}

/**********************************************************************
 */

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_window_update(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	uint32_t wu;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_ORNULL(r2, H2_REQ_MAGIC);

	if (h2->rxf_len != 4)
		return (H2CE_FRAME_SIZE_ERROR);
	wu = vbe32dec(h2->rxf_data) & ~(1LU<<31);
	if (wu == 0)
		return (H2SE_PROTOCOL_ERROR);
	if (r2 == NULL)
		return (0);
	Lck_Lock(&h2->sess->mtx);
	r2->t_window += wu;
	if (r2 == h2->req0)
		PTOK(pthread_cond_broadcast(h2->winupd_cond));
	else if (r2->cond != NULL)
		PTOK(pthread_cond_signal(r2->cond));
	Lck_Unlock(&h2->sess->mtx);
	if (r2->t_window >= (1LL << 31))
		return (H2SE_FLOW_CONTROL_ERROR);
	return (0);
}

/**********************************************************************
 * Incoming PRIORITY, possibly an ACK of one we sent.
 */

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_priority(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_ORNULL(r2, H2_REQ_MAGIC);
	return (0);
}

/**********************************************************************
 * Incoming SETTINGS, possibly an ACK of one we sent.
 */

#define H2_SETTING(U,l, ...)					\
static void v_matchproto_(h2_setsetting_f)			\
h2_setting_##l(struct h2_settings* s, uint32_t v)		\
{								\
	s -> l = v;						\
}
#include <tbl/h2_settings.h>

#define H2_SETTING(U, l, ...)					\
const struct h2_setting_s H2_SET_##U[1] = {{			\
	#l,							\
	h2_setting_##l,						\
	__VA_ARGS__						\
}};
#include <tbl/h2_settings.h>

static const struct h2_setting_s * const h2_setting_tbl[] = {
#define H2_SETTING(U,l,v, ...) [v] = H2_SET_##U,
#include <tbl/h2_settings.h>
};

#define H2_SETTING_TBL_LEN (sizeof(h2_setting_tbl)/sizeof(h2_setting_tbl[0]))

static void
h2_win_adjust(const struct h2_sess *h2, uint32_t oldval, uint32_t newval)
{
	struct h2_req *r2;

	Lck_AssertHeld(&h2->sess->mtx);
	// rfc7540,l,2668,2674
	VTAILQ_FOREACH(r2, &h2->streams, list) {
		CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
		if (r2 == h2->req0)
			continue; // rfc7540,l,2699,2699
		switch (r2->state) {
		case H2_S_IDLE:
		case H2_S_OPEN:
		case H2_S_CLOS_REM:
			/*
			 * We allow a window to go negative, as per
			 * rfc7540,l,2676,2680
			 */
			r2->t_window += (int64_t)newval - oldval;
			break;
		default:
			break;
		}
	}
}

h2_error
h2_set_setting(struct h2_sess *h2, const uint8_t *d)
{
	const struct h2_setting_s *s;
	uint16_t x;
	uint32_t y;

	x = vbe16dec(d);
	y = vbe32dec(d + 2);
	if (x >= H2_SETTING_TBL_LEN || h2_setting_tbl[x] == NULL) {
		// rfc7540,l,2181,2182
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug,
		    "H2SETTING unknown setting 0x%04x=%08x (ignored)", x, y);
		Lck_Unlock(&h2->sess->mtx);
		return (0);
	}
	s = h2_setting_tbl[x];
	AN(s);
	if (y < s->minval || y > s->maxval) {
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug, "H2SETTING invalid %s=0x%08x",
		    s->name, y);
		Lck_Unlock(&h2->sess->mtx);
		AN(s->range_error);
		if (!DO_DEBUG(DBG_H2_NOCHECK))
			return (s->range_error);
	}
	Lck_Lock(&h2->sess->mtx);
	if (s == H2_SET_INITIAL_WINDOW_SIZE)
		h2_win_adjust(h2, h2->remote_settings.initial_window_size, y);
	VSLb(h2->vsl, SLT_Debug, "H2SETTING %s=0x%08x", s->name, y);
	Lck_Unlock(&h2->sess->mtx);
	AN(s->setfunc);
	s->setfunc(&h2->remote_settings, y);
	return (0);
}

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_settings(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	const uint8_t *p;
	unsigned l;
	h2_error retval = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	assert(r2 == h2->req0);
	AZ(h2->rxf_stream);

	if (h2->rxf_flags == H2FF_SETTINGS_ACK) {
		if (h2->rxf_len > 0)			// rfc7540,l,2047,2049
			return (H2CE_FRAME_SIZE_ERROR);
		return (0);
	} else {
		if (h2->rxf_len % 6)			// rfc7540,l,2062,2064
			return (H2CE_PROTOCOL_ERROR);
		p = h2->rxf_data;
		for (l = h2->rxf_len; l >= 6; l -= 6, p += 6) {
			retval = h2_set_setting(h2, p);
			if (retval)
				return (retval);
		}
		H2_Send_Get(wrk, h2, r2);
		H2_Send_Frame(wrk, h2,
		    H2_F_SETTINGS, H2FF_SETTINGS_ACK, 0, 0, NULL);
		H2_Send_Rel(h2, r2);
	}
	return (0);
}

/**********************************************************************
 * Incoming HEADERS, this is where the partys at...
 */

void v_matchproto_(task_func_t)
h2_do_req(struct worker *wrk, void *priv)
{
	struct req *req;
	struct h2_req *r2;
	struct h2_sess *h2;

	CAST_OBJ_NOTNULL(req, priv, REQ_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	THR_SetRequest(req);
	CNT_Embark(wrk, req);

	if (CNT_Request(req) != REQ_FSM_DISEMBARK) {
		wrk->stats->client_req++;
		assert(!WS_IsReserved(req->ws));
		AZ(req->top->vcl0);
		h2 = r2->h2sess;
		CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
		Lck_Lock(&h2->sess->mtx);
		r2->scheduled = 0;
		r2->state = H2_S_CLOSED;
		r2->h2sess->do_sweep = 1;
		Lck_Unlock(&h2->sess->mtx);
	}
	THR_SetRequest(NULL);
}

static h2_error
h2_end_headers(struct worker *wrk, struct h2_sess *h2,
    struct req *req, struct h2_req *r2)
{
	h2_error h2e;
	ssize_t cl;

	ASSERT_RXTHR(h2);
	assert(r2->state == H2_S_OPEN);
	h2e = h2h_decode_fini(h2);
	h2->new_req = NULL;
	if (h2e != NULL) {
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug, "HPACK/FINI %s", h2e->name);
		Lck_Unlock(&h2->sess->mtx);
		assert(!WS_IsReserved(r2->req->ws));
		h2_del_req(wrk, r2);
		return (h2e);
	}
	VSLb_ts_req(req, "Req", req->t_req);

	// XXX: Smarter to do this already at HPACK time into tail end of
	// XXX: WS, then copy back once all headers received.
	// XXX: Have I mentioned H/2 Is hodge-podge ?
	http_CollectHdrSep(req->http, H_Cookie, "; ");	// rfc7540,l,3114,3120

	cl = http_GetContentLength(req->http);
	assert(cl >= -2);
	if (cl == -2) {
		VSLb(h2->vsl, SLT_Debug, "Non-parseable Content-Length");
		return (H2SE_PROTOCOL_ERROR);
	}

	if (req->req_body_status == NULL) {
		if (cl == -1)
			req->req_body_status = BS_EOF;
		else {
			/* Note: If cl==0 here, we still need to have
			 * req_body_status==BS_LENGTH, so that there will
			 * be a wait for the stream to reach H2_S_CLOS_REM
			 * while dealing with the request body. */
			req->req_body_status = BS_LENGTH;
		}
		/* Set req->htc->content_length because this is used as
		 * the hint in vrb_pull() for how large the storage
		 * buffers need to be */
		req->htc->content_length = cl;
	} else {
		/* A HEADER frame contained END_STREAM */
		assert (req->req_body_status == BS_NONE);
		r2->state = H2_S_CLOS_REM;
		if (cl > 0)
			return (H2CE_PROTOCOL_ERROR); //rfc7540,l,1838,1840
	}

	if (req->http->hd[HTTP_HDR_METHOD].b == NULL) {
		VSLb(h2->vsl, SLT_Debug, "Missing :method");
		return (H2SE_PROTOCOL_ERROR); //rfc7540,l,3087,3090
	}
	if (req->http->hd[HTTP_HDR_URL].b == NULL) {
		VSLb(h2->vsl, SLT_Debug, "Missing :path");
		return (H2SE_PROTOCOL_ERROR); //rfc7540,l,3087,3090
	}
	AN(req->http->hd[HTTP_HDR_PROTO].b);

	if (*req->http->hd[HTTP_HDR_URL].b == '*' &&
	    (Tlen(req->http->hd[HTTP_HDR_METHOD]) != 7 ||
	    strncmp(req->http->hd[HTTP_HDR_METHOD].b, "OPTIONS", 7))) {
		VSLb(h2->vsl, SLT_BogoHeader, "Illegal :path pseudo-header");
		return (H2SE_PROTOCOL_ERROR); //rfc7540,l,3068,3071
	}

	assert(req->req_step == R_STP_TRANSPORT);
	VCL_TaskEnter(req->privs);
	VCL_TaskEnter(req->top->privs);
	req->task->func = h2_do_req;
	req->task->priv = req;
	if (!cache_param->accept_traffic) {
		r2->state = H2_S_CLOSED;
		h2_tx_goaway(wrk, h2, H2SE_REFUSED_STREAM);
		return (H2SE_REFUSED_STREAM);
	}
	r2->scheduled = 1;
	if (Pool_Task(wrk->pool, req->task, TASK_QUEUE_STR) != 0) {
		r2->scheduled = 0;
		r2->state = H2_S_CLOSED;
		return (H2SE_REFUSED_STREAM); //rfc7540,l,3326,3329
	}
	return (0);
}

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_headers(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	struct req *req;
	h2_error h2e;
	const uint8_t *p;
	size_t l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);

	if (r2 == NULL) {
		if (h2->rxf_stream <= h2->highest_stream)
			return (H2CE_PROTOCOL_ERROR);	// rfc7540,l,1153,1158
		/* NB: we don't need to guard the read of h2->open_streams
		 * because headers are handled sequentially so it cannot
		 * increase under our feet.
		 */
		if (h2->open_streams >=
		    h2->local_settings.max_concurrent_streams) {
			VSLb(h2->vsl, SLT_Debug,
			     "H2: stream %u: Hit maximum number of "
			     "concurrent streams", h2->rxf_stream);
			return (H2SE_REFUSED_STREAM);	// rfc7540,l,1200,1205
		}
		h2->highest_stream = h2->rxf_stream;
		r2 = h2_new_req(h2, h2->rxf_stream, NULL);
	}
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);

	if (r2->state != H2_S_IDLE)
		return (H2CE_PROTOCOL_ERROR);	// XXX spec ?
	r2->state = H2_S_OPEN;

	req = r2->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	req->vsl->wid = VXID_Get(wrk, VSL_CLIENTMARKER);
	VSLb(req->vsl, SLT_Begin, "req %ju rxreq", VXID(req->sp->vxid));
	VSL(SLT_Link, req->sp->vxid, "req %ju rxreq", VXID(req->vsl->wid));

	h2->new_req = req;
	req->sp = h2->sess;
	req->transport = &HTTP2_transport;

	req->t_first = VTIM_real();
	req->t_req = VTIM_real();
	req->t_prev = req->t_first;
	VSLb_ts_req(req, "Start", req->t_first);
	req->acct.req_hdrbytes += h2->rxf_len;

	HTTP_Setup(req->http, req->ws, req->vsl, SLT_ReqMethod);
	http_SetH(req->http, HTTP_HDR_PROTO, "HTTP/2.0");

	h2h_decode_init(h2);

	p = h2->rxf_data;
	l = h2->rxf_len;
	if (h2->rxf_flags & H2FF_HEADERS_PADDED) {
		if (*p + 1 > l)
			return (H2CE_PROTOCOL_ERROR);	// rfc7540,l,1884,1887
		l -= 1 + *p;
		p += 1;
	}
	if (h2->rxf_flags & H2FF_HEADERS_PRIORITY) {
		if (l < 5)
			return (H2CE_PROTOCOL_ERROR);
		l -= 5;
		p += 5;
	}
	h2e = h2h_decode_bytes(h2, p, l);
	if (h2e != NULL) {
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug, "HPACK(hdr) %s", h2e->name);
		Lck_Unlock(&h2->sess->mtx);
		(void)h2h_decode_fini(h2);
		assert(!WS_IsReserved(r2->req->ws));
		h2_del_req(wrk, r2);
		return (h2e);
	}

	if (h2->rxf_flags & H2FF_HEADERS_END_STREAM)
		req->req_body_status = BS_NONE;

	if (h2->rxf_flags & H2FF_HEADERS_END_HEADERS)
		return (h2_end_headers(wrk, h2, req, r2));
	return (0);
}

/**********************************************************************/

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_continuation(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	struct req *req;
	h2_error h2e;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_ORNULL(r2, H2_REQ_MAGIC);

	if (r2 == NULL || r2->state != H2_S_OPEN || r2->req != h2->new_req)
		return (H2CE_PROTOCOL_ERROR);	// XXX spec ?
	req = r2->req;
	h2e = h2h_decode_bytes(h2, h2->rxf_data, h2->rxf_len);
	r2->req->acct.req_hdrbytes += h2->rxf_len;
	if (h2e != NULL) {
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug, "HPACK(cont) %s", h2e->name);
		Lck_Unlock(&h2->sess->mtx);
		(void)h2h_decode_fini(h2);
		assert(!WS_IsReserved(r2->req->ws));
		h2_del_req(wrk, r2);
		return (h2e);
	}
	if (h2->rxf_flags & H2FF_HEADERS_END_HEADERS)
		return (h2_end_headers(wrk, h2, req, r2));
	return (0);
}

/**********************************************************************/

static h2_error v_matchproto_(h2_rxframe_f)
h2_rx_data(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	char buf[4];
	ssize_t l;
	uint64_t l2, head;
	const uint8_t *src;
	unsigned len;

	/* XXX: Shouldn't error handling, setting of r2->error and
	 * r2->cond signalling be handled more generally at the end of
	 * procframe()??? */

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	ASSERT_RXTHR(h2);
	CHECK_OBJ_ORNULL(r2, H2_REQ_MAGIC);

	if (r2 == NULL)
		return (0);

	if (r2->state >= H2_S_CLOS_REM) {
		r2->error = H2SE_STREAM_CLOSED;
		return (H2SE_STREAM_CLOSED); // rfc7540,l,1766,1769
	}

	Lck_Lock(&h2->sess->mtx);
	CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);

	if (h2->error || r2->error) {
		if (r2->cond)
			PTOK(pthread_cond_signal(r2->cond));
		Lck_Unlock(&h2->sess->mtx);
		return (h2->error ? h2->error : r2->error);
	}

	/* Check padding if present */
	src = h2->rxf_data;
	len = h2->rxf_len;
	if (h2->rxf_flags & H2FF_DATA_PADDED) {
		if (*src >= len) {
			VSLb(h2->vsl, SLT_Debug,
			    "H2: stream %u: Padding larger than frame length",
			    h2->rxf_stream);
			r2->error = H2CE_PROTOCOL_ERROR;
			if (r2->cond)
				PTOK(pthread_cond_signal(r2->cond));
			Lck_Unlock(&h2->sess->mtx);
			return (H2CE_PROTOCOL_ERROR);
		}
		len -= 1 + *src;
		src += 1;
	}

	/* Check against the Content-Length header if given */
	if (r2->req->htc->content_length >= 0) {
		if (r2->rxbuf)
			l = r2->rxbuf->head;
		else
			l = 0;
		l += len;
		if (l > r2->req->htc->content_length ||
		    ((h2->rxf_flags & H2FF_DATA_END_STREAM) &&
		     l != r2->req->htc->content_length)) {
			VSLb(h2->vsl, SLT_Debug,
			    "H2: stream %u: Received data and Content-Length"
			    " mismatch", h2->rxf_stream);
			r2->error = H2SE_PROTOCOL_ERROR;
			if (r2->cond)
				PTOK(pthread_cond_signal(r2->cond));
			Lck_Unlock(&h2->sess->mtx);
			return (H2SE_PROTOCOL_ERROR);
		}
	}

	/* Check and charge connection window. The entire frame including
	 * padding (h2->rxf_len) counts towards the window. */
	if (h2->rxf_len > h2->req0->r_window) {
		VSLb(h2->vsl, SLT_Debug,
		    "H2: stream %u: Exceeded connection receive window",
		    h2->rxf_stream);
		r2->error = H2CE_FLOW_CONTROL_ERROR;
		if (r2->cond)
			PTOK(pthread_cond_signal(r2->cond));
		Lck_Unlock(&h2->sess->mtx);
		return (H2CE_FLOW_CONTROL_ERROR);
	}
	h2->req0->r_window -= h2->rxf_len;
	if (h2->req0->r_window < cache_param->h2_rx_window_low_water) {
		h2->req0->r_window += cache_param->h2_rx_window_increment;
		vbe32enc(buf, cache_param->h2_rx_window_increment);
		Lck_Unlock(&h2->sess->mtx);
		H2_Send_Get(wrk, h2, h2->req0);
		H2_Send_Frame(wrk, h2, H2_F_WINDOW_UPDATE, 0, 4, 0, buf);
		H2_Send_Rel(h2, h2->req0);
		Lck_Lock(&h2->sess->mtx);
	}

	/* Check stream window. The entire frame including padding
	 * (h2->rxf_len) counts towards the window. */
	if (h2->rxf_len > r2->r_window) {
		VSLb(h2->vsl, SLT_Debug,
		    "H2: stream %u: Exceeded stream receive window",
		    h2->rxf_stream);
		r2->error = H2SE_FLOW_CONTROL_ERROR;
		if (r2->cond)
			PTOK(pthread_cond_signal(r2->cond));
		Lck_Unlock(&h2->sess->mtx);
		return (H2SE_FLOW_CONTROL_ERROR);
	}

	/* Handle zero size frame before starting to allocate buffers */
	if (len == 0) {
		r2->r_window -= h2->rxf_len;

		/* Handle the specific corner case where the entire window
		 * has been exhausted using nothing but padding
		 * bytes. Since no bytes have been buffered, no bytes
		 * would be consumed by the request thread and no stream
		 * window updates sent. Unpaint ourselves from this corner
		 * by sending a stream window update here. */
		CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);
		if (r2->r_window == 0 &&
		    (r2->rxbuf == NULL || r2->rxbuf->tail == r2->rxbuf->head)) {
			if (r2->rxbuf)
				l = r2->rxbuf->size;
			else
				l = h2->local_settings.initial_window_size;
			r2->r_window += l;
			Lck_Unlock(&h2->sess->mtx);
			vbe32enc(buf, l);
			H2_Send_Get(wrk, h2, h2->req0);
			H2_Send_Frame(wrk, h2, H2_F_WINDOW_UPDATE, 0, 4,
			    r2->stream, buf);
			H2_Send_Rel(h2, h2->req0);
			Lck_Lock(&h2->sess->mtx);
		}

		if (h2->rxf_flags & H2FF_DATA_END_STREAM)
			r2->state = H2_S_CLOS_REM;
		if (r2->cond)
			PTOK(pthread_cond_signal(r2->cond));
		Lck_Unlock(&h2->sess->mtx);
		return (0);
	}

	/* Make the buffer on demand */
	if (r2->rxbuf == NULL) {
		unsigned bufsize;
		size_t bstest;
		struct stv_buffer *stvbuf;
		struct h2_rxbuf *rxbuf;

		Lck_Unlock(&h2->sess->mtx);

		bufsize = h2->local_settings.initial_window_size;
		if (bufsize < r2->r_window) {
			/* This will not happen because we do not have any
			 * mechanism to change the initial window size on
			 * a running session. But if we gain that ability,
			 * this future proofs it. */
			bufsize = r2->r_window;
		}
		assert(bufsize > 0);
		if ((h2->rxf_flags & H2FF_DATA_END_STREAM) &&
		    bufsize > len)
			/* Cap the buffer size when we know this is the
			 * single data frame. */
			bufsize = len;
		CHECK_OBJ_NOTNULL(stv_h2_rxbuf, STEVEDORE_MAGIC);
		stvbuf = STV_AllocBuf(wrk, stv_h2_rxbuf,
		    bufsize + sizeof *rxbuf);
		if (stvbuf == NULL) {
			VSLb(h2->vsl, SLT_Debug,
			    "H2: stream %u: Failed to allocate request body"
			    " buffer",
			    h2->rxf_stream);
			Lck_Lock(&h2->sess->mtx);
			r2->error = H2SE_INTERNAL_ERROR;
			if (r2->cond)
				PTOK(pthread_cond_signal(r2->cond));
			Lck_Unlock(&h2->sess->mtx);
			return (H2SE_INTERNAL_ERROR);
		}
		rxbuf = STV_GetBufPtr(stvbuf, &bstest);
		AN(rxbuf);
		assert(bstest >= bufsize + sizeof *rxbuf);
		assert(PAOK(rxbuf));
		INIT_OBJ(rxbuf, H2_RXBUF_MAGIC);
		rxbuf->size = bufsize;
		rxbuf->stvbuf = stvbuf;

		r2->rxbuf = rxbuf;

		Lck_Lock(&h2->sess->mtx);
	}

	CHECK_OBJ_NOTNULL(r2->rxbuf, H2_RXBUF_MAGIC);
	assert(r2->rxbuf->tail <= r2->rxbuf->head);
	l = r2->rxbuf->head - r2->rxbuf->tail;
	assert(l <= r2->rxbuf->size);
	l = r2->rxbuf->size - l;
	assert(len <= l); /* Stream window handling ensures this */

	Lck_Unlock(&h2->sess->mtx);

	l = len;
	head = r2->rxbuf->head;
	do {
		l2 = l;
		if ((head % r2->rxbuf->size) + l2 > r2->rxbuf->size)
			l2 = r2->rxbuf->size - (head % r2->rxbuf->size);
		assert(l2 > 0);
		memcpy(&r2->rxbuf->data[head % r2->rxbuf->size], src, l2);
		src += l2;
		head += l2;
		l -= l2;
	} while (l > 0);

	Lck_Lock(&h2->sess->mtx);

	/* Charge stream window. The entire frame including padding
	 * (h2->rxf_len) counts towards the window. The used padding
	 * bytes will be included in the next connection window update
	 * sent when the buffer bytes are consumed because that is
	 * calculated against the available buffer space. */
	r2->r_window -= h2->rxf_len;
	r2->rxbuf->head += len;
	assert(r2->rxbuf->tail <= r2->rxbuf->head);
	if (h2->rxf_flags & H2FF_DATA_END_STREAM)
		r2->state = H2_S_CLOS_REM;
	if (r2->cond)
		PTOK(pthread_cond_signal(r2->cond));
	Lck_Unlock(&h2->sess->mtx);

	return (0);
}

static enum vfp_status v_matchproto_(vfp_pull_f)
h2_vfp_body(struct vfp_ctx *vc, struct vfp_entry *vfe, void *ptr, ssize_t *lp)
{
	struct h2_req *r2;
	struct h2_sess *h2;
	enum vfp_status retval;
	ssize_t l, l2;
	uint64_t tail;
	uint8_t *dst;
	char buf[4];
	int i;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(r2, vfe->priv1, H2_REQ_MAGIC);
	h2 = r2->h2sess;

	AN(ptr);
	AN(lp);
	assert(*lp >= 0);

	Lck_Lock(&h2->sess->mtx);

	r2->cond = &vc->wrk->cond;
	while (1) {
		CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);
		if (r2->rxbuf) {
			assert(r2->rxbuf->tail <= r2->rxbuf->head);
			l = r2->rxbuf->head - r2->rxbuf->tail;
		} else
			l = 0;

		if (h2->error || r2->error)
			retval = VFP_ERROR;
		else if (r2->state >= H2_S_CLOS_REM && l <= *lp)
			retval = VFP_END;
		else {
			if (l > *lp)
				l = *lp;
			retval = VFP_OK;
		}

		if (retval != VFP_OK || l > 0)
			break;

		i = Lck_CondWaitTimeout(r2->cond, &h2->sess->mtx,
		    SESS_TMO(h2->sess, timeout_idle));
		if (i == ETIMEDOUT) {
			retval = VFP_ERROR;
			break;
		}
	}
	r2->cond = NULL;

	Lck_Unlock(&h2->sess->mtx);

	if (l == 0 || retval == VFP_ERROR) {
		*lp = 0;
		return (retval);
	}

	*lp = l;
	dst = ptr;
	tail = r2->rxbuf->tail;
	do {
		l2 = l;
		if ((tail % r2->rxbuf->size) + l2 > r2->rxbuf->size)
			l2 = r2->rxbuf->size - (tail % r2->rxbuf->size);
		assert(l2 > 0);
		memcpy(dst, &r2->rxbuf->data[tail % r2->rxbuf->size], l2);
		dst += l2;
		tail += l2;
		l -= l2;
	} while (l > 0);

	Lck_Lock(&h2->sess->mtx);

	CHECK_OBJ_NOTNULL(r2->rxbuf, H2_RXBUF_MAGIC);
	r2->rxbuf->tail = tail;
	assert(r2->rxbuf->tail <= r2->rxbuf->head);

	if (r2->r_window < cache_param->h2_rx_window_low_water &&
	    r2->state < H2_S_CLOS_REM) {
		/* l is free buffer space */
		/* l2 is calculated window increment */
		l = r2->rxbuf->size - (r2->rxbuf->head - r2->rxbuf->tail);
		assert(r2->r_window <= l);
		l2 = cache_param->h2_rx_window_increment;
		if (r2->r_window + l2 > l)
			l2 = l - r2->r_window;
		r2->r_window += l2;
	} else
		l2 = 0;

	Lck_Unlock(&h2->sess->mtx);

	if (l2 > 0) {
		vbe32enc(buf, l2);
		H2_Send_Get(vc->wrk, h2, r2);
		H2_Send_Frame(vc->wrk, h2, H2_F_WINDOW_UPDATE, 0, 4,
		    r2->stream, buf);
		H2_Send_Rel(h2, r2);
	}

	return (retval);
}

static void
h2_vfp_body_fini(struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct h2_req *r2;
	struct h2_sess *h2;
	struct stv_buffer *stvbuf = NULL;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(r2, vfe->priv1, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(r2->req, REQ_MAGIC);
	h2 = r2->h2sess;

	if (vc->failed) {
		CHECK_OBJ_NOTNULL(r2->req->wrk, WORKER_MAGIC);
		H2_Send_Get(r2->req->wrk, h2, r2);
		H2_Send_RST(r2->req->wrk, h2, r2, r2->stream,
		    H2SE_REFUSED_STREAM);
		H2_Send_Rel(h2, r2);
		Lck_Lock(&h2->sess->mtx);
		r2->error = H2SE_REFUSED_STREAM;
		Lck_Unlock(&h2->sess->mtx);
	}

	if (r2->state >= H2_S_CLOS_REM && r2->rxbuf != NULL) {
		Lck_Lock(&h2->sess->mtx);
		CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);
		if (r2->rxbuf != NULL) {
			stvbuf = r2->rxbuf->stvbuf;
			r2->rxbuf = NULL;
		}
		Lck_Unlock(&h2->sess->mtx);
		if (stvbuf != NULL) {
			STV_FreeBuf(vc->wrk, &stvbuf);
			AZ(stvbuf);
		}
	}
}

static const struct vfp h2_body = {
	.name = "H2_BODY",
	.pull = h2_vfp_body,
	.fini = h2_vfp_body_fini
};

void v_matchproto_(vtr_req_body_t)
h2_req_body(struct req *req)
{
	struct h2_req *r2;
	struct vfp_entry *vfe;

	CHECK_OBJ(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	vfe = VFP_Push(req->vfc, &h2_body);
	AN(vfe);
	vfe->priv1 = r2;
}

/**********************************************************************/

void v_matchproto_(vtr_req_fail_f)
h2_req_fail(struct req *req, stream_close_t reason)
{
	assert(reason != SC_NULL);
	assert(req->sp->fd != 0);
	VSLb(req->vsl, SLT_Debug, "H2FAILREQ");
}

/**********************************************************************/

static enum htc_status_e v_matchproto_(htc_complete_f)
h2_frame_complete(struct http_conn *htc)
{
	struct h2_sess *h2;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CAST_OBJ_NOTNULL(h2, htc->priv, H2_SESS_MAGIC);
	if (htc->rxbuf_b + 9 > htc->rxbuf_e ||
	    htc->rxbuf_b + 9 + (vbe32dec(htc->rxbuf_b) >> 8) > htc->rxbuf_e)
		return (HTC_S_MORE);
	return (HTC_S_COMPLETE);
}

/**********************************************************************/

static h2_error
h2_procframe(struct worker *wrk, struct h2_sess *h2, h2_frame h2f)
{
	struct h2_req *r2;
	h2_error h2e;

	ASSERT_RXTHR(h2);
	if (h2->rxf_stream == 0 && h2f->act_szero != 0)
		return (h2f->act_szero);

	if (h2->rxf_stream != 0 && h2f->act_snonzero != 0)
		return (h2f->act_snonzero);

	if (h2->rxf_stream > h2->highest_stream && h2f->act_sidle != 0)
		return (h2f->act_sidle);

	if (h2->rxf_stream != 0 && !(h2->rxf_stream & 1)) {
		// rfc7540,l,1140,1145
		// rfc7540,l,1153,1158
		/* No even streams, we don't do PUSH_PROMISE */
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug, "H2: illegal stream (=%u)",
		    h2->rxf_stream);
		Lck_Unlock(&h2->sess->mtx);
		return (H2CE_PROTOCOL_ERROR);
	}

	VTAILQ_FOREACH(r2, &h2->streams, list)
		if (r2->stream == h2->rxf_stream)
			break;

	if (h2->new_req != NULL &&
	    !(r2 && h2->new_req == r2->req && h2f == H2_F_CONTINUATION))
		return (H2CE_PROTOCOL_ERROR);	// rfc7540,l,1859,1863

	h2e = h2f->rxfunc(wrk, h2, r2);
	if (h2e == 0)
		return (0);
	if (h2->rxf_stream == 0 || h2e->connection)
		return (h2e);	// Connection errors one level up

	H2_Send_Get(wrk, h2, h2->req0);
	H2_Send_RST(wrk, h2, h2->req0, h2->rxf_stream, h2e);
	H2_Send_Rel(h2, h2->req0);
	return (0);
}

int
h2_stream_tmo(struct h2_sess *h2, const struct h2_req *r2, vtim_real now)
{
	int r = 0;

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	Lck_AssertHeld(&h2->sess->mtx);

	/* NB: when now is NAN, it means that idle_send_timeout was hit
	 * on a lock condwait operation.
	 */
	if (isnan(now))
		AN(r2->t_winupd);

	if (r2->t_winupd == 0 && r2->t_send == 0)
		return (0);

	if (isnan(now) || (r2->t_winupd != 0 &&
	    now - r2->t_winupd > SESS_TMO(h2->sess, idle_send_timeout))) {
		VSLb(h2->vsl, SLT_Debug,
		     "H2: stream %u: Hit idle_send_timeout waiting for"
		     " WINDOW_UPDATE", r2->stream);
		r = 1;
	}

	if (r == 0 && r2->t_send != 0 &&
	    now - r2->t_send > SESS_TMO(h2->sess, send_timeout)) {
		VSLb(h2->vsl, SLT_Debug,
		     "H2: stream %u: Hit send_timeout", r2->stream);
		r = 1;
	}

	return (r);
}

static int
h2_stream_tmo_unlocked(struct h2_sess *h2, const struct h2_req *r2)
{
	int r;

	Lck_Lock(&h2->sess->mtx);
	r = h2_stream_tmo(h2, r2, h2->sess->t_idle);
	Lck_Unlock(&h2->sess->mtx);

	return (r);
}

/*
 * This is the janitorial task of cleaning up any closed & refused
 * streams, and checking if the session is timed out.
 */
static int
h2_sweep(struct worker *wrk, struct h2_sess *h2)
{
	int tmo = 0;
	struct h2_req *r2, *r22;

	ASSERT_RXTHR(h2);

	h2->do_sweep = 0;
	VTAILQ_FOREACH_SAFE(r2, &h2->streams, list, r22) {
		if (r2 == h2->req0) {
			assert (r2->state == H2_S_IDLE);
			continue;
		}
		switch (r2->state) {
		case H2_S_CLOSED:
			if (!r2->scheduled)
				h2_del_req(wrk, r2);
			break;
		case H2_S_CLOS_REM:
			if (!r2->scheduled) {
				H2_Send_Get(wrk, h2, h2->req0);
				H2_Send_RST(wrk, h2, h2->req0, r2->stream,
				    H2SE_REFUSED_STREAM);
				H2_Send_Rel(h2, h2->req0);
				h2_del_req(wrk, r2);
				continue;
			}
			/* FALLTHROUGH */
		case H2_S_CLOS_LOC:
		case H2_S_OPEN:
			if (h2_stream_tmo_unlocked(h2, r2)) {
				tmo = 1;
				continue;
			}
			break;
		case H2_S_IDLE:
			/* Current code make this unreachable: h2_new_req is
			 * only called inside h2_rx_headers, which immediately
			 * sets the new stream state to H2_S_OPEN */
			/* FALLTHROUGH */
		default:
			WRONG("Wrong h2 stream state");
			break;
		}
	}
	if (tmo)
		return (0);
	return (h2->refcnt > 1);
}


/***********************************************************************
 * Called in loop from h2_new_session()
 */

#define H2_FRAME(l,U,...) const struct h2_frame_s H2_F_##U[1] = \
    {{ #U, h2_rx_##l, __VA_ARGS__ }};
#include "tbl/h2_frames.h"

static const h2_frame h2flist[] = {
#define H2_FRAME(l,U,t,...) [t] = H2_F_##U,
#include "tbl/h2_frames.h"
};

#define H2FMAX (sizeof(h2flist) / sizeof(h2flist[0]))

int
h2_rxframe(struct worker *wrk, struct h2_sess *h2)
{
	enum htc_status_e hs;
	h2_frame h2f;
	h2_error h2e;
	const char *s = NULL;

	ASSERT_RXTHR(h2);

	if (h2->goaway && h2->open_streams == 0)
		return (0);

	VTCP_blocking(*h2->htc->rfd);
	h2->sess->t_idle = VTIM_real();
	hs = HTC_RxStuff(h2->htc, h2_frame_complete,
	    NULL, NULL, NAN,
	    h2->sess->t_idle + SESS_TMO(h2->sess, timeout_idle),
	    NAN, h2->local_settings.max_frame_size + 9);
	switch (hs) {
	case HTC_S_COMPLETE:
		break;
	case HTC_S_TIMEOUT:
		if (h2_sweep(wrk, h2))
			return (1);

		/* FALLTHROUGH */
	default:
		/* XXX: HTC_S_OVERFLOW / FRAME_SIZE_ERROR handling */
#define HTC_STATUS(e, n, d, l)			\
		do {				\
			if (hs == HTC_S_ ## e)	\
				s = #e;		\
		} while (0);
#include "tbl/htc.h"
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug, "H2: No frame (hs=%s)", s);
		h2->error = H2CE_NO_ERROR;
		Lck_Unlock(&h2->sess->mtx);
		return (0);
	}

	if (h2->do_sweep)
		(void)h2_sweep(wrk, h2);

	h2->rxf_len = vbe32dec(h2->htc->rxbuf_b) >> 8;
	h2->rxf_type = h2->htc->rxbuf_b[3];
	h2->rxf_flags = h2->htc->rxbuf_b[4];
	h2->rxf_stream = vbe32dec(h2->htc->rxbuf_b + 5);
	h2->rxf_stream &= ~(1LU<<31);			// rfc7540,l,690,692
	h2->rxf_data = (void*)(h2->htc->rxbuf_b + 9);
	/* XXX: later full DATA will not be rx'ed yet. */
	HTC_RxPipeline(h2->htc, h2->htc->rxbuf_b + h2->rxf_len + 9);

	h2_vsl_frame(h2, h2->htc->rxbuf_b, 9L + h2->rxf_len);
	h2->srq->acct.req_hdrbytes += 9;

	if (h2->rxf_type >= H2FMAX) {
		// rfc7540,l,679,681
		// XXX: later, drain rest of frame
		h2->bogosity++;
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug,
		    "H2: Unknown frame type 0x%02x (ignored)",
		    (uint8_t)h2->rxf_type);
		Lck_Unlock(&h2->sess->mtx);
		h2->srq->acct.req_bodybytes += h2->rxf_len;
		return (1);
	}
	h2f = h2flist[h2->rxf_type];

	AN(h2f->name);
	AN(h2f->rxfunc);
	if (h2f->overhead)
		h2->srq->acct.req_bodybytes += h2->rxf_len;

	if (h2->rxf_flags & ~h2f->flags) {
		// rfc7540,l,687,688
		h2->bogosity++;
		Lck_Lock(&h2->sess->mtx);
		VSLb(h2->vsl, SLT_Debug,
		    "H2: Unknown flags 0x%02x on %s (ignored)",
		    (uint8_t)h2->rxf_flags & ~h2f->flags, h2f->name);
		Lck_Unlock(&h2->sess->mtx);
		h2->rxf_flags &= h2f->flags;
	}

	h2e = h2_procframe(wrk, h2, h2f);

	if (h2->error == NULL && h2e != NULL) {
		h2->error = h2e;
		h2_tx_goaway(wrk, h2, h2e);
	}

	return (h2->error ? 0 : 1);
}
