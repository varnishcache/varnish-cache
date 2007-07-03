/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>

#ifndef HAVE_CLOCK_GETTIME
#include "compat/clock_gettime.h"
#endif

#include "shmlog.h"
#include "heritage.h"
#include "cache.h"

static void
rdf(struct pollfd *fds, int idx)
{
	int i, j;
	char buf[BUFSIZ];

	i = read(fds[idx].fd, buf, sizeof buf);
	if (i <= 0 || fds[1-idx].events == 0) {
		VSL(SLT_Debug, fds[idx].fd, "Pipe Shut read(read)");
		VSL(SLT_Debug, fds[1-idx].fd, "Pipe Shut write(read)");
		shutdown(fds[idx].fd, SHUT_RD);
		shutdown(fds[1-idx].fd, SHUT_WR);
		fds[idx].events = 0;
	} else {
		j = write(fds[1-idx].fd, buf, i);
		if (i != j) {
			VSL(SLT_Debug, fds[idx].fd, "Pipe Shut write(write)");
			VSL(SLT_Debug, fds[1-idx].fd, "Pipe Shut read(write)");
			shutdown(fds[idx].fd, SHUT_WR);
			shutdown(fds[1-idx].fd, SHUT_RD);
			fds[1-idx].events = 0;
		}
	}
}

void
PipeSession(struct sess *sp)
{
	struct vbe_conn *vc;
	char *b, *e;
	struct worker *w;
	struct pollfd fds[2];
	struct bereq *bereq;
	struct http *hp;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	w = sp->wrk;

	vc = VBE_GetFd(sp);
	if (vc == NULL)
		return;

	bereq = vbe_new_bereq();
	AN(bereq);
	hp = bereq->http;
	hp->logtag = HTTP_Tx;

	http_CopyReq(w, vc->fd, hp, sp->http);
	http_FilterHeader(w, vc->fd, hp, sp->http, HTTPH_R_PIPE);
	http_PrintfHeader(w, vc->fd, hp, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(w, vc->fd, hp,
	    "X-Forwarded-for: %s", sp->addr);

	/* XXX: does this belong in VCL ? */
	if (!http_GetHdr(hp, H_Host, &b)) {
		http_PrintfHeader(w, vc->fd, hp, "Host: %s",
		    sp->backend->hostname);
	}

	VCL_pipe_method(sp);

	if (sp->handling == VCL_RET_ERROR)
		INCOMPL();

	WRK_Reset(w, &vc->fd);
	http_Write(w, hp, 0);

	if (http_GetTail(sp->http, 0, &b, &e) && b != e)
		WRK_Write(w, b, e - b);

	if (WRK_Flush(w)) {
		vca_close_session(sp, "pipe");
		VBE_ClosedFd(sp->wrk, vc, 0);
		return;
	}

	vbe_free_bereq(bereq);
	bereq = NULL;
	hp = NULL;

	clock_gettime(CLOCK_REALTIME, &sp->t_resp);

	memset(fds, 0, sizeof fds);
	fds[0].fd = vc->fd;
	fds[0].events = POLLIN | POLLERR;
	fds[1].fd = sp->fd;
	fds[1].events = POLLIN | POLLERR;

	while (fds[0].events || fds[1].events) {
		fds[0].revents = 0;
		fds[1].revents = 0;
		i = poll(fds, 2, params->pipe_timeout * 1000);
		if (i != 1)
			break;
		if (fds[0].revents)
			rdf(fds, 0);
		if (fds[1].revents)
			rdf(fds, 1);
	}
	vca_close_session(sp, "pipe");
	(void)close (vc->fd);
	VBE_ClosedFd(sp->wrk, vc, 1);
}
