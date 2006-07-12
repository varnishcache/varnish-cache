/*
 * $Id$
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
rdf(int fd, short event __unused, void *arg)
{
	int i, j;
	struct edir *ep;
	char buf[BUFSIZ];

	ep = arg;
	i = read(fd, buf, sizeof buf);
	if (i <= 0) {
		shutdown(fd, SHUT_RD);
		shutdown(ep->fd, SHUT_WR);
		event_del(&ep->ev);
	} else {
		j = write(ep->fd, buf, i);
		if (i != j) {
			shutdown(fd, SHUT_WR);
			shutdown(ep->fd, SHUT_RD);
			event_del(&ep->ev);
		}
	}
}

void
PipeSession(struct worker *w, struct sess *sp)
{
	int i;
	struct vbe_conn *vc;
	struct edir e1, e2;
	char *b, *e;

	vc = VBE_GetFd(sp->backend, sp->xid);
	assert(vc != NULL);
	VSL(SLT_Backend, sp->fd, "%d %s", vc->fd, sp->backend->vcl_name);

	http_BuildSbuf(vc->fd, Build_Pipe, w->sb, sp->http);
	i = write(vc->fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));
	if (http_GetTail(sp->http, 99999999, &b, &e) && b != e) { /* XXX */
		i = write(vc->fd, b, e - b);
		if (i != e - b) {
			close (vc->fd);
			vca_close_session(sp, "pipe");
			VBE_ClosedFd(vc);
		}
	}

	e1.fd = vc->fd;
	e2.fd = sp->fd;
	event_set(&e1.ev, sp->fd, EV_READ | EV_PERSIST, rdf, &e1);
	event_base_set(w->eb, &e1.ev);
	event_set(&e2.ev, vc->fd, EV_READ | EV_PERSIST, rdf, &e2);
	event_base_set(w->eb, &e2.ev);
	event_add(&e1.ev, NULL);
	event_add(&e2.ev, NULL);
	event_base_loop(w->eb, 0);
	vca_close_session(sp, "pipe");
	VBE_ClosedFd(vc);
}
