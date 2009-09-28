/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 *
 * XXX: charge bytes to srcaddr
 */

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "shmlog.h"
#include "cache.h"

static int
rdf(int fd0, int fd1)
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
		if (i != j)
			(void)usleep(100000);		/* XXX hack */
	}
	return (0);
}

void
PipeSession(struct sess *sp)
{
	struct vbe_conn *vc;
	struct worker *w;
	struct bereq *bereq;
	struct pollfd fds[2];
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	w = sp->wrk;
	bereq = sp->bereq;
	sp->bereq = NULL;

	VBE_GetFd(sp);
	if (sp->vbe == NULL)
		return;
	vc = sp->vbe;
	TCP_blocking(vc->fd);

	WRW_Reserve(w, &vc->fd);
	sp->acct_req.hdrbytes += http_Write(w, bereq->http, 0);

	if (sp->htc->pipeline.b != NULL)
		sp->acct_req.bodybytes +=
		    WRW_Write(w, sp->htc->pipeline.b, Tlen(sp->htc->pipeline));

	i = WRW_FlushRelease(w);
	VBE_free_bereq(&bereq);

	if (i) {
		vca_close_session(sp, "pipe");
		VBE_ClosedFd(sp);
		return;
	}

	sp->t_resp = TIM_real();

	memset(fds, 0, sizeof fds);

	TCP_linger(vc->fd, 0);
	fds[0].fd = vc->fd;
	fds[0].events = POLLIN | POLLERR;

	TCP_linger(sp->fd, 0);
	fds[1].fd = sp->fd;
	fds[1].events = POLLIN | POLLERR;

	while (fds[0].fd > -1 || fds[1].fd > -1) {
		fds[0].revents = 0;
		fds[1].revents = 0;
		i = poll(fds, 2, params->pipe_timeout * 1000);
		if (i < 1)
			break;
		if (fds[0].revents && rdf(vc->fd, sp->fd)) {
			(void)shutdown(vc->fd, SHUT_RD);
			(void)shutdown(sp->fd, SHUT_WR);
			fds[0].events = 0;
			fds[0].fd = -1;
		}
		if (fds[1].revents && rdf(sp->fd, vc->fd)) {
			(void)shutdown(sp->fd, SHUT_RD);
			(void)shutdown(vc->fd, SHUT_WR);
			fds[1].events = 0;
			fds[1].fd = -1;
		}
	}
	if (fds[0].fd >= 0) {
		(void)shutdown(vc->fd, SHUT_RD);
		(void)shutdown(sp->fd, SHUT_WR);
	}
	if (fds[1].fd >= 0) {
		(void)shutdown(sp->fd, SHUT_RD);
		(void)shutdown(vc->fd, SHUT_WR);
	}
	vca_close_session(sp, "pipe");
	VBE_ClosedFd(sp);
}
