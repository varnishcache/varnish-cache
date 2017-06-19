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
 * This is the public API for the VSC access.
 *
 * VSC is a "subclass" of VSM.
 *
 */

#ifndef VAPI_VSC_H_INCLUDED
#define VAPI_VSC_H_INCLUDED

#include "vapi/vsc_int.h"

struct vsm;
struct vsm_fantom;

/*---------------------------------------------------------------------
 * VSC level access functions
 */

int VSC_Arg(struct vsm *vd, int arg, const char *opt);
	/*
	 * Handle standard stat-presenter arguments
	 * Return:
	 *	-1 error, VSM_Error() returns diagnostic string
	 *	 0 not handled
	 *	 1 Handled.
	 */

struct VSC_level_desc;
struct VSC_point;

struct VSC_level_desc {
	const char *name;		/* name */
	const char *label;		/* label */
	const char *sdesc;		/* short description */
	const char *ldesc;		/* long description */
};

struct VSC_point {
	const volatile uint64_t *ptr;	/* field value			*/
	const char *name;		/* field name			*/
	const char *ctype;		/* C-type			*/
	int semantics;			/* semantics			*/
	int format;			/* display format		*/
	const struct VSC_level_desc *level; /* verbosity level		*/
	const char *sdesc;		/* short description		*/
	const char *ldesc;		/* long description		*/
};

struct VSC_point *VSC_Clone_Point(const struct VSC_point * const);

void VSC_Destroy_Point(struct VSC_point **);

typedef int VSC_iter_f(void *priv, const struct VSC_point *const pt);

int VSC_Iter(struct vsm *vd, struct vsm_fantom *fantom, VSC_iter_f *func,
    void *priv);
	/*
	 * Iterate over all statistics counters, calling "func" for
	 * each counter not suppressed by any "-f" arguments.
	 *
	 * fantom points to a struct vsm_fantom. If non-NULL, it can be
	 * used with VSM_StillValid to check the validity of the points
	 * returned.
	 *
	 * The returned points are valid for at most 60 seconds after
	 * VSM_StillValid(,fantom) starts returning anything but
	 * VSM_valid, or until the next call to VSC_Iter. Using the point
	 * values after any of these events gives undefined behavior.
	 *
	 * Func is called with pt == NULL, whenever VSM allocations
	 * change (child restart, allocations/deallocations)
	 *
	 * Arguments:
	 *	    vd: The vsm context
	 *	fantom: Pointer to a fantom. Can be NULL.
	 *	  func: The callback function
	 *	  priv: Passed as argument to func
	 *
	 * Returns:
	 *	!=0:	func returned non-zero
	 *	-1:	No VSC's available
	 *	0:	Done
	 */

const struct VSC_level_desc *VSC_ChangeLevel(const struct VSC_level_desc*, int);

#endif /* VAPI_VSC_H_INCLUDED */
