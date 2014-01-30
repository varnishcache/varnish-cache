/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * This is the public API for the VSM access.
 *
 * The VSM "class" acts as parent class for the VSL and VSC subclasses.
 *
 */

#ifndef VAPI_VSM_H_INCLUDED
#define VAPI_VSM_H_INCLUDED

#include "vsm_int.h"

struct VSM_chunk;
struct VSM_data;

/*
 * This structure is used to reference a VSM chunk
 */

struct VSM_fantom {
	struct VSM_chunk	*chunk;
	void			*b;		/* first byte of payload */
	void			*e;		/* first byte past payload */
	uintptr_t		priv;		/* VSM private */
	char			class[VSM_MARKER_LEN];
	char			type[VSM_MARKER_LEN];
	char			ident[VSM_IDENT_LEN];
};

#define VSM_FANTOM_NULL { 0, 0, 0, 0, {0}, {0}, {0} }

/*---------------------------------------------------------------------
 * VSM level access functions
 */

struct VSM_data *VSM_New(void);
	/*
	 * Allocate and initialize a VSL_data handle structure.
	 * This is the first thing you will have to do, always.
	 * You can have multiple active VSM_data handles at the same time
	 * referencing the same or different shared memory files.
	 * Returns:
	 *	Pointer to usable VSL_data handle.
	 *	NULL: malloc failed.
	 */

void VSM_Delete(struct VSM_data *vd);
	/*
	 * Close and deallocate all storage and mappings.
	 * (including any VSC and VSL "sub-classes")
	 */

const char *VSM_Error(const struct VSM_data *vd);
	/*
	 * Return the latest error message.
	 */

void VSM_ResetError(struct VSM_data *vd);
	/*
	 * Reset any error message.
	 */

#define VSM_n_USAGE	"[-n varnish_name]"

int VSM_n_Arg(struct VSM_data *vd, const char *n_arg);
	/*
	 * Configure which varnishd instance to access.
	 * Uses hostname if n_arg is NULL or "".
	 *
	 * Returns:
	 *	 1 on success
	 *	 <0 on failure, VSM_Error() returns diagnostic string
	 */

#define VSM_N_USAGE	"[-N filename]"

int VSM_N_Arg(struct VSM_data *vd, const char *N_arg);
	/*
	 * Configure the library to use the specified VSM file name. This
	 * bypasses abandonment checks and allows looking at stale VSM
	 * files without a running Varnish instance.
	 *
	 * Returns:
	 *	1 on success
	 */

const char *VSM_Name(const struct VSM_data *vd);
	/*
	 * Return the instance name.
	 */

int VSM_Open(struct VSM_data *vd);
	/*
	 * Attempt to open and map the VSM file.
	 *
	 * Returns:
	 *	0 on success, or the VSM log was already open
	 *	<0 on failure, VSM_Error() returns diagnostic string
	 */

int VSM_Abandoned(struct VSM_data *vd);
	/*
	 * Find out if the VSM file has been abandoned or closed and should
	 * be reopened.  This function calls stat(2) and should only be
	 * used when lack of activity or invalidation of fantoms indicate
	 * abandonment.
	 *
	 * Returns:
	 *	0  No reopen needed.
	 *	1  VSM abandoned.
	 */

void VSM_Close(struct VSM_data *vd);
	/*
	 * Close and unmap shared memory, if open. Any reference to
	 * previously returned memory areas will cause segmentation
	 * fault. This includes any VSC counter areas or any VSL SHM
	 * record references.
	 */


void VSM__iter0(const struct VSM_data *vd, struct VSM_fantom *vf);
int VSM__itern(const struct VSM_data *vd, struct VSM_fantom *vf);

#define VSM_FOREACH(vf, vd) \
    for(VSM__iter0((vd), (vf)); VSM__itern((vd), (vf));)
	/*
	 * Iterate over all chunks in shared memory
	 * vf = "struct VSM_fantom *"
	 * vd = "struct VSM_data *"
	 */

enum VSM_valid_e {
	VSM_invalid,
	VSM_valid,
	VSM_similar,
};

enum VSM_valid_e VSM_StillValid(const struct VSM_data *vd,
    struct VSM_fantom *vf);
	/*
	 * Check the validity of a previously looked up VSM_fantom.
	 *
	 * VSM_invalid means that the SHM chunk this fantom points to does
	 * not exist in the log file any longer. Using the fantom's
	 * pointer gives undefined results. Further checking with
	 * VSM_Abandoned() may be a good idea.
	 *
	 * VSM_valid means that the SHM structure has not changed since
	 * the fantom was looked up or since the last call to
	 * VSM_StillValid().
	 *
	 * VSM_similar means that the SHM structure has changed, but there
	 * is still a valid chunk present with the same the same type and
	 * identifier. The fantom's pointers and dimensions haven't
	 * changed. The next call to VSM_StillValid() on this fantom will
	 * return VSM_valid.
	 *
	 * Applications using the fantom to monitor a single chunk can
	 * treat VSM_similar as equal to VSM_valid.  Applications using a
	 * fantom to monitor the SHM file for new or removed chunks,
	 * should reiterate over the chunks on VSM_similar as the
	 * structure has changed.
	 *
	 * Return:
	 *   VSM_invalid: fantom is not valid any more.
	 *   VSM_valid:   fantom is still the same.
	 *   VSM_similar: a fantom with same dimensions exist in same position.
	 */

int VSM_Get(const struct VSM_data *vd, struct VSM_fantom *vf,
    const char *class, const char *type, const char *ident);
	/*
	 * Find a chunk, produce fantom for it.
	 * Returns zero on failure.
	 * class is mandatory, type and ident optional.
	 */

#endif /* VAPI_VSM_H_INCLUDED */
