/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#ifndef VARNISHAPI_H_INCLUDED
#define VARNISHAPI_H_INCLUDED

#include <stdint.h>

#include "vsl.h"

/*
 * Various notes:
 *	All malloc failures will result in assert tripping.
 *	API use failures will trip assert.
 */


/*---------------------------------------------------------------------
 * VSM level access functions
 */

struct VSM_data *VSM_New(void);
	/*
	 * Allocate and initialize a VSL_data handle structure.
	 * This is the first thing you will have to do, always.
	 * You can have multiple active VSL_data handles at the same time
	 * referencing the same or different shared memory files.
	 * Returns:
	 *	Pointer to usable VSL_data handle.
	 */

typedef void VSM_diag_f(void *priv, const char *fmt, ...);

void VSM_Diag(struct VSM_data *vd, VSM_diag_f *func, void *priv);
	/*
	 * Set the diagnostics reporting function.
	 * Default is fprintf(stderr, ...)
	 * If func is NULL, diagnostics are disabled.
	 */

#define VSM_n_USAGE	"[-n varnish_name]"

int VSM_n_Arg(struct VSM_data *vd, const char *n_arg);
	/*
	 * Configure which varnishd instance to access.
	 * Can also be, and normally is done through the VSL_Log_arg()
	 * and VSC_Arg() functions.
	 * Returns:
	 *	 1 on success
	 *	 -1 on failure, with diagnostic on stderr.
	 */

const char *VSM_Name(const struct VSM_data *vd);
	/*
	 * Return the instance name.
	 */

void VSM_Delete(struct VSM_data *vd);
	/*
	 * Close and deallocate all storage and mappings.
	 */

/* XXX: extension:  Patience argument for sleeps */

int VSM_Open(struct VSM_data *vd, int diag);
	/*
	 * Attempt to open and map the shared memory file.
	 * If diag is non-zero, diagnostics are emitted.
	 * Returns:
	 *	0 on success
	 *	!= 0 on failure
	 */

int VSM_ReOpen(struct VSM_data *vd, int diag);
	/*
	 * Check if shared memory segment needs to be reopened/remapped
	 * typically when the varnishd master process restarts.
	 * diag is passed to VSM_Open()
	 * Returns:
	 *	0  No reopen needed.
	 *	1  shared memory reopened/remapped.
	 *	-1 failure to reopen.
	 */

unsigned VSM_Seq(struct VSM_data *vd);
	/*
	 * Return the allocation sequence number
	 */

struct VSM_head *VSM_Head(const struct VSM_data *vd);
	/*
	 * Return the head of the VSM.
	 */

void *VSM_Find_Chunk(struct VSM_data *vd, const char *class,
    const char *type, const char *ident, unsigned *lenp);
	/*
	 * Find a given chunk in the shared memory.
	 * Returns pointer or NULL.
	 * Lenp, if non-NULL, is set to length of chunk.
	 */

void VSM_Close(struct VSM_data *vd);
	/*
	 * Unmap shared memory
	 * Deallocate all storage (including VSC and VSL allocations)
	 */

struct VSM_chunk *VSM_iter0(struct VSM_data *vd);
void VSM_itern(const struct VSM_data *vd, struct VSM_chunk **pp);

#define VSM_FOREACH(var, vd) \
    for((var) = VSM_iter0((vd)); (var) != NULL; VSM_itern((vd), &(var)))

	/*
	 * Iterate over all chunks in shared memory
	 * var = "struct VSM_chunk *"
	 * vd = "struct VSM_data"
	 */

/*---------------------------------------------------------------------
 * VSC level access functions
 */

void VSC_Setup(struct VSM_data *vd);
	/*
	 * Setup vd for use with VSC functions.
	 */

#define VSC_ARGS	"f:n:"
#define VSC_n_USAGE	VSM_n_USAGE
#define VSC_USAGE	VSC_N_USAGE

