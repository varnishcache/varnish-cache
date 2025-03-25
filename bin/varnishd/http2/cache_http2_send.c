/*-
 * Copyright (c) 2016 Varnish Software AS
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

#include <sys/uio.h>
#include <stdio.h>
#include <poll.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_transport.h"
#include "http2/cache_http2.h"

#include "vend.h"
#include "vtim.h"

static void
h2_send_vsl(struct vsl_log *vsl, const void *ptr, size_t len)
{
	const uint8_t *b;
	struct vsb *vsb;
	const char *p;
	unsigned u;

	if (VSL_tag_is_masked(SLT_H2TxHdr) &&
	    VSL_tag_is_masked(SLT_H2TxBody))
		return;

	AN(ptr);
	assert(len >= 9);
	b = ptr;

	vsb = VSB_new_auto();
	AN(vsb);
	p = h2_framename(b[3]);
	if (p != NULL)
		VSB_cat(vsb, p);
	else
		VSB_quote(vsb, b + 3, 1, VSB_QUOTE_HEX);

	u = vbe32dec(b) >> 8;
	VSB_printf(vsb, "[%u] ", u);
	VSB_quote(vsb, b + 4, 1, VSB_QUOTE_HEX);
	VSB_putc(vsb, ' ');
	VSB_quote(vsb, b + 5, 4, VSB_QUOTE_HEX);
	AZ(VSB_finish(vsb));
	VSLb_bin(vsl, SLT_H2TxHdr, 9, b);
	if (len > 9)
		VSLb_bin(vsl, SLT_H2TxBody, len - 9, b + 9);

	VSLb(vsl, SLT_Debug, "H2TXF %s", VSB_data(vsb));
	VSB_destroy(&vsb);
}

static void
h2_mk_hdr(uint8_t *hdr, h2_frame ftyp, uint8_t flags,
    uint32_t len, uint32_t stream)
{

	AN(hdr);
	AZ(flags & ~(ftyp->flags));
	if (stream == 0)
		AZ(ftyp->act_szero);
	else
		AZ(ftyp->act_snonzero);
	assert(len < (1U << 24));
	vbe32enc(hdr, len << 8);
	hdr[3] = ftyp->type;
	hdr[4] = flags;
	vbe32enc(hdr + 5, stream);
}

static int64_t
h2_win_limit(const struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(r2->h2sess, H2_SESS_MAGIC);

	return (vmin_t(int64_t, r2->tx_window, r2->h2sess->tx_window));
}

static void
h2_win_charge(struct h2_req *r2, uint32_t w)
{
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(r2->h2sess, H2_SESS_MAGIC);

	r2->tx_window -= w;
	r2->h2sess->tx_window -= w;
}

static int
h2_send_small(struct h2_sess *h2, h2_frame ftyp, uint8_t flags,
    uint32_t stream, uint32_t len, const void *ptr)
{

	ASSERT_H2_SESS(h2);
	AN(ftyp);
	AZ(flags & ~(ftyp->flags));
	if (stream == 0)
		AZ(ftyp->act_szero);
	else
		AZ(ftyp->act_snonzero);
	assert(len + 9 <= pdiff(h2->tx_s_start, h2->tx_s_end));
	if (len > 0)
		AN(ptr);

	while (len + 9 > pdiff(h2->tx_s_head, h2->tx_s_end)) {
		/* Send something (up until h2->deadline) to free up space. */
		if (H2_Send_Something(h2) < 0)
			return (-1);
	}

	h2_mk_hdr(h2->tx_s_head, ftyp, flags, len, stream);
	h2->tx_s_head += 9;
	if (len > 0) {
		memcpy(h2->tx_s_head, ptr, len);
		h2->tx_s_head += len;
	}
	assert(h2->tx_s_head <= h2->tx_s_end);
	h2_send_vsl(h2->vsl, h2->tx_s_head - (9 + len), 9 + len);

	h2->srq->acct.resp_hdrbytes += 9;
	if (ftyp->overhead)
		h2->srq->acct.resp_bodybytes += len;

	return (0);
}

int
H2_Send_RST(struct h2_sess *h2, uint32_t stream, h2_error h2e)
{
	uint8_t buf[4];

	vbe32enc(buf, h2e->val);
	return (h2_send_small(h2, H2_F_RST_STREAM, 0, stream,
		sizeof buf, buf));
}

int
H2_Send_SETTINGS(struct h2_sess *h2, uint8_t flags, ssize_t len,
    const uint8_t *buf)
{
	if (flags & H2FF_ACK)
		assert(len == 0);
	return (h2_send_small(h2, H2_F_SETTINGS, flags, 0, len, buf));
}

