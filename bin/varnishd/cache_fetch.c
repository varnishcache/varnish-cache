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

	while (cl > 0) {
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
FetchBody(struct sess *sp)
{
	int cls;
	struct vbe_conn *vc;
	char *b;
	int body = 1;		/* XXX */

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	assert(sp->obj->busy != 0);

	vc = sp->vbc;

	if (http_GetHdr(vc->http, H_Last_Modified, &b))
		sp->obj->last_modified = TIM_parse(b);

	/*
	 * We borrow the sessions workspace and http header for building the
	 * headers to store in the object, then copy them over there.
	 * The actual headers to reply with are built later on over in
	 * cache_response.c
	 */
	http_ClrHeader(sp->http);
	sp->http->objlog = 1;	/* log as SLT_ObjHeader */
	http_CopyResp(sp->fd, sp->http, vc->http);
	http_FilterHeader(sp->fd, sp->http, vc->http, HTTPH_A_INS);
	
	if (body) {
		if (http_GetHdr(vc->http, H_Content_Length, &b))
			cls = fetch_straight(sp, vc->fd, vc->http, b);
		else if (http_HdrIs(vc->http, H_Transfer_Encoding, "chunked"))
			cls = fetch_chunked(sp, vc->fd, vc->http);
		else 
			cls = fetch_eof(sp, vc->fd, vc->http);
		http_PrintfHeader(sp->fd, sp->http,
		    "Content-Length: %u", sp->obj->len);
	} else
		cls = 0;
	sp->http->objlog = 0;
	http_CopyHttp(&sp->obj->http, sp->http);

	if (http_GetHdr(vc->http, H_Connection, &b) && !strcasecmp(b, "close"))
		cls = 1;

	if (cls)
		VBE_ClosedFd(vc);
	else
		VBE_RecycleFd(vc);

	return (0);
}

/*--------------------------------------------------------------------*/

int
FetchHeaders(struct sess *sp)
{
	int i;
	struct vbe_conn *vc;
	struct worker *w;
	char *b;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	assert(sp->obj->busy != 0);
	w = sp->wrk;

	sp->obj->xid = sp->xid;

	vc = VBE_GetFd(sp->backend, sp->xid);
	if (vc == NULL)
		vc = VBE_GetFd(sp->backend, sp->xid);
	assert(vc != NULL);	/* XXX: handle this */
	VSL(SLT_Backend, sp->fd, "%d %s", vc->fd, sp->backend->vcl_name);

	http_GetReq(vc->fd, vc->http, sp->http);
	http_FilterHeader(vc->fd, vc->http, sp->http, HTTPH_R_FETCH);
	http_PrintfHeader(vc->fd, vc->http, "X-Varnish: %u", sp->xid);
	if (!http_GetHdr(vc->http, H_Host, &b)) {
		http_PrintfHeader(vc->fd, vc->http, "Host: %s",
		    sp->backend->hostname);
	}

	sp->t_req = time(NULL);
	WRK_Reset(w, &vc->fd);
	http_Write(w, vc->http, 0);
	i = WRK_Flush(w);
	assert(i == 0);

	/*
	 * XXX: It might be cheaper to avoid the event_engine and simply
	 * XXX: read(2) the header
	 */
	http_RecvHead(vc->http, vc->fd, w->eb, NULL, NULL);
	(void)event_base_loop(w->eb, 0);
	sp->t_resp = time(NULL);
	assert(http_DissectResponse(vc->http, vc->fd) == 0);
	sp->vbc = vc;
	return (0);
}
