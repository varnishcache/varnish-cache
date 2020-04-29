/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 */

#include "config.h"

#include "cache_varnishd.h"

#include <stdio.h>

static const void * const snap_overflowed = &snap_overflowed;

void
WS_Assert(const struct ws *ws)
{

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	DSL(DBG_WORKSPACE, 0, "WS(%p) = (%s, %p %u %u %u)",
	    ws, ws->id, ws->s, pdiff(ws->s, ws->f),
	    ws->r == NULL ? 0 : pdiff(ws->f, ws->r),
	    pdiff(ws->s, ws->e));
	assert(ws->s != NULL);
	assert(PAOK(ws->s));
	assert(ws->e != NULL);
	assert(PAOK(ws->e));
	assert(ws->s < ws->e);
	assert(ws->f >= ws->s);
	assert(ws->f <= ws->e);
	assert(PAOK(ws->f));
	if (ws->r) {
		assert(ws->r > ws->s);
		assert(ws->r <= ws->e);
		assert(PAOK(ws->r));
	}
	assert(*ws->e == 0x15);
}

int
WS_Inside(const struct ws *ws, const void *bb, const void *ee)
{
	const char *b = bb;
	const char *e = ee;

	WS_Assert(ws);
	if (b < ws->s || b >= ws->e)
		return (0);
	if (e != NULL && (e < b || e > ws->e))
		return (0);
	return (1);
}

void
WS_Assert_Allocated(const struct ws *ws, const void *ptr, ssize_t len)
{
	const char *p = ptr;

	WS_Assert(ws);
	if (len < 0)
		len = strlen(p) + 1;
	assert(p >= ws->s && (p + len) <= ws->f);
}

/*
 * NB: The id must be max 3 char and lower-case.
 * (upper-case the first char to indicate overflow)
 */

void
WS_Init(struct ws *ws, const char *id, void *space, unsigned len)
{

	DSL(DBG_WORKSPACE, 0,
	    "WS_Init(%p, \"%s\", %p, %u)", ws, id, space, len);
	assert(space != NULL);
	INIT_OBJ(ws, WS_MAGIC);
	ws->s = space;
	assert(PAOK(space));
	len = PRNDDN(len - 1);
	ws->e = ws->s + len;
	*ws->e = 0x15;
	ws->f = ws->s;
	assert(id[0] & 0x20);		// cheesy islower()
	bstrcpy(ws->id, id);
	WS_Assert(ws);
}

void
WS_MarkOverflow(struct ws *ws)
{
	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);

	ws->id[0] &= ~0x20;		// cheesy toupper()
}

static void
ws_ClearOverflow(struct ws *ws)
{
	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);

	ws->id[0] |= 0x20;		// cheesy tolower()
}

/*
 * Reset a WS to a cookie from WS_Snapshot
 *
 * for use by any code using cache.h
 *
 * does not reset the overflow bit and asserts that, if WS_Snapshot had found
 * the workspace overflown, the marker is intact
 */

void
WS_Reset(struct ws *ws, uintptr_t pp)
{
	char *p;

	WS_Assert(ws);
	AN(pp);
	if (pp == (uintptr_t)snap_overflowed) {
		DSL(DBG_WORKSPACE, 0, "WS_Reset(%p, overflowed)", ws);
		AN(WS_Overflowed(ws));
		return;
	}
	p = (char *)pp;
	DSL(DBG_WORKSPACE, 0, "WS_Reset(%p, %p)", ws, p);
	assert(ws->r == NULL);
	assert(p >= ws->s);
	assert(p <= ws->e);
	ws->f = p;
	WS_Assert(ws);
}

/*
 * Reset the WS to a cookie or its start and clears any overflow
 *
 * for varnishd internal use only
 */

void
WS_Rollback(struct ws *ws, uintptr_t pp)
{
	WS_Assert(ws);

	if (pp == 0)
		pp = (uintptr_t)ws->s;

	ws_ClearOverflow(ws);
	WS_Reset(ws, pp);
}

void *
WS_Alloc(struct ws *ws, unsigned bytes)
{
	char *r;

	WS_Assert(ws);
	bytes = PRNDUP(bytes);

	assert(ws->r == NULL);
	if (ws->f + bytes > ws->e) {
		WS_MarkOverflow(ws);
		return (NULL);
	}
	r = ws->f;
	ws->f += bytes;
	DSL(DBG_WORKSPACE, 0, "WS_Alloc(%p, %u) = %p", ws, bytes, r);
	WS_Assert(ws);
	return (r);
}

void *
WS_Copy(struct ws *ws, const void *str, int len)
{
	char *r;
	unsigned bytes;

	WS_Assert(ws);
	assert(ws->r == NULL);

	if (len == -1)
		len = strlen(str) + 1;
	assert(len >= 0);

	bytes = PRNDUP((unsigned)len);
	if (ws->f + bytes > ws->e) {
		WS_MarkOverflow(ws);
		return (NULL);
	}
	r = ws->f;
	ws->f += bytes;
	memcpy(r, str, len);
	DSL(DBG_WORKSPACE, 0, "WS_Copy(%p, %d) = %p", ws, len, r);
	WS_Assert(ws);
	return (r);
}

void *
WS_Printf(struct ws *ws, const char *fmt, ...)
{
	unsigned u, v;
	va_list ap;
	char *p;

	u = WS_ReserveAll(ws);
	p = ws->f;
	va_start(ap, fmt);
	v = vsnprintf(p, u, fmt, ap);
	va_end(ap);
	if (v >= u) {
		WS_Release(ws, 0);
		WS_MarkOverflow(ws);
		p = NULL;
	} else {
		WS_Release(ws, v + 1);
	}
	return (p);
}

