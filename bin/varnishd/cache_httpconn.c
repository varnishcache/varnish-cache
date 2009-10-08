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
 *
 * HTTP protocol requests
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include "shmlog.h"
#include "cache.h"

/*--------------------------------------------------------------------
 * Check if we have a complete HTTP request or response yet
 *
 * Return values:
 *	-1  No, and you can nuke the (white-space) content.
 *	 0  No, keep trying
 *	>0  Yes, it is this many bytes long.
 */

static int
htc_header_complete(txt *t)
{
	const char *p;

	Tcheck(*t);
	assert(*t->e == '\0');
	/* Skip any leading white space */
	for (p = t->b ; isspace(*p); p++)
		continue;
	if (*p == '\0') {
		t->e = t->b;
		*t->e = '\0';
		return (0);
	}
	while (1) {
		p = strchr(p, '\n');
		if (p == NULL)
			return (0);
		p++;
		if (*p == '\r')
			p++;
		if (*p == '\n')
			break;
	}
	p++;
	return (p - t->b);
}

/*--------------------------------------------------------------------*/

void
HTC_Init(struct http_conn *htc, struct ws *ws, int fd)
{

	htc->magic = HTTP_CONN_MAGIC;
	htc->ws = ws;
	htc->fd = fd;
	(void)WS_Reserve(htc->ws, (htc->ws->e - htc->ws->s) / 2);
	htc->rxbuf.b = ws->f;
	htc->rxbuf.e = ws->f;
	*htc->rxbuf.e = '\0';
	htc->pipeline.b = NULL;
	htc->pipeline.e = NULL;
}

/*--------------------------------------------------------------------
 * Start over, and recycle any pipelined input.
 * The WS_Reset is safe, even though the pipelined input is stored in
 * the ws somewhere, because WS_Reset only fiddles pointers.
 */

int
HTC_Reinit(struct http_conn *htc)
{
	unsigned l;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	(void)WS_Reserve(htc->ws, (htc->ws->e - htc->ws->s) / 2);
	htc->rxbuf.b = htc->ws->f;
	htc->rxbuf.e = htc->ws->f;
	if (htc->pipeline.b != NULL) {
		l = Tlen(htc->pipeline);
		memmove(htc->rxbuf.b, htc->pipeline.b, l);
		htc->rxbuf.e += l;
		htc->pipeline.b = NULL;
		htc->pipeline.e = NULL;
	}
	*htc->rxbuf.e = '\0';
	return (HTC_Complete(htc));
}

/*--------------------------------------------------------------------
 *
 */

int
HTC_Complete(struct http_conn *htc)
{
	int i;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	i = htc_header_complete(&htc->rxbuf);
	if (i < 0)
		htc->rxbuf.e = htc->rxbuf.b;
	if (i <= 0)
		return (0);
	WS_ReleaseP(htc->ws, htc->rxbuf.e);
	if (htc->rxbuf.b + i < htc->rxbuf.e) {
		htc->pipeline.b = htc->rxbuf.b + i;
		htc->pipeline.e = htc->rxbuf.e;
		htc->rxbuf.e = htc->pipeline.b;
	}
	return (1);
}

/*--------------------------------------------------------------------
 * Receive more HTTP protocol bytes
 * Returns:
 *	-2 overflow
 *	-1 error
 *	 0 more needed
 *	 1 got complete HTTP header
 */

int
HTC_Rx(struct http_conn *htc)
{
	int i;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	AN(htc->ws->r);
	i = (htc->ws->r - htc->rxbuf.e) - 1;	/* space for NUL */
	if (i <= 0) {
		WS_ReleaseP(htc->ws, htc->rxbuf.b);
		return (-2);
	}
	i = read(htc->fd, htc->rxbuf.e, i);
	if (i <= 0) {
		WS_ReleaseP(htc->ws, htc->rxbuf.b);
		return (-1);
	}
	htc->rxbuf.e += i;
	*htc->rxbuf.e = '\0';
	return (HTC_Complete(htc));
}

int
HTC_Read(struct http_conn *htc, void *d, unsigned len)
{
	unsigned l;
	unsigned char *p;
	int i;

	l = 0;
	p = d;
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	if (htc->pipeline.b) {
		l = Tlen(htc->pipeline);
		if (l > len)
			l = len;
		memcpy(p, htc->pipeline.b, l);
		p += l;
		len -= l;
		htc->pipeline.b += l;
		if (htc->pipeline.b == htc->pipeline.e)
			htc->pipeline.b = htc->pipeline.e = NULL;
	}
	if (len == 0)
		return (l);
	i = read(htc->fd, p, len);
	if (i < 0)
		return (i);
	return (i + l);
}
