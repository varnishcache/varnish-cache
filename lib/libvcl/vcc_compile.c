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
 *
 * $Id$
 */

/*
 * XXX:
 *	generate interface structure
 *
 * XXX:
 *	Better error messages, throughout.
 *	>It also accured to me that we could link the errors to the error 
 *	>documentation.
 *	>
 *	>Unreferenced  function 'request_policy', first mention is
 *	>         Line 8 Pos 4
 *	>         sub request_policy {
 *	>         ----##############--
 *	>Read more about this type of error:
 *	>http://varnish/doc/error.html#Unreferenced%20function
 *	>
 *	>
 *	>         Unknown variable 'obj.bandwidth'
 *	>         At: Line 88 Pos 12
 *	>                 if (obj.bandwidth < 1 kb/h) {
 *	>         ------------#############------------
 *	>Read more about this type of error:
 *	>http://varnish/doc/error.html#Unknown%20variable
 * 
 * XXX:
 *	Create proper tmp filenames for .h, .c and .o
 *
 * XXX:
 *	and all the rest...
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

static struct method method_tab[] = {
#define VCL_RET_MAC(a,b,c,d)
#define VCL_MET_MAC(a,b,c)	{ "vcl_"#a, "default_vcl_"#a, c },
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC
	{ NULL, 0U }
};

/*--------------------------------------------------------------------*/

static void Compound(struct tokenlist *tl);
static void Cond_0(struct tokenlist *tl);
static struct proc *AddProc(struct tokenlist *tl, struct token *t, int def);
static void AddCall(struct tokenlist *tl, struct token *t);
const char *vcc_default_vcl_b, *vcc_default_vcl_e;

/*--------------------------------------------------------------------*/

#define L(tl, foo)	do {	\
	tl->indent += INDENT;	\
	foo;			\
	tl->indent -= INDENT;	\
} while (0)

#define C(tl, sep)	do {				\
	Fc(tl, 1, "VRT_count(sp, %u)%s\n", ++tl->cnt, sep);	\
	tl->t->cnt = tl->cnt; 				\
} while (0)
	
/*--------------------------------------------------------------------
 * Printf output to the two vsbs, possibly indented
 */

void
Fh(struct tokenlist *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fh, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fh, fmt, ap);
	va_end(ap);
}

void
Fc(struct tokenlist *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fc, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fc, fmt, ap);
	va_end(ap);
}

void
Fi(struct tokenlist *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->fi, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->fi, fmt, ap);
	va_end(ap);
}

void
Ff(struct tokenlist *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		vsb_printf(tl->ff, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	vsb_vprintf(tl->ff, fmt, ap);
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
EncString(struct vsb *sb, struct token *t)
{
	const char *p;

	assert(t->tok == CSTR);
	vsb_cat(sb, "\"");
	for (p = t->dec; *p != '\0'; p++) {
		if (*p == '\\' || *p == '"')
			vsb_printf(sb, "\\%c", *p);
		else if (isgraph(*p))
			vsb_printf(sb, "%c", *p);
		else
			vsb_printf(sb, "\\%03o", *p);
	}
	vsb_cat(sb, "\"");
}

/*--------------------------------------------------------------------*/

static int
IsMethod(struct token *t)
{
	struct method *m;

	for(m = method_tab; m->name != NULL; m++) {
		if (vcc_IdIs(t, m->defname))
			return (2);
		if (vcc_IdIs(t, m->name))
			return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Keep track of definitions and references
 */

static struct ref *
FindRef(struct tokenlist *tl, struct token *t, enum ref_type type)
{
	struct ref *r;

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->type != type)
			continue;
		if (vcc_Teq(r->name, t))
			return (r);
	}
	r = calloc(sizeof *r, 1);
	assert(r != NULL);
	r->name = t;
	r->type = type;
	TAILQ_INSERT_TAIL(&tl->refs, r, list);
	return (r);
}

static int
FindRefStr(struct tokenlist *tl, const char *s, enum ref_type type)
{
	struct ref *r;

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->type != type)
			continue;
		if (vcc_IdIs(r->name, s))
			return (1);
	}
	return (0);
}

