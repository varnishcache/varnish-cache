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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 */

#include <sys/types.h>
#include <sys/uio.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "heritage.h"
#include "shmlog.h"
#include "vcl.h"
#include "cli_priv.h"
#include "cache.h"

void
WS_Assert(const struct ws *ws)
{

	assert(ws != NULL);
	assert(ws->s != NULL);
	assert(ws->e != NULL);
	assert(ws->s < ws->e);
	assert(ws->f >= ws->s);
	assert(ws->f <= ws->e);
	if (ws->r) {
		assert(ws->r > ws->s);
		assert(ws->r <= ws->e);
	}
}

void
WS_Init(struct ws *ws, void *space, unsigned len)
{

	assert(space != NULL);
	memset(ws, 0, sizeof *ws);
	ws->s = space;
	ws->e = ws->s + len;
	ws->f = ws->s;
	WS_Assert(ws);
}

void
WS_Reset(struct ws *ws)
{

	WS_Assert(ws);
	assert(ws->r == NULL);
	ws->f = ws->s;
}

char *
WS_Alloc(struct ws *ws, unsigned bytes)
{
	char *r;

	WS_Assert(ws);
	assert(ws->r == NULL);
	if (ws->f + bytes > ws->e)
		return(NULL);
	r = ws->f;
	ws->f += bytes;
	return (r);
}

char *
WS_Dup(struct ws *ws, const char *s)
{
	unsigned l;
	char *p;

	l = strlen(s) + 1;
	p = WS_Alloc(ws, l);
	if (p != NULL)
		memcpy(p, s, l);
	return (p);
}

unsigned
WS_Reserve(struct ws *ws, unsigned bytes)
{
	WS_Assert(ws);
	assert(ws->r == NULL);
	if (bytes == 0)
		bytes = ws->e - ws->f;
	xxxassert(ws->f + bytes <= ws->e);
	ws->r = ws->f + bytes;
	return (ws->r - ws->f);
}

void
WS_Release(struct ws *ws, unsigned bytes)
{
	WS_Assert(ws);
	assert(ws->r != NULL);
	assert(ws->f + bytes <= ws->r);
	ws->f += bytes;
	ws->r = NULL;
}

void
WS_ReleaseP(struct ws *ws, char *ptr)
{
	WS_Assert(ws);
	assert(ws->r != NULL);
	assert(ptr >= ws->f);
	assert(ptr <= ws->r);
	ws->f = ptr;
	ws->r = NULL;
}

#if 0
/* XXX: not used anywhere (yet) */
void
WS_Return(struct ws *ws, char *s, char *e)
{

	WS_Assert(ws);
	if (e == ws->f)
		ws->f = s;
}
#endif
