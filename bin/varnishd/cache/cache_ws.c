/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2020 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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
#include <stdlib.h>

#define WS_REDZONE_BEFORE	'\xfa'
#define WS_REDZONE_AFTER	'\xfb'
#define WS_REDZONE_ALIGN	'\xfc'
#define WS_REDZONE_END		'\x15'
#define WS_RESERVE_ALIGN	(sizeof(void *))

#ifndef WS_REDZONE_SIZE
#  define WS_REDZONE_SIZE	16
#endif

#define WS_REDZONE_PSIZE	PRNDUP(WS_REDZONE_SIZE)

struct ws_alloc {
	unsigned		magic;
#define WS_ALLOC_MAGIC		0x22e7fd05
	char			*ptr;
	unsigned		len;
	unsigned		align;
	VTAILQ_ENTRY(ws_alloc)	list;
};

struct wssan {
	unsigned		magic;
#define WSSAN_MAGIC		0x1c89b6ab
	struct ws		*ws;
	VTAILQ_HEAD(, ws_alloc)	head;
};

static const uintptr_t snap_overflowed = (uintptr_t)&snap_overflowed;

/*---------------------------------------------------------------------
 * Workspace sanitizer-aware management
 */

static void
wssan_Init(struct ws *ws)
{
	struct wssan *san;

	if (!DO_DEBUG(DBG_WSSAN) || ws->f != ws->s)
		return;

	san = WS_Alloc(ws, sizeof *san);
	assert((uintptr_t)san == (uintptr_t)ws->s);
	INIT_OBJ(san, WSSAN_MAGIC);
	VTAILQ_INIT(&san->head);
	san->ws = ws;
}

static void
wssan_Clear(struct wssan *san)
{
	struct ws *ws;

	ws = san->ws;
	assert(ws->f == ws->s + sizeof *san);
	ZERO_OBJ(san, sizeof *san);
	ws->f = ws->s;
}

static void
wssan_Unwind(struct wssan *san)
{
	struct ws_alloc *wa;
	struct ws *ws;
	unsigned b;

	ws = san->ws;
	wa = VTAILQ_FIRST(&san->head);
	if (wa == NULL) {
		wssan_Clear(san);
		return;
	}

	CHECK_OBJ(wa, WS_ALLOC_MAGIC);
	assert(wa->align < WS_RESERVE_ALIGN);

	/* XXX: wssan_Assert(san, wa); */
	b = wa->len + wa->align + WS_REDZONE_PSIZE;
	assert(wa->ptr + b == ws->f);

	ws->f = wa->ptr - WS_REDZONE_PSIZE;
	assert(ws->f >= ws->s + sizeof *san);

	VTAILQ_REMOVE(&san->head, wa, list);
	FREE_OBJ(wa);
}

static void
wssan_Mark(const struct ws_alloc *wa)
{
	char *c;

	CHECK_OBJ_NOTNULL(wa, WS_ALLOC_MAGIC);
	AN(wa->ptr);
	AN(wa->len);
	assert(wa->align <= WS_RESERVE_ALIGN);

	c = wa->ptr - WS_REDZONE_PSIZE;
	memset(c, WS_REDZONE_BEFORE, WS_REDZONE_PSIZE);
	c = wa->ptr + wa->len;
	memset(c, WS_REDZONE_ALIGN, wa->align);
	c += wa->align;
	memset(c, WS_REDZONE_AFTER, WS_REDZONE_PSIZE);
}

static struct wssan *
ws_Sanitizer(const struct ws *ws)
{
	struct wssan *san;

	if (ws->s + sizeof *san > ws->f)
		return (NULL);

	san = TRUST_ME(ws->s);
	if (VALID_OBJ(san, WSSAN_MAGIC) && san->ws == ws)
		return (san);

	return (NULL);
}

static void *
ws_Alloc(struct ws *ws, unsigned bytes)
{
	struct wssan *san;
	struct ws_alloc *wa;
	void *r;

	AZ(ws->r);

	san = ws_Sanitizer(ws);
	if (san == NULL) {
		bytes = PRNDUP(bytes);
		if (ws->f + bytes > ws->e) {
			WS_MarkOverflow(ws);
			return (NULL);
		}
		r = ws->f;
		ws->f += bytes;
		return (r);
	}

	ALLOC_OBJ(wa, WS_ALLOC_MAGIC);
	AN(wa);
	wa->len = bytes;
	wa->align = PRNDUP(bytes) - bytes;
	assert(wa->align < WS_RESERVE_ALIGN);
	wa->ptr = ws->f + WS_REDZONE_PSIZE;
	bytes = wa->len + wa->align + WS_REDZONE_PSIZE * 2;

	if (ws->f + bytes > ws->e) {
		FREE_OBJ(wa);
		WS_MarkOverflow(ws);
		return (NULL);
	}

	VTAILQ_INSERT_HEAD(&san->head, wa, list);
	ws->f += bytes;
	r = wa->ptr;
	wssan_Mark(wa);
	return (r);
}

