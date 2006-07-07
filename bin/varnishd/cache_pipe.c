/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <queue.h>
#include <sys/socket.h>
#include <sbuf.h>
#include <event.h>

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

	ep = arg;
	i = read(fd, buf, sizeof buf);
	if (i <= 0) {
		shutdown(fd, SHUT_RD);
		shutdown(ep->fd, SHUT_WR);
		event_del(&ep->ev);
	} else {
		j = write(ep->fd, buf, i);
		assert(i == j);
	}
}

void
PipeSession(struct worker *w, struct sess *sp)
{
	int fd, i;
	void *fd_token;
	struct edir e1, e2;
	char *b, *e;

	fd = VBE_GetFd(sp->backend, &fd_token, sp->xid);
	assert(fd != -1);

	http_BuildSbuf(fd, Build_Pipe, w->sb, sp->http);
	i = write(fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));
	if (http_GetTail(sp->http, 99999999, &b, &e) && b != e) { /* XXX */
		i = write(fd, b, e - b);
		assert(i == e - b);
	}

	e1.fd = fd;
	e2.fd = sp->fd;
	event_set(&e1.ev, sp->fd, EV_READ | EV_PERSIST, rdf, &e1);
	event_base_set(w->eb, &e1.ev);
	event_set(&e2.ev, fd,     EV_READ | EV_PERSIST, rdf, &e2);
	event_base_set(w->eb, &e2.ev);
	event_add(&e1.ev, NULL);
	event_add(&e2.ev, NULL);
	event_base_loop(w->eb, 0);
	close (fd);
	vca_close_session(sp, "pipe");
	VBE_ClosedFd(fd_token);
}
