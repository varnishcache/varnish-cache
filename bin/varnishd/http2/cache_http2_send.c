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

#include "cache/cache_transport.h"
#include "http2/cache_http2.h"

#include "vend.h"

void
H2_Send_Get(struct worker *wrk, struct h2_sess *h2, struct h2_req *r2)
{
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);

	Lck_Lock(&h2->sess->mtx);
	r2->wrk = wrk;
	VTAILQ_INSERT_TAIL(&h2->txqueue, r2, tx_list);
	while (VTAILQ_FIRST(&h2->txqueue) != r2)
		AZ(Lck_CondWait(&wrk->cond, &h2->sess->mtx, 0));
	r2->wrk = NULL;
	Lck_Unlock(&h2->sess->mtx);
}

void
H2_Send_Rel(struct h2_sess *h2, const struct h2_req *r2)
{
	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(r2, H2_REQ_MAGIC);
	Lck_Lock(&h2->sess->mtx);
	assert(VTAILQ_FIRST(&h2->txqueue) == r2);
	VTAILQ_REMOVE(&h2->txqueue, r2, tx_list);
	r2 = VTAILQ_FIRST(&h2->txqueue);
	if (r2 != NULL) {
		CHECK_OBJ_NOTNULL(r2->wrk, WORKER_MAGIC);
		AZ(pthread_cond_signal(&r2->wrk->cond));
	}
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

/*
 * This is the per-stream frame sender.
 * XXX: windows
 * XXX: priority
 */

h2_error
H2_Send(struct worker *wrk, const struct h2_req *r2,
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

	if (r2->error)
		return (r2->error);

	if (h2->error && r2->stream > h2->goaway_last_stream)
		return (h2->error);

	AN(ftyp);
	AZ(flags & ~(ftyp->flags));
	if (r2->stream == 0)
		AZ(ftyp->act_szero);
	else
		AZ(ftyp->act_snonzero);

	Lck_Lock(&h2->sess->mtx);
	mfs = h2->remote_settings.max_frame_size;
	Lck_Unlock(&h2->sess->mtx);
	if (len < mfs) {
		retval = H2_Send_Frame(wrk, h2,
		    ftyp, flags, len, r2->stream, ptr);
	} else {
		AN(ptr);
		p = ptr;
		final_flags = ftyp->final_flags & flags;
		flags &= ~ftyp->final_flags;
		do {
			AN(ftyp->continuation);
			tf = mfs;
			if (tf < len) {
				retval = H2_Send_Frame(wrk, h2, ftyp,
				    flags, tf, r2->stream, p);
			} else {
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
	return (retval);
}
