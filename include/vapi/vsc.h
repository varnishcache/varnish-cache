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

struct vsm;
struct vsc;
struct vsm_fantom;

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
	int semantics;			/* semantics
					 * 'c' = Counter
					 * 'g' = Gauge
					 * 'b' = bitmap
					 * '?' = unknown
					 */
	int format;			/* display format
					 * 'i' = integer
					 * 'B' = bytes
					 * 'b' = bitmap
					 * 'd' = duration
					 * '?' = unknown
					 */
	const struct VSC_level_desc *level; /* verbosity level		*/
	const char *sdesc;		/* short description		*/
	const char *ldesc;		/* long description		*/
	void *priv;			/* return val from VSC_new_f	*/
};

/*---------------------------------------------------------------------
 * Function pointers
 */

typedef void *VSC_new_f(void *priv, const struct VSC_point *const pt);
	/*
	 * priv is from VSC_State().
	 *
	 * The return value is installed in pt->priv
	 */

typedef void VSC_destroy_f(void *priv, const struct VSC_point *const pt);
	/*
	 * priv is from VSC_State().
	 */

typedef int VSC_iter_f(void *priv, const struct VSC_point *const pt);
	/*
	 * priv is the argument to VSC_Iter() and not from VSC_State().
	 *
	 * A non-zero return terminates the iteration
	 */

/*---------------------------------------------------------------------
 * VSC level access functions
 */

struct vsc *VSC_New(void);
	/*
	 * Create a new VSC instance
	 */

void VSC_Destroy(struct vsc **, struct vsm *);
	/*
	 * Destroy a VSC instance
	 *
	 * If a destroy function was installed with VSC_State()
	 * it will be called for all remaining points
	 */

int VSC_Arg(struct vsc *, char arg, const char *opt);
	/*
	 * Handle standard stat-presenter arguments
	 *	'f' - filter
	 *
	 * Return:
	 *	-1 error, VSM_Error() returns diagnostic string
	 *	 0 not handled
	 *	 1 Handled.
	 */

void VSC_State(struct vsc *, VSC_new_f *, VSC_destroy_f *, void *);
	/*
	 * Install function pointers for create/destroy and their
	 * priv pointer.  All arguments can be NULL.
	 */

int VSC_Iter(struct vsc *, struct vsm *, VSC_iter_f *, void *priv);
	/*
	 * Iterate over all statistics counters, calling a function for
	 * each counter not suppressed by any "-f" arguments.
	 *
	 * To discover new/deleted points, call VSM_Status() first.
	 *
	 * The returned points are valid until the next call to VSC_Iter()
	 *
	 * Not safe for concurrent reads with the same vsc and vsm
	 * handles.  For concurrency, initialize and attach separate
	 * structs vsc and vsm.
	 *
	 * Arguments:
	 *	    vd: The vsm context
	 *	  func: The callback function
	 *	  priv: Passed as argument to func
	 *
	 * Returns:
	 *	!=0:	func returned non-zero
	 *	0:	Done
	 */

const struct VSC_level_desc *VSC_ChangeLevel(const struct VSC_level_desc*, int);
	/*
	 * Change a level up or down.
	 */

#endif /* VAPI_VSC_H_INCLUDED */