void
AddRef(struct tokenlist *tl, struct token *t, enum ref_type type)
{

	FindRef(tl, t, type)->refcnt++;
}

static void
AddRefStr(struct tokenlist *tl, const char *s, enum ref_type type)
{
	struct token *t;

	t = calloc(sizeof *t, 1);
	assert(t != NULL);
	t->b = s;
	t->e = strchr(s, '\0');
	t->tok = METHOD;
	AddRef(tl, t, type);
}

void
AddDef(struct tokenlist *tl, struct token *t, enum ref_type type)
{
	struct ref *r;

	r = FindRef(tl, t, type);
	r->defcnt++;
	r->name = t;
}

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
UintVal(struct tokenlist *tl)
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

static double
DoubleVal(struct tokenlist *tl)
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

static struct var *
HeaderVar(struct tokenlist *tl, struct token *t, struct var *vh)
{
	char *p;
	struct var *v;
	int i, w;

	(void)tl;

	v = calloc(sizeof *v, 1);
	assert(v != NULL);
	i = t->e - t->b;
	p = malloc(i + 1);
	assert(p != NULL);
	memcpy(p, t->b, i);
	p[i] = '\0';
	v->name = p;
	v->fmt = STRING;
	if (!memcmp(vh->name, "req.", 4))
		w = 1;
	else
		w = 2;
	asprintf(&p, "VRT_GetHdr(sp, %d, \"\\%03o%s:\")", w,
	    (unsigned)(strlen(v->name + vh->len) + 1), v->name + vh->len);
	assert(p != NULL);
	v->rname = p;
	return (v);
}

/*--------------------------------------------------------------------*/

static struct var *
FindVar(struct tokenlist *tl, struct token *t, struct var *vl)
{
	struct var *v;

	for (v = vl; v->name != NULL; v++) {
		if (v->fmt == HEADER  && t->e - t->b <= v->len)
			continue;
		if (v->fmt != HEADER  && t->e - t->b != v->len)
			continue;
		if (memcmp(t->b, v->name, v->len))
			continue;
		if (v->fmt != HEADER)
			return (v);
		return (HeaderVar(tl, t, v));
	}
	vsb_printf(tl->sb, "Unknown variable ");
	vcc_ErrToken(tl, t);
	vsb_cat(tl->sb, "\nAt: ");
	vcc_ErrWhere(tl, t);
	return (NULL);
}


/*--------------------------------------------------------------------*/

static void
TimeVal(struct tokenlist *tl)
{
	double v, sc;

	v = DoubleVal(tl);
	ExpectErr(tl, ID);
	sc = TimeUnit(tl);
	Fc(tl, 0, "(%g * %g)", v, sc);
}

static void
SizeVal(struct tokenlist *tl)
{
	double v, sc;

	v = DoubleVal(tl);
	ExpectErr(tl, ID);
	sc = SizeUnit(tl);
	Fc(tl, 0, "(%g * %g)", v, sc);
}

static void
RateVal(struct tokenlist *tl)
{
	double v, sc;

	v = DoubleVal(tl);
	ExpectErr(tl, ID);
	sc = RateUnit(tl);
	Fc(tl, 0, "(%g * %g)", v, sc);
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

	Fc(tl, 1, "VRT_re_match(%s, %s)\n", str, buf);
	Fh(tl, 0, "void *%s;\n", buf);
	Fi(tl, 0, "\tVRT_re_init(&%s, ",buf);
	EncString(tl->fi, re);
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
		Fc(tl, 1, "%sstrcmp(%s, ",
		    tl->t->tok == T_EQ ? "!" : "", vp->rname);
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		EncString(tl->fc, tl->t);
		Fc(tl, 0, ")\n");
		vcc_NextToken(tl);
		break;
	default:
		Fc(tl, 1, "%s != (void*)0", vp->rname);
		break;
	}
}