static unsigned
ws_Reserve(struct ws *ws, unsigned bytes)
{
	struct wssan *san;
	struct ws_alloc *wa;
	unsigned max, b2;

	AZ(ws->r);

	max = pdiff(ws->f, ws->e);
	if (bytes == 0)
		ws->r = ws->f; /* failsafe */

	san = ws_Sanitizer(ws);
	if (san == NULL) {
		if (bytes == 0)
			bytes = max;
		if (bytes > max)
			return (0);
		ws->r = ws->f + bytes;
		return (bytes);
	}

	ALLOC_OBJ(wa, WS_ALLOC_MAGIC);
	AN(wa);
	wa->align = WS_RESERVE_ALIGN;
	b2 = wa->align + WS_REDZONE_PSIZE * 2;

	if (b2 > max) {
		FREE_OBJ(wa);
		return (0);
	}
	if (bytes == 0)
		bytes = max - b2;
	if (bytes == 0 || bytes + b2 > max) {
		FREE_OBJ(wa);
		return (0);
	}

	ws->f += WS_REDZONE_PSIZE;
	ws->r = ws->f + bytes;
	wa->ptr = ws->f;
	wa->len = bytes;
	VTAILQ_INSERT_HEAD(&san->head, wa, list);
	wssan_Mark(wa);

	return (bytes);
}

static void
ws_Release(struct ws *ws, unsigned bytes)
{
	struct wssan *san;
	struct ws_alloc *wa;

	AN(ws->r);
	assert(bytes <= ws->r - ws->f);

	san = ws_Sanitizer(ws);
	wa = NULL;

	if (san == NULL) {
		bytes = PRNDUP(bytes);
		assert(bytes <= ws->e - ws->f);
	} else if (ws->r != ws->f) {
		wa = VTAILQ_FIRST(&san->head);
		CHECK_OBJ_NOTNULL(wa, WS_ALLOC_MAGIC);
		assert(wa->align == WS_RESERVE_ALIGN);
		/* XXX: wssan_Assert(san, wa); */
		if (bytes == 0) {
			/* NB: ws->f is already past WS_REDZONE_BEFORE */
			ws->f -= WS_REDZONE_PSIZE;
			VTAILQ_REMOVE(&san->head, wa, list);
			FREE_OBJ(wa);
		}
	}

	if (wa != NULL) {
		wa->len = bytes;
		wa->align = PRNDUP(bytes) - bytes;
		assert(wa->align < WS_RESERVE_ALIGN);
		/* NB: ws->f is already past WS_REDZONE_BEFORE */
		bytes = wa->len + wa->align + WS_REDZONE_PSIZE;
		wssan_Mark(wa);
	}

	ws->f += bytes;
	ws->r = NULL;
}

static void
ws_Reset(struct ws *ws, uintptr_t pp)
{
	struct wssan *san;
	const char *tmp;
	char *p;

	AZ(ws->r);

	p = (char *)pp;
	assert(p >= ws->s);
	assert(p <= ws->f);
	assert(p <= ws->e);

	san = ws_Sanitizer(ws);
	if (san == NULL) {
		ws->f = p;
		return;
	}

	while (p < ws->f) {
		tmp = ws->f;
		wssan_Unwind(san);
		assert(ws->f < tmp);
	}

	assert(p == ws->f);
}

/*---------------------------------------------------------------------
 * Workspace management
 */

void
WS_Assert(const struct ws *ws)
{
	const struct wssan *san;

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
	}
	assert(*ws->e == WS_REDZONE_END);

	san = ws_Sanitizer(ws);
	(void)san; /* NB: soft INCOMPL(); */
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
	unsigned l;

	DSL(DBG_WORKSPACE, 0,
	    "WS_Init(%p, \"%s\", %p, %u)", ws, id, space, len);
	assert(space != NULL);
	INIT_OBJ(ws, WS_MAGIC);
	ws->s = space;
	assert(PAOK(space));
	l = PRNDDN(len - 1);
	ws->e = ws->s + l;
	memset(ws->e, WS_REDZONE_END, len - l);
	ws->f = ws->s;
	assert(id[0] & 0x20);		// cheesy islower()
	bstrcpy(ws->id, id);
	wssan_Init(ws);
	WS_Assert(ws);
}

