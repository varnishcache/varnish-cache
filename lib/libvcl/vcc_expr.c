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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/

#define L(tl, foo)	do {	\
	tl->indent += INDENT;	\
	foo;			\
	tl->indent -= INDENT;	\
} while (0)

#if 0
#define C(tl, sep)	do {					\
	Fb(tl, 1, "VRT_count(sp, %u)%s\n", ++tl->cnt, sep);	\
	tl->t->cnt = tl->cnt;					\
} while (0)
#endif

/*--------------------------------------------------------------------
 * Recognize and convert units of time, return seconds.
 */

static double
vcc_TimeUnit(struct vcc *tl)
{
	double sc = 1.0;

	assert(tl->t->tok == ID);
	if (vcc_IdIs(tl->t, "ms"))
		sc = 1e-3;
	else if (vcc_IdIs(tl->t, "s"))
		sc = 1.0;
	else if (vcc_IdIs(tl->t, "m"))
		sc = 60.0;
	else if (vcc_IdIs(tl->t, "h"))
		sc = 60.0 * 60.0;
	else if (vcc_IdIs(tl->t, "d"))
		sc = 60.0 * 60.0 * 24.0;
	else if (vcc_IdIs(tl->t, "w"))
		sc = 60.0 * 60.0 * 24.0 * 7.0;
	else {
		vsb_printf(tl->sb, "Unknown time unit ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, ".  Legal are 's', 'm', 'h' and 'd'\n");
		vcc_ErrWhere(tl, tl->t);
		return (1.0);
	}
	vcc_NextToken(tl);
	return (sc);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM } to unsigned value
 * The tokenizer made sure we only get digits.
 */

unsigned
vcc_UintVal(struct vcc *tl)
{
	unsigned d = 0;
	const char *p;

	Expect(tl, CNUM);
	for (p = tl->t->b; p < tl->t->e; p++) {
		d *= 10;
		d += *p - '0';
	}
	vcc_NextToken(tl);
	return (d);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM [ '.' [ CNUM ] ] } to double value
 * The tokenizer made sure we only get digits and a '.'
 */

static void
vcc_NumVal(struct vcc *tl, double *d, int *frac)
{
	double e = 0.1;
	const char *p;

	*frac = 0;
	*d = 0.0;
	Expect(tl, CNUM);
	if (tl->err) {
		*d = NAN;
		return;
	}
	for (p = tl->t->b; p < tl->t->e; p++) {
		*d *= 10;
		*d += *p - '0';
	}
	vcc_NextToken(tl);
	if (tl->t->tok != '.')
		return;
	*frac = 1;
	vcc_NextToken(tl);
	if (tl->t->tok != CNUM)
		return;
	for (p = tl->t->b; p < tl->t->e; p++) {
		*d += (*p - '0') * e;
		e *= 0.1;
	}
	vcc_NextToken(tl);
}

double
vcc_DoubleVal(struct vcc *tl)
{
	double d;
	int i;

	vcc_NumVal(tl, &d, &i);
	return (d);
}

/*--------------------------------------------------------------------*/

void
vcc_RTimeVal(struct vcc *tl, double *d)
{
	double v, sc;
	int sign = 1;

	if (tl->t->tok == '-') {
		sign *= -1;
		vcc_NextToken(tl);
	}
	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	ExpectErr(tl, ID);
	sc = vcc_TimeUnit(tl);
	*d = sign * v * sc;
}

/*--------------------------------------------------------------------*/

void
vcc_TimeVal(struct vcc *tl, double *d)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	ExpectErr(tl, ID);
	sc = vcc_TimeUnit(tl);
	*d = v * sc;
}

/*--------------------------------------------------------------------*/

