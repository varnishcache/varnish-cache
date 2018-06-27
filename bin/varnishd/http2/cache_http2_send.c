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

#include "cache/cache_transport.h"
#include "http2/cache_http2.h"

#include "vend.h"
#include "vtim.h"

static void
h2_send_get(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	Lck_AssertHeld(&h2->sess->mtx);
	if (&wrk->cond == h2->cond)
		ASSERT_RXTHR(h2);
	r2->wrk = wrk;
	VTAILQ_INSERT_TAIL(&h2->txqueue, r2, tx_list);
	while (VTAILQ_FIRST(&h2->txqueue) != r2)
		AZ(Lck_CondWait(&wrk->cond, &h2->sess->mtx, 0));
	r2->wrk = NULL;
}

void
H2_Send_Get(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	Lck_Lock(&h2->sess->mtx);
	h2_send_get(wrk, h2, r2);
	Lck_Unlock(&h2->sess->mtx);
}

static void
h2_send_rel(struct h2_sess *h2, const struct h2_req *r2)
{
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);

	Lck_AssertHeld(&h2->sess->mtx);
	assert(VTAILQ_FIRST(&h2->txqueue) == r2);
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
	h2_send_rel(h2, r2);
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
 * This is the "raw" frame sender, all per stream accounting and
 * prioritization must have happened before this is called, and
 * the session mtx must be held.
 */

h2_error
H2_Send_Frame(struct worker *wrk, const struct h2_sess *h2,
    h2_frame ftyp, uint8_t flags,
    uint32_t len, uint32_t stream, const void *ptr)
{
	uint8_t hdr[9];
	ssize_t s;

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

	s = write(h2->sess->fd, hdr, sizeof hdr);
	if (s != sizeof hdr)
		return (H2CE_PROTOCOL_ERROR);		// XXX Need private ?
	if (len > 0) {
		s = write(h2->sess->fd, ptr, len);
		if (s != len)
			return (H2CE_PROTOCOL_ERROR);	// XXX Need private ?
		Lck_Lock(&h2->sess->mtx);
		VSLb_bin(h2->vsl, SLT_H2TxBody, len, ptr);
		Lck_Unlock(&h2->sess->mtx);
	}
	return (0);
}

static int64_t
h2_win_limit(const struct h2_req *r2, const struct h2_sess *h2)
{
	int64_t m;

	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(h2->req0, H2_REQ_MAGIC);

	Lck_AssertHeld(&h2->sess->mtx);
	m = r2->t_window;
	if (m > h2->req0->t_window)
		m = h2->req0->t_window;
	return (m);
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
		h2_send_rel(h2, r2);
		while (r2->t_window <= 0 && h2_errcheck(r2, h2) == 0) {
			r2->cond = &wrk->cond;
			AZ(Lck_CondWait(r2->cond, &h2->sess->mtx, 0));
			r2->cond = NULL;
		}
		while (h2->req0->t_window <= 0 && h2_errcheck(r2, h2) == 0) {
			AZ(Lck_CondWait(h2->winupd_cond, &h2->sess->mtx, 0));
		}

		if (h2_errcheck(r2, h2) == 0) {
			w = h2_win_limit(r2, h2);
			if (w > wanted)
				w = wanted;
			h2_win_charge(r2, h2, w);
			assert (w > 0);
		}
		h2_send_get(wrk, h2, r2);
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

void
H2_Send(struct worker *wrk, struct h2_req *r2,
    h2_frame ftyp, uint8_t flags, uint32_t len, const void *ptr)
{
	h2_error retval;
	struct h2_sess *h2;
	uint32_t mfs, tf;
	const char *p;
	uint8_t final_flags;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	h2 = r2->h2sess;
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	assert(len == 0 || ptr != NULL);

	assert(VTAILQ_FIRST(&h2->txqueue) == r2);

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
	Lck_Unlock(&h2->sess->mtx);

	if (ftyp->respect_window) {
		tf = h2_do_window(wrk, r2, h2,
				  (len > mfs) ? mfs : len);
		if (h2_errcheck(r2, h2))
			return;
		assert(VTAILQ_FIRST(&h2->txqueue) == r2);
	} else
		tf = mfs;

	if (len <= tf) {
		(void)H2_Send_Frame(wrk, h2,
		    ftyp, flags, len, r2->stream, ptr);
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
				assert(VTAILQ_FIRST(&h2->txqueue) == r2);
			}
			if (tf < len) {
				retval = H2_Send_Frame(wrk, h2, ftyp,
				    flags, tf, r2->stream, p);
			} else {
				if (ftyp->respect_window)
					assert(tf == len);
				tf = len;
				retval = H2_Send_Frame(wrk, h2, ftyp,
				    final_flags, tf, r2->stream, p);
				flags = 0;
			}
			p += tf;
			len -= tf;
			ftyp = ftyp->continuation;
		} while (len > 0 && retval == 0);
	}
}
