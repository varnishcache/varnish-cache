/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Write data to fd
 * We try to use writev() if possible in order to minimize number of
 * syscalls made and packets sent.  It also just might allow the worker
 * thread to complete the request without holding stuff locked.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/uio.h>

#include <limits.h>
#include <stdio.h>

#include "cache.h"
#include "vtim.h"

/*--------------------------------------------------------------------*/

struct wrw {
	unsigned		magic;
#define WRW_MAGIC		0x2f2142e5
	int			*wfd;
	unsigned		werr;	/* valid after WRW_Flush() */
	struct iovec		*iov;
	unsigned		siov;
	unsigned		niov;
	ssize_t			liov;
	ssize_t			cliov;
	unsigned		ciov;	/* Chunked header marker */
	double			t0;
	struct vsl_log		*vsl;
	ssize_t			cnt;	/* Flushed byte count */
};

/*--------------------------------------------------------------------
 */

int
WRW_Error(const struct worker *wrk)
{

	return (wrk->wrw->werr);
}

void
WRW_Reserve(struct worker *wrk, int *fd, struct vsl_log *vsl, double t0)
{
	struct wrw *wrw;
	unsigned u;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AZ(wrk->wrw);
	wrw = (void*)WS_Alloc(wrk->aws, sizeof *wrw);
	AN(wrw);
	memset(wrw, 0, sizeof *wrw);
	wrw->magic = WRW_MAGIC;
	u = WS_Reserve(wrk->aws, 0);
	u = PRNDDN(u);
	u /= sizeof(struct iovec);
	if (u > IOV_MAX)
		u = IOV_MAX;
	AN(u);
	wrw->iov = (void*)PRNDUP(wrk->aws->f);
	wrw->siov = u;
	wrw->ciov = u;
	wrw->werr = 0;
	wrw->liov = 0;
	wrw->niov = 0;
	wrw->wfd = fd;
	wrw->t0 = t0;
	wrw->vsl = vsl;
	wrk->wrw = wrw;
}

static void
wrw_release(struct worker *wrk, ssize_t *pacc)
{
	struct wrw *wrw;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	wrw = wrk->wrw;
	wrk->wrw = NULL;
	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);
	if (pacc != NULL)
		*pacc += wrw->cnt;
	WS_Release(wrk->aws, 0);
	WS_Reset(wrk->aws, NULL);
}

static void
wrw_prune(struct wrw *wrw, ssize_t bytes)
{
	ssize_t used = 0;
	ssize_t j, used_here;

	for (j = 0; j < wrw->niov; j++) {
		if (used + wrw->iov[j].iov_len > bytes) {
			/* Cutoff is in this iov */
			used_here = bytes - used;
			wrw->iov[j].iov_len -= used_here;
			wrw->iov[j].iov_base =
			    (char*)wrw->iov[j].iov_base + used_here;
			memmove(wrw->iov, &wrw->iov[j],
			    (wrw->niov - j) * sizeof(struct iovec));
			wrw->niov -= j;
			wrw->liov -= bytes;
			return;
		}
		used += wrw->iov[j].iov_len;
	}
	assert(wrw->liov == 0);
}

