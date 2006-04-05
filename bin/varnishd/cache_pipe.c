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
#include "vcl_lang.h"
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

	sbuf_clear(w->sb);
	assert(w->sb != NULL);
	sbuf_cat(w->sb, sp->http.req);
	sbuf_cat(w->sb, " ");
	sbuf_cat(w->sb, sp->http.url);
	if (sp->http.proto != NULL) {
		sbuf_cat(w->sb, " ");
		sbuf_cat(w->sb, sp->http.proto);
	}
	sbuf_cat(w->sb, "\r\n");
#define HTTPH(a, b, c, d, e, f, g) 				\
	do {							\
		if (sp->http.b != NULL) {			\
			sbuf_cat(w->sb, a ": ");		\
			sbuf_cat(w->sb, sp->http.b);		\
			sbuf_cat(w->sb, "\r\n");		\
		}						\
	} while (0);
#include "http_headers.h"
#undef HTTPH
	sbuf_cat(w->sb, "\r\n");
	sbuf_finish(w->sb);
	i = write(fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));

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
	close (sp->fd);
	VBE_ClosedFd(fd_token);
	sp->fd = -1;
}
