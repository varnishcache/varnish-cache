/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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

#define V_DEAD __attribute__ ((noreturn))

/* base64.c */
void base64_init(void);
int base64_decode(char *d, unsigned dlen, const char *s);

/* shmlog.c */
typedef int vsl_handler(void *priv, enum shmlogtag tag, unsigned fd, unsigned len, unsigned spec, const char *ptr);
#define VSL_S_CLIENT	(1 << 0)
#define VSL_S_BACKEND	(1 << 1)
#define VSL_ARGS	"bCcdI:i:r:X:x:"
#define VSL_USAGE	"[-bCcd] [-i tag] [-I regexp] [-r file] [-X regexp] [-x tag]"
vsl_handler VSL_H_Print;
struct VSL_data;
struct VSL_data *VSL_New(void);
void VSL_Select(struct VSL_data *vd, unsigned tag);
int VSL_OpenLog(struct VSL_data *vd);
void VSL_NonBlocking(struct VSL_data *vd, int nb);
int VSL_Dispatch(struct VSL_data *vd, vsl_handler *func, void *priv);
int VSL_NextLog(struct VSL_data *lh, unsigned char **pp);
int VSL_Arg(struct VSL_data *vd, int arg, const char *opt);
struct varnish_stats *VSL_OpenStats(void);
extern const char *VSL_tags[256];

/* varnish_debug.c */
void		 vdb_panic(const char *, ...) V_DEAD;

/* varnish_log.c */
typedef struct vlo_buffer vlo_buffer_t;
vlo_buffer_t	*vlo_open(const char *, size_t, int);
ssize_t		 vlo_write(vlo_buffer_t *, const void *, size_t);
vlo_buffer_t	*vlo_attach(const char *);
ssize_t		 vlo_read(vlo_buffer_t *, const void *, size_t);
#if 0
uuid_t		 vlo_get_uuid(vlo_buffer_t *);
#endif
int		 vlo_close(vlo_buffer_t *);

/* varnish_util.c */
int		 vut_open_lock(const char *, int, int, int);

#endif
