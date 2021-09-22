/*-
 * Copyright (c) 2021 Varnish Software AS
 * All rights reserved.
 *
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

#ifdef ENABLE_WORKSPACE_EMULATOR

#if HAVE_SANITIZER_ASAN_INTERFACE_H
#  include <sanitizer/asan_interface.h>
#endif

#include "cache_varnishd.h"

#include <stdlib.h>

struct ws_alloc {
	unsigned		magic;
#define WS_ALLOC_MAGIC		0x22e7fd05
	unsigned		off;
	unsigned		len;
	char			*ptr;
	VTAILQ_ENTRY(ws_alloc)	list;
};

VTAILQ_HEAD(ws_alloc_head, ws_alloc);

struct ws_emu {
	unsigned		magic;
#define WS_EMU_MAGIC		0x1c89b6ab
	unsigned		len;
	struct ws		*ws;
	struct ws_alloc_head	head;
};

static const uintptr_t snap_overflowed = (uintptr_t)&snap_overflowed;

static struct ws_emu *
ws_emu(const struct ws *ws)
{
	struct ws_emu *we;

	CAST_OBJ_NOTNULL(we, (void *)ws->s, WS_EMU_MAGIC);
	return (we);
}

void
WS_Assert(const struct ws *ws)
{
	struct ws_emu *we;
	struct ws_alloc *wa, *wa2 = NULL;
	size_t len;

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	assert(ws->s != NULL);
	assert(PAOK(ws->s));
	assert(ws->e != NULL);
	assert(PAOK(ws->e));

	we = ws_emu(ws);
	len = pdiff(ws->s, ws->e);
	assert(len == we->len);

	len = 0;
	VTAILQ_FOREACH(wa, &we->head, list) {
		CHECK_OBJ_NOTNULL(wa, WS_ALLOC_MAGIC);
		wa2 = wa;
		assert(len == wa->off);
		if (wa->ptr == ws->f || wa->ptr == NULL) /* reservation */
			break;
		AN(wa->len);
		len += PRNDUP(wa->len);
		assert(len <= we->len);
	}

	if (wa != NULL) {
		AZ(VTAILQ_NEXT(wa, list));
		if (wa->ptr == NULL) {
			AZ(wa->len);
			assert(ws->f == ws->e);
			assert(ws->r == ws->e);
		} else {
			AN(wa->len);
			assert(ws->f == wa->ptr);
			assert(ws->r == ws->f + wa->len);
		}
		len += PRNDUP(wa->len);
		assert(len <= we->len);
	} else {
		AZ(ws->f);
		AZ(ws->r);
	}

	DSL(DBG_WORKSPACE, 0, "WS(%p) = (%s, %p %zu %zu %zu)",
	    ws, ws->id, ws->s, wa2 == NULL ? 0 : wa2->off + PRNDUP(wa2->len),
	    ws->r == NULL ? 0 : pdiff(ws->f, ws->r),
	    pdiff(ws->s, ws->e));
}

int
WS_Allocated(const struct ws *ws, const void *ptr, ssize_t len)
{
	struct ws_emu *we;
	struct ws_alloc *wa;
	uintptr_t p, pa;

	WS_Assert(ws);
	AN(ptr);
	if (len < 0)
		len = strlen(ptr) + 1;
	p = (uintptr_t)ptr;
	we = ws_emu(ws);

	VTAILQ_FOREACH(wa, &we->head, list) {
		pa = (uintptr_t)wa->ptr;
		if (p >= (uintptr_t)ws->f && p <= (uintptr_t)ws->r)
			return (1);
		/* XXX: clang 12's ubsan triggers a pointer overflow on
		 * the if statement below. Since the purpose is to check
		 * that a pointer+length is within bounds of another
		 * pointer+length it's unclear whether a pointer overflow
		 * is relevant. Worked around for now with uintptr_t.
		 */
		if (p >= pa && p + len <= pa + wa->len)
			return (1);
	}
	return (0);
}