void
WS_Id(const struct ws *ws, char *id)
{

	WS_Assert(ws);
	AN(id);
	memcpy(id, ws->id, WS_ID_SIZE);
	id[0] |= 0x20;			// cheesy tolower()
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

	WS_Assert(ws);
	AN(pp);
	if (pp == snap_overflowed) {
		DSL(DBG_WORKSPACE, 0, "WS_Reset(%p, overflowed)", ws);
		AN(WS_Overflowed(ws));
		return;
	}
	DSL(DBG_WORKSPACE, 0, "WS_Reset(%p, %ju)", ws, pp);
	ws_Reset(ws, pp);
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
	wssan_Init(ws);
}

void *
WS_Alloc(struct ws *ws, unsigned bytes)
{
	char *r;

	WS_Assert(ws);

	r = ws_Alloc(ws, bytes);
	if (r != NULL) {
		DSL(DBG_WORKSPACE, 0, "WS_Alloc(%p, %u) = %p", ws, bytes, r);
		WS_Assert(ws);
	}
	return (r);
}

void *
WS_Copy(struct ws *ws, const void *str, int len)
{
	char *r;

	WS_Assert(ws);

	if (len == -1)
		len = strlen(str) + 1;
	assert(len >= 0);

	r = ws_Alloc(ws, len);
	if (r != NULL) {
		memcpy(r, str, len);
		DSL(DBG_WORKSPACE, 0, "WS_Copy(%p, %d) = %p", ws, len, r);
		WS_Assert(ws);
	}
	return (r);
}

const char *
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
	AZ(ws->r);
	if (WS_Overflowed(ws)) {
		DSL(DBG_WORKSPACE, 0, "WS_Snapshot(%p) = overflowed", ws);
		return (snap_overflowed);
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
	b = ws_Reserve(ws, 0);
	AN(ws->r);
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
	unsigned b;

	WS_Assert(ws);
	AN(bytes);
	b = ws_Reserve(ws, bytes);
	if (b == 0) {
		AZ(ws->r);
		WS_MarkOverflow(ws);
		return (0);
	}
	AN(ws->r);
	assert(b == bytes);
	DSL(DBG_WORKSPACE, 0, "WS_ReserveSize(%p, %u/%u) = %u",
	    ws, bytes, pdiff(ws->f, ws->e), pdiff(ws->f, ws->r));
	WS_Assert(ws);
	return (b);
}

unsigned
WS_ReserveLumps(struct ws *ws, size_t sz)
{

	AN(sz);
	return (WS_ReserveAll(ws) / sz);
}

void
WS_Release(struct ws *ws, unsigned bytes)
{
	WS_Assert(ws);
	DSL(DBG_WORKSPACE, 0, "WS_Release(%p, %u)", ws, bytes);
	ws_Release(ws, bytes);
	AZ(ws->r);
	WS_Assert(ws);
}

void
WS_ReleaseP(struct ws *ws, const char *ptr)
{
	WS_Assert(ws);
	assert(ptr >= ws->f);
	assert(ptr <= ws->r);
	DSL(DBG_WORKSPACE, 0, "WS_ReleaseP(%p, %p (%zd))", ws, ptr, ptr - ws->f);
	ws_Release(ws, ptr - ws->f);
	AZ(ws->r);
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

void *
WS_AtOffset(const struct ws *ws, unsigned off, unsigned len)
{
	char *ptr;

	WS_Assert(ws);
	ptr = ws->s + off;
	WS_Assert_Allocated(ws, ptr, len);
	return (ptr);
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
		AN(VSB_init(vsb, WS_Reservation(ws), u));
}

char *
WS_VSB_finish(struct vsb *vsb, struct ws *ws, size_t *szp)
{
	char *p;

	AN(vsb);
	WS_Assert(ws);
	if (!VSB_finish(vsb)) {
		p = VSB_data(vsb);
		if (p == ws->f) {
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

/*--------------------------------------------------------------------*/

void
WS_Panic(const struct ws *ws, struct vsb *vsb)
{

	VSB_printf(vsb, "ws = %p {\n", ws);
	if (PAN_already(vsb, ws))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, ws, WS_MAGIC);
	if (ws->id[0] != '\0' && (!(ws->id[0] & 0x20)))	// cheesy islower()
		VSB_cat(vsb, "OVERFLOWED ");
	VSB_printf(vsb, "id = \"%s\",\n", ws->id);
	VSB_printf(vsb, "{s, f, r, e} = {%p", ws->s);
	if (ws->f >= ws->s)
		VSB_printf(vsb, ", +%ld", (long) (ws->f - ws->s));
	else
		VSB_printf(vsb, ", %p", ws->f);
	if (ws->r >= ws->s)
		VSB_printf(vsb, ", +%ld", (long) (ws->r - ws->s));
	else
		VSB_printf(vsb, ", %p", ws->r);
	if (ws->e >= ws->s)
		VSB_printf(vsb, ", +%ld", (long) (ws->e - ws->s));
	else
		VSB_printf(vsb, ", %p", ws->e);
	VSB_cat(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}
