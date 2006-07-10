/*
 * $Id$
 */

#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sbuf.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "cache.h"

/*
 * Chunked encoding is a hack.  We prefer to have a single chunk or a 
 * few large chunks, and not a terribly long list of small ones.
 * If our stevedore can trim, we alloc big chunks and trim the last one
 * at the end when we know the result.
 *
 * Good testcase: http://www.washingtonpost.com/
 */
#define CHUNK_PREALLOC		(128 * 1024)

/*--------------------------------------------------------------------*/
static int
fetch_straight(struct worker *w, struct sess *sp, int fd, struct http *hp, char *b)
{
	int i;
	char *e;
	unsigned char *p;
	off_t	cl;
	struct storage *st;

	cl = strtoumax(b, NULL, 0);

	st = stevedore->alloc(stevedore, cl);
	TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	st->len = cl;
	sp->obj->len = cl;
	p = st->ptr;

	i = fcntl(fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);

	if (http_GetTail(hp, cl, &b, &e)) {
		i = e - b;
		memcpy(p, b, i);
		p += i;
		cl -= i;
	}

	while (cl != 0) {
		i = read(fd, p, cl);
		assert(i > 0);	/* XXX seen */
		p += i;
		cl -= i;
	}
	return (0);
}

/*--------------------------------------------------------------------*/
/* XXX: Cleanup.  It must be possible somehow :-( */

static int
fetch_chunked(struct worker *w, struct sess *sp, int fd, struct http *hp)
{
	int i;
	char *b, *q, *e;
	unsigned char *p;
	struct storage *st;
	unsigned u, v;
	char buf[20];
	char *bp, *be;

	i = fcntl(fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);

	be = buf + sizeof buf;
	bp = buf;
	st = NULL;
	while (1) {
		if (http_GetTail(hp, be - bp, &b, &e)) {
			memcpy(bp, b, e - b);
			bp += e - b;
			*bp = '\0';
		} else {
			i = read(fd, bp, be - bp);
			assert(i >= 0);
			bp += i;
			*bp = '\0';
		}
		u = strtoul(buf, &q, 16);
		if (q == NULL || q == buf)
			continue;
		assert(isspace(*q));
		while (*q == '\t' || *q == ' ')
			q++;
		if (*q == '\r')
			q++;
		assert(*q == '\n');
		q++;
		if (u == 0)
			break;
		sp->obj->len += u;

		while (u > 0) {
			if (st != NULL && st->len < st->space) {
				p = st->ptr + st->len;
			} else {
				st = stevedore->alloc(stevedore,
				    stevedore->trim == NULL ? u : CHUNK_PREALLOC);
				TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
				p = st->ptr;
			}
			v = st->space - st->len;
			if (v > u)
				v = u;

			i = bp - q;
			if (i == 0) {
			} else if (v > i) {
				assert(i > 0);
				memcpy(p, q, i);
				p += i;
				st->len += i;
				u -= i;
				v -= i;
				q = bp = buf;
			} else if (i >= v) {
				memcpy(p, q, v);
				p += v;
				st->len += i;
				q += v;
				u -= v;
				v -= v;
				if (u == 0 && bp > q) {
					memcpy(buf, q, bp - q);
					q = bp = buf + (bp - q);
				}
			}
			if (u == 0)
				break;
			if (v == 0)
				continue;
			if (http_GetTail(hp, v, &b, &e)) {
				memcpy(p, b, e - b);
				p += e - b;
				st->len += e - b;
				v -= e - b;
				u -= e - b;
			}
			while (v > 0) {
				i = read(fd, p, v);
				assert(i > 0);
				st->len += i;
				v -= i;
				u -= i;
				p += i;
			}
		}
	}

	if (st != NULL && stevedore->trim != NULL)
		stevedore->trim(st, st->len);
	return (0);
}


/*--------------------------------------------------------------------*/

#include <errno.h>

static int
fetch_eof(struct worker *w, struct sess *sp, int fd, struct http *hp)
{
	int i;
	char *b, *e;
	unsigned char *p;
	struct storage *st;
	unsigned v;

	i = fcntl(fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);

	p = NULL;
	v = 0;
	st = NULL;
	while (1) {
		if (v == 0) {
			st = stevedore->alloc(stevedore, CHUNK_PREALLOC);
			TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
			p = st->ptr + st->len;
			v = st->space - st->len;
		}
		assert(p != NULL);
		assert(st != NULL);
		if (http_GetTail(hp, v, &b, &e)) {
			memcpy(p, b, e - b);
			p += e - b;
			v -= e - b;
			st->len += e - b;
			sp->obj->len += e - b;
			*p = '\0';
		}
		i = read(fd, p, v);
		assert(i >= 0);
		if (i == 0)
		     break;
		p += i;
		v -= i;
		st->len += i;
		sp->obj->len += i;
	}

	if (st != NULL && stevedore->trim != NULL)
		stevedore->trim(st, st->len);

	return (1);
}

/*--------------------------------------------------------------------*/
int
FetchSession(struct worker *w, struct sess *sp)
{
	int i, cls;
	struct vbe_conn *vc;
	struct http *hp;
	char *b;
	int body;

	sp->obj->xid = sp->xid;

	vc = VBE_GetFd(sp->backend, sp->xid);
	if (vc == NULL)
		vc = VBE_GetFd(sp->backend, sp->xid);
	assert(vc != NULL);	/* XXX: handle this */
	VSL(SLT_Backend, sp->fd, "%d %s", vc->fd, sp->backend->vcl_name);

	hp = vc->http;
	http_BuildSbuf(vc->fd, Build_Fetch, w->sb, sp->http);
	i = write(vc->fd, sbuf_data(w->sb), sbuf_len(w->sb));
	assert(i == sbuf_len(w->sb));
	time(&sp->t_req);

	/* XXX: copy any contents */

	/*
	 * XXX: It might be cheaper to avoid the event_engine and simply
	 * XXX: read(2) the header
	 */
	http_RecvHead(hp, vc->fd, w->eb, NULL, NULL);
	event_base_loop(w->eb, 0);
	time(&sp->t_resp);
	assert(http_Dissect(hp, vc->fd, 2) == 0);

	body = RFC2616_cache_policy(sp, hp);

	VCL_fetch_method(sp);

	if (sp->obj->cacheable)
		EXP_Insert(sp->obj);

	http_BuildSbuf(sp->fd, Build_Reply, w->sb, hp);
	if (body) {
		if (http_GetHdr(hp, "Content-Length", &b))
			cls = fetch_straight(w, sp, vc->fd, hp, b);
		else if (http_HdrIs(hp, "Transfer-Encoding", "chunked"))
			cls = fetch_chunked(w, sp, vc->fd, hp);
		else 
			cls = fetch_eof(w, sp, vc->fd, hp);
		sbuf_printf(w->sb, "Content-Length: %u\r\n", sp->obj->len);
	} else
		cls = 0;
	sbuf_finish(w->sb);
	sp->obj->header = strdup(sbuf_data(w->sb));
	VSL_stats->n_header++;

	vca_write_obj(w, sp);

	if (http_GetHdr(hp, "Connection", &b) && !strcasecmp(b, "close"))
		cls = 1;

	if (cls)
		VBE_ClosedFd(vc);
	else
		VBE_RecycleFd(vc);

	HSH_Unbusy(sp->obj);
	if (!sp->obj->cacheable)
		HSH_Deref(sp->obj);

	return (1);
}