int
H2_Send_PING(struct h2_sess *h2, uint8_t flags, uint64_t data)
{
	return (h2_send_small(h2, H2_F_PING, flags, 0, sizeof data, &data));
}

int
H2_Send_GOAWAY(struct h2_sess *h2, uint32_t last_stream_id, h2_error h2e)
{
	uint8_t buf[8];

	vbe32enc(&buf[0], last_stream_id);
	vbe32enc(&buf[4], h2e->val);
	return (h2_send_small(h2, H2_F_GOAWAY, 0, 0, sizeof buf, buf));
}

int
H2_Send_WINDOW_UPDATE(struct h2_sess *h2, uint32_t stream, uint32_t incr)
{
	uint8_t buf[4];

	vbe32enc(&buf[0], incr);
	return (h2_send_small(h2, H2_F_WINDOW_UPDATE, 0, stream,
		sizeof buf, buf));
}

struct h2_send_large {
	unsigned			magic;
#define H2_SEND_LARGE_MAGIC		0x478020e3

	char				last;
	char				started;
	char				returned;

	uint8_t				flags;
	h2_frame			ftyp;

	VTAILQ_ENTRY(h2_send_large)	list;

	pthread_cond_t			cond;

	struct h2_req			*r2;

	const void			*ptr;
	uint32_t			len;
	uint32_t			count;
};

int
H2_Send(struct vsl_log *vsl, struct h2_req *r2, h2_frame ftyp, uint8_t flags,
    uint32_t len, const void *ptr)
{
	struct h2_sess *h2;
	struct h2_send_large large;
	h2_error h2e;

	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	h2 = r2->h2sess;
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);

	ASSERT_H2_REQ(h2);

	assert(ftyp == H2_F_HEADERS || ftyp == H2_F_DATA);
	AZ(flags & ~(ftyp->flags));

	h2e = h2_errcheck(r2);
	if (h2e != NULL) {
		VSLb(vsl, SLT_Error, "H2: send error (%s)", h2e->name);
		return (-1);
	}

	assert(r2->state > H2_S_IDLE);
	if (r2->state >= H2_S_CLOSED) {
		VSLb(vsl, SLT_Error, "H2: send on closed stream");
		return (-1);
	}

	INIT_OBJ(&large, H2_SEND_LARGE_MAGIC);
	PTOK(pthread_cond_init(&large.cond, NULL));

	large.ftyp = ftyp;
	large.flags = flags;
	large.r2 = r2;
	large.ptr = ptr;
	large.len = len;

	Lck_Lock(&h2->sess->mtx);

	VTAILQ_INSERT_TAIL(&h2->tx_l_queue, &large, list);
	h2->tx_l_stuck = 0;
	h2_attention(h2);

	AZ(Lck_CondWait(&large.cond, &h2->sess->mtx));
	AN(large.returned);	/* Sanity check */
	/* Note: We will have been removed from the `h2->tx_l_queue`
	 * list by the signaller. */

	h2e = h2_errcheck(r2);

	Lck_Unlock(&h2->sess->mtx);

	PTOK(pthread_cond_destroy(&large.cond));
	large.magic = 0;

	if (h2e != NULL) {
		VSLb(vsl, SLT_Error, "H2: send error (%s)", h2e->name);
		return (-1);
	}

	return (0);
}

