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

#define VSC_ARGS	"f:n:"
#define VSC_n_USAGE	VSM_n_USAGE
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

struct VSC_C_main *VSC_Main(struct VSM_data *vd);
	/*
	 * return Main stats structure
	 * returns NULL until child has been started.
	 */

struct VSC_type_desc {
	const char *label;		/* label */
	const char *sdesc;		/* short description */
	const char *ldesc;		/* long description */
};

struct VSC_desc {
	const char *name;		/* field name			*/
	const char *fmt;		/* field format ("uint64_t")	*/
	int flag;			/* 'c' = counter, 'g' = gauge	*/
	const char *sdesc;		/* short description		*/
	const char *ldesc;		/* long description		*/
};

struct VSC_point {
	const struct VSC_desc *desc;	/* point description		*/
	const volatile void *ptr;	/* field value			*/
	struct VSM_fantom *fantom;
};

typedef int VSC_iter_f(void *priv, const struct VSC_point *const pt);

int VSC_Iter(struct VSM_data *vd, VSC_iter_f *func, void *priv);
	/*
	 * Iterate over all statistics counters, calling "func" for
	 * each counter not suppressed by any "-f" arguments.
	 *
	 * Func is called with pt == NULL, whenever VSM allocations
	 * change (child restart, allocations/deallocations)
	 *
	 * Returns:
	 *	!=0:	func returned non-zero
	 *	-1:	No VSC's available
	 *	0:	Done
	 */

/**********************************************************************
 * Precompiled VSC_type_desc's and VSC_desc's for all know VSCs.
 */

#define VSC_TYPE_F(n,t,l,e,d) \
	extern const struct VSC_type_desc VSC_type_desc_##n;
#include "tbl/vsc_types.h"
#undef VSC_TYPE_F

#define VSC_DO(U,l,t) extern const struct VSC_desc VSC_desc_##l[];
#define VSC_F(n,t,l,f,d,e)
#define VSC_DONE(U,l,t)
#include "tbl/vsc_all.h"
#undef VSC_DO
#undef VSC_F
#undef VSC_DONE

#endif /* VAPI_VSC_H_INCLUDED */
