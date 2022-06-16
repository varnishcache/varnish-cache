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

#include "cache/cache_varnishd.h"

#include "cache/cache_transport.h"
#include "http2/cache_http2.h"

#include "vend.h"
#include "vtim.h"

#define H2_SEND_HELD(h2, r2) (VTAILQ_FIRST(&(h2)->txqueue) == (r2))

static int
h2_cond_wait(pthread_cond_t *cond, struct h2_sess *h2, struct h2_req *r2)
{
	vtim_dur tmo = 0.;
	vtim_real now;
	int r;

	AN(cond);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);

	Lck_AssertHeld(&h2->sess->mtx);

	if (cache_param->resp_idle_interrupt > 0.)
		tmo = cache_param->resp_idle_interrupt;

	r = Lck_CondWaitTimeout(cond, &h2->sess->mtx, tmo);
	assert(r == 0 || r == ETIMEDOUT);

	now = VTIM_real();

	/* NB: when we grab resp_idle_interrupt before acquiring the session
	 * lock we may time out, but once we wake up both send_timeout and
	 * resp_idle_interrupt may have changed meanwhile. For this reason
	 * h2_stream_tmo() may not log what timed out and we need to call
	 * again with a magic NAN "now" that indicates to h2_stream_tmo()
	 * that the stream reached the resp_idle_interrupt via the lock and
	 * force it to log it.
	 */
	if (h2_stream_tmo(h2, r2, now))
		r = ETIMEDOUT;
	else if (r == ETIMEDOUT)
		AN(h2_stream_tmo(h2, r2, NAN));

	if (r == ETIMEDOUT) {
		if (r2->error == NULL)
			r2->error = H2SE_CANCEL;
		return (-1);
	}

	return (0);
}

static void
h2_send_get_locked(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	Lck_AssertHeld(&h2->sess->mtx);
	if (&wrk->cond == h2->cond)
		ASSERT_RXTHR(h2);
	r2->wrk = wrk;
	VTAILQ_INSERT_TAIL(&h2->txqueue, r2, tx_list);
	while (!H2_SEND_HELD(h2, r2))
		AZ(Lck_CondWait(&wrk->cond, &h2->sess->mtx));
	r2->wrk = NULL;
}

void
H2_Send_Get(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	Lck_Lock(&h2->sess->mtx);
	h2_send_get_locked(wrk, h2, r2);
	Lck_Unlock(&h2->sess->mtx);
}

static void
h2_send_rel_locked(struct h2_sess *h2, const struct h2_req *r2)
{
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);

	Lck_AssertHeld(&h2->sess->mtx);
	AN(H2_SEND_HELD(h2, r2));
	VTAILQ_REMOVE(&h2->txqueue, r2, tx_list);
	r2 = VTAILQ_FIRST(&h2->txqueue);
	if (r2 != NULL) {
		CHECK_OBJ_NOTNULL(r2->wrk, WORKER_MAGIC);
		AZ(pthread_cond_signal(&r2->wrk->cond));
	}
}

void
H2_Send_Rel(struct h2_sess *h2, const struct h2_req *r2)
{
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);

	Lck_Lock(&h2->sess->mtx);
	h2_send_rel_locked(h2, r2);
	Lck_Unlock(&h2->sess->mtx);
}

static void
h2_mk_hdr(uint8_t *hdr, h2_frame ftyp, uint8_t flags,
    uint32_t len, uint32_t stream)
{

	AN(hdr);
	assert(len < (1U << 24));
	vbe32enc(hdr, len << 8);
	hdr[3] = ftyp->type;
	hdr[4] = flags;
	vbe32enc(hdr + 5, stream);
}

/*
 * This is the "raw" frame sender, all per-stream accounting and
 * prioritization must have happened before this is called, and
 * the session mtx must be held.
 */

