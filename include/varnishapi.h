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

#include "vsl.h"

/*
 * Various notes:
 *	All malloc failures will result in assert tripping.
 *	API use failures will trip assert.
 */


/*---------------------------------------------------------------------
 * Level 0:  Create and destroy the VSL_data handle structure
 */

struct VSL_data *VSL_New(void);
	/*
	 * Allocate and initialize a VSL_data handle structure.
	 * This is the first thing you will have to do, always.
	 * You can have multiple active VSL_data handles at the same time
	 * referencing the same or different shared memory files.
	 * Returns:
	 * 	Pointer to usable VSL_data handle.
	 */

typedef void vsl_diag_f(void *priv, const char *fmt, ...);

void VSL_Diag(struct VSL_data *vd, vsl_diag_f *func, void *priv);
	/*
	 * Set the diagnostics reporting function.
	 * Default is fprintf(stderr, ...)
	 * If func is NULL, diagnostics are disabled.
	 */

int VSL_n_Arg(struct VSL_data *vd, const char *n_arg);
	/*
	 * Configure which varnishd instance to access.
	 * Can also be, and normally is done through the VSL_Log_arg()
	 * and VSL_Stat_Arg() functions.
	 * Returns:
	 *	 1 on success
	 *	 -1 on failure, with diagnostic on stderr.
	 */

const char *VSL_Name(const struct VSL_data *vd);
	/*
	 * Return the instance name.
	 */

void VSL_Delete(struct VSL_data *vd);
	/*
	 * Close and deallocate all storage and mappings.
	 */

/* XXX: extension:  Patience argument for sleeps */

/*---------------------------------------------------------------------
 * Level 1:  Open/Close and find allocation in shared memory segment
 */

int VSL_Open(struct VSL_data *vd, int diag);
	/*
	 * Attempt to open and map the shared memory file.
	 * If diag is non-zero, diagnostics are emitted.
	 * Returns:
	 *	0 on success
	 * 	!= 0 on failure
	 */

int VSL_ReOpen(struct VSL_data *vd, int diag);
	/*
	 * Check if shared memory segment needs to be reopened/remapped
	 * typically when the varnishd master process restarts.
	 * diag is passed to VSL_Open()
	 * Returns:
	 *	0  No reopen needed.
	 *	1  shared memory reopened/remapped.
	 *	-1 failure to reopen.
	 */

void *VSL_Find_Alloc(struct VSL_data *vd, const char *class, const char *type,
    const char *ident, unsigned *lenp);
void VSL_Close(struct VSL_data *vd);


/* shmlog.c */
typedef int vsl_handler(void *priv, enum vsl_tag tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr);
#define VSL_S_CLIENT	(1 << 0)
#define VSL_S_BACKEND	(1 << 1)
#define VSL_LOG_ARGS	"bCcdI:i:k:L:n:r:s:X:x:"
#define VSL_STAT_ARGS	"L:n:"
#define VSL_USAGE	"[-bCcd] [-i tag] [-I regexp] [-k keep]" \
			" [-r file] [-s skip] [-X regexp] [-x tag]"
vsl_handler VSL_H_Print;
struct VSL_data;
void VSL_Select(const struct VSL_data *vd, unsigned tag);
int VSL_OpenLog(struct VSL_data *vd);
void VSL_NonBlocking(struct VSL_data *vd, int nb);
int VSL_Dispatch(struct VSL_data *vd, vsl_handler *func, void *priv);
int VSL_NextLog(struct VSL_data *lh, uint32_t **pp);
int VSL_Log_Arg(struct VSL_data *vd, int arg, const char *opt);
int VSL_Stat_Arg(struct VSL_data *vd, int arg, const char *opt);
struct vsc_main *VSL_OpenStats(struct VSL_data *vd);
extern const char *VSL_tags[256];


struct vsm_chunk *vsl_iter0(const struct VSL_data *vd);
void vsl_itern(const struct VSL_data *vd, struct vsm_chunk **pp);

#define VSL_FOREACH(var, vd) \
	for((var) = vsl_iter0((vd)); (var) != NULL; vsl_itern((vd), &(var)))

struct vsl_statpt {
	const char *class;	/* stat struct type			*/
	const char *ident;	/* stat struct ident			*/
	const char *name;	/* field name				*/
	const char *fmt;	/* field format ("uint64_t")		*/
	int flag;		/* 'a' = counter, 'i' = gauge		*/
	const char *desc;	/* description				*/
	const volatile void *ptr;	/* field value		*/
};

typedef int vsl_stat_f(void *priv, const struct vsl_statpt *const pt);

int VSL_IterStat(const struct VSL_data *vd, vsl_stat_f *func, void *priv);

/* base64.c */
void base64_init(void);
int base64_decode(char *d, unsigned dlen, const char *s);

#endif
