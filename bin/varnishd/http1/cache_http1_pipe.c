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
 * XXX: charge bytes to srcaddr
 */

#include "config.h"

#include <poll.h>
#include <stdio.h>

#include "cache/cache.h"

#include "vrt.h"

#include "cache_http1.h"

static struct lock pipestat_mtx;

static int
rdf(int fd0, int fd1, uint64_t *pcnt)
{
	int i, j;
	char buf[BUFSIZ], *p;

	i = read(fd0, buf, sizeof buf);
	if (i <= 0)
		return (1);
	for (p = buf; i > 0; i -= j, p += j) {
		j = write(fd1, p, i);
		if (j <= 0)
			return (1);
		*pcnt += j;
		if (i != j)
			(void)usleep(100000);		/* XXX hack */
	}
	return (0);
}

void
V1P_Charge(struct req *req, const struct v1p_acct *a, struct VSC_C_vbe *b)
{

	AN(b);
	VSLb(req->vsl, SLT_PipeAcct, "%ju %ju %ju %ju",
	    (uintmax_t)a->req,
	    (uintmax_t)a->bereq,
	    (uintmax_t)a->in,
	    (uintmax_t)a->out);

	Lck_Lock(&pipestat_mtx);
	VSC_C_main->s_pipe_hdrbytes += a->req;
	VSC_C_main->s_pipe_in += a->in;
	VSC_C_main->s_pipe_out += a->out;
	b->pipe_hdrbytes += a->bereq;
	b->pipe_out += a->in;
	b->pipe_in += a->out;
	Lck_Unlock(&pipestat_mtx);
}

void
V1P_Process(struct req *req, int fd, struct v1p_acct *v1a)
{
	struct pollfd fds[2];
	int i, j;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);
	assert(fd > 0);

	if (req->htc->pipeline_b != NULL) {
		j = write(fd,  req->htc->pipeline_b,
		    req->htc->pipeline_e - req->htc->pipeline_b);
		if (j < 0)
			return;
		req->htc->pipeline_b = NULL;
		req->htc->pipeline_e = NULL;
		v1a->in += j;
	}
	memset(fds, 0, sizeof fds);
	fds[0].fd = fd;
	fds[0].events = POLLIN | POLLERR;
	fds[1].fd = req->sp->fd;
	fds[1].events = POLLIN | POLLERR;

	while (fds[0].fd > -1 || fds[1].fd > -1) {
		fds[0].revents = 0;
		fds[1].revents = 0;
		i = poll(fds, 2,
		    (int)(cache_param->pipe_timeout * 1e3));
		if (i < 1)
			break;
		if (fds[0].revents &&
		    rdf(fd, req->sp->fd, &v1a->out)) {
			if (fds[1].fd == -1)
				break;
			(void)shutdown(fd, SHUT_RD);
			(void)shutdown(req->sp->fd, SHUT_WR);
			fds[0].events = 0;
			fds[0].fd = -1;
		}
		if (fds[1].revents &&
		    rdf(req->sp->fd, fd, &v1a->in)) {
			if (fds[0].fd == -1)
				break;
			(void)shutdown(req->sp->fd, SHUT_RD);
			(void)shutdown(fd, SHUT_WR);
			fds[1].events = 0;
			fds[1].fd = -1;
		}
	}
}

/*--------------------------------------------------------------------*/

void
V1P_Init(void)
{

	Lck_New(&pipestat_mtx, lck_pipestat);
}