void
H2_Send_Frame(struct worker *wrk, struct h2_sess *h2,
    h2_frame ftyp, uint8_t flags,
    uint32_t len, uint32_t stream, const void *ptr)
{
	uint8_t hdr[9];
	ssize_t s;
	struct iovec iov[2];

	(void)wrk;

	AN(ftyp);
	AZ(flags & ~(ftyp->flags));
	if (stream == 0)
		AZ(ftyp->act_szero);
	else
		AZ(ftyp->act_snonzero);

	h2_mk_hdr(hdr, ftyp, flags, len, stream);
	Lck_Lock(&h2->sess->mtx);
	VSLb_bin(h2->vsl, SLT_H2TxHdr, 9, hdr);
	h2->srq->acct.resp_hdrbytes += 9;
	if (ftyp->overhead)
		h2->srq->acct.resp_bodybytes += len;
	Lck_Unlock(&h2->sess->mtx);

	memset(iov, 0, sizeof iov);
	iov[0].iov_base = (void*)hdr;
	iov[0].iov_len = sizeof hdr;
	iov[1].iov_base = TRUST_ME(ptr);
	iov[1].iov_len = len;
	s = writev(h2->sess->fd, iov, len == 0 ? 1 : 2);
	if (s != sizeof hdr + len) {
		/*
		 * There is no point in being nice here, we will be unable
		 * to send a GOAWAY once the code unrolls, so go directly
		 * to the finale and be done with it.
		 */
		h2->error = H2CE_PROTOCOL_ERROR;
	} else if (len > 0) {
		Lck_Lock(&h2->sess->mtx);
		VSLb_bin(h2->vsl, SLT_H2TxBody, len, ptr);
		Lck_Unlock(&h2->sess->mtx);
	}
}

static int64_t
h2_win_limit(const struct h2_req *r2, const struct h2_sess *h2)
{

	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(h2->req0, H2_REQ_MAGIC);

	Lck_AssertHeld(&h2->sess->mtx);
	return (vmin_t(int64_t, r2->t_window, h2->req0->t_window));
}

static void
h2_win_charge(struct h2_req *r2, const struct h2_sess *h2, uint32_t w)
{
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(h2->req0, H2_REQ_MAGIC);

	Lck_AssertHeld(&h2->sess->mtx);
	r2->t_window -= w;
	h2->req0->t_window -= w;
}

static h2_error
h2_errcheck(const struct h2_req *r2, const struct h2_sess *h2)
{
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);

	if (r2->error)
		return (r2->error);
	if (h2->error && r2->stream > h2->goaway_last_stream)
		return (h2->error);
	return (0);
}

static int64_t
h2_do_window(struct worker *wrk, struct h2_req *r2,
    struct h2_sess *h2, int64_t wanted)
{
	int64_t w = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);

	if (wanted == 0)
		return (0);

	Lck_Lock(&h2->sess->mtx);
	if (r2->t_window <= 0 || h2->req0->t_window <= 0) {
		r2->t_winupd = VTIM_real();
		h2_send_rel_locked(h2, r2);
		while (r2->t_window <= 0 && h2_errcheck(r2, h2) == 0) {
			r2->cond = &wrk->cond;
			(void)h2_cond_wait(r2->cond, h2, r2);
			r2->cond = NULL;
		}
		while (h2->req0->t_window <= 0 && h2_errcheck(r2, h2) == 0)
			(void)h2_cond_wait(h2->winupd_cond, h2, r2);

		if (h2_errcheck(r2, h2) == 0) {
			w = vmin_t(int64_t, h2_win_limit(r2, h2), wanted);
			h2_win_charge(r2, h2, w);
			assert (w > 0);
		}
		h2_send_get_locked(wrk, h2, r2);
	}

	if (w == 0 && h2_errcheck(r2, h2) == 0) {
		assert(r2->t_window > 0);
		assert(h2->req0->t_window > 0);
		w = h2_win_limit(r2, h2);
		if (w > wanted)
			w = wanted;
		h2_win_charge(r2, h2, w);
		assert (w > 0);
	}
	r2->t_winupd = 0;
	Lck_Unlock(&h2->sess->mtx);
	return (w);
}

/*
 * This is the per-stream frame sender.
 * XXX: priority
 */

