/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sbuf.h>
#include <event.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "vcl_lang.h"
#include "cache.h"

/*--------------------------------------------------------------------*/
int
FetchSession(struct worker *w, struct sess *sp)
{
	int fd, i;
	void *fd_token;
	struct sess sp2;
	off_t	cl;
	struct storage *st;
	unsigned char *p;
	char *b, *e;
	struct http *hp;

	fd = VBE_GetFd(sp->backend, &fd_token);
	assert(fd != -1);
	VSL(SLT_Handling, sp->fd, "Fetch fd %d", fd);

	hp = http_New();
	http_BuildSbuf(0, w->sb, sp->http);
	i = write(fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));

	/* XXX: copy any contents */

	memset(&sp2, 0, sizeof sp2);
	sp2.rd_e = &w->e1;
	sp2.fd = fd;
	/*
	 * XXX: It might be cheaper to avoid the event_engine and simply
	 * XXX: read(2) the header
	 */
	http_RecvHead(hp, sp2.fd, w->eb, NULL, NULL);
	event_base_loop(w->eb, 0);
	http_Dissect(hp, sp2.fd, 2);

	/* XXX: fill in object from headers */
	sp->obj->valid = 1;
	sp->obj->cacheable = 1;

	/* XXX: unbusy, and kick other sessions into action */
	sp->obj->busy = 0;

	assert(http_GetHdr(hp, "Content-Length", &b));

	cl = strtoumax(b, NULL, 0);

	sp->handling = HND_Unclass;
	sp->vcl->fetch_func(sp);

	st = stevedore->alloc(cl);
	TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	st->len = cl;
	sp->obj->len = cl;
	p = st->ptr;

	i = fcntl(sp2.fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(sp2.fd, F_SETFL, i);

	if (http_GetTail(hp, cl, &b, &e)) {
		i = e - b;
		memcpy(p, b, i);
		p += i;
		cl -= i;
	}

	while (cl != 0) {
		i = read(sp2.fd, p, cl);
		assert(i > 0);
		p += i;
		cl -= i;
	}

	http_BuildSbuf(1, w->sb, hp);
	i = write(sp->fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));

	i = write(sp->fd, st->ptr, st->len);
	assert(i == st->len);

	hash->deref(sp->obj);

	if (http_GetHdr(sp->http, "Connection", &b) &&
	    !strcasecmp(b, "close")) {
		VBE_ClosedFd(fd_token);
	} else {
		VBE_RecycleFd(fd_token);
	}
	return (1);
}
