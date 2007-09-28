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
 * $Id: cache_http.c 2052 2007-09-28 11:07:14Z phk $
 *
 * HTTP protocol requests
 */

#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include "shmlog.h"
#include "cache.h"

/*--------------------------------------------------------------------
 * Check if we have a complete HTTP request or response yet between the
 * two pointers given.
 *
 * Return values:
 *	-1  No, and you can nuke the (white-space) content.
 *	 0  No, keep trying
 *	>0  Yes, it is this many bytes long.
 */

static int
http_header_complete(const char *b, const char *e)
{
	const char *p;

	AN(b);
	AN(e);
	assert(b <= e);
	assert(*e == '\0');
	/* Skip any leading white space */
	for (p = b ; isspace(*p); p++)
		continue;
	if (*p == '\0')
		return (-1);
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
	return (p - b);
}

/*--------------------------------------------------------------------*/

void
http_RecvPrep(struct http *hp)
{
	unsigned l;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	WS_Assert(hp->ws);
	WS_Reset(hp->ws);
	WS_Reserve(hp->ws, 0);
	hp->rx.b = hp->ws->f;
	hp->rx.e = hp->rx.b;
	if (hp->pl.b != NULL) {
		l = Tlen(hp->pl);
		memmove(hp->rx.b, hp->pl.b, l);
		hp->rx.e = hp->rx.b + l;
		hp->pl.b = hp->pl.e = NULL;
	}
	*hp->rx.e = '\0';
}

int
http_RecvPrepAgain(struct http *hp)
{
	int i;

	http_RecvPrep(hp);
	if (hp->rx.b == hp->rx.e)
		return (0);
	i = http_header_complete(hp->rx.b, hp->rx.e);
	if (i == -1)
		hp->rx.e = hp->rx.b;
	if (i <= 0)
		return (0);
	WS_ReleaseP(hp->ws, hp->rx.e);
	if (hp->rx.e != hp->rx.b + i) {
		hp->pl.b = hp->rx.b + i;
		hp->pl.e = hp->rx.e;
		hp->rx.e = hp->pl.b;
	}
	return (i);
}

/*--------------------------------------------------------------------*/

int
http_RecvSome(int fd, struct http *hp)
{
	unsigned l;
	int i;

	l = pdiff(hp->rx.e, hp->ws->e) - 1;
	l /= 2;		/* Don't fill all of workspace with read-ahead */
	if (l <= 1) {
		VSL(SLT_HttpError, fd, "Received too much");
		VSLR(SLT_HttpGarbage, fd, hp->rx);
		hp->rx.b = hp->rx.e = NULL;
		WS_Release(hp->ws, 0);
		return (1);
	}
	errno = 0;
	i = read(fd, hp->rx.e, l - 1);
	if (i > 0) {
		hp->rx.e += i;
		*hp->rx.e = '\0';
		i = http_header_complete(hp->rx.b, hp->rx.e);
		if (i == -1)
			hp->rx.e = hp->rx.b;
		if (i == 0)
			return (-1);
		WS_ReleaseP(hp->ws, hp->rx.e);
		if (hp->rx.e != hp->rx.b + i) {
			hp->pl.b = hp->rx.b + i;
			hp->pl.e = hp->rx.e;
			hp->rx.e = hp->pl.b;
		}
		return (0);
	}

	if (hp->rx.e != hp->rx.b) {
		VSL(SLT_HttpError, fd,
		    "Received (only) %d bytes, errno %d",
		    hp->rx.e - hp->rx.b, errno);
		VSLR(SLT_Debug, fd, hp->rx);
	} else if (errno == 0)
		VSL(SLT_HttpError, fd, "Received nothing");
	else
		VSL(SLT_HttpError, fd, "Received errno %d", errno);
	hp->rx.b = hp->rx.e = NULL;
	WS_Release(hp->ws, 0);
	return(2);
}

/*--------------------------------------------------------------------*/

int
http_RecvHead(struct http *hp, int fd)
{
	int i;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	http_RecvPrep(hp);
	do
		i = http_RecvSome(fd, hp);
	while (i == -1);
	return (i);
}

/*--------------------------------------------------------------------*/

int
http_GetTail(struct http *hp, unsigned len, char **b, char **e)
{

	if (hp->pl.b >= hp->pl.e)
		return (0);

	if (len == 0)
		len = Tlen(hp->pl);

	if (hp->pl.b + len > hp->pl.e)
		len = Tlen(hp->pl);
	if (len == 0)
		return (0);
	*b = hp->pl.b;
	*e = hp->pl.b + len;
	hp->pl.b += len;
	Tcheck(hp->pl);
	return (1);
}

/*--------------------------------------------------------------------*/
/* Read from fd, but soak up any tail first */

int
http_Read(struct http *hp, int fd, void *p, unsigned len)
{
	int i;
	unsigned u;
	char *b = p;

	u = 0;
	if (hp->pl.b < hp->pl.e) {
		u = Tlen(hp->pl);
		if (u > len)
			u = len;
		memcpy(b, hp->pl.b, u);
		hp->pl.b += u;
		b += u;
		len -= u;
	}
	if (hp->pl.e == hp->pl.b)
		hp->pl.b = hp->pl.e = NULL;
	if (len > 0) {
		i = read(fd, b, len);
		if (i < 0)		/* XXX i == 0 ?? */
			return (i);
		u += i;
	}
	return (u);
}

