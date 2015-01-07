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

#include "cache/cache_backend.h"
#include "cache/cache_director.h"
#include "vtcp.h"
#include "vtim.h"

static struct lock pipestat_mtx;

struct acct_pipe {
	uint64_t	req;
	uint64_t	bereq;
	uint64_t	in;
	uint64_t	out;
};

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

static void
pipecharge(struct req *req, const struct acct_pipe *a, struct VSC_C_vbe *b)
{

	VSLb(req->vsl, SLT_PipeAcct, "%ju %ju %ju %ju",
	    (uintmax_t)a->req,
	    (uintmax_t)a->bereq,
	    (uintmax_t)a->in,
	    (uintmax_t)a->out);

	Lck_Lock(&pipestat_mtx);
	VSC_C_main->s_pipe_hdrbytes += a->req;
	VSC_C_main->s_pipe_in += a->in;
	VSC_C_main->s_pipe_out += a->out;
	if (b != NULL) {
		b->pipe_hdrbytes += a->bereq;
		b->pipe_out += a->in;
		b->pipe_in += a->out;
	}
	Lck_Unlock(&pipestat_mtx);
}

void
V1P_Process(struct req *req, struct busyobj *bo, int fd)
{
	struct worker *wrk;
	struct pollfd fds[2];
	int i;
	struct acct_pipe acct_pipe;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);
	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	req->res_mode = RES_PIPE;

	memset(&acct_pipe, 0, sizeof acct_pipe);
	acct_pipe.req = req->acct.req_hdrbytes;
	req->acct.req_hdrbytes = 0;

	if (fd < 0) {
		pipecharge(req, &acct_pipe, NULL);
		SES_Close(req->sp, SC_OVERLOAD);
		return;
	}
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	bo->wrk = req->wrk;
	bo->director_state = DIR_S_BODY;
	(void)VTCP_blocking(fd);

	V1L_Reserve(wrk, wrk->aws, &fd, bo->vsl, req->t_req);
	acct_pipe.bereq += HTTP1_Write(wrk, bo->bereq, HTTP1_Req);

	if (req->htc->pipeline_b != NULL)
		acct_pipe.in += V1L_Write(wrk, req->htc->pipeline_b,
		    req->htc->pipeline_e - req->htc->pipeline_b);

	i = V1L_FlushRelease(wrk);

	VSLb_ts_req(req, "Pipe", W_TIM_real(wrk));

	if (i == 0) {
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
			    rdf(fd, req->sp->fd, &acct_pipe.out)) {
				if (fds[1].fd == -1)
					break;
				(void)shutdown(fd, SHUT_RD);
				(void)shutdown(req->sp->fd, SHUT_WR);
				fds[0].events = 0;
				fds[0].fd = -1;
			}
			if (fds[1].revents &&
			    rdf(req->sp->fd, fd, &acct_pipe.in)) {
				if (fds[0].fd == -1)
					break;
				(void)shutdown(req->sp->fd, SHUT_RD);
				(void)shutdown(fd, SHUT_WR);
				fds[1].events = 0;
				fds[1].fd = -1;
			}
		}
	}
	VSLb_ts_req(req, "PipeSess", W_TIM_real(wrk));
	pipecharge(req, &acct_pipe, bo->htc->vbc->backend->vsc);
	SES_Close(req->sp, SC_TX_PIPE);
	bo->doclose = SC_TX_PIPE;
}

/*--------------------------------------------------------------------*/

void
V1P_Init(void)
{

	Lck_New(&pipestat_mtx, lck_pipestat);
}
