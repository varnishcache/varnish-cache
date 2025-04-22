/*-
 * Copyright (c) 2016-2025 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include <pthread.h>
#include <errno.h>

#include "cache/cache_varnishd.h"

#include "cache/cache_transport.h"
#include "cache/cache_filter.h"
#include "http2/cache_http2.h"
#include "storage/storage.h"

#include "vtim.h"

struct h2_reqbody_waiter {
	unsigned		magic;
#define H2_REQBODY_WAITER_MAGIC	0xb6f4c52c
	pthread_cond_t		cond;
};

static int
h2_reqbody_wait(struct h2_req *r2, vtim_real when)
{
	struct h2_reqbody_waiter w;
	int retval;

	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(r2->h2sess, H2_SESS_MAGIC);

	Lck_AssertHeld(&r2->h2sess->sess->mtx);

	INIT_OBJ(&w, H2_REQBODY_WAITER_MAGIC);
	PTOK(pthread_cond_init(&w.cond, NULL));

	AZ(r2->reqbody_waiter);
	r2->reqbody_waiter = &w;
	retval = Lck_CondWaitUntil(&w.cond, &r2->h2sess->sess->mtx, when);
	r2->reqbody_waiter = NULL;

	PTOK(pthread_cond_destroy(&w.cond));
	w.magic = 0;

	return (retval);
}

void
h2_reqbody_kick(struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(r2->h2sess, H2_SESS_MAGIC);

	Lck_AssertHeld(&r2->h2sess->sess->mtx);

	CHECK_OBJ_ORNULL(r2->reqbody_waiter, H2_REQBODY_WAITER_MAGIC);
	if (r2->reqbody_waiter != NULL)
		PTOK(pthread_cond_signal(&r2->reqbody_waiter->cond));
}

h2_error
h2_reqbody_data(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	ssize_t l;
	uint64_t l2, head;
	const uint8_t *src;
	unsigned len;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);

	ASSERT_H2_SESS(h2);

	CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);

	/* XXX: errcheck? */
	if (h2->error != NULL || r2->error != NULL)
		return (h2->error != NULL ? h2->error : r2->error);

	/* Check padding if present */
	src = h2->rxf_data;
	len = h2->rxf_len;
	if (h2->rxf_flags & H2FF_PADDED) {
		if (*src >= len) {
			VSLb(h2->vsl, SLT_SessError,
			    "H2: stream %u: Padding larger than frame length",
			    h2->rxf_stream);
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
		    ((h2->rxf_flags & H2FF_END_STREAM) &&
		     l != r2->req->htc->content_length)) {
			VSLb(h2->vsl, SLT_Debug,
			    "H2: stream %u: Received data and Content-Length"
			    " mismatch", h2->rxf_stream);
			return (H2SE_PROTOCOL_ERROR);
		}
	}

	/* Check and charge connection window. The entire frame including
	 * padding (h2->rxf_len) counts towards the window. */
	if (h2->rxf_len > h2->rx_window) {
		VSLb(h2->vsl, SLT_SessError,
		    "H2: stream %u: Exceeded connection receive window",
		    h2->rxf_stream);
		return (H2CE_FLOW_CONTROL_ERROR);
	}
	h2->rx_window -= h2->rxf_len;
	if (h2->rx_window < cache_param->h2_rx_window_low_water) {
		/* Running low, increase the window */
		l = cache_param->h2_rx_window_increment;
		assert(l < (1UL << 31));
		h2->rx_window += l;
		H2_Send_WINDOW_UPDATE(h2, 0, l);
	}

	/* Check stream window. The entire frame including padding
	 * (h2->rxf_len) counts towards the window. */
	if (h2->rxf_len > r2->rx_window) {
		VSLb(h2->vsl, SLT_Debug,
		    "H2: stream %u: Exceeded stream receive window",
		    h2->rxf_stream);
		return (H2SE_FLOW_CONTROL_ERROR);
	}

	/* Handle zero size frame before starting to allocate buffers */
	if (len == 0) {
		r2->rx_window -= h2->rxf_len;

		/* Handle the specific corner case where the entire window
		 * has been exhausted using nothing but padding
		 * bytes. Since no bytes have been buffered, no bytes
		 * would be consumed by the request thread and no stream
		 * window updates sent. Unpaint ourselves from this corner
		 * by sending a stream window update here. */
		CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);
		if (r2->rx_window == 0 &&
		    (r2->rxbuf == NULL || r2->rxbuf->tail == r2->rxbuf->head)) {
			/* XXX: bogosity++? */
			if (r2->rxbuf)
				l = r2->rxbuf->size;
			else
				l = h2->local_settings.initial_window_size;
			r2->rx_window += l;
			H2_Send_WINDOW_UPDATE(h2, r2->stream, l);
		}

		if (h2->rxf_flags & H2FF_END_STREAM)
			h2_stream_setstate(r2, H2_S_CLOS_REM);
		Lck_Lock(&h2->sess->mtx);
		h2_reqbody_kick(r2);
		Lck_Unlock(&h2->sess->mtx);
		return (0);
	}

	/* Make the buffer on demand */
	if (r2->rxbuf == NULL) {
		unsigned bufsize;
		size_t bstest;
		struct stv_buffer *stvbuf;
		struct h2_rxbuf *rxbuf;

		bufsize = h2->local_settings.initial_window_size;
		if (bufsize < r2->rx_window) {
			/* This will not happen because we do not have any
			 * mechanism to change the initial window size on
			 * a running session. But if we gain that ability,
			 * this future proofs it. */
			bufsize = r2->rx_window;
		}
		assert(bufsize > 0);
		if ((h2->rxf_flags & H2FF_END_STREAM) &&
		    bufsize > len) {
			/* Cap the buffer size when we know this is the
			 * single data frame. */
			bufsize = len;
		}
		CHECK_OBJ_NOTNULL(stv_h2_rxbuf, STEVEDORE_MAGIC);
		stvbuf = STV_AllocBuf(wrk, stv_h2_rxbuf,
		    bufsize + sizeof *rxbuf);
		if (stvbuf == NULL) {
			VSLb(h2->vsl, SLT_Debug,
			    "H2: stream %u: Failed to allocate request body"
			    " buffer",
			    h2->rxf_stream);
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
	}

	CHECK_OBJ_NOTNULL(r2->rxbuf, H2_RXBUF_MAGIC);
	assert(r2->rxbuf->tail <= r2->rxbuf->head);
	l = r2->rxbuf->head - r2->rxbuf->tail;
	assert(l <= r2->rxbuf->size);
	l = r2->rxbuf->size - l;
	assert(len <= l); /* Stream window handling ensures this */

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
	r2->rx_window -= h2->rxf_len;
	r2->rxbuf->head += len;
	assert(r2->rxbuf->tail <= r2->rxbuf->head);
	if (h2->rxf_flags & H2FF_END_STREAM)
		h2_stream_setstate(r2, H2_S_CLOS_REM);
	h2_reqbody_kick(r2);
	Lck_Unlock(&h2->sess->mtx);

	return (0);
}

