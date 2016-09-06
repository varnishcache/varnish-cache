/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#ifndef WS_H_INCLUDED
#define WS_H_INCLUDED

/*--------------------------------------------------------------------
 * Workspace structure for quick memory allocation.
 */

#include "vdef.h"

typedef void ws_debug_f(const char *, ...) __v_printflike(1, 2);

/*
 * we should add the missing bits to the ws api to allow struct ws to become
 * private
 */

struct ws {
	unsigned		magic;
#define WS_MAGIC		0x35fac554
	ws_debug_f		*debug_f;
	char			id[4];		/* identity */
	char			*s;		/* (S)tart of buffer */
	char			*f;		/* (F)ree/front pointer */
	char			*r;		/* (R)eserved length */
	char			*e;		/* (E)nd of buffer */
};

void WS_Init(struct ws *ws, const char *id, void *space, unsigned len);
void WS_Init_Debug(struct ws *ws, const char *id, void *space, unsigned len,
    ws_debug_f *debug_f);
unsigned WS_Reserve(struct ws *ws, unsigned bytes);
void WS_MarkOverflow(struct ws *ws);
void WS_Release(struct ws *ws, unsigned bytes);
void WS_ReleaseP(struct ws *ws, char *ptr);
void WS_Assert(const struct ws *ws);
void WS_Reset(struct ws *ws, char *p);
void *WS_Alloc(struct ws *ws, unsigned bytes);
void *WS_Copy(struct ws *ws, const void *str, int len);
char *WS_Snapshot(struct ws *ws);
int WS_Overflowed(const struct ws *ws);
void *WS_Printf(struct ws *ws, const char *fmt, ...) __v_printflike(2, 3);

#endif /* WS_H_INCLUDED */
