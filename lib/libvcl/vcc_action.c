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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <queue.h>
#include <unistd.h>

#include "compat/asprintf.h"
#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"

#include "vrt.h"
#include "libvcl.h"

/*--------------------------------------------------------------------*/

#define L(tl, foo)	do {	\
	tl->indent += INDENT;	\
	foo;			\
	tl->indent -= INDENT;	\
} while (0)

#define C(tl, sep)	do {					\
	Fb(tl, 1, "VRT_count(sp, %u)%s\n", ++tl->cnt, sep);	\
	tl->t->cnt = tl->cnt; 					\
} while (0)

/*--------------------------------------------------------------------*/

void
vcc_ParseAction(struct tokenlist *tl)
{
	unsigned a;
	struct var *vp;
	struct token *at, *vt;

	at = tl->t;
	vcc_NextToken(tl);
	switch (at->tok) {
	case T_NO_NEW_CACHE:
		Fb(tl, 1, "VCL_no_new_cache(sp);\n");
		return;
	case T_NO_CACHE:
		Fb(tl, 1, "VCL_no_cache(sp);\n");
		return;
#define VCL_RET_MAC(a,b,c,d) case T_##b: \
		Fb(tl, 1, "VRT_done(sp, VCL_RET_%s);\n", #b); \
		vcc_ProcAction(tl->curproc, d, at); \
		return;
#include "vcl_returns.h"
#undef VCL_RET_MAC
	case T_ERROR:
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
		return;
	case T_SWITCH_CONFIG:
		ExpectErr(tl, ID);
		Fb(tl, 1, "VCL_switch_config(\"%.*s\");\n", PF(tl->t));
		vcc_NextToken(tl);
		return;
	case T_CALL:
		ExpectErr(tl, ID);
		vcc_AddCall(tl, tl->t);
		vcc_AddRef(tl, tl->t, R_FUNC);
		Fb(tl, 1, "if (VGC_function_%.*s(sp))\n", PF(tl->t));
		Fb(tl, 1, "\treturn (1);\n");
		vcc_NextToken(tl);
		return;
	case T_REWRITE:
		ExpectErr(tl, CSTR);
		Fb(tl, 1, "VCL_rewrite(%.*s", PF(tl->t));
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		Fb(tl, 0, ", %.*s);\n", PF(tl->t));
		vcc_NextToken(tl);
		return;
	case T_SET:
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
		default:
			vsb_printf(tl->sb,
			    "Assignments not possible for '%s'\n", vp->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		return;
	default:
		vsb_printf(tl->sb, "Expected action, 'if' or '}'\n");
		vcc_ErrWhere(tl, at);
		return;
	}
}
