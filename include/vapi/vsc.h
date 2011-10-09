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

#ifndef VAPI_VSC_H_INCLUDED
#define VAPI_VSC_H_INCLUDED

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

#endif /* VAPI_VSC_H_INCLUDED */