void
WS_Init(struct ws *ws, const char *id, void *space, unsigned len)
{
	struct ws_emu *we;

	DSL(DBG_WORKSPACE, 0,
	    "WS_Init(%p, \"%s\", %p, %u)", ws, id, space, len);
	assert(space != NULL);
	assert(PAOK(space));
	assert(len >= sizeof *we);

	len = PRNDDN(len - 1);
	INIT_OBJ(ws, WS_MAGIC);
	ws->s = space;
	ws->e = ws->s + len;

	assert(id[0] & 0x20);		// cheesy islower()
	bstrcpy(ws->id, id);

	we = space;
	INIT_OBJ(we, WS_EMU_MAGIC);
	VTAILQ_INIT(&we->head);
	we->len = len;

	WS_Assert(ws);
}

static void
ws_alloc_free(struct ws_emu *we, struct ws_alloc **wap)
{
	struct ws_alloc *wa;

	TAKE_OBJ_NOTNULL(wa, wap, WS_ALLOC_MAGIC);
	AZ(VTAILQ_NEXT(wa, list));
	VTAILQ_REMOVE(&we->head, wa, list);
	free(wa->ptr);
	FREE_OBJ(wa);
}

void
WS_Reset(struct ws *ws, uintptr_t pp)
{
	struct ws_emu *we;
	struct ws_alloc *wa;
	char *p;

	WS_Assert(ws);
	AN(pp);
	if (pp == snap_overflowed) {
		DSL(DBG_WORKSPACE, 0, "WS_Reset(%p, overflowed)", ws);
		AN(WS_Overflowed(ws));
		return;
	}
	p = (char *)pp;
	DSL(DBG_WORKSPACE, 0, "WS_Reset(%p, %p)", ws, p);
	AZ(ws->r);

	we = ws_emu(ws);
	while ((wa = VTAILQ_LAST(&we->head, ws_alloc_head)) != NULL &&
	    wa->ptr != p)
		ws_alloc_free(we, &wa);
	if (wa == NULL)
		assert(p == ws->s);

	WS_Assert(ws);
}

unsigned
WS_ReqPipeline(struct ws *ws, const void *b, const void *e)
{
	struct ws_emu *we;
	struct ws_alloc *wa;
	unsigned l;

	WS_Assert(ws);
	AZ(ws->f);
	AZ(ws->r);

	if (strcasecmp(ws->id, "req"))
		AZ(b);

	if (b == NULL) {
		AZ(e);
		if (!strcasecmp(ws->id, "req"))
			WS_Rollback(ws, 0);
		(void)WS_ReserveAll(ws);
		return (0);
	}

	we = ws_emu(ws);
	ALLOC_OBJ(wa, WS_ALLOC_MAGIC);
	AN(wa);
	wa->len = we->len;
	wa->ptr = malloc(wa->len);
	AN(wa->ptr);

	AN(e);
	l = pdiff(b, e);
	assert(l <= wa->len);
	memcpy(wa->ptr, b, l);

	WS_Rollback(ws, 0);
	ws->f = wa->ptr;
	ws->r = ws->f + wa->len;
	VTAILQ_INSERT_TAIL(&we->head, wa, list);
	WS_Assert(ws);
	return (l);
}

static struct ws_alloc *
ws_emu_alloc(struct ws *ws, unsigned len)
{
	struct ws_emu *we;
	struct ws_alloc *wa;
	size_t off = 0;

	WS_Assert(ws);
	AZ(ws->r);

	we = ws_emu(ws);
	wa = VTAILQ_LAST(&we->head, ws_alloc_head);
	CHECK_OBJ_ORNULL(wa, WS_ALLOC_MAGIC);

	if (wa != NULL)
		off = wa->off + PRNDUP(wa->len);
	if (off + len > we->len) {
		WS_MarkOverflow(ws);
		return (NULL);
	}
	if (len == 0)
		len = we->len - off;

	ALLOC_OBJ(wa, WS_ALLOC_MAGIC);
	AN(wa);
	wa->off = off;
	wa->len = len;
	if (len > 0) {
		wa->ptr = malloc(len);
		AN(wa->ptr);
	}
	VTAILQ_INSERT_TAIL(&we->head, wa, list);
	return (wa);
}

void *
WS_Alloc(struct ws *ws, unsigned bytes)
{
	struct ws_alloc *wa;

	assert(bytes > 0);
	wa = ws_emu_alloc(ws, bytes);
	WS_Assert(ws);
	if (wa != NULL) {
		AN(wa->ptr);
		DSL(DBG_WORKSPACE, 0, "WS_Alloc(%p, %u) = %p",
		    ws, bytes, wa->ptr);
		return (wa->ptr);
	}
	return (NULL);
}