static void
vcc_Expr2(struct vcc *tl, enum var_type *fmt)
{
	const struct symbol *sym;
	const struct var *vp;
	double d;
	int frac;

	*fmt = VOID;
	switch(tl->t->tok) {
	case ID:
		sym = VCC_FindSymbol(tl, tl->t);
		if (sym == NULL) {
			vsb_printf(tl->sb,
			    "Unknown symbol in numeric expression:\n");
			vcc_ErrToken(tl, tl->t);
			vsb_printf(tl->sb, "\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_AddUses(tl, tl->t, sym->r_methods, "Not available");
		AN(sym->var);
		vp = vcc_FindVar(tl, tl->t, 0, "cannot be read");
		ERRCHK(tl);
		assert(vp != NULL);
		Fb(tl, 1, "%s\n", vp->rname);
		*fmt = sym->fmt;
		vcc_NextToken(tl);
		return;
	case CNUM:
		vcc_NumVal(tl, &d, &frac);
		ERRCHK(tl);
		if (tl->t->tok == ID) {
			d *= vcc_TimeUnit(tl);
			ERRCHK(tl);
			*fmt = DURATION;
		} else if (!frac) {
			*fmt = INT;
		} else {
			WRONG("numeric constant botch");
		}
		Fb(tl, 1, "%g\n", d);
		return;

	default:
		vsb_printf(tl->sb,
		    "Unknown token in numeric expression:\n");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, "\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}

static void
vcc_Expr1(struct vcc *tl, enum var_type fmt)
{
	enum var_type lfmt, rfmt, afmt;
	struct token *top;
	struct token *tfirst;

	tfirst = tl->t;
	Fb(tl, 1, "(\n");
	L(tl, vcc_Expr2(tl, &lfmt));
	ERRCHK(tl);
	afmt = lfmt;
	while (1) {
		top = tl->t;
		if (tl->t->tok == '+') {
			vcc_NextToken(tl);
			Fb(tl, 1, " +\n");
			L(tl, vcc_Expr2(tl, &rfmt));
			ERRCHK(tl);
			if (lfmt == INT && rfmt == INT)
				afmt = INT;
			else if (lfmt == DURATION && rfmt == DURATION)
				afmt = DURATION;
			else if (lfmt == TIME && rfmt == DURATION)
				afmt = TIME;
			else if (lfmt == DURATION && rfmt == TIME)
				afmt = TIME;
			else {
				vsb_printf(tl->sb,
				    /* XXX print actual types */
				    "Incompatible types in addition.\n"
				    "Legal combinations:\n"
				    "\tINT+INT,\n"
				    "\tDURATION+DURATION,\n"
				    "\tDURATION+TIME,\n"
				    "\tTIME+DURATION\n");
				vcc_ErrWhere(tl, top);
				return;
			}
		} else if (tl->t->tok == '-') {
			vcc_NextToken(tl);
			Fb(tl, 1, " -\n");
			L(tl, vcc_Expr2(tl, &rfmt));
			if (lfmt == INT && rfmt == INT)
				afmt = INT;
			else if (lfmt == DURATION && rfmt == DURATION)
				afmt = DURATION;
			else if (lfmt == TIME && rfmt == DURATION)
				afmt = TIME;
			else if (lfmt == TIME && rfmt == TIME)
				afmt = DURATION;
			else {
				vsb_printf(tl->sb,
				    /* XXX print actual types */
				    "Incompatible types in subtraction.\n"
				    "Legal combinations:\n"
				    "\tINT-INT,\n"
				    "\tDURATION-DURATION,\n"
				    "\tTIME-DURATION,\n"
				    "\tTIME-TIME,\n");
				vcc_ErrWhere(tl, top);
				return;
			}
		} else
			break;
		lfmt = afmt;
	}
	Fb(tl, 1, ")\n");
	if (fmt != afmt) {
		vsb_printf(tl->sb,
		    /* XXX print actual types */
		    "Add/Subtract results in wrong type.\n"
		    "\nExpression starting at:\n" );
		vcc_ErrWhere(tl, tfirst);
		vsb_printf(tl->sb, "\nending before:\n\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}

void
vcc_Expr(struct vcc *tl, enum var_type fmt)
{

	switch (fmt) {
	case DURATION:
	case INT:
	case TIME:
		/* These types support addition and subtraction */
		Fb(tl, 1, "(\n");
		L(tl, vcc_Expr1(tl, fmt));
		ERRCHK(tl);
		Fb(tl, 1, ")\n");
		return;
	default:
		WRONG("type not implemented yet");
	}
}