static void
h2_send_prep_large(struct h2_sess *h2, struct h2_send_large *large)
{
	struct h2_req *r2;
	uint8_t flags;
	ssize_t l, limit;
	h2_frame ftyp;

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	AZ(h2->tx_l_current);

	CHECK_OBJ_NOTNULL(large, H2_SEND_LARGE_MAGIC);
	AN(large->ftyp);
	r2 = large->r2;
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);

	assert(large->ftyp == H2_F_DATA || large->ftyp == H2_F_HEADERS ||
	    large->ftyp == H2_F_PUSH_PROMISE);
	AN(large->ftyp->continuation);

	l = large->len - large->count;
	if (l > h2->remote_settings.max_frame_size)
		l = h2->remote_settings.max_frame_size;

	if (large->ftyp->respect_window) {
		limit = h2_win_limit(r2);
		assert(limit > 0);
		if (l > limit)
			l = limit;
		h2_win_charge(r2, l);
		if (r2->t_win_low == 0. && r2->tx_window == 0) {
			/* The send window is low. Set a timestamp to
			 * record when this happened, so that we can
			 * become emo if the window isn't extended
			 * promptly. */
			/* XXX: This mechanism would be more effective if
			 * we had some threshold (10% of initial window
			 * size or something. */
			r2->t_win_low = VTIM_real();
			h2->win_low_streams++;
		}
	}
	assert(large->count + l <= large->len);

	ftyp = large->ftyp;
	flags = large->flags;
	AZ(flags & ~(ftyp->flags));

	if (large->count > 0) {
		/* This is a continuation. Switch frame type and mask out
		 * the flags not defined on its continuation type. */
		ftyp = ftyp->continuation;
		AN(ftyp);
		flags &= ftyp->flags;
	}

	if (large->count + l < large->len) {
		/* We are breaking it up into smaller frames. Clear the
		 * last marker from the flags if present. */
		flags &= ~(ftyp->final_flags);
	}

	h2_mk_hdr(h2->tx_l_hdrbuf, ftyp, flags, l, r2->stream);
	h2_send_vsl(h2->vsl, h2->tx_l_hdrbuf, 9);
	h2->tx_vec[0].iov_base = h2->tx_l_hdrbuf;
	h2->tx_vec[0].iov_len = 9;
	if (l == 0) {
		/* Zero payload frame is valid. Will be used on
		 * "chunked encoding" and the end of stream is
		 * found. */
		h2->tx_nvec = 1;
	} else {
		h2->tx_vec[1].iov_base =
		    TRUST_ME((uintptr_t)large->ptr + large->count);
		h2->tx_vec[1].iov_len = l;
		h2->tx_nvec = 2;
		large->count += l;
	}
	h2->tx_l_current = large;

	/* Charge the session accounting for the protocol bytes */
	h2->srq->acct.resp_hdrbytes += 9;
	if (ftyp->overhead)
		h2->srq->acct.resp_bodybytes += l;

	/* Charge the request accounting for HEADERS and DATA frames */
	if (large->ftyp == H2_F_HEADERS)
		r2->req->acct.resp_hdrbytes += l;
	else if (large->ftyp == H2_F_DATA)
		r2->req->acct.resp_bodybytes += l;
}