void *
WS_Copy(struct ws *ws, const void *str, int len)
{
	struct ws_alloc *wa;

	AN(str);
	if (len == -1)
		len = strlen(str) + 1;
	assert(len > 0);
	wa = ws_emu_alloc(ws, len);
	WS_Assert(ws);
	if (wa != NULL) {
		AN(wa->ptr);
		memcpy(wa->ptr, str, len);
		DSL(DBG_WORKSPACE, 0, "WS_Copy(%p, %d) = %p",
		    ws, len, wa->ptr);
		return (wa->ptr);
	}
	return (NULL);
}

uintptr_t
WS_Snapshot(struct ws *ws)
{
	struct ws_emu *we;
	struct ws_alloc *wa;
	void *p;

	WS_Assert(ws);
	assert(ws->r == NULL);
	if (WS_Overflowed(ws)) {
		DSL(DBG_WORKSPACE, 0, "WS_Snapshot(%p) = overflowed", ws);
		return (snap_overflowed);
	}

	we = ws_emu(ws);
	wa = VTAILQ_LAST(&we->head, ws_alloc_head);
	CHECK_OBJ_ORNULL(wa, WS_ALLOC_MAGIC);
	p = (wa == NULL ? ws->s : wa->ptr);
	DSL(DBG_WORKSPACE, 0, "WS_Snapshot(%p) = %p", ws, p);
	return ((uintptr_t)p);
}

unsigned
WS_ReserveAll(struct ws *ws)
{
	struct ws_alloc *wa;
	unsigned b;

	wa = ws_emu_alloc(ws, 0);
	AN(wa);

	if (wa->ptr != NULL) {
		AN(wa->len);
		ws->f = wa->ptr;
		ws->r = ws->f + wa->len;
	} else {
		ws->f = ws->r = ws->e;
	}

	b = pdiff(ws->f, ws->r);
	DSL(DBG_WORKSPACE, 0, "WS_ReserveAll(%p) = %u", ws, b);
	WS_Assert(ws);
	return (b);
}

unsigned
WS_ReserveSize(struct ws *ws, unsigned bytes)
{
	struct ws_emu *we;
	struct ws_alloc *wa;

	assert(bytes > 0);
	wa = ws_emu_alloc(ws, bytes);
	if (wa == NULL)
		return (0);

	AN(wa->ptr);
	assert(wa->len == bytes);
	ws->f = wa->ptr;
	ws->r = ws->f + bytes;
	we = ws_emu(ws);
	DSL(DBG_WORKSPACE, 0, "WS_ReserveSize(%p, %u/%u) = %u",
	    ws, bytes, we->len - wa->off, bytes);
	WS_Assert(ws);
	return (bytes);
}

static void
ws_release(struct ws *ws, unsigned bytes)
{
	struct ws_emu *we;
	struct ws_alloc *wa;

	WS_Assert(ws);
	AN(ws->f);
	AN(ws->r);
	we = ws_emu(ws);
	wa = VTAILQ_LAST(&we->head, ws_alloc_head);
	AN(wa);
	assert(bytes <= wa->len);
	ws->f = ws->r = NULL;

	if (bytes == 0) {
		ws_alloc_free(we, &wa);
		return;
	}

	AN(wa->ptr);
#ifdef ASAN_POISON_MEMORY_REGION
	ASAN_POISON_MEMORY_REGION(wa->ptr + bytes, wa->len - bytes);
#endif
	wa->len = bytes;
	WS_Assert(ws);
}

void
WS_Release(struct ws *ws, unsigned bytes)
{

	ws_release(ws, bytes);
	DSL(DBG_WORKSPACE, 0, "WS_Release(%p, %u)", ws, bytes);
}

void
WS_ReleaseP(struct ws *ws, const char *ptr)
{
	unsigned l;

	WS_Assert(ws);
	assert(ws->r != NULL);
	assert(ptr >= ws->f);
	assert(ptr <= ws->r);
	l = pdiff(ws->f, ptr);
	ws_release(ws, l);
	DSL(DBG_WORKSPACE, 0, "WS_ReleaseP(%p, %p (%u))", ws, ptr, l);
}

