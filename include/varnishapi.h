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
 * $Id$
 */

#ifndef VARNISHAPI_H_INCLUDED
#define VARNISHAPI_H_INCLUDED

#include "shmlog.h"

/* base64.c */
void base64_init(void);
int base64_decode(char *d, unsigned dlen, const char *s);

/* shmlog.c */
typedef int vsl_handler(void *priv, enum shmlogtag tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr);
#define VSL_S_CLIENT	(1 << 0)
#define VSL_S_BACKEND	(1 << 1)
#define VSL_LOG_ARGS	"bCcdI:i:k:L:n:r:s:X:x:"
#define VSL_STAT_ARGS	"L:n:"
#define VSL_USAGE	"[-bCcd] [-i tag] [-I regexp] [-k keep]" \
			" [-r file] [-s skip] [-X regexp] [-x tag]"
vsl_handler VSL_H_Print;
struct VSL_data;
struct VSL_data *VSL_New(void);
void VSL_Select(struct VSL_data *vd, unsigned tag);
int VSL_OpenLog(struct VSL_data *vd);
void VSL_NonBlocking(struct VSL_data *vd, int nb);
int VSL_Dispatch(struct VSL_data *vd, vsl_handler *func, void *priv);
int VSL_NextLog(struct VSL_data *lh, unsigned char **pp);
int VSL_Log_Arg(struct VSL_data *vd, int arg, const char *opt);
int VSL_Stat_Arg(struct VSL_data *vd, int arg, const char *opt);
void VSL_Close(struct VSL_data *vd);
int VSL_Open(struct VSL_data *vd);
void VSL_Delete(struct VSL_data *vd);
struct varnish_stats *VSL_OpenStats(struct VSL_data *vd);
const char *VSL_Name(const struct VSL_data *vd);
extern const char *VSL_tags[256];
void *VSL_Find_Alloc(struct VSL_data *vd, const char *class, const char *type,
    const char *ident, unsigned *lenp);

int VSL_ReOpen(struct VSL_data *vd);

struct shmalloc *vsl_iter0(const struct VSL_data *vd);
void vsl_itern(const struct VSL_data *vd, struct shmalloc **pp);

#define VSL_FOREACH(var, vd) \
	for((var) = vsl_iter0((vd)); (var) != NULL; vsl_itern((vd), &(var)))

struct vsl_statpt {
	const char *type;	/* stat struct type			*/
	const char *ident;	/* stat struct ident			*/
	const char *nm;		/* field name				*/
	const char *fmt;	/* field format ("uint64_t")		*/
	int flag;		/* 'a' = counter, 'i' = gauge		*/
	const char *desc;	/* description				*/
	const volatile void *ptr;	/* field value		*/
};

typedef int vsl_stat_f(void *priv, const struct vsl_statpt *const pt);

int VSL_IterStat(const struct VSL_data *vd, vsl_stat_f *func, void *priv);

#endif
