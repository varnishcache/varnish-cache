/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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

#include "config.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"
#include "cli_priv.h"

static unsigned fetchfrag;

/*--------------------------------------------------------------------*/

static int
fetch_straight(struct sess *sp, struct http_conn *htc, const char *b)
{
	int i;
	unsigned char *p;
	uintmax_t cll;
	unsigned cl;
	struct storage *st;

	cll = strtoumax(b, NULL, 0);
	if (cll == 0)
		return (0);

	cl = (unsigned)cll;
	assert((uintmax_t)cl == cll); /* Protect against bogusly large values */

	st = STV_alloc(sp, cl);
	VTAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	st->len = cl;
	sp->obj->len = cl;
	p = st->ptr;

	while (cl > 0) {
		i = HTC_Read(htc, p, cl);
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
fetch_chunked(struct sess *sp, struct http_conn *htc)
{
	int i;
	char *q;
	struct storage *st;
	unsigned u, v, w;
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
			if (bp >= be)
				return (-1);
			/*
			 * The semantics we need here is "read until you have
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
			i = HTC_Read(htc, bp, 1);
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
				st = STV_alloc(sp, v);
				VTAILQ_INSERT_TAIL(&sp->obj->store, st, list);
			}
			v = st->space - st->len;
			if (v > u)
				v = u;

			/* Handle anything left in our buffer first */
			w = pdiff(q, bp);
			if (w > v)
				w = v;
			if (w != 0) {
				memcpy(st->ptr + st->len, q, w);
				st->len += w;
				sp->obj->len += w;
				u -= w;
				v -= w;
				q += w;
			}
			if (u == 0)
				break;
			if (v == 0)
				continue;

			/* Pick up the rest of this chunk */
			while (v > 0) {
				i = HTC_Read(htc, st->ptr + st->len, v);
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
		v = pdiff(q, bp);
		if (v > 0)
			memcpy(buf, q, v);
		q = bp = buf + v;
	}

	if (st != NULL && st->len == 0) {
		VTAILQ_REMOVE(&sp->obj->store, st, list);
		STV_free(st);
	} else if (st != NULL)
		STV_trim(st, st->len);
	return (0);
}


/*--------------------------------------------------------------------*/

static void
dump_st(const struct sess *sp, const struct storage *st)
{
	txt t;

	t.b = (void*)st->ptr;
	t.e = (void*)(st->ptr + st->len);
	WSLR(sp->wrk, SLT_Debug, sp->fd, t);
}

static int
fetch_eof(struct sess *sp, struct http_conn *htc)
{
	int i;
	unsigned char *p;
	struct storage *st;
	unsigned v;

	p = NULL;
	v = 0;
	st = NULL;
	while (1) {
		if (v == 0) {
			if (st != NULL && fetchfrag > 0)
				dump_st(sp, st);
			st = STV_alloc(sp, params->fetch_chunksize * 1024);
			VTAILQ_INSERT_TAIL(&sp->obj->store, st, list);
			p = st->ptr + st->len;
			v = st->space - st->len;
			if (fetchfrag > 0 && v > fetchfrag)
				v = fetchfrag;
		}
		AN(p);
		AN(st);
		i = HTC_Read(htc, p, v);
		if (i < 0)
			return (-1);
		if (i == 0)
			break;
		p += i;
		v -= i;
		st->len += i;
		sp->obj->len += i;
	}
	if (fetchfrag > 0)
		dump_st(sp, st);

	if (st->len == 0) {
		VTAILQ_REMOVE(&sp->obj->store, st, list);
		STV_free(st);
	} else
		STV_trim(st, st->len);

	return (1);
}

/*--------------------------------------------------------------------
 * Fetch any body attached to the incoming request, and either write it
 * to the backend (if we pass) or discard it (anything else).
 * This is mainly a separate function to isolate the stack buffer and
 * to contain the complexity when we start handling chunked encoding.
 */

int
FetchReqBody(struct sess *sp)
{
	unsigned long content_length;
	char buf[8192];
	char *ptr, *endp;
	int rdcnt;

	if (http_GetHdr(sp->http, H_Content_Length, &ptr)) {

		content_length = strtoul(ptr, &endp, 10);
		/* XXX should check result of conversion */
		while (content_length) {
			if (content_length > sizeof buf)
				rdcnt = sizeof buf;
			else
				rdcnt = content_length;
			rdcnt = HTC_Read(sp->htc, buf, rdcnt);
			if (rdcnt <= 0)
				return (1);
			content_length -= rdcnt;
			if (!sp->sendbody)
				continue;
			(void)WRW_Write(sp->wrk, buf, rdcnt); /* XXX: stats ? */
			if (WRW_Flush(sp->wrk))
				return (2);
		}
	}
	if (http_GetHdr(sp->http, H_Transfer_Encoding, NULL)) {
		/* XXX: Handle chunked encoding. */
		WSL(sp->wrk, SLT_Debug, sp->fd, "Transfer-Encoding in request");
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int
Fetch(struct sess *sp)
{
	struct vbe_conn *vc;
	struct worker *w;
	char *b;
	int cls;
	struct http *hp, *hp2;
	struct storage *st;
	struct bereq *bereq;
	int mklen, is_head;
	struct http_conn htc[1];
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(sp->bereq, BEREQ_MAGIC);
	AN(sp->director);
	AN(sp->obj->busy);
	AN(sp->bereq);
	w = sp->wrk;
	bereq = sp->bereq;
	hp = bereq->http;
	is_head = (strcasecmp(http_GetReq(hp), "head") == 0);

	sp->obj->xid = sp->xid;

	/* Set up obj's workspace */
	WS_Assert(sp->obj->ws_o);
	VBE_GetFd(sp);
	if (sp->vbe == NULL)
		return (__LINE__);
	vc = sp->vbe;
	/* Inherit the backend timeouts from the selected backend */
	SES_InheritBackendTimeouts(sp);

	/*
	 * Now that we know our backend, we can set a default Host:
	 * header if one is necessary.
	 * XXX: This possibly ought to go into the default VCL
	 */
	if (!http_GetHdr(hp, H_Host, &b))
		VBE_AddHostHeader(sp);

	TCP_blocking(vc->fd);	/* XXX: we should timeout instead */
	WRW_Reserve(w, &vc->fd);
	(void)http_Write(w, hp, 0);	/* XXX: stats ? */

	/* Deal with any message-body the request might have */
	i = FetchReqBody(sp);
	if (WRW_FlushRelease(w) || i > 0) {
		VBE_ClosedFd(sp);
		/* XXX: other cleanup ? */
		return (__LINE__);
	}

	/* Checkpoint the shmlog here */
	WSL_Flush(w, 0);

	/* XXX is this the right place? */
	VSL_stats->backend_req++;

	HTC_Init(htc, bereq->ws, vc->fd);
	TCP_set_read_timeout(vc->fd, sp->first_byte_timeout);
	do {
		i = HTC_Rx(htc);
		TCP_set_read_timeout(vc->fd, sp->between_bytes_timeout);
	}
	while (i == 0);

	if (i < 0) {
		VBE_ClosedFd(sp);
		/* XXX: other cleanup ? */
		return (__LINE__);
	}

	if (http_DissectResponse(sp->wrk, htc, hp)) {
		VBE_ClosedFd(sp);
		/* XXX: other cleanup ? */
		return (__LINE__);
	}

	sp->obj->entered = TIM_real();

	if (http_GetHdr(hp, H_Last_Modified, &b))
		sp->obj->last_modified = TIM_parse(b);

	/* Filter into object */
	hp2 = sp->obj->http;

	hp2->logtag = HTTP_Obj;
	http_CopyResp(hp2, hp);
	http_FilterFields(sp->wrk, sp->fd, hp2, hp, HTTPH_A_INS);
	http_CopyHome(sp->wrk, sp->fd, hp2);

	/* Determine if we have a body or not */
	cls = 0;
	mklen = 0;
	if (is_head) {
		/* nothing */
	} else if (http_GetHdr(hp, H_Content_Length, &b)) {
		cls = fetch_straight(sp, htc, b);
		mklen = 1;
	} else if (http_HdrIs(hp, H_Transfer_Encoding, "chunked")) {
		cls = fetch_chunked(sp, htc);
		mklen = 1;
	} else if (http_GetHdr(hp, H_Transfer_Encoding, &b)) {
		/* XXX: AUGH! */
		WSL(sp->wrk, SLT_Debug, vc->fd, "Invalid Transfer-Encoding");
		VBE_ClosedFd(sp);
		return (__LINE__);
	} else if (http_HdrIs(hp, H_Connection, "keep-alive")) {
		/*
		 * If we have Connection: keep-alive, it cannot possibly be
		 * EOF encoded, and since it is neither length nor chunked
		 * it must be zero length.
		 */
		mklen = 1;
	} else if (http_HdrIs(hp, H_Connection, "close")) {
		/*
		 * If we have connection closed, it is safe to read what
		 * comes in any case.
		 */
		cls = fetch_eof(sp, htc);
		mklen = 1;
	} else if (hp->protover < 1.1) {
		/*
		 * With no Connection header, assume EOF
		 */
		cls = fetch_eof(sp, htc);
		mklen = 1;
	} else {
		/*
		 * Assume zero length
		 */
		mklen = 1;
	}

	if (cls < 0) {
		/* XXX: Wouldn't this store automatically be released ? */
		while (!VTAILQ_EMPTY(&sp->obj->store)) {
			st = VTAILQ_FIRST(&sp->obj->store);
			VTAILQ_REMOVE(&sp->obj->store, st, list);
			STV_free(st);
		}
		VBE_ClosedFd(sp);
		sp->obj->len = 0;
		return (__LINE__);
	}

	{
	/* Sanity check fetch methods accounting */
		unsigned uu;

		uu = 0;
		VTAILQ_FOREACH(st, &sp->obj->store, list)
			uu += st->len;
		assert(uu == sp->obj->len);
	}

	if (mklen > 0)
		http_PrintfHeader(sp->wrk, sp->fd, hp2,
		    "Content-Length: %u", sp->obj->len);

	if (http_HdrIs(hp, H_Connection, "close")) 
		cls = 1;

	if (cls)
		VBE_ClosedFd(sp);
	else
		VBE_RecycleFd(sp);

	return (0);
}

/*--------------------------------------------------------------------
 * Debugging aids
 */

static void
debug_fragfetch(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	(void)cli;
	fetchfrag = strtoul(av[2], NULL, 0);
}

static struct cli_proto debug_cmds[] = {
	{ "debug.fragfetch", "debug.fragfetch",
		"\tEnable fetch fragmentation\n", 1, 1, debug_fragfetch },
	{ NULL }
};

/*--------------------------------------------------------------------
 *
 */

void
Fetch_Init(void)
{

	CLI_AddFuncs(DEBUG_CLI, debug_cmds);
}