static void
h2_send(struct worker *wrk, struct h2_req *r2, h2_frame ftyp, uint8_t flags,
    uint32_t len, const void *ptr, uint64_t *counter)
{
	struct h2_sess *h2;
	uint32_t mfs, tf;
	const char *p;
	uint8_t final_flags;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	h2 = r2->h2sess;
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	assert(len == 0 || ptr != NULL);
	AN(counter);

	AN(H2_SEND_HELD(h2, r2));

	if (h2_errcheck(r2, h2))
		return;

	AN(ftyp);
	AZ(flags & ~(ftyp->flags));
	if (r2->stream == 0)
		AZ(ftyp->act_szero);
	else
		AZ(ftyp->act_snonzero);

	Lck_Lock(&h2->sess->mtx);
	mfs = h2->remote_settings.max_frame_size;
	if (r2->counted && (
	    (ftyp == H2_F_HEADERS && (flags & H2FF_HEADERS_END_STREAM)) ||
	    (ftyp == H2_F_DATA && (flags & H2FF_DATA_END_STREAM)) ||
	    ftyp == H2_F_RST_STREAM
	    )) {
		assert(h2->open_streams > 0);
		h2->open_streams--;
		r2->counted = 0;
	}
	Lck_Unlock(&h2->sess->mtx);

	if (ftyp->respect_window) {
		tf = h2_do_window(wrk, r2, h2, (len > mfs) ? mfs : len);
		if (h2_errcheck(r2, h2))
			return;
		AN(H2_SEND_HELD(h2, r2));
	} else
		tf = mfs;

	if (len <= tf) {
		H2_Send_Frame(wrk, h2, ftyp, flags, len, r2->stream, ptr);
		*counter += len;
	} else {
		AN(ptr);
		p = ptr;
		final_flags = ftyp->final_flags & flags;
		flags &= ~ftyp->final_flags;
		do {
			AN(ftyp->continuation);
			if (!ftyp->respect_window)
				tf = mfs;
			if (ftyp->respect_window && p != ptr) {
				tf = h2_do_window(wrk, r2, h2,
				    (len > mfs) ? mfs : len);
				if (h2_errcheck(r2, h2))
					return;
				AN(H2_SEND_HELD(h2, r2));
			}
			if (tf < len) {
				H2_Send_Frame(wrk, h2, ftyp,
				    flags, tf, r2->stream, p);
			} else {
				if (ftyp->respect_window)
					assert(tf == len);
				tf = len;
				H2_Send_Frame(wrk, h2, ftyp, final_flags, tf,
				    r2->stream, p);
				flags = 0;
			}
			p += tf;
			len -= tf;
			*counter += tf;
			ftyp = ftyp->continuation;
			flags &= ftyp->flags;
			final_flags &= ftyp->flags;
		} while (!h2->error && len > 0);
	}
}

void
H2_Send_RST(struct worker *wrk, struct h2_sess *h2, const struct h2_req *r2,
    uint32_t stream, h2_error h2e)
{
	char b[4];

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	AN(H2_SEND_HELD(h2, r2));

	Lck_Lock(&h2->sess->mtx);
	VSLb(h2->vsl, SLT_Debug, "H2: stream %u: %s", stream, h2e->txt);
	Lck_Unlock(&h2->sess->mtx);
	vbe32enc(b, h2e->val);

	H2_Send_Frame(wrk, h2, H2_F_RST_STREAM, 0, sizeof b, stream, b);
}

void
H2_Send(struct worker *wrk, struct h2_req *r2, h2_frame ftyp, uint8_t flags,
    uint32_t len, const void *ptr, uint64_t *counter)
{
	uint64_t dummy_counter;

	if (counter == NULL)
		counter = &dummy_counter;

	h2_send(wrk, r2, ftyp, flags, len, ptr, counter);

	if (h2_errcheck(r2, r2->h2sess) == H2SE_CANCEL)
		H2_Send_RST(wrk, r2->h2sess, r2, r2->stream, H2SE_CANCEL);
}
