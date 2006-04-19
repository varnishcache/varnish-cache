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

static void
FetchReturn(struct sess *sp)
{

	/* do nothing */
}

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

	fd = VBE_GetFd(sp->backend, &fd_token);
	assert(fd != -1);
	VSL(SLT_HandlingFetch, sp->fd, "%d", fd);

	HttpdBuildSbuf(0, 1, w->sb, sp);
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
	HttpdGetHead(&sp2, w->eb, FetchReturn);
	event_base_loop(w->eb, 0);
	HttpdAnalyze(&sp2, 2);

	/* XXX: fill in object from headers */
	sp->obj->valid = 1;
	sp->obj->cacheable = 1;

	/* XXX: unbusy, and kick other sessions into action */
	sp->obj->busy = 0;

	assert (sp2.http.H_Content_Length != NULL); /* XXX */

	cl = strtoumax(sp2.http.H_Content_Length, NULL, 0);

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

	i = sp2.rcv_len - sp2.rcv_ptr;
	if (i > 0) {
		memcpy(p, sp2.rcv + sp2.rcv_ptr, i);
		p += i;
		cl -= i;
	}
	if (cl != 0) {
		i = read(sp2.fd, p, cl);
		VSL(SLT_Debug, 0, "R i %d cl %jd", i, cl);
		assert(i == cl);
	}

	HttpdBuildSbuf(1, 1, w->sb, &sp2);
	i = write(sp->fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));

	i = write(sp->fd, st->ptr, st->len);
	VSL(SLT_Debug, 0, "W i %d st->len %u", i, st->len);
	assert(i == st->len);

	hash->deref(sp->obj);

	if (sp2.http.H_Connection != NULL &&
	    !strcmp(sp2.http.H_Connection, "close")) {
		close(fd);
		VBE_ClosedFd(fd_token);
	} else {
		VBE_RecycleFd(fd_token);
	}

	/* XXX: this really belongs in the acceptor */
	if (sp->rcv_len > sp->rcv_ptr)
		memmove(sp->rcv, sp->rcv + sp->rcv_ptr,
		    sp->rcv_len - sp->rcv_ptr);
	sp->rcv_len -= sp->rcv_ptr;
	sp->rcv_ptr = 0;
	return (1);
}