static void
Cond_Int(struct var *vp, struct tokenlist *tl)
{

	Fc(tl, 1, "%s ", vp->rname);
	switch (tl->t->tok) {
	case T_EQ:
	case T_NEQ:
	case T_LEQ:
	case T_GEQ:
	case '>':
	case '<':
		Fc(tl, 0, "%.*s ", PF(tl->t));
		vcc_NextToken(tl);
		switch(vp->fmt) {
		case TIME:
			TimeVal(tl);
			break;
		case INT:
			ExpectErr(tl, CNUM);
			Fc(tl, 0, "%.*s ", PF(tl->t));
			vcc_NextToken(tl);
			break;
		case SIZE:
			SizeVal(tl);
			break;
		default:
			vsb_printf(tl->sb,
			    "No conditions available for variable '%s'\n",
			    vp->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		Fc(tl, 0, "\n");
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

	Fc(tl, 1, "%s\n", vp->rname);
}

static void
Cond_Backend(struct var *vp, struct tokenlist *tl)
{

	Fc(tl, 1, "%s\n", vp->rname);
}

static void
Cond_2(struct tokenlist *tl)
{
	struct var *vp;

	C(tl, ",");
	if (tl->t->tok == '!') {
		Fc(tl, 1, "!(\n");
		vcc_NextToken(tl);
	} else {
		Fc(tl, 1, "(\n");
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
	Fc(tl, 1, ")\n");
}

static void
Cond_1(struct tokenlist *tl)
{

	Fc(tl, 1, "(\n");
	L(tl, Cond_2(tl));
	while (tl->t->tok == T_CAND) {
		vcc_NextToken(tl);
		Fc(tl, 1, ") && (\n");
		L(tl, Cond_2(tl));
	}
	Fc(tl, 1, ")\n");
}

static void
Cond_0(struct tokenlist *tl)
{

	Fc(tl, 1, "(\n");
	L(tl, Cond_1(tl));
	while (tl->t->tok == T_COR) {
		vcc_NextToken(tl);
		Fc(tl, 1, ") || (\n");
		L(tl, Cond_1(tl));
	}
	Fc(tl, 1, ")\n");
}

static void
Conditional(struct tokenlist *tl)
{

	ExpectErr(tl, '(');
	vcc_NextToken(tl);
	Fc(tl, 1, "(\n");
	L(tl, Cond_0(tl));
	ERRCHK(tl);
	Fc(tl, 1, ")\n");
	ExpectErr(tl, ')');
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------*/

static void
IfStmt(struct tokenlist *tl)
{

	ExpectErr(tl, T_IF);
	Fc(tl, 1, "if \n");
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
				Fc(tl, 1, "else \n");
				L(tl, Compound(tl));
				ERRCHK(tl);
				return;
			}
			/* FALLTHROUGH */
		case T_ELSEIF:
		case T_ELSIF:
			Fc(tl, 1, "else if \n");
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
Action(struct tokenlist *tl)
{
	unsigned a;
	struct var *vp;
	struct token *at;

	at = tl->t;
	vcc_NextToken(tl);
	switch (at->tok) {
	case T_NO_NEW_CACHE:
		Fc(tl, 1, "VCL_no_new_cache(sp);\n");
		return;
	case T_NO_CACHE:
		Fc(tl, 1, "VCL_no_cache(sp);\n");
		return;
#define VCL_RET_MAC(a,b,c,d) case T_##b: \
		Fc(tl, 1, "VRT_done(sp, VCL_RET_%s);\n", #b); \
		tl->curproc->returns |= VCL_RET_##b; \
		tl->curproc->returnt[d] = at; \
		return;
#include "vcl_returns.h"
#undef VCL_RET_MAC
	case T_ERROR:
		if (tl->t->tok == CNUM)
			a = UintVal(tl);
		else
			a = 0;
		Fc(tl, 1, "VRT_error(sp, %u", a);
		if (tl->t->tok == CSTR) {
			Fc(tl, 0, ", %.*s", PF(tl->t));
			vcc_NextToken(tl);
		} else {
			Fc(tl, 0, ", (const char *)0");
		}
		Fc(tl, 0, ");\n");
		Fc(tl, 1, "VRT_done(sp, VCL_RET_ERROR);\n");
		return;
	case T_SWITCH_CONFIG:
		ExpectErr(tl, ID);
		Fc(tl, 1, "VCL_switch_config(\"%.*s\");\n", PF(tl->t));
		vcc_NextToken(tl);
		return;
	case T_CALL:
		ExpectErr(tl, ID);
		AddCall(tl, tl->t);
		AddRef(tl, tl->t, R_FUNC);
		Fc(tl, 1, "if (VGC_function_%.*s(sp))\n", PF(tl->t));
		Fc(tl, 1, "\treturn (1);\n");
		vcc_NextToken(tl);
		return;
	case T_REWRITE:
		ExpectErr(tl, CSTR);
		Fc(tl, 1, "VCL_rewrite(%.*s", PF(tl->t));
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		Fc(tl, 0, ", %.*s);\n", PF(tl->t));
		vcc_NextToken(tl);
		return;
	case T_SET:
		ExpectErr(tl, VAR);
		vp = FindVar(tl, tl->t, vcc_vars);
		ERRCHK(tl);
		assert(vp != NULL);
		Fc(tl, 1, "%s", vp->lname);
		vcc_NextToken(tl);
		switch (vp->fmt) {
		case INT:
		case SIZE:
		case RATE:
		case TIME:
		case FLOAT:
			if (tl->t->tok != '=') 
				Fc(tl, 0, "%s %c ", vp->rname, *tl->t->b);
			a = tl->t->tok;
			vcc_NextToken(tl);
			if (a == T_MUL || a == T_DIV)
				Fc(tl, 0, "%g", DoubleVal(tl));
			else if (vp->fmt == TIME)
				TimeVal(tl);
			else if (vp->fmt == SIZE)
				SizeVal(tl);
			else if (vp->fmt == RATE)
				RateVal(tl);
			else 
				Fc(tl, 0, "%g", DoubleVal(tl));
			Fc(tl, 0, ");\n");
			break;
#if 0	/* XXX: enable if we find a legit use */
		case IP:
			if (tl->t->tok == '=') {
				vcc_NextToken(tl);
				u = vcc_IpVal(tl);
				Fc(tl, 0, "= %uU; /* %u.%u.%u.%u */\n",
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
				AddRef(tl, tl->t, R_BACKEND);
				Fc(tl, 0, "VGC_backend_%.*s", PF(tl->t));
				vcc_NextToken(tl);
				Fc(tl, 0, ");\n");
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

/*--------------------------------------------------------------------*/

static void
Compound(struct tokenlist *tl)
{

	ExpectErr(tl, '{');
	Fc(tl, 1, "{\n");
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
			Fc(tl, 1, "}\n");
			return;
		case EOI:
			vsb_printf(tl->sb,
			    "End of input while in compound statement\n");
			tl->err = 1;
			return;
		default:
			Action(tl);
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
	struct var *vp;
	struct token *t_be = NULL;
	struct token *t_host = NULL;
	struct token *t_port = NULL;
	const char *ep;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	t_be = tl->t;
	AddDef(tl, tl->t, R_BACKEND);
	if (tl->nbackend == 0)
		AddRef(tl, tl->t, R_BACKEND);
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
		ExpectErr(tl, T_SET);
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
			EncString(tl->fc, t_host);
			Fc(tl, 0, ");\n");
			vcc_NextToken(tl);
			break;
		case PORTNAME:
			ExpectErr(tl, CSTR);
			t_port = tl->t;
			Fc(tl, 1, "\t%s ", vp->lname);
			EncString(tl->fc, t_port);
			Fc(tl, 0, ");\n");
			vcc_NextToken(tl);
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
	struct token *tn;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	tl->curproc = AddProc(tl, tl->t, 1);
	tl->curproc->exists++;
	tn = tl->t;
	AddDef(tl, tl->t, R_FUNC);
	Fh(tl, 0, "static int VGC_function_%.*s (struct sess *sp);\n",
	    PF(tl->t));
	Fc(tl, 1, "static int\n");
	Fc(tl, 1, "VGC_function_%.*s (struct sess *sp)\n", PF(tl->t));
	vcc_NextToken(tl);
	tl->indent += INDENT;
	Fc(tl, 1, "{\n");
	L(tl, Compound(tl));
	if (IsMethod(tn) == 1) {
		Fc(tl, 1, "VGC_function_default_%.*s(sp);\n", PF(tn));
	}
	Fc(tl, 1, "}\n");
	tl->indent -= INDENT;
	Fc(tl, 0, "\n");
}

/*--------------------------------------------------------------------
 * Top level of parser, recognize:
 *	Function definitions
 *	Backend definitions
 *	End of input
 */

static void
Parse(struct tokenlist *tl)
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

/*--------------------------------------------------------------------
 * Consistency check
 */

static struct proc *
AddProc(struct tokenlist *tl, struct token *t, int def)
{
	struct proc *p;

	TAILQ_FOREACH(p, &tl->procs, list) {
		if (!vcc_Teq(p->name, t)) 
			continue;
		if (def)
			p->name = t;
		return (p);
	}
	p = calloc(sizeof *p, 1);
	assert(p != NULL);
	p->name = t;
	TAILQ_INIT(&p->calls);
	TAILQ_INSERT_TAIL(&tl->procs, p, list);
	return (p);
}

static void
AddCall(struct tokenlist *tl, struct token *t)
{
	struct proccall *pc;
	struct proc *p;

	p = AddProc(tl, t, 0);
	TAILQ_FOREACH(pc, &tl->curproc->calls, list) {
		if (pc->p == p)
			return;
	}
	pc = calloc(sizeof *pc, 1);
	assert(pc != NULL);
	pc->p = p;
	pc->t = t;
	TAILQ_INSERT_TAIL(&tl->curproc->calls, pc, list);
}

static int
Consist_Decend(struct tokenlist *tl, struct proc *p, unsigned returns)
{
	unsigned u;
	struct proccall *pc;

	if (!p->exists) {
		vsb_printf(tl->sb, "Function %.*s does not exist\n",
		    PF(p->name));
		return (1);
	}
	if (p->active) {
		vsb_printf(tl->sb, "Function recurses on\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	u = p->returns & ~returns;
	if (u) {
#define VCL_RET_MAC(a, b, c, d) \
		if (u & VCL_RET_##b) { \
			vsb_printf(tl->sb, "Illegal return for method\n"); \
			vcc_ErrWhere(tl, p->returnt[d]); \
		} 
#include "vcl_returns.h"
#undef VCL_RET_MAC
		vsb_printf(tl->sb, "In function\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	p->active = 1;
	TAILQ_FOREACH(pc, &p->calls, list) {
		if (Consist_Decend(tl, pc->p, returns)) {
			vsb_printf(tl->sb, "\nCalled from\n");
			vcc_ErrWhere(tl, p->name);
			vsb_printf(tl->sb, "at\n");
			vcc_ErrWhere(tl, pc->t);
			return (1);
		}
	}
	p->active = 0;
	p->called++;
	return (0);
}

static int
Consistency(struct tokenlist *tl)
{
	struct proc *p;
	struct method *m;

	TAILQ_FOREACH(p, &tl->procs, list) {
		for(m = method_tab; m->name != NULL; m++) {
			if (vcc_IdIs(p->name, m->defname))
				p->called = 1;
			if (vcc_IdIs(p->name, m->name))
				break;
		}
		if (m->name == NULL) 
			continue;
		if (Consist_Decend(tl, p, m->returns)) {
			vsb_printf(tl->sb,
			    "\nwhich is a %s method\n", m->name);
			return (1);
		}
	}
	TAILQ_FOREACH(p, &tl->procs, list) {
		if (p->called)
			continue;
		vsb_printf(tl->sb, "Function unused\n");
		vcc_ErrWhere(tl, p->name);
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

static int
CheckRefs(struct tokenlist *tl)
{
	struct ref *r;
	const char *type;
	int nerr = 0;

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->defcnt != 0 && r->refcnt != 0)
			continue;
		nerr++;

		switch(r->type) {
		case R_FUNC:
			type = "function";
			break;
		case R_ACL:
			type = "acl";
			break;
		case R_BACKEND:
			type = "backend";
			break;
		default:
			ErrInternal(tl);
			vsb_printf(tl->sb, "Ref ");
			vcc_ErrToken(tl, r->name);
			vsb_printf(tl->sb, " has unknown type %d\n",
			    r->type);
			continue;
		}
		if (r->defcnt == 0 && r->name->tok == METHOD) {
			vsb_printf(tl->sb,
			    "No definition for method %.*s\n", PF(r->name));
			continue;
		}

		if (r->defcnt == 0) {
			vsb_printf(tl->sb,
			    "Undefined %s %.*s, first reference:\n",
			    type, PF(r->name));
			vcc_ErrWhere(tl, r->name);
			continue;
		} 

		vsb_printf(tl->sb, "Unused %s %.*s, defined:\n",
		    type, PF(r->name));
		vcc_ErrWhere(tl, r->name);
	}
	return (nerr);
}

/*--------------------------------------------------------------------*/

static void
LocTable(struct tokenlist *tl)
{
	struct token *t;
	unsigned fil, lin, pos;
	const char *p;
	
	Fh(tl, 0, "#define VGC_NREFS %u\n", tl->cnt + 1);
	Fh(tl, 0, "static struct vrt_ref VGC_ref[VGC_NREFS];\n");
	Fc(tl, 0, "static struct vrt_ref VGC_ref[VGC_NREFS] = {\n");
	fil = 0;
	lin = 1;
	pos = 0;
	p = vcc_default_vcl_b;
	TAILQ_FOREACH(t, &tl->tokens, list) {
		if (t->cnt == 0)
			continue;
		for (;p < t->b; p++) {
			if (p == vcc_default_vcl_e) {
				p = tl->b;
				fil = 1;
				lin = 1;
				pos = 0;
			}
			if (*p == '\n') {
				lin++;
				pos = 0;
			} else if (*p == '\t') {
				pos &= ~7;
				pos += 8;
			} else
				pos++;
		
		}
		Fc(tl, 0, "  [%3u] = { %d, %4u, %3u, 0, \"%.*s\" },\n",
		    t->cnt, fil, lin, pos + 1, PF(t));
	}
	Fc(tl, 0, "};\n");
}


/*--------------------------------------------------------------------*/

static void
EmitInitFunc(struct tokenlist *tl)
{

	Fc(tl, 0, "\nstatic void\nVGC_Init(void)\n{\n\n");
	vsb_finish(tl->fi);
	vsb_cat(tl->fc, vsb_data(tl->fi));
	Fc(tl, 0, "}\n");
}

static void
EmitFiniFunc(struct tokenlist *tl)
{

	Fc(tl, 0, "\nstatic void\nVGC_Fini(void)\n{\n\n");
	vsb_finish(tl->ff);
	vsb_cat(tl->fc, vsb_data(tl->ff));
	Fc(tl, 0, "}\n");
}

/*--------------------------------------------------------------------*/

static void
EmitStruct(struct tokenlist *tl)
{

	Fc(tl, 0, "\nstruct VCL_conf VCL_conf = {\n");
	Fc(tl, 0, "\t.magic = VCL_CONF_MAGIC,\n");
	Fc(tl, 0, "\t.init_func = VGC_Init,\n");
	Fc(tl, 0, "\t.fini_func = VGC_Fini,\n");
	Fc(tl, 0, "\t.nbackend = %d,\n", tl->nbackend);
	Fc(tl, 0, "\t.ref = VGC_ref,\n");
	Fc(tl, 0, "\t.nref = VGC_NREFS,\n");
#define VCL_RET_MAC(l,u,b,n)
#define VCL_MET_MAC(l,u,b) \
	if (FindRefStr(tl, "vcl_" #l, R_FUNC)) { \
		Fc(tl, 0, "\t." #l "_func = VGC_function_vcl_" #l ",\n"); \
		AddRefStr(tl, "vcl_" #l, R_FUNC); \
	} else { \
		Fc(tl, 0, "\t." #l "_func = VGC_function_default_vcl_" #l ",\n"); \
	} \
	AddRefStr(tl, "default_vcl_" #l, R_FUNC);
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC
	Fc(tl, 0, "};\n");
}

/*--------------------------------------------------------------------*/

char *
VCC_Compile(struct vsb *sb, const char *b, const char *e)
{
	struct tokenlist tokens;
	struct ref *r;
	struct token *t;
	FILE *fo;
	char *of = NULL;
	char buf[BUFSIZ];
	int i;

	memset(&tokens, 0, sizeof tokens);
	TAILQ_INIT(&tokens.tokens);
	TAILQ_INIT(&tokens.refs);
	TAILQ_INIT(&tokens.procs);
	tokens.sb = sb;

	tokens.fc = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(tokens.fc != NULL);

	tokens.fh = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(tokens.fh != NULL);

	tokens.fi = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(tokens.fi != NULL);

	tokens.ff = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
	assert(tokens.ff != NULL);

	Fh(&tokens, 0, "extern struct VCL_conf VCL_conf;\n");

	Fi(&tokens, 0, "\tVRT_alloc_backends(&VCL_conf);\n");

	tokens.b = b;
	if (e == NULL)
		e = strchr(b, '\0');
	assert(e != NULL);
	tokens.e = e;
	vcc_Lexer(&tokens, vcc_default_vcl_b, vcc_default_vcl_e);
	vcc_Lexer(&tokens, b, e);
	vcc_AddToken(&tokens, EOI, e, e);
	if (tokens.err)
		goto done;
	tokens.t = TAILQ_FIRST(&tokens.tokens);
	Parse(&tokens);
	if (tokens.err)
		goto done;
	Consistency(&tokens);
	if (tokens.err)
		goto done;
	LocTable(&tokens);

	Ff(&tokens, 0, "\tVRT_free_backends(&VCL_conf);\n");

	EmitInitFunc(&tokens);

	EmitFiniFunc(&tokens);

	EmitStruct(&tokens);

	if (CheckRefs(&tokens))
		goto done;

	of = strdup("/tmp/vcl.XXXXXXXX");
	assert(of != NULL);
	mktemp(of);

	sprintf(buf, 
	    "tee /tmp/_.c |"
	    "cc -fpic -shared -Wl,-x -o %s -x c - ", of);

	fo = popen(buf, "w");
	assert(fo != NULL);

	vcl_output_lang_h(fo);
	fputs(vrt_obj_h, fo);

	vsb_finish(tokens.fh);
	fputs(vsb_data(tokens.fh), fo);
	vsb_delete(tokens.fh);

	vsb_finish(tokens.fc);
	fputs(vsb_data(tokens.fc), fo);
	vsb_delete(tokens.fc);

	i = pclose(fo);
	fprintf(stderr, "pclose=%d\n", i);
	if (i) {
		vsb_printf(sb, "Internal error: GCC returned 0x%04x\n", i);
		unlink(of);
		free(of);
		return (NULL);
	}
done:

	/* Free References */
	while (!TAILQ_EMPTY(&tokens.refs)) {
		r = TAILQ_FIRST(&tokens.refs);
		TAILQ_REMOVE(&tokens.refs, r, list);
		free(r);
	}

	/* Free Tokens */
	while (!TAILQ_EMPTY(&tokens.tokens)) {
		t = TAILQ_FIRST(&tokens.tokens);
		TAILQ_REMOVE(&tokens.tokens, t, list);
		free(t);
	}
	return (of);
}

/*--------------------------------------------------------------------*/

char *
VCC_CompileFile(struct vsb *sb, const char *fn)
{
	char *f, *r;
	int fd, i;
	struct stat st;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		vsb_printf(sb, "Cannot open file '%s': %s",
		    fn, strerror(errno));
		return (NULL);
	}
	assert(0 == fstat(fd, &st));
	f = malloc(st.st_size + 1);
	assert(f != NULL);
	i = read(fd, f, st.st_size); 
	assert(i == st.st_size);
	f[i] = '\0';
	r = VCC_Compile(sb, f, NULL);
	free(f);
	return (r);
}

/*--------------------------------------------------------------------*/

void
VCC_InitCompile(const char *default_vcl)
{
	struct var *v;

	vcc_default_vcl_b = default_vcl;
	vcc_default_vcl_e = strchr(default_vcl, '\0');
	assert(vcc_default_vcl_e != NULL);
	
	vcl_init_tnames();
	for (v = vcc_vars; v->name != NULL; v++)
		v->len = strlen(v->name);
	for (v = vcc_be_vars; v->name != NULL; v++)
		v->len = strlen(v->name);
}
