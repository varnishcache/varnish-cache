/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2017 Varnish Software AS
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

#ifdef COMMON_COMMON_VSM_H
#error "Multiple includes of common/common_vsm.h"
#endif
#define COMMON_COMMON_VSM_H

/* common_vsm.c */
struct vsm_sc;
struct VSC_main;
struct vsm_sc *CVSM_new(void *ptr, ssize_t len);
void *CVSM_alloc(struct vsm_sc *sc, ssize_t size,
    const char *class, const char *type, const char *ident);
void CVSM_free(struct vsm_sc *sc, void *ptr);
void CVSM_delete(struct vsm_sc **sc);
void CVSM_copy(struct vsm_sc *to, const struct vsm_sc *from);
void CVSM_cleaner(struct vsm_sc *sc, struct VSC_main *stats);
void CVSM_ageupdate(const struct vsm_sc *sc);

void *VSC_Alloc(const char *, size_t, size_t, const unsigned char *, size_t,
    const char *, va_list);
void VSC_Destroy(const char *, const void *);
