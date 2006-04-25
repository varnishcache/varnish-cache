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

static int
fetch_straight(struct worker *w, struct sess *sp, struct http *hp, char *b)
{
	int i;
	char *e;
	struct sess sp2;
	unsigned char *p;
	off_t	cl;
	struct storage *st;

	cl = strtoumax(b, NULL, 0);

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
		return (1);
	} else {
		return (0);
	}
}

static int
fetch_chunked(struct worker *w, struct sess *sp, struct http *hp)
{
	int i;
	char *b, *q, *e;
	struct sess sp2;
	unsigned char *p;
	struct storage *st;
	unsigned u;
	char buf[20];
	char *bp, *be;

	i = fcntl(sp2.fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(sp2.fd, F_SETFL, i);

	be = buf + sizeof buf;
	while (1) {
		bp = buf;
		if (http_GetTail(hp, be - bp, &b, &e)) {
if (0)
printf("Tail: (H)\n%#H\n", b, e - b);
			memcpy(bp, b, e - b);
			bp += e - b;
		} else {
			i = read(sp2.fd, bp, be - bp);
			assert(i >= 0);
			bp += i;
		}
		u = strtoul(buf, &q, 16);
if (0)
printf("Buf: u %u q %p buf %p\n%#H\n", u, q, buf, buf, bp - buf);
		if (q == NULL || (*q != '\n' && *q != '\r'))
			continue;
		if (*q == '\r')
			q++;
		assert(*q == '\n');
		q++;
		if (u == 0)
			break;
		st = stevedore->alloc(u);
		TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
		st->len = u;
		sp->obj->len += u;
		p = st->ptr;
		memcpy(p, q, bp - q);
		p += bp - q;
		u -= bp - q;
		if (http_GetTail(hp, u, &b, &e)) {
if (0)
printf("Tail: (B)\n%#H\n", b, e - b);
			memcpy(p, b, e - b);
			p += e - b;
			u -= e - b;
		}
		if (u > 0) {
			i = read(sp2.fd, p, u);
if (0)
printf("u = %u i = %d\n", u, i);
			assert(i == u);
		}
if (0)
printf("Store:\n%#H\n", st->ptr, st->len);
	}

	http_BuildSbuf(1, w->sb, hp);
	i = write(sp->fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));

	TAILQ_FOREACH(st, &sp->obj->store, list) {
		i = write(sp->fd, st->ptr, st->len);
		assert(i == st->len);
	}

	hash->deref(sp->obj);

	if (http_GetHdr(sp->http, "Connection", &b) &&
	    !strcasecmp(b, "close")) {
		return (1);
	} else {
		return (0);
	}
}

/*--------------------------------------------------------------------*/
int
FetchSession(struct worker *w, struct sess *sp)
{
	int fd, i, cls;
	void *fd_token;
	struct sess sp2;
	struct http *hp;
	char *b;

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

	sp->handling = HND_Insert;
	sp->vcl->fetch_func(sp);

	assert(sp->handling == HND_Insert);

	if (http_GetHdr(hp, "Content-Length", &b)) {
		cls = fetch_straight(w, sp, hp, b);
	} else if (http_GetHdr(hp, "Transfer-Encoding", &b) &&
	    !strcasecmp(b, "chunked")) {
		cls = fetch_chunked(w, sp, hp);
	} else {
		assert(0 == 1);
		cls = 0;
	}

	if (cls)
		VBE_ClosedFd(fd_token);
	else
		VBE_RecycleFd(fd_token);

	/* XXX: unbusy, and kick other sessions into action */
	sp->obj->busy = 0;

	return (1);
}