ssize_t
H2_Send_TxStuff(struct h2_sess *h2)
{
	struct h2_send_large *large;
	ssize_t l, ltot = 0;
	int err = 0;

	ASSERT_H2_SESS(h2);

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);

	if (h2->tx_nvec == 0 && h2->tx_s_head != h2->tx_s_start) {
		/* Prioritise sending the small frames */
		assert(h2->tx_s_start < h2->tx_s_head);
		assert(h2->tx_s_head <= h2->tx_s_end);
		assert(h2->tx_s_mark == h2->tx_s_start);
		h2->tx_vec[0].iov_base = h2->tx_s_start;
		h2->tx_vec[0].iov_len = h2->tx_s_head - h2->tx_s_start;
		h2->tx_nvec = 1;
		h2->tx_s_mark = h2->tx_s_head;
	} else if (h2->tx_nvec == 0) {
		/* Construct a large frame from the queue (if possible
		 * considering the current windows). If we ever implement
		 * priorities, this would be the place to take them into
		 * account. */
		Lck_Lock(&h2->sess->mtx);

		VTAILQ_FOREACH(large, &h2->tx_l_queue, list) {
			CHECK_OBJ_NOTNULL(large, H2_SEND_LARGE_MAGIC);
			CHECK_OBJ_NOTNULL(large->r2, H2_REQ_MAGIC);
			assert(large->count <= large->len);
			AN(large->ftyp);

			if (h2_errcheck(large->r2) != NULL) {
				VTAILQ_REMOVE(&h2->tx_l_queue, large, list);
				large->returned = 1;
				PTOK(pthread_cond_signal(&large->cond));
				continue;
			}

			if (!large->ftyp->respect_window)
				break;

			if (h2->tx_window <= 0) {
				/* If the session window is empty, none of
				 * the respect_window frame types can be
				 * selected. */
				continue;
			}

			if (large->r2->tx_window > 0)
				break;
		}

		if (large == NULL) {
			/* Tx is unable to make progress until there has
			 * been a window update. */
			h2->tx_l_stuck = 1;
		} else {
			h2->tx_l_stuck = 0;
		}

		Lck_Unlock(&h2->sess->mtx);

		if (large == NULL)
			return (0);

		h2_send_prep_large(h2, large);
	}

	assert(h2->tx_nvec > 0);
	while (h2->tx_nvec > 0) {
		l = writev(h2->sess->fd, h2->tx_vec, h2->tx_nvec);
		if (l < 0) {
			/* Save the value of errno. This is strictly not
			 * necessary as none of the calls between here and
			 * the return should update errno, but done for
			 * future proofing. */
			err = errno;
			break;
		}

		assert(l > 0);
		VIOV_prune(h2->tx_vec, &h2->tx_nvec, l);
		ltot += l;
	}

	if (h2->tx_nvec == 0 && h2->tx_l_current != NULL) {
		/* We have just finished sending a large frame. */
		assert(h2->tx_s_mark == h2->tx_s_start);

		TAKE_OBJ_NOTNULL(large, &h2->tx_l_current, H2_SEND_LARGE_MAGIC);
		AZ(h2->tx_l_current);

		AN(large->ftyp);

		assert(large->count <= large->len);
		if (large->count == large->len) {
			if (large->r2->state < H2_S_CLOSED &&
			    large->flags & H2FF_END_STREAM) {
				large->r2->state = H2_S_CLOSED;
				assert(h2->open_streams > 0);
				h2->open_streams--;
			}

			/* Signal that we are finished */
			Lck_Lock(&h2->sess->mtx);
			VTAILQ_REMOVE(&h2->tx_l_queue, large, list);
			PTOK(pthread_cond_signal(&large->cond));
			large->returned = 1;
			Lck_Unlock(&h2->sess->mtx);
		} else if (large->ftyp == H2_F_HEADERS ||
		    large->ftyp == H2_F_PUSH_PROMISE) {
			/* A CONTINUATION frame must come immediately
			 * after the previous
			 * HEADER|PUSH_PROMISE|CONTINUATION frame. Prepare
			 * the `large` again, which will force that to be
			 * the next output. */
			h2_send_prep_large(h2, large);
			assert(large == h2->tx_l_current);
			assert(h2->tx_nvec > 0);
		}
	} else if (h2->tx_nvec == 0) {
		/* We have just finished sending the small buffer */
		assert(h2->tx_s_start < h2->tx_s_mark);
		assert(h2->tx_s_mark <= h2->tx_s_head);
		assert(h2->tx_s_head <= h2->tx_s_end);
		memmove(h2->tx_s_start, h2->tx_s_mark,
		    h2->tx_s_head - h2->tx_s_mark);
		h2->tx_s_head -= h2->tx_s_mark - h2->tx_s_start;
		h2->tx_s_mark = h2->tx_s_start;
	}

	if (ltot > 0)
		return (ltot);

	errno = err;
	return (-1);
}

int
H2_Send_Something(struct h2_sess *h2)
{
	ssize_t l;
	vtim_real now;
	struct pollfd pfd[1];

	/* Block up until h2->deadline and then send something. */

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	ASSERT_H2_SESS(h2);
	assert(h2->sess->fd >= 0);
	pfd->fd = h2->sess->fd;
	pfd->events = POLLOUT;

	do {
		now = VTIM_real();
		if (now > h2->deadline)
			goto error;
		l = poll(pfd, 1, VTIM_poll_tmo(h2->deadline - now));
	} while (l < 0 && errno == EINTR);

	if (l == 0 || !(pfd->revents & POLLOUT))
		goto error;

	l = H2_Send_TxStuff(h2);
	if (l < 0 && errno != EWOULDBLOCK)
		goto error;

	return (0);

error:
	/* Failure to send on the socket (IO error or timeout). */
	if (h2->error == NULL)
		h2->error = H2CE_IO_ERROR;
	return (-1);
}

int
H2_Send_Pending(struct h2_sess *h2)
{
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	ASSERT_H2_SESS(h2);

	if (h2->tx_nvec > 0)
		return (1);
	if (h2->tx_s_head != h2->tx_s_start)
		return (1);
	if (!VTAILQ_EMPTY(&h2->tx_l_queue) && !h2->tx_l_stuck)
		return (1);
	return (0);
}

void
H2_Send_Shutdown(struct h2_sess *h2)
{
	struct h2_send_large *large, *large2;

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	ASSERT_H2_SESS(h2);

	Lck_Lock(&h2->sess->mtx);
	AN(h2->error);
	VTAILQ_FOREACH_SAFE(large, &h2->tx_l_queue, list, large2) {
		CHECK_OBJ_NOTNULL(large, H2_SEND_LARGE_MAGIC);
		VTAILQ_REMOVE(&h2->tx_l_queue, large, list);
		large->returned = 1;
		PTOK(pthread_cond_signal(&large->cond));
	}
	Lck_Unlock(&h2->sess->mtx);
}
