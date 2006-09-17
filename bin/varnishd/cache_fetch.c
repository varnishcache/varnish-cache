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
#include "cache.h"
#include "heritage.h"

/*--------------------------------------------------------------------*/

static int
fetch_straight(const struct sess *sp, int fd, struct http *hp, char *b)
{
	int i;
	unsigned char *p;
	off_t	cl;
	struct storage *st;

	cl = strtoumax(b, NULL, 0);
	if (cl == 0)
		return (0);

	st = stevedore->alloc(stevedore, cl);
	XXXAN(st->stevedore);
	TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	st->len = cl;
	sp->obj->len = cl;
	p = st->ptr;

	i = fcntl(fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);

	while (cl > 0) {
		i = http_Read(hp, fd, p, cl);
		xxxassert(i > 0);	/* XXX seen */
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
	struct storage *st;
	unsigned u, v;
	char buf[20];		/* XXX: arbitrary */
	char *bp, *be;

	be = buf + sizeof buf - 1;
	bp = buf;
	st = NULL;
	u = 0;
	while (1) {
		/* Try to parse buf as a chunk length */
		*bp = '\0';
		u = strtoul(buf, &q, 16);

		/* Skip trailing whitespace */
		if (q != NULL && q > buf) {
			while (*q == '\t' || *q == ' ')
				q++;
			if (*q == '\r')
				q++;
		}

		/* If we didn't succeed, add to buffer, try again */
		if (q == NULL || q == buf || *q != '\n') {
			xxxassert(be > bp);
			i = http_Read(hp, fd, bp, be - bp);
			xxxassert(i >= 0);
			bp += i;
			continue;
		}

		/* Skip NL */
		q++;

		/* Last chunk is zero bytes long */
		if (u == 0)
			break;

		while (u > 0) {

			/* Get some storage if we don't have any */
			if (st == NULL || st->len == st->space) {
				v = u;
				if (u < params->fetch_chunksize * 1024 && 
				    stevedore->trim != NULL)
					v = params->fetch_chunksize * 1024;
				st = stevedore->alloc(stevedore, v);
				XXXAN(st->stevedore);
				TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
			}
			v = st->space - st->len;
			if (v > u)
				v = u;

			/* Handle anything left in our buffer first */
			i = bp - q;
			assert(i >= 0);
			if (i > v)
				i = v;
			if (i != 0) {
				memcpy(st->ptr + st->len, q, i);
				st->len += i;
				sp->obj->len += i;
				u -= i;
				v -= i;
				q += i;
			}
			if (u == 0)
				break;
			if (v == 0)
				continue;

			/* Pick up the rest of this chunk */
			while (v > 0) {
				i = http_Read(hp, fd, st->ptr + st->len, v);
				st->len += i;
				sp->obj->len += i;
				u -= i;
				v -= i;
			}
		}
		assert(u == 0);

		/* We might still have stuff in our buffer */
		v = bp - q;
		if (v > 0)
			memcpy(buf, q, v);
		q = bp = buf + v;
	}

	if (st != NULL && st->len == 0) {
		TAILQ_REMOVE(&sp->obj->store, st, list);
		stevedore->free(st);
	} else if (st != NULL && stevedore->trim != NULL)
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
			st = stevedore->alloc(stevedore,
			    params->fetch_chunksize * 1024);
			XXXAN(st->stevedore);
			TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
			p = st->ptr + st->len;
			v = st->space - st->len;
		}
		AN(p);
		AN(st);
		i = http_Read(hp, fd, p, v);
		xxxassert(i >= 0);
		if (i == 0)
		     break;
		p += i;
		v -= i;
		st->len += i;
		sp->obj->len += i;
	}

	if (st->len == 0) {
		TAILQ_REMOVE(&sp->obj->store, st, list);
		stevedore->free(st);
	} else if (stevedore->trim != NULL)
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
	struct http *hp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	assert(sp->obj->busy != 0);

	vc = sp->vbc;
	sp->vbc = NULL;

	if (http_GetHdr(vc->http, H_Last_Modified, &b))
		sp->obj->last_modified = TIM_parse(b);

	hp = vc->http2;
	http_ClrHeader(hp);
	hp->logtag = HTTP_Obj;
	http_CopyResp(sp->wrk, sp->fd, hp, vc->http);
	http_FilterHeader(sp->wrk, sp->fd, hp, vc->http, HTTPH_A_INS);
	
	if (body) {
		if (http_GetHdr(vc->http, H_Content_Length, &b))
			cls = fetch_straight(sp, vc->fd, vc->http, b);
		else if (http_HdrIs(vc->http, H_Transfer_Encoding, "chunked"))
			cls = fetch_chunked(sp, vc->fd, vc->http);
		else 
			cls = fetch_eof(sp, vc->fd, vc->http);
		http_PrintfHeader(sp->wrk, sp->fd, hp,
		    "Content-Length: %u", sp->obj->len);
	} else
		cls = 0;

	{
	/* Sanity check fetch methods accounting */
		struct storage *st;
		unsigned uu;

		uu = 0;
		TAILQ_FOREACH(st, &sp->obj->store, list)
			uu += st->len;
		assert(uu == sp->obj->len);
	}

	http_CopyHttp(&sp->obj->http, hp);

	if (http_GetHdr(vc->http, H_Connection, &b) && !strcasecmp(b, "close"))
		cls = 1;

	if (cls)
		VBE_ClosedFd(vc, 0);
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
	XXXAN(vc);
	WSL(w, SLT_Backend, sp->fd, "%d %s", vc->fd, sp->backend->vcl_name);

	http_ClrHeader(vc->http);
	vc->http->logtag = HTTP_Tx;
	http_GetReq(w, vc->fd, vc->http, sp->http);
	http_FilterHeader(w, vc->fd, vc->http, sp->http, HTTPH_R_FETCH);
	http_PrintfHeader(w, vc->fd, vc->http, "X-Varnish: %u", sp->xid);
	if (!http_GetHdr(vc->http, H_Host, &b)) {
		http_PrintfHeader(w, vc->fd, vc->http, "Host: %s",
		    sp->backend->hostname);
	}

	WRK_Reset(w, &vc->fd);
	http_Write(w, vc->http, 0);
	i = WRK_Flush(w);
	xxxassert(i == 0);

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);

	i = http_RecvHead(vc->http, vc->fd);
	xxxassert(i == 0);
	xxxassert(http_DissectResponse(sp->wrk, vc->http, vc->fd) == 0);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	AZ(sp->vbc);
	sp->vbc = vc;

	sp->obj->entered = time(NULL);

	return (0);
}
