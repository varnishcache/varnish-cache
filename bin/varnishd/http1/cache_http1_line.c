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

#include "cache/cache.h"
#include "vtim.h"

/*--------------------------------------------------------------------*/

struct v1l {
	unsigned		magic;
#define V1L_MAGIC		0x2f2142e5
	int			*wfd;
	unsigned		werr;	/* valid after V1L_Flush() */
	struct iovec		*iov;
	unsigned		siov;
	unsigned		niov;
	ssize_t			liov;
	ssize_t			cliov;
	unsigned		ciov;	/* Chunked header marker */
	double			t0;
	struct vsl_log		*vsl;
	ssize_t			cnt;	/* Flushed byte count */
	struct ws		*ws;
	void			*res;
};

/*--------------------------------------------------------------------
 */

void
V1L_Reserve(struct worker *wrk, struct ws *ws, int *fd, struct vsl_log *vsl,
    double t0)
{
	struct v1l *v1l;
	unsigned u;
	void *res;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AZ(wrk->v1l);
	
	res = WS_Snapshot(ws);
	v1l = WS_Alloc(ws, sizeof *v1l);
	AN(v1l);
	INIT_OBJ(v1l, V1L_MAGIC);

	v1l->ws = ws;
	v1l->res = res;

	u = WS_Reserve(ws, 0);
	u = PRNDDN(u);
	u /= sizeof(struct iovec);
	if (u > IOV_MAX)
		u = IOV_MAX;
	AN(u);
	v1l->iov = (void*)PRNDUP(ws->f);
	v1l->siov = u;
	v1l->ciov = u;
	v1l->werr = 0;
	v1l->liov = 0;
	v1l->niov = 0;
	v1l->wfd = fd;
	v1l->t0 = t0;
	v1l->vsl = vsl;
	wrk->v1l = v1l;
}

unsigned
V1L_FlushRelease(struct worker *wrk)
{
	struct v1l *v1l;
	unsigned u;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	u = V1L_Flush(wrk);
	v1l = wrk->v1l;
	wrk->v1l = NULL;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);
	WS_Release(v1l->ws, 0);
	WS_Reset(v1l->ws, v1l->res);
	return (u);
}

static void
v1l_prune(struct v1l *v1l, ssize_t bytes)
{
	ssize_t used = 0;
	ssize_t j, used_here;

	for (j = 0; j < v1l->niov; j++) {
		if (used + v1l->iov[j].iov_len > bytes) {
			/* Cutoff is in this iov */
			used_here = bytes - used;
			v1l->iov[j].iov_len -= used_here;
			v1l->iov[j].iov_base =
			    (char*)v1l->iov[j].iov_base + used_here;
			memmove(v1l->iov, &v1l->iov[j],
			    (v1l->niov - j) * sizeof(struct iovec));
			v1l->niov -= j;
			v1l->liov -= bytes;
			return;
		}
		used += v1l->iov[j].iov_len;
	}
	AZ(v1l->liov);
}

