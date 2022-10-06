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

#include <stdio.h>

#include "cache_varnishd.h"

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

int
WS_Overflowed(const struct ws *ws)
{
	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	AN(ws->id[0]);

	if (ws->id[0] & 0x20)		// cheesy islower()
		return (0);
	return (1);
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

/*--------------------------------------------------------------------*/

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
		WS_ReportSize(ws, v + 1);
		WS_Release(ws, v + 1);
	}
	return (p);
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
	size_t sz;

	AN(vsb);
	WS_Assert(ws);
	if (!VSB_finish(vsb)) {
		p = VSB_data(vsb);
		if (p == ws->f) {
			sz = VSB_len(vsb) + 1;
			WS_ReportSize(ws, sz);
			WS_Release(ws, sz);
			if (szp != NULL)
				*szp = sz - 1;
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