int VSC_Arg(struct VSM_data *vd, int arg, const char *opt);
	/*
	 * Handle standard stat-presenter arguments
	 * Return:
	 *	-1 error
	 *	 0 not handled
	 *	 1 Handled.
	 */

int VSC_Open(struct VSM_data *vd, int diag);
	/*
	 * Open shared memory for VSC processing.
	 * args and returns as VSM_Open()
	 */

struct VSC_C_main *VSC_Main(struct VSM_data *vd);
	/*
	 * return Main stats structure
	 */

struct VSC_point {
	const char *class;		/* stat struct type		*/
	const char *ident;		/* stat struct ident		*/
	const char *name;		/* field name			*/
	const char *fmt;		/* field format ("uint64_t")	*/
	int flag;			/* 'a' = counter, 'i' = gauge	*/
	const char *desc;		/* description			*/
	const volatile void *ptr;	/* field value			*/
};

typedef int VSC_iter_f(void *priv, const struct VSC_point *const pt);

int VSC_Iter(struct VSM_data *vd, VSC_iter_f *func, void *priv);
	/*
	 * Iterate over all statistics counters, calling "func" for
	 * each counter not suppressed by any "-f" arguments.
	 */

/*---------------------------------------------------------------------
 * VSL level access functions
 */

void VSL_Setup(struct VSM_data *vd);
	/*
	 * Setup vd for use with VSL functions.
	 */

int VSL_Open(struct VSM_data *vd, int diag);
	/*
	 * Attempt to open and map the shared memory file.
	 * If diag is non-zero, diagnostics are emitted.
	 * Returns:
	 *	0 on success
	 *	!= 0 on failure
	 */

#define VSL_ARGS	"bCcdI:i:k:n:r:s:X:x:m:"
#define VSL_b_USAGE	"[-b]"
#define VSL_c_USAGE	"[-c]"
#define VSL_C_USAGE	"[-C]"
#define VSL_d_USAGE	"[-d]"
#define VSL_i_USAGE	"[-i tag]"
#define VSL_I_USAGE	"[-I regexp]"
#define VSL_k_USAGE	"[-k keep]"
#define VSL_m_USAGE	"[-m tag:regex]"
#define VSL_n_USAGE	VSM_n_USAGE
#define VSL_r_USAGE	"[-r file]"
#define VSL_s_USAGE	"[-s skip]"
#define VSL_x_USAGE	"[-x tag]"
#define VSL_X_USAGE	"[-X regexp]"
#define VSL_USAGE	"[-bCcd] "		\
			VSL_i_USAGE " "		\
			VSL_I_USAGE " "		\
			VSL_k_USAGE " "		\
			VSL_m_USAGE " "		\
			VSL_n_USAGE " "		\
			VSL_r_USAGE " "		\
			VSL_s_USAGE " "		\
			VSL_X_USAGE " "		\
			VSL_x_USAGE

int VSL_Arg(struct VSM_data *vd, int arg, const char *opt);
	/*
	 * Handle standard log-presenter arguments
	 * Return:
	 *	-1 error
	 *	 0 not handled
	 *	 1 Handled.
	 */

typedef int VSL_handler_f(void *priv, enum VSL_tag_e tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr, uint64_t bitmap);

#define VSL_S_CLIENT	(1 << 0)
#define VSL_S_BACKEND	(1 << 1)
VSL_handler_f VSL_H_Print;
struct VSM_data;
void VSL_Select(const struct VSM_data *vd, unsigned tag);
void VSL_NonBlocking(const struct VSM_data *vd, int nb);
int VSL_Dispatch(struct VSM_data *vd, VSL_handler_f *func, void *priv);
int VSL_NextLog(const struct VSM_data *lh, uint32_t **pp, uint64_t *bitmap);
int VSL_Matched(const struct VSM_data *vd, uint64_t bitmap);
extern const char *VSL_tags[256];


/* base64.c */
void VB64_init(void);
int VB64_decode(char *d, unsigned dlen, const char *s);

#endif
