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
#include <time.h>

#include "shmlog.h"
#include "libvarnish.h"
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
fetch_straight(const struct sess *sp, int fd, struct http *hp, char *b)
{
	int i;
	unsigned char *p;
	off_t	cl;
	struct storage *st;

	cl = strtoumax(b, NULL, 0);

	st = stevedore->alloc(stevedore, cl);
	assert(st->stevedore != NULL);
	TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	st->len = cl;
	sp->obj->len = cl;
	p = st->ptr;

	i = fcntl(fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);

	while (cl != 0) {
		i = http_Read(hp, fd, p, cl);
		assert(i > 0);	/* XXX seen */
		p += i;
		cl -= i;
	}
	return (0);
}

/*--------------------------------------------------------------------*/
/* XXX: Cleanup.  It must be possible somehow :-( */

static int
fetch_chunked(const struct sess *sp, int fd, struct http *hp)
{
	int i;
	char *q;
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
		i = http_Read(hp, fd, bp, be - bp);
		assert(i >= 0);
		bp += i;
		*bp = '\0';
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
				assert(st->stevedore != NULL);
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
			while (v > 0) {
				i = http_Read(hp, fd, p, v);
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
fetch_eof(const struct sess *sp, int fd, struct http *hp)
{
	int i;
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
			assert(st->stevedore != NULL);
			TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
			p = st->ptr + st->len;
			v = st->space - st->len;
		}
		assert(p != NULL);
		assert(st != NULL);
		i = http_Read(hp, fd, p, v);
		assert(i >= 0);
		if (i == 0)
		     break;
		p += i;
		v -= i;
		st->len += i;
		sp->obj->len += i;
	}

	if (stevedore->trim != NULL)
		stevedore->trim(st, st->len);

	return (1);
}

/*--------------------------------------------------------------------*/

int
FetchBody(struct worker *w, struct sess *sp)
{
	int cls;
	struct vbe_conn *vc;
	struct http *hp;
	char *b;
	int body = 1;		/* XXX */

	vc = sp->vbc;
	hp = sp->bkd_http;

	if (http_GetHdr(hp, "Last-Modified", &b))
		sp->obj->last_modified = TIM_parse(b);
	http_BuildSbuf(sp->fd, Build_Reply, w->sb, hp);
	if (body) {
		if (http_GetHdr(hp, "Content-Length", &b))
			cls = fetch_straight(sp, vc->fd, hp, b);
		else if (http_HdrIs(hp, "Transfer-Encoding", "chunked"))
			cls = fetch_chunked(sp, vc->fd, hp);
		else 
			cls = fetch_eof(sp, vc->fd, hp);
		sbuf_printf(w->sb, "Content-Length: %u\r\n", sp->obj->len);
	} else
		cls = 0;
	sbuf_finish(w->sb);
	sp->obj->header = strdup(sbuf_data(w->sb));
	VSL_stats->n_header++;

	if (http_GetHdr(hp, "Connection", &b) && !strcasecmp(b, "close"))
		cls = 1;

	if (cls)
		VBE_ClosedFd(vc);
	else
		VBE_RecycleFd(vc);

	return (0);
}

/*--------------------------------------------------------------------*/

int
FetchHeaders(struct worker *w, struct sess *sp)
{
	int i;
	struct vbe_conn *vc;
	struct http *hp;

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
	sp->t_req = time(NULL);

	/* XXX: copy any body ?? */

	/*
	 * XXX: It might be cheaper to avoid the event_engine and simply
	 * XXX: read(2) the header
	 */
	http_RecvHead(hp, vc->fd, w->eb, NULL, NULL);
	(void)event_base_loop(w->eb, 0);
	sp->t_resp = time(NULL);
	assert(http_DissectResponse(hp, vc->fd) == 0);
	sp->vbc = vc;
	sp->bkd_http = hp;
	return (0);
}
