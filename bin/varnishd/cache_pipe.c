/*
 * $Id$
 *
 * XXX: charge bytes to srcaddr
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "cache.h"

struct edir {
	int fd;
	struct event ev;
};

static void
rdf(int fd, short event, void *arg)
{
	int i, j;
	struct edir *ep;
	char buf[BUFSIZ];

	(void)event;

	ep = arg;
	i = read(fd, buf, sizeof buf);
	if (i <= 0) {
		shutdown(fd, SHUT_RD);
		shutdown(ep->fd, SHUT_WR);
		AZ(event_del(&ep->ev));
	} else {
		j = write(ep->fd, buf, i);
		if (i != j) {
			shutdown(fd, SHUT_WR);
			shutdown(ep->fd, SHUT_RD);
			AZ(event_del(&ep->ev));
		}
	}
}

void
PipeSession(struct sess *sp)
{
	struct vbe_conn *vc;
	struct edir e1, e2;
	char *b, *e;
	struct worker *w;

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

	e1.fd = vc->fd;
	e2.fd = sp->fd;
	event_set(&e1.ev, sp->fd, EV_READ | EV_PERSIST, rdf, &e1);
	AZ(event_base_set(w->eb, &e1.ev));
	event_set(&e2.ev, vc->fd, EV_READ | EV_PERSIST, rdf, &e2);
	AZ(event_base_set(w->eb, &e2.ev));
	AZ(event_add(&e1.ev, NULL));
	AZ(event_add(&e2.ev, NULL));
	(void)event_base_loop(w->eb, 0);
	vca_close_session(sp, "pipe");
	VBE_ClosedFd(vc);
}