static enum vfp_status v_matchproto_(vfp_pull_f)
h2_vfp_body(struct vfp_ctx *vc, struct vfp_entry *vfe, void *ptr, ssize_t *lp)
{
	struct h2_req *r2;
	struct h2_sess *h2;
	enum vfp_status retval;
	h2_error h2e = NULL;
	ssize_t l, l2;
	uint64_t tail;
	uint8_t *dst;
	int wait_error = 0;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(r2, vfe->priv1, H2_REQ_MAGIC);
	h2 = r2->h2sess;

	ASSERT_H2_REQ(h2);

	AN(ptr);
	AN(lp);
	assert(*lp >= 0);

	Lck_Lock(&h2->sess->mtx);

	while (1) {
		CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);
		if (r2->rxbuf) {
			assert(r2->rxbuf->tail <= r2->rxbuf->head);
			l = r2->rxbuf->head - r2->rxbuf->tail;
		} else
			l = 0;

		h2e = h2_errcheck(r2);
		if (h2e != NULL)
			break;
		else if (r2->state >= H2_S_CLOS_REM && l <= *lp)
			retval = VFP_END;
		else {
			if (l > *lp)
				l = *lp;
			retval = VFP_OK;
		}

		if (retval != VFP_OK || l > 0)
			break;

		wait_error = h2_reqbody_wait(r2,
		    VTIM_real() + SESS_TMO(h2->sess, timeout_idle));
		if (wait_error == ETIMEDOUT)
			break;
	}

	Lck_Unlock(&h2->sess->mtx);

	if (h2e != NULL)
		retval = VFP_Error(vc, "H2: Request body error (%s)", h2e->txt);
	else if (wait_error == ETIMEDOUT)
		retval = VFP_Error(vc, "H2: Request body timed out");

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

	if (r2->rx_window < cache_param->h2_rx_window_low_water &&
	    r2->state < H2_S_CLOS_REM) {
		/* Kick the session thread so it can hand out an extended
		 * window to the peer. */
		h2_attention(h2);
	}

	Lck_Unlock(&h2->sess->mtx);
	return (retval);
}

static void
h2_vfp_body_fini(struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct h2_req *r2;
	struct stv_buffer *stvbuf = NULL;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(r2, vfe->priv1, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(r2->req, REQ_MAGIC);

	ASSERT_H2_REQ(r2->h2sess);

	if (vc->failed)
		h2_async_error(r2, H2SE_REFUSED_STREAM);

	CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);
	if (r2->state >= H2_S_CLOS_REM && r2->rxbuf != NULL) {
		/* Free the buffer. This is safe without any locking
		 * because the session thread will only free the buffer as
		 * part of h2_del_req(), which won't be run as long as we
		 * are scheduled. */
		AN(r2->scheduled);
		stvbuf = r2->rxbuf->stvbuf;
		r2->rxbuf = NULL;
		STV_FreeBuf(vc->wrk, &stvbuf);
	}
}

static const struct vfp h2_body = {
	.name = "H2_BODY",
	.pull = h2_vfp_body,
	.fini = h2_vfp_body_fini,
};

void v_matchproto_(vtr_req_body_t)
h2_reqbody(struct req *req)
{
	struct h2_req *r2;
	struct vfp_entry *vfe;

	CHECK_OBJ(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(r2, req->transport_priv, H2_REQ_MAGIC);
	vfe = VFP_Push(req->vfc, &h2_body);
	AN(vfe);
	vfe->priv1 = r2;
}
