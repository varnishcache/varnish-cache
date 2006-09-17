/*
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

#include "shmlog.h"
#include "heritage.h"
#include "cache.h"

static void
rdf(struct pollfd *fds, int idx)
{
	int i, j;
	char buf[BUFSIZ];

	i = read(fds[idx].fd, buf, sizeof buf);
	if (i <= 0) {
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
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	w = sp->wrk;

	vc = VBE_GetFd(sp->backend, sp->xid);
	if (vc == NULL) {
		RES_Error(sp, 503, "Backend did not respond.");
		return;
	}
	VSL(SLT_Backend, sp->fd, "%d %s", vc->fd, sp->backend->vcl_name);
	vc->http->logtag = HTTP_Tx;

	http_CopyReq(w, vc->fd, vc->http, sp->http);
	http_FilterHeader(w, vc->fd, vc->http, sp->http, HTTPH_R_PIPE);
	http_PrintfHeader(w, vc->fd, vc->http, "X-Varnish: %u", sp->xid);
	WRK_Reset(w, &vc->fd);
	http_Write(w, vc->http, 0);

	if (http_GetTail(sp->http, 0, &b, &e) && b != e)
		WRK_Write(w, b, e - b);

	if (WRK_Flush(w)) {
		vca_close_session(sp, "pipe");
		VBE_ClosedFd(vc, 0);
		return;
	}

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
	VBE_ClosedFd(vc, 1);
}