void *
WS_AtOffset(const struct ws *ws, unsigned off, unsigned len)
{
	struct ws_emu *we;
	struct ws_alloc *wa;

	WS_Assert(ws);
	we = ws_emu(ws);

	VTAILQ_FOREACH(wa, &we->head, list) {
		if (wa->off == off) {
			assert(wa->len >= len);
			return (wa->ptr);
		}
	}

	WRONG("invalid offset");
	NEEDLESS(return (NULL));
}

unsigned
WS_ReservationOffset(const struct ws *ws)
{
	struct ws_emu *we;
	struct ws_alloc *wa;

	WS_Assert(ws);
	AN(ws->f);
	AN(ws->r);
	we = ws_emu(ws);
	wa = VTAILQ_LAST(&we->head, ws_alloc_head);
	AN(wa);
	return (wa->off);
}

unsigned
WS_Dump(const struct ws *ws, char where, size_t off, void *buf, size_t len)
{
	struct ws_emu *we;
	struct ws_alloc *wa;
	unsigned l;
	char *b;

	WS_Assert(ws);
	AN(buf);
	AN(len);

	if (strchr("sfr", where) == NULL) {
		errno = EINVAL;
		return (0);
	}

	if (where == 'r' && ws->r == NULL) {
		errno = EAGAIN;
		return (0);
	}

	we = ws_emu(ws);
	wa = VTAILQ_LAST(&we->head, ws_alloc_head);

	l = we->len;
	if (where != 's' && wa != NULL) {
		l -= wa->off;
		if (where == 'f')
			l -= wa->len;
	}

	if (off > l) {
		errno = EFAULT;
		return (0);
	}

	b = buf;
	if (where == 'f' && ws->r != NULL) {
		if (l > len)
			l = len;
		memcpy(b, wa->ptr, l);
		b += l;
		len -= l;
	}

	if (where == 's') {
		VTAILQ_FOREACH(wa, &we->head, list) {
			if (len == 0)
				break;
			if (wa->ptr == NULL)
				break;
			l = wa->len;
			if (l > len)
				l = len;
			memcpy(b, wa->ptr, l);
			b += l;
			len -= l;
		}
	}

	if (len > 0)
		memset(b, 0xa5, len);
	return (l);
}

static void
ws_emu_panic(struct vsb *vsb, const struct ws *ws)
{
	const struct ws_emu *we;
	const struct ws_alloc *wa;

	we = (void *)ws->s;
	if (PAN_dump_once(vsb, we, WS_EMU_MAGIC, "ws_emu"))
		return;
	VSB_printf(vsb, "len = %u,\n", we->len);

	VTAILQ_FOREACH(wa, &we->head, list) {
		if (PAN_dump_once_oneline(vsb, wa, WS_ALLOC_MAGIC, "ws_alloc"))
			break;
		VSB_printf(vsb, "off, len, ptr} = {%u, %u, %p}\n",
		    wa->off, wa->len, wa->ptr);
	}

	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

void
WS_Panic(struct vsb *vsb, const struct ws *ws)
{

	if (PAN_dump_struct(vsb, ws, WS_MAGIC, "ws"))
		return;
	if (ws->id[0] != '\0' && (!(ws->id[0] & 0x20)))	// cheesy islower()
		VSB_cat(vsb, "OVERFLOWED ");
	VSB_printf(vsb, "id = \"%s\",\n", ws->id);
	VSB_printf(vsb, "{s, e} = {%p", ws->s);
	if (ws->e >= ws->s)
		VSB_printf(vsb, ", +%ld", (long) (ws->e - ws->s));
	else
		VSB_printf(vsb, ", %p", ws->e);
	VSB_cat(vsb, "},\n");
	VSB_printf(vsb, "{f, r} = {%p", ws->f);
	if (ws->r >= ws->f)
		VSB_printf(vsb, ", +%ld", (long) (ws->r - ws->f));
	else
		VSB_printf(vsb, ", %p", ws->r);
	VSB_cat(vsb, "},\n");

	ws_emu_panic(vsb, ws);

	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

#endif /* ENABLE_WORKSPACE_EMULATOR */