uintptr_t
WS_Snapshot(struct ws *ws)
{

	WS_Assert(ws);
	assert(ws->r == NULL);
	if (WS_Overflowed(ws)) {
		DSL(DBG_WORKSPACE, 0, "WS_Snapshot(%p) = overflowed", ws);
		return ((uintptr_t) snap_overflowed);
	}
	DSL(DBG_WORKSPACE, 0, "WS_Snapshot(%p) = %p", ws, ws->f);
	return ((uintptr_t)ws->f);
}

/*
 * WS_Release() must be called in all cases
 */
unsigned
WS_ReserveAll(struct ws *ws)
{
	unsigned b;

	WS_Assert(ws);
	assert(ws->r == NULL);

	ws->r = ws->e;
	b = pdiff(ws->f, ws->r);

	WS_Assert(ws);
	DSL(DBG_WORKSPACE, 0, "WS_ReserveAll(%p) = %u", ws, b);

	return (b);
}

/*
 * WS_Release() must be called for retval > 0 only
 */
unsigned
WS_ReserveSize(struct ws *ws, unsigned bytes)
{
	unsigned b2;

	WS_Assert(ws);
	assert(ws->r == NULL);
	assert(bytes > 0);

	b2 = PRNDDN(ws->e - ws->f);
	if (bytes < b2)
		b2 = PRNDUP(bytes);

	if (bytes > b2) {
		WS_MarkOverflow(ws);
		return (0);
	}
	ws->r = ws->f + b2;
	DSL(DBG_WORKSPACE, 0, "WS_ReserveSize(%p, %u/%u) = %u",
	    ws, b2, bytes, pdiff(ws->f, ws->r));
	WS_Assert(ws);
	return (pdiff(ws->f, ws->r));
}

/* REL_20200915 remove */
unsigned
WS_Reserve(struct ws *ws, unsigned bytes)
{
	unsigned b2;

	WS_Assert(ws);
	assert(ws->r == NULL);

	b2 = PRNDDN(ws->e - ws->f);
	if (bytes != 0 && bytes < b2)
		b2 = PRNDUP(bytes);

	if (ws->f + b2 > ws->e) {
		WS_MarkOverflow(ws);
		return (0);
	}
	ws->r = ws->f + b2;
	DSL(DBG_WORKSPACE, 0, "WS_Reserve(%p, %u/%u) = %u",
	    ws, b2, bytes, pdiff(ws->f, ws->r));
	WS_Assert(ws);
	return (pdiff(ws->f, ws->r));
}

unsigned
WS_ReserveLumps(struct ws *ws, size_t sz)
{
	return (WS_ReserveAll(ws) / sz);
}

void
WS_Release(struct ws *ws, unsigned bytes)
{
	WS_Assert(ws);
	bytes = PRNDUP(bytes);
	assert(bytes <= ws->e - ws->f);
	DSL(DBG_WORKSPACE, 0, "WS_Release(%p, %u)", ws, bytes);
	assert(ws->r != NULL);
	assert(ws->f + bytes <= ws->r);
	ws->f += bytes;
	ws->r = NULL;
	WS_Assert(ws);
}

void
WS_ReleaseP(struct ws *ws, const char *ptr)
{
	WS_Assert(ws);
	DSL(DBG_WORKSPACE, 0, "WS_ReleaseP(%p, %p (%zd))", ws, ptr, ptr - ws->f);
	assert(ws->r != NULL);
	assert(ptr >= ws->f);
	assert(ptr <= ws->r);
	ws->f += PRNDUP(ptr - ws->f);
	ws->r = NULL;
	WS_Assert(ws);
}

int
WS_Overflowed(const struct ws *ws)
{
	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	AN(ws->id[0]);

	if (ws->id[0] & 0x20)		// cheesy islower()
		return (0);
	return (1);
}

/*---------------------------------------------------------------------
 * Build a VSB on a workspace.
 * Usage pattern:
 *
 *	struct vsb vsb[1];
 *	char *p;
 *
 *	WS_VSB_new(vsb, ctx->ws);
 *	VSB_printf(vsb, "blablabla");
 *	p = WS_VSB_finish(vsb, ctx->ws, NULL);
 *	if (p == NULL)
 *		return (FAILURE);
 */

void
WS_VSB_new(struct vsb *vsb, struct ws *ws)
{
	unsigned u;
	static char bogus[2];	// Smallest possible vsb

	AN(vsb);
	WS_Assert(ws);
	u = WS_ReserveAll(ws);
	if (WS_Overflowed(ws) || u < 2)
		AN(VSB_init(vsb, bogus, sizeof bogus));
	else
		AN(VSB_init(vsb, WS_Front(ws), u));
}

char *
WS_VSB_finish(struct vsb *vsb, struct ws *ws, size_t *szp)
{
	char *p;

	AN(vsb);
	WS_Assert(ws);
	if (!VSB_finish(vsb)) {
		p = VSB_data(vsb);
		if (p == WS_Front(ws)) {
			WS_Release(ws, VSB_len(vsb) + 1);
			if (szp != NULL)
				*szp = VSB_len(vsb);
			VSB_fini(vsb);
			return (p);
		}
	}
	WS_MarkOverflow(ws);
	VSB_fini(vsb);
	WS_Release(ws, 0);
	if (szp)
		*szp = 0;
	return (NULL);
}