unsigned
WRW_Flush(const struct worker *wrk)
{
	ssize_t i;
	struct wrw *wrw;
	char cbuf[32];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	wrw = wrk->wrw;
	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);
	AN(wrw->wfd);

	/* For chunked, there must be one slot reserved for the chunked tail */
	if (wrw->ciov < wrw->siov)
		assert(wrw->niov < wrw->siov);

	if (*wrw->wfd >= 0 && wrw->liov > 0 && wrw->werr == 0) {
		if (wrw->ciov < wrw->siov && wrw->cliov > 0) {
			/* Add chunk head & tail */
			bprintf(cbuf, "00%zx\r\n", wrw->cliov);
			i = strlen(cbuf);
			wrw->iov[wrw->ciov].iov_base = cbuf;
			wrw->iov[wrw->ciov].iov_len = i;
			wrw->liov += i;

			wrw->iov[wrw->niov].iov_base = cbuf + i - 2;
			wrw->iov[wrw->niov++].iov_len = 2;
			wrw->liov += 2;
		} else if (wrw->ciov < wrw->siov) {
			wrw->iov[wrw->ciov].iov_base = cbuf;
			wrw->iov[wrw->ciov].iov_len = 0;
		}

		i = writev(*wrw->wfd, wrw->iov, wrw->niov);
		if (i > 0)
			wrw->cnt += i;
		while (i != wrw->liov && i > 0) {
			/* Remove sent data from start of I/O vector,
			 * then retry; we hit a timeout, but some data
			 * was sent.
			 *
			 * XXX: Add a "minimum sent data per timeout
			 * counter to prevent slowlaris attacks
			*/

			if (VTIM_real() - wrw->t0 > cache_param->send_timeout) {
				VSLb(wrw->vsl, SLT_Debug,
				    "Hit total send timeout, "
				    "wrote = %zd/%zd; not retrying",
				    i, wrw->liov);
				i = -1;
				break;
			}

			VSLb(wrw->vsl, SLT_Debug,
			    "Hit idle send timeout, wrote = %zd/%zd; retrying",
			    i, wrw->liov);

			wrw_prune(wrw, i);
			i = writev(*wrw->wfd, wrw->iov, wrw->niov);
			if (i > 0)
				wrw->cnt += i;
		}
		if (i <= 0) {
			wrw->werr++;
			VSLb(wrw->vsl, SLT_Debug,
			    "Write error, retval = %zd, len = %zd, errno = %s",
			    i, wrw->liov, strerror(errno));
		}
	}
	wrw->liov = 0;
	wrw->cliov = 0;
	wrw->niov = 0;
	if (wrw->ciov < wrw->siov)
		wrw->ciov = wrw->niov++;
	return (wrw->werr);
}

unsigned
WRW_FlushRelease(struct worker *wrk, ssize_t *pacc)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(wrk->wrw->wfd);
	u = WRW_Flush(wrk);
	wrw_release(wrk, pacc);
	return (u);
}

unsigned
WRW_WriteH(const struct worker *wrk, const txt *hh, const char *suf)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(wrk->wrw->wfd);
	AN(wrk);
	AN(hh);
	AN(hh->b);
	AN(hh->e);
	u = WRW_Write(wrk, hh->b, hh->e - hh->b);
	if (suf != NULL)
		u += WRW_Write(wrk, suf, -1);
	return (u);
}

unsigned
WRW_Write(const struct worker *wrk, const void *ptr, int len)
{
	struct wrw *wrw;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	wrw = wrk->wrw;
	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);
	AN(wrw->wfd);
	if (len == 0 || *wrw->wfd < 0)
		return (0);
	if (len == -1)
		len = strlen(ptr);
	if (wrw->niov >= wrw->siov - (wrw->ciov < wrw->siov ? 1 : 0))
		(void)WRW_Flush(wrk);
	wrw->iov[wrw->niov].iov_base = TRUST_ME(ptr);
	wrw->iov[wrw->niov].iov_len = len;
	wrw->liov += len;
	wrw->niov++;
	if (wrw->ciov < wrw->siov) {
		assert(wrw->niov < wrw->siov);
		wrw->cliov += len;
	}
	return (len);
}

void
WRW_Chunked(const struct worker *wrk)
{
	struct wrw *wrw;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	wrw = wrk->wrw;
	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);

	assert(wrw->ciov == wrw->siov);
	/*
	 * If there are not space for chunked header, a chunk of data and
	 * a chunk tail, we might as well flush right away.
	 */
	if (wrw->niov + 3 >= wrw->siov)
		(void)WRW_Flush(wrk);
	wrw->ciov = wrw->niov++;
	wrw->cliov = 0;
	assert(wrw->ciov < wrw->siov);
	assert(wrw->niov < wrw->siov);
}

/*
 * XXX: It is not worth the complexity to attempt to get the
 * XXX: end of chunk into the WRW_Flush(), because most of the time
 * XXX: if not always, that is a no-op anyway, because the calling
 * XXX: code already called WRW_Flush() to release local storage.
 */

void
WRW_EndChunk(const struct worker *wrk)
{
	struct wrw *wrw;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	wrw = wrk->wrw;
	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);

	assert(wrw->ciov < wrw->siov);
	(void)WRW_Flush(wrk);
	wrw->ciov = wrw->siov;
	wrw->niov = 0;
	wrw->cliov = 0;
	(void)WRW_Write(wrk, "0\r\n\r\n", -1);
}
