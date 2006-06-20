/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
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

	fd = VBE_GetFd(sp->backend, &fd_token);
	assert(fd != -1);

	http_BuildSbuf(0, w->sb, sp->http);	/* XXX: 0 ?? */
	i = write(fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));
	assert(__LINE__ == 0);
#if 0
	{ int j;
	j = 0;
	i = sp->rcv_len - sp->rcv_ptr;
	if (i > 0) {
		j = write(sp->fd, sp->rcv + sp->rcv_ptr, i);
		assert(j == i);
	}
	}
#endif

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
