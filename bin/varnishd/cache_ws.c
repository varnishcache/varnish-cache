/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/uio.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shmlog.h"
#include "vcl.h"
#include "cli_priv.h"
#include "cache.h"

void
WS_Assert(const struct ws *ws)
{

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	DSL(0x02, SLT_Debug, 0, "WS(%p = (%s, %p %u %u %u)",
	    ws, ws->id, ws->s, pdiff(ws->s, ws->f),
	    ws->r == NULL ? 0 : pdiff(ws->f, ws->r),
	    pdiff(ws->s, ws->e));
	assert(ws->s != NULL);
	// assert(PAOK(ws->s));
	assert(ws->e != NULL);
	// assert(PAOK(ws->e));
	assert(ws->s < ws->e);
	assert(ws->f >= ws->s);
	assert(ws->f <= ws->e);
	// assert(PAOK(ws->f));
	if (ws->r) {
		assert(ws->r > ws->s);
		assert(ws->r <= ws->e);
		// assert(PAOK(ws->r));
	}
}

void
WS_Init(struct ws *ws, const char *id, void *space, unsigned len)
{

	DSL(0x02, SLT_Debug, 0,
	    "WS_Init(%p, \"%s\", %p, %u)", ws, id, space, len);
	assert(space != NULL);
	memset(ws, 0, sizeof *ws);
	ws->magic = WS_MAGIC;
	ws->s = space;
	assert(PAOK(space));
	ws->e = ws->s + len;
	assert(PAOK(len));
	ws->f = ws->s;
	ws->id = id;
	WS_Assert(ws);
}

/*
 * Reset a WS to start or a given pointer, likely from WS_Snapshot
 */

void
WS_Reset(struct ws *ws, char *p)
{

	WS_Assert(ws);
	DSL(0x02, SLT_Debug, 0, "WS_Reset(%p, %p)", ws, p);
	assert(ws->r == NULL);
	if (p == NULL)
		ws->f = ws->s;
	else {
		assert(p >= ws->s);
		assert(p < ws->e);
		ws->f = p;
	}
	WS_Assert(ws);
}

char *
WS_Alloc(struct ws *ws, unsigned bytes)
{
	char *r;

	WS_Assert(ws);
	bytes = PRNDUP(bytes);

	assert(ws->r == NULL);
	if (ws->f + bytes > ws->e) {
		ws->overflow++;
		WS_Assert(ws);
		return(NULL);
	}
	r = ws->f;
	ws->f += bytes;
	DSL(0x02, SLT_Debug, 0, "WS_Alloc(%p, %u) = %p", ws, bytes, r);
	WS_Assert(ws);
	return (r);
}

char *
WS_Dup(struct ws *ws, const char *s)
{
	unsigned l;
	char *p;

	WS_Assert(ws);
	l = strlen(s) + 1;
	p = WS_Alloc(ws, l);
	if (p != NULL)
		memcpy(p, s, l);
	DSL(0x02, SLT_Debug, 0, "WS_Dup(%p, \"%s\") = %p", ws, s, p);
	WS_Assert(ws);
	return (p);
}

unsigned
WS_Free(const struct ws *ws)
{

	WS_Assert(ws);
	return(ws->e - ws->f);
}

char *
WS_Snapshot(struct ws *ws)
{

	WS_Assert(ws);
	assert(ws->r == NULL);
	DSL(0x02, SLT_Debug, 0, "WS_Snapshot(%p) = %p", ws, ws->f);
	return (ws->f);
}

unsigned
WS_Reserve(struct ws *ws, unsigned bytes)
{
	unsigned b2;

	WS_Assert(ws);
	assert(ws->r == NULL);
	if (bytes == 0)
		b2 = ws->e - ws->f;
	else
		b2 = bytes;
	// b2 = PRNDND(b2);
	xxxassert(ws->f + b2 <= ws->e);
	ws->r = ws->f + b2;
	DSL(0x02, SLT_Debug, 0, "WS_Reserve(%p, %u/%u) = %u",
	    ws, b2, bytes, pdiff(ws->f, ws->r));
	WS_Assert(ws);
	return (pdiff(ws->f, ws->r));
}

void
WS_Release(struct ws *ws, unsigned bytes)
{
	WS_Assert(ws);
	// bytes = PRNDUP(bytes);
	assert(bytes <= ws->e - ws->f);
	DSL(0x02, SLT_Debug, 0, "WS_Release(%p, %u)", ws, bytes);
	assert(ws->r != NULL);
	assert(ws->f + bytes <= ws->r);
	ws->f += bytes;
	ws->r = NULL;
	WS_Assert(ws);
}

void
WS_ReleaseP(struct ws *ws, char *ptr)
{
	WS_Assert(ws);
	DSL(0x02, SLT_Debug, 0, "WS_ReleaseP(%p, %p)", ws, ptr);
	assert(ws->r != NULL);
	assert(ptr >= ws->f);
	assert(ptr <= ws->r);
	// ws->f += PRNDUP(ptr - ws->f);
	ws->f += (ptr - ws->f);
	ws->r = NULL;
	WS_Assert(ws);
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
