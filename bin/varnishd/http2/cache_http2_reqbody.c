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
#include "vend.h"

h2_error
h2_reqbody_data(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	char buf[4];
	ssize_t l;
	uint64_t l2, head;
	const uint8_t *src;
	unsigned len;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);

	ASSERT_RXTHR(h2);

	Lck_Lock(&h2->sess->mtx);
	CHECK_OBJ_ORNULL(r2->rxbuf, H2_RXBUF_MAGIC);

	if (h2->error != NULL || r2->error != NULL) {
		if (r2->cond)
			PTOK(pthread_cond_signal(r2->cond));
		Lck_Unlock(&h2->sess->mtx);
		return (h2->error != NULL ? h2->error : r2->error);
	}

	/* Check padding if present */
	src = h2->rxf_data;
	len = h2->rxf_len;
	if (h2->rxf_flags & H2FF_PADDED) {
		if (*src >= len) {
			VSLb(h2->vsl, SLT_SessError,
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
		    ((h2->rxf_flags & H2FF_END_STREAM) &&
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
	if (h2->rxf_len > h2->req0->rx_window) {
		VSLb(h2->vsl, SLT_SessError,
		    "H2: stream %u: Exceeded connection receive window",
		    h2->rxf_stream);
		r2->error = H2CE_FLOW_CONTROL_ERROR;
		if (r2->cond)
			PTOK(pthread_cond_signal(r2->cond));
		Lck_Unlock(&h2->sess->mtx);
		return (H2CE_FLOW_CONTROL_ERROR);
	}
	h2->req0->rx_window -= h2->rxf_len;
	if (h2->req0->rx_window < cache_param->h2_rx_window_low_water) {
		h2->req0->rx_window += cache_param->h2_rx_window_increment;
		vbe32enc(buf, cache_param->h2_rx_window_increment);
		Lck_Unlock(&h2->sess->mtx);
		H2_Send_Get(wrk, h2, h2->req0);
		H2_Send_Frame(wrk, h2, H2_F_WINDOW_UPDATE, 0, 4, 0, buf);
		H2_Send_Rel(h2, h2->req0);
		Lck_Lock(&h2->sess->mtx);
	}

	/* Check stream window. The entire frame including padding
	 * (h2->rxf_len) counts towards the window. */
	if (h2->rxf_len > r2->rx_window) {
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
			if (r2->rxbuf)
				l = r2->rxbuf->size;
			else
				l = h2->local_settings.initial_window_size;
			r2->rx_window += l;
			Lck_Unlock(&h2->sess->mtx);
			vbe32enc(buf, l);
			H2_Send_Get(wrk, h2, h2->req0);
			H2_Send_Frame(wrk, h2, H2_F_WINDOW_UPDATE, 0, 4,
			    r2->stream, buf);
			H2_Send_Rel(h2, h2->req0);
			Lck_Lock(&h2->sess->mtx);
		}

		if (h2->rxf_flags & H2FF_END_STREAM)
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
		if (bufsize < r2->rx_window) {
			/* This will not happen because we do not have any
			 * mechanism to change the initial window size on
			 * a running session. But if we gain that ability,
			 * this future proofs it. */
			bufsize = r2->rx_window;
		}
		assert(bufsize > 0);
		if ((h2->rxf_flags & H2FF_END_STREAM) &&
		    bufsize > len)
			/* Cap the buffer size when we know this is the
			 * single data frame. */
			bufsize = len;
		CHECK_OBJ_NOTNULL(stv_h2_rxbuf, STEVEDORE_MAGIC);
		stvbuf = STV_AllocBuf(wrk, stv_h2_rxbuf,
		    bufsize + sizeof *rxbuf);
		if (stvbuf == NULL) {
			Lck_Lock(&h2->sess->mtx);
			VSLb(h2->vsl, SLT_Debug,
			    "H2: stream %u: Failed to allocate request body"
			    " buffer",
			    h2->rxf_stream);
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
	r2->rx_window -= h2->rxf_len;
	r2->rxbuf->head += len;
	assert(r2->rxbuf->tail <= r2->rxbuf->head);
	if (h2->rxf_flags & H2FF_END_STREAM)
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

		if (h2->error != NULL || r2->error != NULL)
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

	if (r2->rx_window < cache_param->h2_rx_window_low_water &&
	    r2->state < H2_S_CLOS_REM) {
		/* l is free buffer space */
		/* l2 is calculated window increment */
		l = r2->rxbuf->size - (r2->rxbuf->head - r2->rxbuf->tail);
		assert(r2->rx_window <= l);
		l2 = cache_param->h2_rx_window_increment;
		if (r2->rx_window + l2 > l)
			l2 = l - r2->rx_window;
		r2->rx_window += l2;
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