unsigned
V1L_Flush(const struct worker *wrk)
{
	ssize_t i;
	struct v1l *v1l;
	char cbuf[32];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v1l = wrk->v1l;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);
	AN(v1l->wfd);

	/* For chunked, there must be one slot reserved for the chunked tail */
	if (v1l->ciov < v1l->siov)
		assert(v1l->niov < v1l->siov);

	if (*v1l->wfd >= 0 && v1l->liov > 0 && v1l->werr == 0) {
		if (v1l->ciov < v1l->siov && v1l->cliov > 0) {
			/* Add chunk head & tail */
			bprintf(cbuf, "00%zx\r\n", v1l->cliov);
			i = strlen(cbuf);
			v1l->iov[v1l->ciov].iov_base = cbuf;
			v1l->iov[v1l->ciov].iov_len = i;
			v1l->liov += i;

			v1l->iov[v1l->niov].iov_base = cbuf + i - 2;
			v1l->iov[v1l->niov++].iov_len = 2;
			v1l->liov += 2;
		} else if (v1l->ciov < v1l->siov) {
			v1l->iov[v1l->ciov].iov_base = cbuf;
			v1l->iov[v1l->ciov].iov_len = 0;
		}

		i = writev(*v1l->wfd, v1l->iov, v1l->niov);
		if (i > 0)
			v1l->cnt += i;
		while (i != v1l->liov && i > 0) {
			/* Remove sent data from start of I/O vector,
			 * then retry; we hit a timeout, but some data
			 * was sent.
			 *
			 * XXX: Add a "minimum sent data per timeout
			 * counter to prevent slowlaris attacks
			*/

			if (VTIM_real() - v1l->t0 > cache_param->send_timeout) {
				VSLb(v1l->vsl, SLT_Debug,
				    "Hit total send timeout, "
				    "wrote = %zd/%zd; not retrying",
				    i, v1l->liov);
				i = -1;
				break;
			}

			VSLb(v1l->vsl, SLT_Debug,
			    "Hit idle send timeout, wrote = %zd/%zd; retrying",
			    i, v1l->liov);

			v1l_prune(v1l, i);
			i = writev(*v1l->wfd, v1l->iov, v1l->niov);
			if (i > 0)
				v1l->cnt += i;
		}
		if (i <= 0) {
			v1l->werr++;
			VSLb(v1l->vsl, SLT_Debug,
			    "Write error, retval = %zd, len = %zd, errno = %s",
			    i, v1l->liov, strerror(errno));
		}
	}
	v1l->liov = 0;
	v1l->cliov = 0;
	v1l->niov = 0;
	if (v1l->ciov < v1l->siov)
		v1l->ciov = v1l->niov++;
	return (v1l->werr);
}

unsigned
V1L_Write(const struct worker *wrk, const void *ptr, int len)
{
	struct v1l *v1l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v1l = wrk->v1l;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);
	AN(v1l->wfd);
	if (len == 0 || *v1l->wfd < 0)
		return (0);
	if (len == -1)
		len = strlen(ptr);
	if (v1l->niov >= v1l->siov - (v1l->ciov < v1l->siov ? 1 : 0))
		(void)V1L_Flush(wrk);
	v1l->iov[v1l->niov].iov_base = TRUST_ME(ptr);
	v1l->iov[v1l->niov].iov_len = len;
	v1l->liov += len;
	v1l->niov++;
	if (v1l->ciov < v1l->siov) {
		assert(v1l->niov < v1l->siov);
		v1l->cliov += len;
	}
	return (len);
}

void
V1L_Chunked(const struct worker *wrk)
{
	struct v1l *v1l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v1l = wrk->v1l;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);

	assert(v1l->ciov == v1l->siov);
	/*
	 * If there are not space for chunked header, a chunk of data and
	 * a chunk tail, we might as well flush right away.
	 */
	if (v1l->niov + 3 >= v1l->siov)
		(void)V1L_Flush(wrk);
	v1l->ciov = v1l->niov++;
	v1l->cliov = 0;
	assert(v1l->ciov < v1l->siov);
	assert(v1l->niov < v1l->siov);
}

/*
 * XXX: It is not worth the complexity to attempt to get the
 * XXX: end of chunk into the V1L_Flush(), because most of the time
 * XXX: if not always, that is a no-op anyway, because the calling
 * XXX: code already called V1L_Flush() to release local storage.
 */

void
V1L_EndChunk(const struct worker *wrk)
{
	struct v1l *v1l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v1l = wrk->v1l;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);

	assert(v1l->ciov < v1l->siov);
	(void)V1L_Flush(wrk);
	v1l->ciov = v1l->siov;
	v1l->niov = 0;
	v1l->cliov = 0;
	(void)V1L_Write(wrk, "0\r\n\r\n", -1);
}
