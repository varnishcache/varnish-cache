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

static void Compound(struct tokenlist *tl);
static void Cond_0(struct tokenlist *tl);

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

/*--------------------------------------------------------------------
 * Recognize and convert units of time, return seconds.
 */

static double
TimeUnit(struct tokenlist *tl)
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
 * Recognize and convert units of size, return bytes.
 */

static double
SizeUnit(struct tokenlist *tl)
{
	double sc = 1.0;

	assert(tl->t->tok == ID);
	if (vcc_IdIs(tl->t, "b"))
		sc = 1.0;
	else if (vcc_IdIs(tl->t, "kb"))
		sc = 1024.0;
	else if (vcc_IdIs(tl->t, "mb") || vcc_IdIs(tl->t, "Mb"))
		sc = 1024.0 * 1024.0;
	else if (vcc_IdIs(tl->t, "gb") || vcc_IdIs(tl->t, "Gb"))
		sc = 1024.0 * 1024.0 * 1024.0;
	else {
		vsb_printf(tl->sb, "Unknown size unit ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, ".  Legal are 'kb', 'mb' and 'gb'\n");
		vcc_ErrWhere(tl, tl->t);
		return (1.0);
	}
	vcc_NextToken(tl);
	return (sc);
}

/*--------------------------------------------------------------------
 * Recognize and convert units of rate as { space '/' time }
 */

static double
RateUnit(struct tokenlist *tl)
{
	double sc = 1.0;

	assert(tl->t->tok == ID);
	sc = SizeUnit(tl);
	Expect(tl, '/');
	vcc_NextToken(tl);
	sc /= TimeUnit(tl);
	return (sc);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM } to unsigned value
 */

unsigned
vcc_UintVal(struct tokenlist *tl)
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
 */

double
vcc_DoubleVal(struct tokenlist *tl)
{
	double d = 0.0, e = 0.1;
	const char *p;

	Expect(tl, CNUM);
	for (p = tl->t->b; p < tl->t->e; p++) {
		d *= 10;
		d += *p - '0';
	}
	vcc_NextToken(tl);
	if (tl->t->tok != '.')
		return (d);
	vcc_NextToken(tl);
	if (tl->t->tok != CNUM)
		return (d);
	for (p = tl->t->b; p < tl->t->e; p++) {
		d += (*p - '0') * e;
		e *= 0.1;
	}
	vcc_NextToken(tl);
	return (d);
}

/*--------------------------------------------------------------------*/

void
vcc_TimeVal(struct tokenlist *tl)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ExpectErr(tl, ID);
	sc = TimeUnit(tl);
	Fb(tl, 0, "(%g * %g)", v, sc);
}

void
vcc_SizeVal(struct tokenlist *tl)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ExpectErr(tl, ID);
	sc = SizeUnit(tl);
	Fb(tl, 0, "(%g * %g)", v, sc);
}

void
vcc_RateVal(struct tokenlist *tl)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ExpectErr(tl, ID);
	sc = RateUnit(tl);
	Fb(tl, 0, "(%g * %g)", v, sc);
}

/*--------------------------------------------------------------------*/

static void
vcc_re(struct tokenlist *tl, const char *str, struct token *re)
{
	char buf[32];

	assert(re->tok == CSTR);
	if (VRT_re_test(tl->sb, re->dec)) {
		vcc_ErrWhere(tl, re);
		return;
	}
	sprintf(buf, "VGC_re_%u", tl->recnt++);

	Fb(tl, 1, "VRT_re_match(%s, %s)\n", str, buf);
	Fh(tl, 0, "void *%s;\n", buf);
	Fi(tl, 0, "\tVRT_re_init(&%s, ",buf);
	EncToken(tl->fi, re);
	Fi(tl, 0, ");\n");
	Ff(tl, 0, "\tVRT_re_fini(%s);\n", buf);
}


/*--------------------------------------------------------------------*/

static void
Cond_String(struct var *vp, struct tokenlist *tl)
{

	switch (tl->t->tok) {
	case '~':
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		vcc_re(tl, vp->rname, tl->t);
		vcc_NextToken(tl);
		break;
	case T_EQ:
	case T_NEQ:
		Fb(tl, 1, "%sstrcmp(%s, ",
		    tl->t->tok == T_EQ ? "!" : "", vp->rname);
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		EncToken(tl->fb, tl->t);
		Fb(tl, 0, ")\n");
		vcc_NextToken(tl);
		break;
	default:
		Fb(tl, 1, "%s != (void*)0\n", vp->rname);
		break;
	}
}

