/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2021 Varnish Software AS
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

#define WS_REDZONE_END		'\x15'

static const uintptr_t snap_overflowed = (uintptr_t)&snap_overflowed;

void
WS_Assert(const struct ws *ws)
{

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	DSLb(DBG_WORKSPACE, "WS(%s, %p) = {%p %zu %zu %zu}",
	    ws->id, ws, ws->s, pdiff(ws->s, ws->f),
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
		assert(ws->r >= ws->f);
		assert(ws->r <= ws->e);
	}
	assert(*ws->e == WS_REDZONE_END);
}

int
WS_Allocated(const struct ws *ws, const void *ptr, ssize_t len)
{
	const char *p = ptr;

	WS_Assert(ws);
	if (len < 0)
		len = strlen(p) + 1;
	assert(!(p > ws->f && p <= ws->e));
	return (p >= ws->s && (p + len) <= ws->f);
}

/*
 * NB: The id must be max 3 char and lower-case.
 * (upper-case the first char to indicate overflow)
 */

void
WS_Init(struct ws *ws, const char *id, void *space, unsigned len)
{
	unsigned l;

	DSLb(DBG_WORKSPACE,
	    "WS_Init(%s, %p, %p, %u)", id, ws, space, len);
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
	WS_Assert(ws);
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
	if (pp == snap_overflowed) {
		DSLb(DBG_WORKSPACE, "WS_Reset(%s, %p, overflowed)", ws->id, ws);
		AN(WS_Overflowed(ws));
		return;
	}
	p = (char *)pp;
	DSLb(DBG_WORKSPACE, "WS_Reset(%s, %p, %p)", ws->id, ws, p);
	assert(ws->r == NULL);
	assert(p >= ws->s);
	assert(p <= ws->e);
	ws->f = p;
	WS_Assert(ws);
}

/*
 * Make a reservation and optionally pipeline a memory region that may or
 * may not originate from the same workspace.
 */

unsigned
WS_ReqPipeline(struct ws *ws, const void *b, const void *e)
{
	unsigned r, l;

	WS_Assert(ws);

	if (!strcasecmp(ws->id, "req"))
		WS_Rollback(ws, 0);
	else
		AZ(b);

	r = WS_ReserveAll(ws);

	if (b == NULL) {
		AZ(e);
		return (0);
	}

	AN(e);
	l = pdiff(b, e);
	assert(l <= r);
	memmove(ws->f, b, l);
	return (l);
}

void *
WS_Alloc(struct ws *ws, unsigned bytes)
{
	char *r;

	WS_Assert(ws);
	assert(bytes > 0);
	bytes = PRNDUP(bytes);

	assert(ws->r == NULL);
	if (ws->f + bytes > ws->e) {
		WS_MarkOverflow(ws);
		return (NULL);
	}
	r = ws->f;
	ws->f += bytes;
	DSLb(DBG_WORKSPACE, "WS_Alloc(%s, %p, %u) = %p", ws->id, ws, bytes, r);
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
	assert(len > 0);

	bytes = PRNDUP((unsigned)len);
	if (ws->f + bytes > ws->e) {
		WS_MarkOverflow(ws);
		return (NULL);
	}
	r = ws->f;
	ws->f += bytes;
	memcpy(r, str, len);
	DSLb(DBG_WORKSPACE, "WS_Copy(%s, %p, %d) = %p", ws->id, ws, len, r);
	WS_Assert(ws);
	return (r);
}

uintptr_t
WS_Snapshot(struct ws *ws)
{

	WS_Assert(ws);
	assert(ws->r == NULL);
	if (WS_Overflowed(ws)) {
		DSLb(DBG_WORKSPACE, "WS_Snapshot(%s, %p) = overflowed",
		    ws->id, ws);
		return (snap_overflowed);
	}
	DSLb(DBG_WORKSPACE, "WS_Snapshot(%s, %p) = %p", ws->id, ws, ws->f);
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
	DSLb(DBG_WORKSPACE, "WS_ReserveAll(%s, %p) = %u", ws->id, ws, b);

	return (b);
}

