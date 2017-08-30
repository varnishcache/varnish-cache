/*-
 * Copyright 2015-2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
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
 */

#include <stdbool.h>
#include <unistd.h>

#include "cache/cache.h"

struct ws;

struct wb_s {
	struct ws	*ws; // varnish workspace
	char		*w;  // current write position
};

/* return one byte less for the final zero byte */
static inline const char*
wb_end(struct wb_s *wb) {
	return wb->ws->r - 1;
}

/* return the write position */
static inline char*
wb_buf(struct wb_s *wb) {
	return wb->w;
}

/* return one byte less for the final zero byte */
static inline ssize_t
wb_space(struct wb_s *wb) {
	ssize_t f = wb->ws->r - wb->w;
	assert(f > 0);
	return f - 1;
}

static inline ssize_t
wb_len(struct wb_s *wb) {
	ssize_t l = wb->w - wb->ws->f;
	assert(l >= 0);
	return l;
}

static inline void
wb_advance(struct wb_s *wb, ssize_t l) {
	wb->w += l;			// final byte
	assert(wb->w < wb_end(wb));
}

static inline void
wb_advanceP(struct wb_s *wb, char *w) {
	assert(w > wb->w);
	assert(w < wb_end(wb));	// final byte
	wb->w = w;
}

char *wb_create(struct ws *ws, struct wb_s *wb);
void wb_reset(struct wb_s *wb);
bool wb_printf(struct wb_s *wb, const char *format, ...);
bool wb_append(struct wb_s *wb, const char *p, int len);
char *wb_finish(struct wb_s *wb, ssize_t *l);
struct vmod_priv *wb_finish_blob(struct wb_s *wb, struct vmod_priv *blob);
