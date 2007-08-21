/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
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
#include "stevedore.h"

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

	st = STV_alloc(cl);
	TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	st->len = cl;
	sp->obj->len = cl;
	p = st->ptr;

	i = fcntl(fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);

	while (cl > 0) {
		i = http_Read(hp, fd, p, cl);
		if (i <= 0)
			return (-1);
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
			/*
			 * The sematics we need here is "read until you have
			 * received at least one character, but feel free to
			 * return up to (be-bp) if they are available, but do
			 * not wait for those extra characters.
			 *
			 * The canonical way to do that is to do a blocking
			 * read(2) of one char, then change to nonblocking,
			 * read as many as we find, then change back to
			 * blocking reads again.
			 *
			 * Hardly much more efficient and certainly a good
			 * deal more complex than reading a single character
			 * at a time.
			 */
			i = http_Read(hp, fd, bp, 1);
			if (i <= 0)
				return (-1);
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
				if (u < params->fetch_chunksize * 1024)
					v = params->fetch_chunksize * 1024;
				st = STV_alloc(v);
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
				if (i <= 0)
					return (-1);
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
		STV_free(st);
	} else if (st != NULL)
		STV_trim(st, st->len);
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
			st = STV_alloc(params->fetch_chunksize * 1024);
			TAILQ_INSERT_TAIL(&sp->obj->store, st, list);
			p = st->ptr + st->len;
			v = st->space - st->len;
		}
		AN(p);
		AN(st);
		i = http_Read(hp, fd, p, v);
		if (i < 0)
			return (-1);
		if (i == 0)
			break;
		p += i;
		v -= i;
		st->len += i;
		sp->obj->len += i;
	}

	if (st->len == 0) {
		TAILQ_REMOVE(&sp->obj->store, st, list);
		STV_free(st);
	} else
		STV_trim(st, st->len);

	return (1);
}

/*--------------------------------------------------------------------*/

int
Fetch(struct sess *sp)
{
	struct vbe_conn *vc;
	struct worker *w;
	char *b;
	int cls;
	int body = 1;		/* XXX */
	struct http *hp, *hp2;
	struct storage *st;
	struct bereq *bereq;
	int len;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	assert(sp->obj->busy != 0);
	w = sp->wrk;
	bereq = sp->bereq;
	hp = bereq->http;

	sp->obj->xid = sp->xid;

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	vc = VBE_GetFd(sp);
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	if (vc == NULL)
		return (1);
	WRK_Reset(w, &vc->fd);
	http_Write(w, hp, 0);
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	if (WRK_Flush(w)) {
		/* XXX: cleanup */
		
		return (1);
	}

	/* XXX is this the right place? */
	VSL_stats->backend_req++;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	if (http_RecvHead(hp, vc->fd)) {
		/* XXX: cleanup */
		return (1);
	}
	if (http_DissectResponse(sp->wrk, hp, vc->fd)) {
		/* XXX: cleanup */
		return (1);
	}
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);

	sp->obj->entered = TIM_real();

	assert(sp->obj->busy != 0);

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	if (http_GetHdr(hp, H_Last_Modified, &b))
		sp->obj->last_modified = TIM_parse(b);

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	/* Filter into object */
	hp2 = &sp->obj->http;
	len = hp->rx_e - hp->rx_s;
	len += 256;		/* margin for content-length etc */

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	b = malloc(len);
	AN(b);
	http_Setup(hp2, b, len);

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	hp2->logtag = HTTP_Obj;
	http_CopyResp(hp2, hp);
	http_FilterFields(sp->wrk, sp->fd, hp2, hp, HTTPH_A_INS);
	http_CopyHome(sp->wrk, sp->fd, hp2);

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	if (body) {
		if (http_GetHdr(hp, H_Content_Length, &b))
			cls = fetch_straight(sp, vc->fd, hp, b);
		else if (http_HdrIs(hp, H_Transfer_Encoding, "chunked"))
			cls = fetch_chunked(sp, vc->fd, hp);
		else
			cls = fetch_eof(sp, vc->fd, hp);
		http_PrintfHeader(sp->wrk, sp->fd, hp2,
		    "Content-Length: %u", sp->obj->len);
	} else
		cls = 0;

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	if (cls < 0) {
		while (!TAILQ_EMPTY(&sp->obj->store)) {
			st = TAILQ_FIRST(&sp->obj->store);
			TAILQ_REMOVE(&sp->obj->store, st, list);
			STV_free(st);
		}
		VBE_ClosedFd(sp->wrk, vc);
		return (-1);
	}

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	{
	/* Sanity check fetch methods accounting */
		unsigned uu;

		uu = 0;
		TAILQ_FOREACH(st, &sp->obj->store, list)
			uu += st->len;
		assert(uu == sp->obj->len);
	}

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	if (http_GetHdr(hp, H_Connection, &b) && !strcasecmp(b, "close"))
		cls = 1;

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	if (cls)
		VBE_ClosedFd(sp->wrk, vc);
	else
		VBE_RecycleFd(sp->wrk, vc);

	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	return (0);
}