static void
Cond_Int(struct var *vp, struct tokenlist *tl)
{

	Fb(tl, 1, "%s ", vp->rname);
	switch (tl->t->tok) {
	case T_EQ:
	case T_NEQ:
	case T_LEQ:
	case T_GEQ:
	case '>':
	case '<':
		Fb(tl, 0, "%.*s ", PF(tl->t));
		vcc_NextToken(tl);
		switch(vp->fmt) {
		case TIME:
			vcc_TimeVal(tl);
			break;
		case INT:
			ExpectErr(tl, CNUM);
			Fb(tl, 0, "%.*s ", PF(tl->t));
			vcc_NextToken(tl);
			break;
		case SIZE:
			vcc_SizeVal(tl);
			break;
		default:
			vsb_printf(tl->sb,
			    "No conditions available for variable '%s'\n",
			    vp->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		Fb(tl, 0, "\n");
		break;
	default:
		vsb_printf(tl->sb, "Illegal condition ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, " on integer variable\n");
		vsb_printf(tl->sb,
		    "  only '==', '!=', '<', '>', '<=' and '>=' are legal\n");
		vcc_ErrWhere(tl, tl->t);
		break;
	}
}

static void
Cond_Bool(struct var *vp, struct tokenlist *tl)
{

	Fb(tl, 1, "%s\n", vp->rname);
}

static void
Cond_Backend(struct var *vp, struct tokenlist *tl)
{

	Fb(tl, 1, "%s\n", vp->rname);
}

static void
Cond_2(struct tokenlist *tl)
{
	struct var *vp;

	C(tl, ",");
	if (tl->t->tok == '!') {
		Fb(tl, 1, "!(\n");
		vcc_NextToken(tl);
	} else {
		Fb(tl, 1, "(\n");
	}
	if (tl->t->tok == '(') {
		vcc_NextToken(tl);
		Cond_0(tl);
		ExpectErr(tl, ')');
		vcc_NextToken(tl);
	} else if (tl->t->tok == VAR) {
		vp = FindVar(tl, tl->t, vcc_vars);
		ERRCHK(tl);
		assert(vp != NULL);
		vcc_NextToken(tl);
		switch (vp->fmt) {
		case INT:	L(tl, Cond_Int(vp, tl)); break;
		case SIZE:	L(tl, Cond_Int(vp, tl)); break;
		case BOOL:	L(tl, Cond_Bool(vp, tl)); break;
		case IP:	L(tl, vcc_Cond_Ip(vp, tl)); break;
		case STRING:	L(tl, Cond_String(vp, tl)); break;
		case TIME:	L(tl, Cond_Int(vp, tl)); break;
		case BACKEND:	L(tl, Cond_Backend(vp, tl)); break;
		default:
			vsb_printf(tl->sb,
			    "Variable '%s'"
			    " has no conditions that can be checked\n",
			    vp->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
	} else {
		vsb_printf(tl->sb,
		    "Syntax error in condition, expected '(', '!' or"
		    " variable name, found ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, "\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	Fb(tl, 1, ")\n");
}

static void
Cond_1(struct tokenlist *tl)
{

	Fb(tl, 1, "(\n");
	L(tl, Cond_2(tl));
	while (tl->t->tok == T_CAND) {
		vcc_NextToken(tl);
		Fb(tl, 1, ") && (\n");
		L(tl, Cond_2(tl));
	}
	Fb(tl, 1, ")\n");
}

static void
Cond_0(struct tokenlist *tl)
{

	Fb(tl, 1, "(\n");
	L(tl, Cond_1(tl));
	while (tl->t->tok == T_COR) {
		vcc_NextToken(tl);
		Fb(tl, 1, ") || (\n");
		L(tl, Cond_1(tl));
	}
	Fb(tl, 1, ")\n");
}

static void
Conditional(struct tokenlist *tl)
{

	ExpectErr(tl, '(');
	vcc_NextToken(tl);
	Fb(tl, 1, "(\n");
	L(tl, Cond_0(tl));
	ERRCHK(tl);
	Fb(tl, 1, ")\n");
	ExpectErr(tl, ')');
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------*/

static void
IfStmt(struct tokenlist *tl)
{

	ExpectErr(tl, T_IF);
	Fb(tl, 1, "if \n");
	vcc_NextToken(tl);
	L(tl, Conditional(tl));
	ERRCHK(tl);
	L(tl, Compound(tl));
	ERRCHK(tl);
	while (1) {
		switch (tl->t->tok) {
		case T_ELSE:
			vcc_NextToken(tl);
			if (tl->t->tok != T_IF) {
				Fb(tl, 1, "else \n");
				L(tl, Compound(tl));
				ERRCHK(tl);
				return;
			}
			/* FALLTHROUGH */
		case T_ELSEIF:
		case T_ELSIF:
			Fb(tl, 1, "else if \n");
			vcc_NextToken(tl);
			L(tl, Conditional(tl));
			ERRCHK(tl);
			L(tl, Compound(tl));
			ERRCHK(tl);
			break;
		default:
			C(tl, ";");
			return;
		}
	}
}

/*--------------------------------------------------------------------*/

static void
Compound(struct tokenlist *tl)
{

	ExpectErr(tl, '{');
	Fb(tl, 1, "{\n");
	tl->indent += INDENT;
	C(tl, ";");
	vcc_NextToken(tl);
	while (1) {
		ERRCHK(tl);
		switch (tl->t->tok) {
		case '{':
			Compound(tl);
			break;
		case T_IF:
			IfStmt(tl);
			break;
		case '}':
			vcc_NextToken(tl);
			tl->indent -= INDENT;
			Fb(tl, 1, "}\n");
			return;
		case EOI:
			vsb_printf(tl->sb,
			    "End of input while in compound statement\n");
			tl->err = 1;
			return;
		default:
			vcc_ParseAction(tl);
			ERRCHK(tl);
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
			break;
		}
	}
}

/*--------------------------------------------------------------------*/

static const char *
CheckHostPort(const char *host, const char *port)
{
	struct addrinfo *res, hint;
	int error;

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(host, port, &hint, &res);
	if (error)
		return (gai_strerror(error));
	freeaddrinfo(res);
	return (NULL);
}

static void
Backend(struct tokenlist *tl)
{
	unsigned a;
	struct var *vp;
	struct token *t_be = NULL;
	struct token *t_host = NULL;
	struct token *t_port = NULL;
	const char *ep;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	t_be = tl->t;
	vcc_AddDef(tl, tl->t, R_BACKEND);
	if (tl->nbackend == 0)
		vcc_AddRef(tl, tl->t, R_BACKEND);
	Fh(tl, 1, "#define VGC_backend_%.*s (VCL_conf.backend[%d])\n",
	    PF(tl->t), tl->nbackend);
	Fc(tl, 0, "\n");
	Fc(tl, 0, "static void\n");
	Fc(tl, 1, "VGC_init_backend_%.*s (void)\n", PF(tl->t));
	Fc(tl, 1, "{\n");
	Fc(tl, 1, "\tstruct backend *backend = VGC_backend_%.*s;\n", PF(tl->t));
	Fc(tl, 1, "\n");
	Fc(tl, 1, "\tVRT_set_backend_name(backend, \"%.*s\");\n", PF(tl->t));
	vcc_NextToken(tl);
	ExpectErr(tl, '{');
	vcc_NextToken(tl);
	while (1) {
		if (tl->t->tok == '}')
			break;
		ExpectErr(tl, ID);
		if (!vcc_IdIs(tl->t, "set")) {
			vsb_printf(tl->sb,
			    "Expected 'set', found ");
			vcc_ErrToken(tl, tl->t);
			vsb_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		ExpectErr(tl, VAR);
		vp = FindVar(tl, tl->t, vcc_be_vars);
		ERRCHK(tl);
		assert(vp != NULL);
		vcc_NextToken(tl);
		ExpectErr(tl, '=');
		vcc_NextToken(tl);
		switch (vp->fmt) {
		case HOSTNAME:
			ExpectErr(tl, CSTR);
			t_host = tl->t;
			Fc(tl, 1, "\t%s ", vp->lname);
			EncToken(tl->fc, t_host);
			Fc(tl, 0, ");\n");
			vcc_NextToken(tl);
			break;
		case PORTNAME:
			ExpectErr(tl, CSTR);
			t_port = tl->t;
			Fc(tl, 1, "\t%s ", vp->lname);
			EncToken(tl->fc, t_port);
			Fc(tl, 0, ");\n");
			vcc_NextToken(tl);
			break;
#if 0
		case INT:
		case SIZE:
		case RATE:
		case FLOAT:
#endif
		case TIME:
			Fc(tl, 1, "\t%s ", vp->lname);
			a = tl->t->tok;
			if (a == T_MUL || a == T_DIV)
				Fc(tl, 0, "%g", vcc_DoubleVal(tl));
			else if (vp->fmt == TIME)
				vcc_TimeVal(tl);
			else if (vp->fmt == SIZE)
				vcc_SizeVal(tl);
			else if (vp->fmt == RATE)
				vcc_RateVal(tl);
			else
				Fc(tl, 0, "%g", vcc_DoubleVal(tl));
			Fc(tl, 0, ");\n");
			break;
		default:
			vsb_printf(tl->sb,
			    "Assignments not possible for '%s'\n", vp->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		ExpectErr(tl, ';');
		vcc_NextToken(tl);
	}
	ExpectErr(tl, '}');
	if (t_host == NULL) {
		vsb_printf(tl->sb, "Backend '%.*s' has no hostname\n",
		    PF(t_be));
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	ep = CheckHostPort(t_host->dec, "80");
	if (ep != NULL) {
		vsb_printf(tl->sb, "Backend '%.*s': %s\n", PF(t_be), ep);
		vcc_ErrWhere(tl, t_host);
		return;
	}
	if (t_port != NULL) {
		ep = CheckHostPort(t_host->dec, t_port->dec);
		if (ep != NULL) {
			vsb_printf(tl->sb,
			    "Backend '%.*s': %s\n", PF(t_be), ep);
			vcc_ErrWhere(tl, t_port);
			return;
		}
	}

	vcc_NextToken(tl);
	Fc(tl, 1, "}\n");
	Fc(tl, 0, "\n");
	Fi(tl, 0, "\tVGC_init_backend_%.*s();\n", PF(t_be));
	Ff(tl, 0, "\tVRT_fini_backend(VGC_backend_%.*s);\n", PF(t_be));
	tl->nbackend++;
}

/*--------------------------------------------------------------------*/

static void
Function(struct tokenlist *tl)
{
	int m;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);

	m = IsMethod(tl->t);
	if (m != -1) {
		assert(m < N_METHODS);
		tl->fb = tl->fm[m];
		if (tl->mprocs[m] == NULL) {
			tl->mprocs[m] = vcc_AddProc(tl, tl->t);
			vcc_AddDef(tl, tl->t, R_FUNC);
			vcc_AddRef(tl, tl->t, R_FUNC);
		}
		tl->curproc = tl->mprocs[m];
	} else {
		tl->fb = tl->fc;
		tl->curproc = vcc_AddProc(tl, tl->t);
		vcc_AddDef(tl, tl->t, R_FUNC);
		Fh(tl, 0, "static int VGC_function_%.*s (struct sess *sp);\n",
		    PF(tl->t));
		Fc(tl, 1, "static int\n");
		Fc(tl, 1, "VGC_function_%.*s (struct sess *sp)\n", PF(tl->t));
	}
	vcc_NextToken(tl);
	tl->indent += INDENT;
	Fb(tl, 1, "{\n");
	L(tl, Compound(tl));
	Fb(tl, 1, "}\n");
	tl->indent -= INDENT;
	Fb(tl, 0, "\n");
	tl->fb = NULL;
}

/*--------------------------------------------------------------------
 * Top level of parser, recognize:
 *	Function definitions
 *	Backend definitions
 *	End of input
 */

void
vcc_Parse(struct tokenlist *tl)
{

	while (tl->t->tok != EOI) {
		ERRCHK(tl);
		switch (tl->t->tok) {
		case T_ACL:
			vcc_Acl(tl);
			break;
		case T_SUB:
			Function(tl);
			break;
		case T_BACKEND:
			Backend(tl);
			break;
		case EOI:
			break;
		default:
			vsb_printf(tl->sb,
			    "Expected 'acl', 'sub' or 'backend', found ");
			vcc_ErrToken(tl, tl->t);
			vsb_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
	}
}
