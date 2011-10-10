/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#ifndef VAPI_VSM_H_INCLUDED
#define VAPI_VSM_H_INCLUDED

struct VSM_head;
struct VSM_data;

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

unsigned VSM_Seq(const struct VSM_data *vd);
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

#endif /* VAPI_VSM_H_INCLUDED */
