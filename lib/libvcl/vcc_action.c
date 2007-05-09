/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

#include <stdio.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/

static void
StringVal(struct tokenlist *tl) 
{
	struct var *vp;
	struct token *vt;

	if (tl->t->tok == CSTR) {
		EncToken(tl->fb, tl->t);
		vcc_NextToken(tl);
		return;
	} 
	ExpectErr(tl, VAR);
	ERRCHK(tl);
	vt = tl->t;
	vp = FindVar(tl, tl->t, vcc_vars);
	ERRCHK(tl);
	if (!vp->has_string) {
		vsb_printf(tl->sb,
		    "No string representation of '%s'\n", vp->name);
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	switch (vp->fmt) {
	case STRING:
		Fb(tl, 0, "%s", vp->rname);
		break;
	default:
		vsb_printf(tl->sb,
		    "String representation of '%s' not implemented yet.\n",
			vp->name);
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------*/

#define VCL_RET_MAC(l,u,b,i) 				\
static void						\
parse_##l(struct tokenlist *tl)				\
{							\
							\
	Fb(tl, 1, "VRT_done(sp, VCL_RET_%s);\n", #u); 	\
	vcc_ProcAction(tl->curproc, i, tl->t); 		\
	vcc_NextToken(tl);				\
}

#include "vcl_returns.h"
#undef VCL_RET_MAC

/*--------------------------------------------------------------------*/

static void
parse_call(struct tokenlist *tl)
{

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	vcc_AddCall(tl, tl->t);
	vcc_AddRef(tl, tl->t, R_FUNC);
	Fb(tl, 1, "if (VGC_function_%.*s(sp))\n", PF(tl->t));
	Fb(tl, 1, "\treturn (1);\n");
	vcc_NextToken(tl);
	return;
}

/*--------------------------------------------------------------------*/

static void
parse_error(struct tokenlist *tl)
{
	unsigned a;

	vcc_NextToken(tl);
	if (tl->t->tok == CNUM)
		a = vcc_UintVal(tl);
	else
		a = 0;
	Fb(tl, 1, "VRT_error(sp, %u", a);
	if (tl->t->tok == CSTR) {
		Fb(tl, 0, ", %.*s", PF(tl->t));
		vcc_NextToken(tl);
	} else {
		Fb(tl, 0, ", (const char *)0");
	}
	Fb(tl, 0, ");\n");
	Fb(tl, 1, "VRT_done(sp, VCL_RET_ERROR);\n");
}

/*--------------------------------------------------------------------*/

static void
parse_set(struct tokenlist *tl)
{
	struct var *vp;
	struct token *at, *vt;

	vcc_NextToken(tl);
	ExpectErr(tl, VAR);
	vt = tl->t;
	vp = FindVar(tl, tl->t, vcc_vars);
	ERRCHK(tl);
	assert(vp != NULL);
	Fb(tl, 1, "%s", vp->lname);
	vcc_NextToken(tl);
	switch (vp->fmt) {
	case INT:
	case SIZE:
	case RATE:
	case TIME:
	case FLOAT:
		if (tl->t->tok != '=')
			Fb(tl, 0, "%s %c ", vp->rname, *tl->t->b);
		at = tl->t;
		vcc_NextToken(tl);
		switch (at->tok) {
		case T_MUL:
		case T_DIV:
			Fb(tl, 0, "%g", vcc_DoubleVal(tl));
			break;
		case T_INCR:
		case T_DECR:
		case '=':
			if (vp->fmt == TIME)
				vcc_TimeVal(tl);
			else if (vp->fmt == SIZE)
				vcc_SizeVal(tl);
			else if (vp->fmt == RATE)
				vcc_RateVal(tl);
			else if (vp->fmt == FLOAT)
				Fb(tl, 0, "%g", vcc_DoubleVal(tl));
			else {
				vsb_printf(tl->sb, "Cannot assign this variable type.\n");
				vcc_ErrWhere(tl, vt);
				return;
			}
			break;
		default:
			vsb_printf(tl->sb, "Illegal assignment operator.\n");
			vcc_ErrWhere(tl, at);
			return;
		}
		Fb(tl, 0, ");\n");
		break;
#if 0	/* XXX: enable if we find a legit use */
	case IP:
		if (tl->t->tok == '=') {
			vcc_NextToken(tl);
			u = vcc_vcc_IpVal(tl);
			Fb(tl, 0, "= %uU; /* %u.%u.%u.%u */\n",
			    u,
			    (u >> 24) & 0xff,
			    (u >> 16) & 0xff,
			    (u >> 8) & 0xff,
			    u & 0xff);
			break;
		}
		vsb_printf(tl->sb, "Illegal assignment operator ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb,
		    " only '=' is legal for IP numbers\n");
		vcc_ErrWhere(tl, tl->t);
		return;
#endif
	case BACKEND:
		if (tl->t->tok == '=') {
			vcc_NextToken(tl);
			vcc_AddRef(tl, tl->t, R_BACKEND);
			Fb(tl, 0, "VGC_backend_%.*s", PF(tl->t));
			vcc_NextToken(tl);
			Fb(tl, 0, ");\n");
			break;
		}
		vsb_printf(tl->sb, "Illegal assignment operator ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb,
		    " only '=' is legal for backend\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	case HASH:
		ExpectErr(tl, T_INCR);
		vcc_NextToken(tl);
		StringVal(tl);
		Fb(tl, 0, ");\n");
		return;
	default:
		vsb_printf(tl->sb,
		    "Assignments not possible for '%s'\n", vp->name);
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}

/*--------------------------------------------------------------------*/

typedef void action_f(struct tokenlist *tl);

static struct action_table {
	const char		*name;
	action_f		*func;
} action_table[] = {
#define VCL_RET_MAC(l, u, b, i) { #l, parse_##l },
#define VCL_RET_MAC_E(l, u, b, i) VCL_RET_MAC(l, u, b, i) 
#include "vcl_returns.h"
#undef VCL_RET_MAC
#undef VCL_RET_MAC_E
	{ "call", 	parse_call },
	{ "set", 	parse_set },
	{ NULL,		NULL }
};

void
vcc_ParseAction(struct tokenlist *tl)
{
	struct token *at;
	struct action_table *atp;

	at = tl->t;
	if (at->tok == ID) {
		for(atp = action_table; atp->name != NULL; atp++) {
			if (vcc_IdIs(at, atp->name)) {
				atp->func(tl);
				return;
			}
		}
	}
	vsb_printf(tl->sb, "Expected action, 'if' or '}'\n");
	vcc_ErrWhere(tl, at);
}
