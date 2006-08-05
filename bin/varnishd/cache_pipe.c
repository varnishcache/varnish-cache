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
#include "cache.h"

static void
rdf(struct pollfd *fds, int idx)
{
	int i, j;
	char buf[BUFSIZ];

	i = read(fds[idx].fd, buf, sizeof buf);
	if (i <= 0) {
		shutdown(fds[idx].fd, SHUT_RD);
		shutdown(fds[1-idx].fd, SHUT_WR);
		fds[idx].events = 0;
	} else {
		j = write(fds[1-idx].fd, buf, i);
		if (i != j) {
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
	assert(vc != NULL);
	VSL(SLT_Backend, sp->fd, "%d %s", vc->fd, sp->backend->vcl_name);

	http_CopyReq(vc->fd, vc->http, sp->http);
	http_FilterHeader(vc->fd, vc->http, sp->http, HTTPH_R_PIPE);
	http_PrintfHeader(vc->fd, vc->http, "X-Varnish: %u", sp->xid);
	WRK_Reset(w, &vc->fd);
	http_Write(w, vc->http, 0);

	if (http_GetTail(sp->http, 0, &b, &e) && b != e)
		WRK_Write(w, b, e - b);

	if (WRK_Flush(w)) {
		vca_close_session(sp, "pipe");
		VBE_ClosedFd(vc);
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
		i = poll(fds, 2, INFTIM);
		assert(i > 0);
		if (fds[0].revents)
			rdf(fds, 0);
		if (fds[1].revents)
			rdf(fds, 1);
	}
	vca_close_session(sp, "pipe");
	VBE_ClosedFd(vc);
}
