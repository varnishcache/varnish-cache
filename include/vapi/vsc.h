/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

struct VSM_data;
struct VSM_fantom;

/*---------------------------------------------------------------------
 * VSC level access functions
 */

#define VSC_ARGS	"f:n:N:"
#define VSC_n_USAGE	VSM_n_USAGE " " VSM_N_USAGE
#define VSC_f_USAGE	"[-f field_name,...]"
#define VSC_USAGE	VSC_n_USAGE \
			VSC_f_USAGE

int VSC_Arg(struct VSM_data *vd, int arg, const char *opt);
	/*
	 * Handle standard stat-presenter arguments
	 * Return:
	 *	-1 error, VSM_Error() returns diagnostic string
	 *	 0 not handled
	 *	 1 Handled.
	 */

struct VSC_C_mgt *VSC_Mgt(const struct VSM_data *vd,
    struct VSM_fantom *fantom);
struct VSC_C_main *VSC_Main(const struct VSM_data *vd,
    struct VSM_fantom *fantom);
	/*
	 * Looks up and returns the management stats and the child main
	 * stats structure. If fantom is non-NULL, it can later be used
	 * with VSM_StillValid. Returns NULL until the management process
	 * has finished initialization or the child has been started.
	 *
	 * The returned structure is valid for at most 60 seconds after
	 * VSM_StillValid(,fantom) starts returning VSM_invalid. Using the
	 * pointer after this event gives undefined behavior.
	 *
	 * Arguments:
	 *	    vd: The VSM_data context
	 *	fantom: Pointer to a fantom. Can be NULL.
	 *
	 * Return values:
	 *      NULL: Failure
	 *  non-NULL: Success
	 */

void *VSC_Get(const struct VSM_data *vd, struct VSM_fantom *fantom,
    const char *type, const char *ident);
	/*
	 * Looks up the given VSC type and identifier. If fantom is
	 * non-NULL, it can if successful later be used with
	 * VSM_StillValid.
	 *
	 * Arguments:
	 *	vd:	The VSM_data context
	 *	fantom: Pointer to a fantom. Can be NULL.
	 *	type:	The type of the counter segment
	 *	ident:	The identifier of the counter segment
	 *
	 * Return values:
	 *  NULL:	Failure
	 *  non-NULL:	A void pointer to the stats structure.
	 */

struct VSC_level_desc;
struct VSC_type_desc;
struct VSC_section;
struct VSC_desc;
struct VSC_point;

struct VSC_level_desc {
	unsigned verbosity;
	const char *label;		/* label */
	const char *sdesc;		/* short description */
	const char *ldesc;		/* long description */
};

struct VSC_type_desc {
	const char *label;		/* label */
	const char *sdesc;		/* short description */
	const char *ldesc;		/* long description */
};

struct VSC_section {
	const char *type;
	const char *ident;
	const struct VSC_type_desc *desc;
	struct VSM_fantom *fantom;
};

struct VSC_desc {
	const char *name;		/* field name			*/
	const char *fmt;		/* field format ("uint64_t")	*/
	int flag;			/* 'c' = counter, 'g' = gauge	*/
	const char *sdesc;		/* short description		*/
	const char *ldesc;		/* long description		*/
	const struct VSC_level_desc *level;
};

struct VSC_point {
	const struct VSC_desc *desc;	/* point description		*/
	const volatile void *ptr;	/* field value			*/
	const struct VSC_section *section;
};

typedef int VSC_iter_f(void *priv, const struct VSC_point *const pt);

int VSC_Iter(struct VSM_data *vd, struct VSM_fantom *fantom, VSC_iter_f *func,
    void *priv);
	/*
	 * Iterate over all statistics counters, calling "func" for
	 * each counter not suppressed by any "-f" arguments.
	 *
	 * fantom points to a struct VSM_fantom. If non-NULL, it can be
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
	 *	    vd: The VSM_data context
	 *	fantom: Pointer to a fantom. Can be NULL.
	 *	  func: The callback function
	 *	  priv: Passed as argument to func
	 *
	 * Returns:
	 *	!=0:	func returned non-zero
	 *	-1:	No VSC's available
	 *	0:	Done
	 */

const struct VSC_level_desc *VSC_LevelDesc(unsigned level);

/**********************************************************************
 * Precompiled VSC_type_desc's and VSC_desc's for all know VSCs.
 */

#define VSC_LEVEL_F(v,l,e,d) \
	extern const struct VSC_level_desc VSC_level_desc_##v;
#include "tbl/vsc_levels.h"
#undef VSC_LEVEL_F

#define VSC_TYPE_F(n,t,l,e,d) \
	extern const struct VSC_type_desc VSC_type_desc_##n;
#include "tbl/vsc_types.h"
#undef VSC_TYPE_F

#define VSC_DO(U,l,t) extern const struct VSC_desc VSC_desc_##l[];
#define VSC_F(n,t,l,f,v,d,e)
#define VSC_DONE(U,l,t)
#include "tbl/vsc_all.h"
#undef VSC_DO
#undef VSC_F
#undef VSC_DONE

#endif /* VAPI_VSC_H_INCLUDED */