/*
 * WS_Release() must be called for retval > 0 only
 */
unsigned
WS_ReserveSize(struct ws *ws, unsigned bytes)
{
	unsigned l;

	WS_Assert(ws);
	assert(ws->r == NULL);
	assert(bytes > 0);

	l = pdiff(ws->f, ws->e);
	if (bytes > l) {
		WS_MarkOverflow(ws);
		return (0);
	}
	ws->r = ws->f + bytes;
	DSLb(DBG_WORKSPACE, "WS_ReserveSize(%s, %p, %u/%u) = %u", ws->id,
	    ws, bytes, l, bytes);
	WS_Assert(ws);
	return (bytes);
}

void
WS_Release(struct ws *ws, unsigned bytes)
{
	WS_Assert(ws);
	assert(bytes <= ws->e - ws->f);
	DSLb(DBG_WORKSPACE, "WS_Release(%s, %p, %u)", ws->id, ws, bytes);
	assert(ws->r != NULL);
	assert(ws->f + bytes <= ws->r);
	ws->f += PRNDUP(bytes);
	ws->r = NULL;
	WS_Assert(ws);
}

void
WS_ReleaseP(struct ws *ws, const char *ptr)
{
	WS_Assert(ws);
	DSLb(DBG_WORKSPACE, "WS_ReleaseP(%s, %p, %p (%zd))", ws->id, ws, ptr,
	    ptr - ws->f);
	assert(ws->r != NULL);
	assert(ptr >= ws->f);
	assert(ptr <= ws->r);
	ws->f += PRNDUP(ptr - ws->f);
	ws->r = NULL;
	WS_Assert(ws);
}

void *
WS_AtOffset(const struct ws *ws, unsigned off, unsigned len)
{
	char *ptr;

	WS_Assert(ws);
	ptr = ws->s + off;
	AN(WS_Allocated(ws, ptr, len));
	return (ptr);
}

unsigned
WS_ReservationOffset(const struct ws *ws)
{

	AN(ws->r);
	return (ws->f - ws->s);
}

/*--------------------------------------------------------------------*/

unsigned
WS_Dump(const struct ws *ws, char where, size_t off, void *buf, size_t len)
{
	char *b, *p;
	size_t l;

	WS_Assert(ws);
	AN(buf);
	AN(len);

	switch (where) {
	case 's': p = ws->s; break;
	case 'f': p = ws->f; break;
	case 'r': p = ws->r; break;
	default:
		errno = EINVAL;
		return (0);
	}

	if (p == NULL) {
		errno = EAGAIN;
		return (0);
	}

	p += off;
	if (p >= ws->e) {
		errno = EFAULT;
		return (0);
	}

	l = pdiff(p, ws->e);
	if (len <= l) {
		memcpy(buf, p, len);
		return (len);
	}

	b = buf;
	memcpy(b, p, l);
	memset(b + l, WS_REDZONE_END, len - l);
	return (l);
}

/*--------------------------------------------------------------------*/

static inline void
ws_printptr(struct vsb *vsb, const char *s, const char *p)
{
	if (p >= s)
		VSB_printf(vsb, ", +%ld", (long) (p - s));
	else
		VSB_printf(vsb, ", %p", p);
}

void
WS_Panic(struct vsb *vsb, const struct ws *ws)
{

	if (PAN_dump_struct(vsb, ws, WS_MAGIC, "ws"))
		return;
	if (ws->id[0] != '\0' && (!(ws->id[0] & 0x20)))	// cheesy islower()
		VSB_cat(vsb, "OVERFLOWED ");
	VSB_printf(vsb, "id = \"%s\",\n", ws->id);
	VSB_printf(vsb, "{s, f, r, e} = {%p", ws->s);
	ws_printptr(vsb, ws->s, ws->f);
	ws_printptr(vsb, ws->s, ws->r);
	ws_printptr(vsb, ws->s, ws->e);
	VSB_cat(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}
