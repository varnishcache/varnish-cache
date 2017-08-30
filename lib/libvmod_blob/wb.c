/*-
 * Copyright 2015 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Nils Goroll <nils.goroll@uplex.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * write buffer: utility functions to append-write on a varnish workspace
 */

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "wb.h"
#include "vdef.h"
#include "vrt.h"

char *
wb_create(struct ws *ws, struct wb_s *wb)
{
	if (WS_Reserve(ws, 0) == 0) {
		wb->w = NULL;
		wb->ws = NULL;
		return NULL;
	}
	wb->w = ws->f;
	wb->ws = ws;

	return wb->w;
}

void
wb_reset(struct wb_s *wb)
{
	WS_Release(wb->ws, 0);
	memset(wb, 0, sizeof(*wb));
}

bool
wb_printf(struct wb_s *wb, const char *format, ...)
{
	int len;
	va_list ap;
	const ssize_t space = wb_space(wb);


	va_start(ap, format);
	len = vsnprintf(wb->w, space, format, ap);
	va_end(ap);
	if (len >= space) {
		wb_reset(wb);
		return false;
	} else {
		wb_advance(wb, len);
	}

	return true;
}

bool
wb_append(struct wb_s *wb, const char *p, int len)
{
	if (len == -1)
		len = strlen(p);
	if (len >= wb_space(wb))
		return false;
	memcpy(wb->w, p, len);
	wb_advance(wb, len);
	return true;
}

/*
 * release varnish workspace
 *
 * return start of buffer
 */
char *
wb_finish(struct wb_s *wb, ssize_t *l)
{
	char *r = wb->ws->f;
	assert(wb->ws->r - wb->w > 0);
	if (l)
		*l = wb_len(wb);

	*wb->w = '\0';
	wb->w++;

	/* amount of space used */
	WS_ReleaseP(wb->ws, wb->w);

	return r;
}

/*
 * finish into a blob
 */
struct vmod_priv *
wb_finish_blob(struct wb_s *wb, struct vmod_priv *blob) {
	ssize_t l;
	blob->priv = wb_finish(wb, &l);
	blob->len = l;
	blob->free = NULL;
	return blob;
}
