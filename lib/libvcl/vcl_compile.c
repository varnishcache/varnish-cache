/*
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
#include <sbuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <queue.h>
#include <unistd.h>

#include "vcl_priv.h"

#include "libvcl.h"

#define ERRCHK(tl)	do { if ((tl)->err) return; } while (0)

#define INDENT		2

struct token {
	unsigned		tok;
	const char		*b;
	const char		*e;
	TAILQ_ENTRY(token)	list;
	unsigned		cnt;
};

struct tokenlist {
	TAILQ_HEAD(, token)	tokens;
	const char		*b;
	const char		*e;
	struct token		*t;
	int			indent;
	unsigned		cnt;
	struct sbuf		*fc, *fh;
	TAILQ_HEAD(, ref)	refs;
	struct sbuf		*sb;
	int			err;
};

enum var_type {
	BACKEND,
	BOOL,
	INT,
	FLOAT,
	SIZE,
	RATE,
	TIME,
	STRING,
	IP,
	HOSTNAME,
	PORTNAME
};

struct var {
	const char		*name;
	enum var_type		fmt;
	int			len;
	const char		*cname;
};

enum ref_type {
	R_FUNC,
	R_ACL,
	R_BACKEND
};

struct ref {
	enum ref_type		type;
	struct token		*name;
	unsigned		defcnt;
	unsigned		refcnt;
	TAILQ_ENTRY(ref)	list;
};


static struct var be_vars[] = {
	{ "backend.host",		HOSTNAME, 0,  "backend->hostname"    },
	{ "backend.port",		PORTNAME, 0,  "backend->portname"    },
};


static struct var vars[] = {
#if 0
	{ "req.ttlfactor",		FLOAT, 0,   "req->ttlfactor" },
	{ "req.url.host",		STRING, 0,  "req->url.host" },
	{ "req.url.path",		STRING, 0,  "req->url.path" },
	{ "req.useragent",		STRING, 0,  "req->useragent" },
	{ "req.backend",		BACKEND, 0, "req->backend"   },
	{ "client.ip",			IP, 0,	    "client->ip"     },
	{ "backend.response_time",	TIME, 0,    "backend->responsetime" },
	{ "backend.down",		BOOL, 0,    "backend->down"  },
	{ "backend.timeout",		TIME, 0,    "backend->timeout" },
	{ "backend.bandwidth",		RATE, 0,    "backend->bandwidth" },
	{ "obj.exist",			BOOL, 0,    "obj->exists" },
	{ "obj.ttl",			TIME, 0,    "obj->ttl" },
	{ "obj.result",			INT, 0,     "obj->result" },
	{ "obj.size",			SIZE, 0,    "obj->size" },
	{ "obj.usage",			INT, 0,     "obj->usage" },
#endif
	{ NULL,				INT, 0,	    "NULL" }
};

static void Compound(struct tokenlist *tl);
static void Cond_0(struct tokenlist *tl);

/*--------------------------------------------------------------------*/

static void
ErrToken(struct tokenlist *tl, struct token *t)
{

	if (t->tok == EOI)
		sbuf_printf(tl->sb, "end of input");
	else
		sbuf_printf(tl->sb, "'%*.*s'", t->e - t->b, t->e - t->b, t->b);
}

static void
_ErrInternal(struct tokenlist *tl, const char *func, unsigned line)
{

	sbuf_printf(tl->sb, "VCL compiler internal error at %s():%u\n",
	    func, line);
	tl->err = 1;
}

#define ErrInternal(tl) _ErrInternal(tl, __func__, __LINE__)

static void
ErrWhere(struct tokenlist *tl, struct token *t)
{
	unsigned lin, pos, x, y;
	const char *p, *l;
	
	lin = 1;
	pos = 0;
	for (l = p = tl->b; p < t->b; p++) {
		if (*p == '\n') {
			lin++;
			pos = 0;
			l = p + 1;
		} else if (*p == '\t') {
			pos &= ~7;
			pos += 8;
		} else
			pos++;
	}
	sbuf_printf(tl->sb, "Line %d Pos %d\n", lin, pos);
	x = y = 0;
	for (p = l; p < tl->e && *p != '\n'; p++) {
		if (*p == '\t') {
			y &= ~7;
			y += 8;
			while (x < y) {
				sbuf_bcat(tl->sb, " ", 1);
				x++;
			}
		} else {
			x++;
			y++;
			sbuf_bcat(tl->sb, p, 1);
		}
	}
	sbuf_cat(tl->sb, "\n");
	x = y = 0;
	for (p = l; p < tl->e && *p != '\n'; p++) {
		if (p >= t->b && p < t->e) {
			sbuf_bcat(tl->sb, "#", 1);
			x++;
			y++;
			continue;
		}
		if (*p == '\t') {
			y &= ~7;
			y += 8;
		} else
			y++;
		while (x < y) {
			sbuf_bcat(tl->sb, "-", 1);
			x++;
		}
	}
	sbuf_cat(tl->sb, "\n");
	tl->err = 1;
}

/*--------------------------------------------------------------------*/

static void
NextToken(struct tokenlist *tl)
{
	tl->t = TAILQ_NEXT(tl->t, list);
	if (tl->t == NULL) {
		sbuf_printf(tl->sb,
		    "Ran out of input, something is missing or"
		    " maybe unbalanced (...) or {...}\n");
		tl->err = 1;
		return;
	}
}

static void
_Expect(struct tokenlist *tl, unsigned tok, int line)
{
	if (tl->t->tok == tok)
		return;
	sbuf_printf(tl->sb, "Expected %s got ", vcl_tnames[tok]);
	ErrToken(tl, tl->t);
	sbuf_printf(tl->sb, "\n(program line %u), at\n", line);
	ErrWhere(tl, tl->t);
}

#define Expect(a, b) _Expect(a, b, __LINE__)
#define ExpectErr(a, b) do { _Expect(a, b, __LINE__); ERRCHK(a);} while (0)

#define I(tl)	do { 		\
	sbuf_printf(tl->fc, "/* %-11s */ ", __func__); \
	sbuf_printf(tl->fc, "%*.*s", tl->indent, tl->indent, ""); \
} while (0)

#define L(tl, foo)	do {	\
	tl->indent += INDENT;	\
	foo;			\
	tl->indent -= INDENT;	\
} while (0)

#define C(tl, sep)	do {				\
	I(tl);						\
	sbuf_printf(tl->fc, "VCL_count(%u)%s\n", ++tl->cnt, sep);	\
	tl->t->cnt = tl->cnt; 				\
} while (0)
	
/*--------------------------------------------------------------------
 * Compare ID token to string, return true of match
 */

static int
IdIs(struct token *t, const char *p)
{
	const char *q;

	assert(t->tok == ID);
	for (q = t->b; q < t->e && *p != '\0'; p++, q++)
		if (*q != *p)
			return (0);
	if (q != t->e || *p != '\0')
		return (0);
	return (1);
}

/*--------------------------------------------------------------------*/

char *
EncString(struct token *t)
{
	char *p, *q;
	const char *r;
	unsigned u;

	assert(t->tok == CSTR);
	p = malloc(t->e - t->b);
	assert(p != NULL);
	q = p;
	for (r = t->b + 1; r < t->e - 1; ) {
		if (*r != '\\') {
			*q++ = *r++;
			continue;
		}
		switch (r[1]) {
		case 'n':	*q++ = '\n';	r += 2; break;
		case 'r':	*q++ = '\r';	r += 2; break;
		case 'v':	*q++ = '\v';	r += 2; break;
		case 'f':	*q++ = '\f';	r += 2; break;
		case 't':	*q++ = '\t';	r += 2; break;
		case 'b':	*q++ = '\b';	r += 2; break;
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
			u = r[1] - '0';
			r += 2;
			if (isdigit(r[0]) && (r[0] - '0') < 8) {
				u <<= 3;
				u |= r[0] - '0';
				r++;
				if (isdigit(r[0]) && (r[0] - '0') < 8) {
					u <<= 3;
					u |= r[0] - '0';
					r++;
				}
			}
			*q++ = u;
			break;
		default:
			*q++ = r[1];	
			r += 2;
			break;
		}
	}
	*q = '\0';
	return (p);
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
		if (r->name->e - r->name->b != t->e - t->b)
			continue;
		if (memcmp(t->b, r->name->b, t->e - t->b))
			continue;
		return (r);
	}
	r = calloc(sizeof *r, 1);
	assert(r != NULL);
	r->name = t;
	r->type = type;
	TAILQ_INSERT_TAIL(&tl->refs, r, list);
	return (r);
}

static void
AddRef(struct tokenlist *tl, struct token *t, enum ref_type type)
{

	FindRef(tl, t, type)->refcnt++;
}

static void
AddDef(struct tokenlist *tl, struct token *t, enum ref_type type)
{

	FindRef(tl, t, type)->defcnt++;
}

/*--------------------------------------------------------------------
 * Recognize and convert units of time, return seconds.
 */

static double
TimeUnit(struct tokenlist *tl)
{
	double sc = 1.0;

	assert(tl->t->tok == ID);
	if (IdIs(tl->t, "ms"))
		sc = 1e-3;
	else if (IdIs(tl->t, "s"))
		sc = 1.0;
	else if (IdIs(tl->t, "m"))
		sc = 60.0;
	else if (IdIs(tl->t, "h"))
		sc = 60.0 * 60.0;
	else if (IdIs(tl->t, "d"))
		sc = 60.0 * 60.0 * 24.0;
	else {
		sbuf_printf(tl->sb, "Unknown time unit ");
		ErrToken(tl, tl->t);
		sbuf_printf(tl->sb, ".  Legal are 's', 'm', 'h' and 'd'\n");
		ErrWhere(tl, tl->t);
		return (1.0);
	}
	NextToken(tl);
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
	if (IdIs(tl->t, "b"))
		sc = 1.0;
	else if (IdIs(tl->t, "kb"))
		sc = 1024.0;
	else if (IdIs(tl->t, "mb") || IdIs(tl->t, "Mb"))
		sc = 1024.0 * 1024.0;
	else if (IdIs(tl->t, "gb") || IdIs(tl->t, "Gb"))
		sc = 1024.0 * 1024.0 * 1024.0;
	else {
		sbuf_printf(tl->sb, "Unknown size unit ");
		ErrToken(tl, tl->t);
		sbuf_printf(tl->sb, ".  Legal are 'kb', 'mb' and 'gb'\n");
		ErrWhere(tl, tl->t);
		return (1.0);
	}
	NextToken(tl);
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
	NextToken(tl);
	sc /= TimeUnit(tl);
	return (sc);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM } to unsigned value
 */

static unsigned
UintVal(struct tokenlist *tl)
{
	unsigned d = 0;
	const char *p;

	Expect(tl, CNUM);
	for (p = tl->t->b; p < tl->t->e; p++) {
		d *= 10;
		d += *p - '0';
	}
	NextToken(tl);
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
	NextToken(tl);
	if (tl->t->tok != '.') 
		return (d);
	NextToken(tl);
	if (tl->t->tok != CNUM)
		return (d);
	for (p = tl->t->b; p < tl->t->e; p++) {
		d += (*p - '0') * e;
		e *= 0.1;
	}
	NextToken(tl);
	return (d);
}

/*--------------------------------------------------------------------*/

static unsigned
IpVal(struct tokenlist *tl)
{
	unsigned u, v;
	struct token *t;

	t = tl->t;
	u = UintVal(tl);
	if (u < 256) {
		v = u << 24;
		Expect(tl, '.');
		NextToken(tl);
		t = tl->t;
		u = UintVal(tl);
		if (u < 256) {
			v |= u << 16;
			Expect(tl, '.');
			NextToken(tl);
			t = tl->t;
			u = UintVal(tl);
			if (u < 256) {
				v |= u << 8;
				Expect(tl, '.');
				NextToken(tl);
				t = tl->t;
				u = UintVal(tl);
				if (u < 256) {
					v |= u;
					return (v);
				}
			}
		}
	}
	sbuf_printf(tl->sb, "Illegal octet in IP number\n");
	ErrWhere(tl, t);
	return (0);
}

/*--------------------------------------------------------------------*/

static struct var *
FindVar(struct tokenlist *tl, struct token *t, struct var *vl)
{
	struct var *v;

	for (v = vl; v->name != NULL; v++) {
		if (t->e - t->b != v->len)
			continue;
		if (!memcmp(t->b, v->name, v->len))
			return (v);
	}
	sbuf_printf(tl->sb, "Unknown variable ");
	ErrToken(tl, t);
	sbuf_cat(tl->sb, "\nAt: ");
	ErrWhere(tl, t);
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
	sbuf_printf(tl->fc, "(%g * %g)", v, sc);
}

static void
SizeVal(struct tokenlist *tl)
{
	double v, sc;

	v = DoubleVal(tl);
	ExpectErr(tl, ID);
	sc = SizeUnit(tl);
	sbuf_printf(tl->fc, "(%g * %g)", v, sc);
}

static void
RateVal(struct tokenlist *tl)
{
	double v, sc;

	v = DoubleVal(tl);
	ExpectErr(tl, ID);
	sc = RateUnit(tl);
	sbuf_printf(tl->fc, "(%g * %g)", v, sc);
}

/*--------------------------------------------------------------------*/

static void
Cond_Ip(struct var *vp, struct tokenlist *tl)
{
	unsigned u;

	switch (tl->t->tok) {
	case '~':
		NextToken(tl);
		ExpectErr(tl, ID);
		I(tl);
		AddRef(tl, tl->t, R_ACL);
		sbuf_printf(tl->fc, "ip_match(%s, acl_%*.*s)\n",
		    vp->cname,
		    tl->t->e - tl->t->b,
		    tl->t->e - tl->t->b, tl->t->b);
		NextToken(tl);
		break;
	case T_EQ:
	case T_NEQ:
		I(tl);
		sbuf_printf(tl->fc, "%s %*.*s ",
		    vp->cname,
		    tl->t->e - tl->t->b,
		    tl->t->e - tl->t->b, tl->t->b);
		NextToken(tl);
		u = IpVal(tl);
		sbuf_printf(tl->fc, "%uU /* %u.%u.%u.%u */\n", u,
		    (u >> 24) & 0xff, (u >> 16) & 0xff,
		    (u >> 8) & 0xff, (u) & 0xff);
		break;
	default:
		sbuf_printf(tl->sb, "Illegal condition ");
		ErrToken(tl, tl->t);
		sbuf_printf(tl->sb, " on IP number variable\n");
		sbuf_printf(tl->sb, "  only '==', '!=' and '~' are legal\n");
		ErrWhere(tl, tl->t);
		break;
	}
}

static void
Cond_String(struct var *vp, struct tokenlist *tl)
{

	(void)vp;
	switch (tl->t->tok) {
	case '~':
		I(tl); sbuf_printf(tl->fc, "string_match(%s, ", vp->cname);
		NextToken(tl);
		ExpectErr(tl, CSTR);
		sbuf_printf(tl->fc, "%*.*s)\n",
			tl->t->e - tl->t->b,
			tl->t->e - tl->t->b, tl->t->b);
		NextToken(tl);
		break;
	default:
		sbuf_printf(tl->sb, "Illegal condition ");
		ErrToken(tl, tl->t);
		sbuf_printf(tl->sb, " on string variable\n");
		sbuf_printf(tl->sb, "  only '~' is legal\n");
		ErrWhere(tl, tl->t);
		break;
	}
}

static void
Cond_Int(struct var *vp, struct tokenlist *tl)
{

	I(tl);
	sbuf_printf(tl->fc, "%s ", vp->cname);
	switch (tl->t->tok) {
	case T_EQ:
	case T_NEQ:
	case T_LEQ:
	case T_GEQ:
	case '>':
	case '<':
		sbuf_printf(tl->fc, "%*.*s ", 
			tl->t->e - tl->t->b,
			tl->t->e - tl->t->b, tl->t->b);
		NextToken(tl);
		switch(vp->fmt) {
		case TIME:
			TimeVal(tl);
			break;
		case INT:
			ExpectErr(tl, CNUM);
			sbuf_printf(tl->fc, "%*.*s ", 
				tl->t->e - tl->t->b,
				tl->t->e - tl->t->b, tl->t->b);
			NextToken(tl);
			break;
		case SIZE:
			SizeVal(tl);
			break;
		default:
			sbuf_printf(tl->sb,
			    "No conditions available for variable '%s'\n",
			    vp->name);
			ErrWhere(tl, tl->t);
			return;
		}
		sbuf_printf(tl->fc, "\n");
		break;
	default:
		sbuf_printf(tl->sb, "Illegal condition ");
		ErrToken(tl, tl->t);
		sbuf_printf(tl->sb, " on integer variable\n");
		sbuf_printf(tl->sb,
		    "  only '==', '!=', '<', '>', '<=' and '>=' are legal\n");
		ErrWhere(tl, tl->t);
		break;
	}
}

static void
Cond_Bool(struct var *vp, struct tokenlist *tl)
{

	I(tl);
	sbuf_printf(tl->fc, "%s\n", vp->cname);
}

static void
Cond_2(struct tokenlist *tl)
{
	struct var *vp;

	C(tl, ",");
	I(tl);
	if (tl->t->tok == '!') {
		sbuf_printf(tl->fc, "!");
		NextToken(tl);
	}
	sbuf_printf(tl->fc, "(\n");
	if (tl->t->tok == '(') {
		NextToken(tl);
		Cond_0(tl);
		ExpectErr(tl, ')');
		NextToken(tl);
	} else if (tl->t->tok == VAR) {
		vp = FindVar(tl, tl->t, vars);
		ERRCHK(tl);
		assert(vp != NULL);
		NextToken(tl);
		switch (vp->fmt) {
		case INT:	L(tl, Cond_Int(vp, tl)); break;
		case SIZE:	L(tl, Cond_Int(vp, tl)); break;
		case BOOL:	L(tl, Cond_Bool(vp, tl)); break;
		case IP:	L(tl, Cond_Ip(vp, tl)); break;
		case STRING:	L(tl, Cond_String(vp, tl)); break;
		case TIME:	L(tl, Cond_Int(vp, tl)); break;
		/* XXX backend == */
		default:	
			sbuf_printf(tl->sb,
			    "Variable '%s'"
			    " has no conditions that can be checked\n",
			    vp->name);
			ErrWhere(tl, tl->t);
			return;
		}
	} else {
		sbuf_printf(tl->sb,
		    "Syntax error in condition, expected '(', '!' or"
		    " variable name, found ");
		ErrToken(tl, tl->t);
		sbuf_printf(tl->sb, "\n");
		ErrWhere(tl, tl->t);
		return;
	}
	I(tl);
	sbuf_printf(tl->fc, ")\n");
}

static void
Cond_1(struct tokenlist *tl)
{

	I(tl); sbuf_printf(tl->fc, "(\n");
	L(tl, Cond_2(tl));
	while (tl->t->tok == T_CAND) {
		NextToken(tl);
		I(tl); sbuf_printf(tl->fc, ") && (\n");
		L(tl, Cond_2(tl));
	}
	I(tl); sbuf_printf(tl->fc, ")\n");
}

static void
Cond_0(struct tokenlist *tl)
{

	I(tl); sbuf_printf(tl->fc, "(\n");
	L(tl, Cond_1(tl));
	while (tl->t->tok == T_COR) {
		NextToken(tl);
		I(tl); sbuf_printf(tl->fc, ") || (\n");
		L(tl, Cond_1(tl));
	}
	I(tl); sbuf_printf(tl->fc, ")\n");
}

static void
Conditional(struct tokenlist *tl)
{

	ExpectErr(tl, '(');
	NextToken(tl);
	I(tl); sbuf_printf(tl->fc, "(\n");
	L(tl, Cond_0(tl));
	ERRCHK(tl);
	I(tl); sbuf_printf(tl->fc, ")\n");
	ExpectErr(tl, ')');
	NextToken(tl);
}

/*--------------------------------------------------------------------*/

static void
IfStmt(struct tokenlist *tl)
{

	ExpectErr(tl, T_IF);
	I(tl); sbuf_printf(tl->fc, "if \n");
	NextToken(tl);
	L(tl, Conditional(tl));
	ERRCHK(tl);
	L(tl, Compound(tl));
	ERRCHK(tl);
	while (1) {
		switch (tl->t->tok) {
		case T_ELSE:
			NextToken(tl);
			if (tl->t->tok != T_IF) {
				I(tl); sbuf_printf(tl->fc, "else \n");
				L(tl, Compound(tl));
				ERRCHK(tl);
				return;
			}
			/* FALLTHROUGH */
		case T_ELSEIF:
		case T_ELSIF:
			I(tl); sbuf_printf(tl->fc, "else if \n");
			NextToken(tl);
			L(tl, Conditional(tl));
			ERRCHK(tl);
			L(tl, Compound(tl));
			ERRCHK(tl);
			break;
		default:
			return;
		}
	}
}

/*--------------------------------------------------------------------*/

static void
Action(struct tokenlist *tl)
{
	unsigned a, u;
	struct var *vp;
	struct token *at;

	at = tl->t;
	NextToken(tl);
	switch (at->tok) {
	case T_NO_NEW_CACHE:
		I(tl);
		sbuf_printf(tl->fc, "VCL_no_new_cache();\n");
		return;
	case T_NO_CACHE:
		I(tl);
		sbuf_printf(tl->fc, "VCL_no_cache();\n");
		return;
	case T_FINISH:
		I(tl);
		sbuf_printf(tl->fc, "return;\n");
		return;
	case T_PASS:
		I(tl);
		sbuf_printf(tl->fc, "VCL_pass(VCL_PASS_ARGS);\n");
		sbuf_printf(tl->fc, "return;\n");
		return;
	case T_FETCH:
		I(tl);
		sbuf_printf(tl->fc, "VCL_fetch();\n");
		return;
	case T_ERROR:
		a = UintVal(tl);
		I(tl);
		sbuf_printf(tl->fc, "VCL_error(%u, ", a);
		if (tl->t->tok == CSTR) {
			sbuf_printf(tl->fc, "%*.*s);\n",
			    tl->t->e - tl->t->b,
			    tl->t->e - tl->t->b, tl->t->b);
			NextToken(tl);
		} else
			sbuf_printf(tl->fc, "(const char *)0);\n");
		return;
	case T_SWITCH_CONFIG:
		ExpectErr(tl, ID);
		I(tl);
		sbuf_printf(tl->fc, "VCL_switch_config(\"%*.*s\");\n",
		    tl->t->e - tl->t->b,
		    tl->t->e - tl->t->b, tl->t->b);
		NextToken(tl);
		return;
	case T_CALL:
		ExpectErr(tl, ID);
		AddRef(tl, tl->t, R_FUNC);
		I(tl);
		sbuf_printf(tl->fc, "VCL_function_%*.*s(VCL_PASS_ARGS);\n",
		    tl->t->e - tl->t->b,
		    tl->t->e - tl->t->b, tl->t->b);
		I(tl); sbuf_printf(tl->fc, "if (sess->done)\n");
		I(tl); sbuf_printf(tl->fc, "\treturn;\n");
		NextToken(tl);
		return;
	case T_REWRITE:
		ExpectErr(tl, CSTR);
		I(tl);
		sbuf_printf(tl->fc, "VCL_rewrite(%*.*s",
		    tl->t->e - tl->t->b,
		    tl->t->e - tl->t->b, tl->t->b);
		NextToken(tl);
		ExpectErr(tl, CSTR);
		sbuf_printf(tl->fc, ", %*.*s);\n",
		    tl->t->e - tl->t->b,
		    tl->t->e - tl->t->b, tl->t->b);
		NextToken(tl);
		return;
	case T_SET:
		ExpectErr(tl, VAR);
		vp = FindVar(tl, tl->t, vars);
		ERRCHK(tl);
		assert(vp != NULL);
		I(tl);
		sbuf_printf(tl->fc, "%s ", vp->cname);
		NextToken(tl);
		switch (vp->fmt) {
		case INT:
		case SIZE:
		case RATE:
		case TIME:
		case FLOAT:
			sbuf_printf(tl->fc, "%*.*s ",
			    tl->t->e - tl->t->b,
			    tl->t->e - tl->t->b, tl->t->b);
			a = tl->t->tok;
			NextToken(tl);
			if (a == T_MUL || a == T_DIV)
				sbuf_printf(tl->fc, "%g", DoubleVal(tl));
			else if (vp->fmt == TIME)
				TimeVal(tl);
			else if (vp->fmt == SIZE)
				SizeVal(tl);
			else if (vp->fmt == RATE)
				RateVal(tl);
			else 
				sbuf_printf(tl->fc, "%g", DoubleVal(tl));
			sbuf_printf(tl->fc, ";\n");
			break;
		case IP:
			if (tl->t->tok == '=') {
				NextToken(tl);
				u = IpVal(tl);
				sbuf_printf(tl->fc, "= %uU; /* %u.%u.%u.%u */\n",
				    u,
				    (u >> 24) & 0xff,
				    (u >> 16) & 0xff,
				    (u >> 8) & 0xff,
				    u & 0xff);
				break;
			}
			sbuf_printf(tl->sb, "Illegal assignment operator ");
			ErrToken(tl, tl->t);
			sbuf_printf(tl->sb,
			    " only '=' is legal for IP numbers\n");
			ErrWhere(tl, tl->t);
			return;
		case BACKEND:
			if (tl->t->tok == '=') {
				NextToken(tl);
				sbuf_printf(tl->fc, "= &VCL_backend_%*.*s;\n",
				    tl->t->e - tl->t->b,
				    tl->t->e - tl->t->b, tl->t->b);
				NextToken(tl);
				break;
			}
			sbuf_printf(tl->sb, "Illegal assignment operator ");
			ErrToken(tl, tl->t);
			sbuf_printf(tl->sb,
			    " only '=' is legal for backend\n");
			ErrWhere(tl, tl->t);
			return;
		default:
			sbuf_printf(tl->sb,
			    "Assignments not possible for '%s'\n", vp->name);
			ErrWhere(tl, tl->t);
			return;
		}
		return;
	default:
		sbuf_printf(tl->sb, "Expected action, 'if' or '}'\n");
		ErrWhere(tl, at);
		return;
	}
}

/*--------------------------------------------------------------------*/

static void
Acl(struct tokenlist *tl)
{
	unsigned u, m;

	NextToken(tl);

	ExpectErr(tl, ID);
	AddDef(tl, tl->t, R_ACL);
	sbuf_printf(tl->fh, "static struct vcl_acl acl_%*.*s[];\n",
	    tl->t->e - tl->t->b,
	    tl->t->e - tl->t->b, tl->t->b);
	I(tl);
	sbuf_printf(tl->fc, "static struct vcl_acl acl_%*.*s[] = {\n",
	    tl->t->e - tl->t->b,
	    tl->t->e - tl->t->b, tl->t->b);
	NextToken(tl);

	tl->indent += INDENT;

	ExpectErr(tl, '{');
	NextToken(tl);

	while (tl->t->tok == CNUM) {
		u = IpVal(tl);
		if (tl->t->tok == '/') {
			NextToken(tl);
			ExpectErr(tl, CNUM);
			m = UintVal(tl);
		} else
			m = 32;
		ExpectErr(tl, ';');
		NextToken(tl);
		I(tl);
		sbuf_printf(tl->fc, "{ %11uU, %3uU }, /* %u.%u.%u.%u/%u */\n",
		    u, m,
		    (u >> 24) & 0xff, (u >> 16) & 0xff,
		    (u >> 8) & 0xff, (u) & 0xff, m);
	}
	ExpectErr(tl, '}');
	I(tl);
	sbuf_printf(tl->fc, "{ %11uU, %3uU }\n", 0, 0);

	tl->indent -= INDENT;

	I(tl);
	sbuf_printf(tl->fc, "};\n\n");
	NextToken(tl);
}

/*--------------------------------------------------------------------*/

static void
Compound(struct tokenlist *tl)
{

	ExpectErr(tl, '{');
	I(tl); sbuf_printf(tl->fc, "{\n");
	tl->indent += INDENT;
	C(tl, ";");
	NextToken(tl);
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
			NextToken(tl);
			tl->indent -= INDENT;
			I(tl); sbuf_printf(tl->fc, "}\n");
			return;
		case EOI:
			sbuf_printf(tl->sb,
			    "End of input while in compound statement\n");
			tl->err = 1;
			return;
		default:
			Action(tl);
			ERRCHK(tl);
			ExpectErr(tl, ';');
			NextToken(tl);
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
	char *host = NULL;
	char *port = NULL;
	const char *ep;

	NextToken(tl);
	ExpectErr(tl, ID);
	t_be = tl->t;
	AddDef(tl, tl->t, R_BACKEND);
	I(tl);
	sbuf_printf(tl->fh, "static struct backend VCL_backend_%*.*s;\n",
	    tl->t->e - tl->t->b,
	    tl->t->e - tl->t->b, tl->t->b);
	sbuf_printf(tl->fc, "static struct backend VCL_backend_%*.*s;\n",
	    tl->t->e - tl->t->b,
	    tl->t->e - tl->t->b, tl->t->b);
	sbuf_printf(tl->fc, "static void\n");
	I(tl);
	sbuf_printf(tl->fc,
	    "VCL_init_backend_%*.*s (struct backend *backend)\n",
	    tl->t->e - tl->t->b,
	    tl->t->e - tl->t->b, tl->t->b);
	I(tl);
	sbuf_printf(tl->fc, "{\n");
	NextToken(tl);
	ExpectErr(tl, '{');
	NextToken(tl);
	while (1) {
		if (tl->t->tok == '}')
			break;
		ExpectErr(tl, T_SET);
		NextToken(tl);
		ExpectErr(tl, VAR);
		vp = FindVar(tl, tl->t, be_vars);
		ERRCHK(tl);
		assert(vp != NULL);
		NextToken(tl);
		ExpectErr(tl, '=');
		NextToken(tl);
		switch (vp->fmt) {
		case HOSTNAME:
			ExpectErr(tl, CSTR);
			t_host = tl->t;
			host = EncString(tl->t);
			I(tl);
			sbuf_printf(tl->fc, "\t%s = %*.*s;\n",
			    vp->cname,
			    tl->t->e - tl->t->b,
			    tl->t->e - tl->t->b, tl->t->b);
			NextToken(tl);
			break;
		case PORTNAME:
			ExpectErr(tl, CSTR);
			t_port = tl->t;
			port = EncString(tl->t);
			sbuf_printf(tl->fc, "\t%s = %*.*s;\n",
			    vp->cname,
			    tl->t->e - tl->t->b,
			    tl->t->e - tl->t->b, tl->t->b);
			NextToken(tl);
			break;
		default:
			sbuf_printf(tl->sb,
			    "Assignments not possible for '%s'\n", vp->name);
			ErrWhere(tl, tl->t);
			return;
		}
		ExpectErr(tl, ';');
		NextToken(tl);
	}
	ExpectErr(tl, '}');
	if (host == NULL) {
		sbuf_printf(tl->sb, "Backend '%*.*s' has no hostname\n",
		    t_be->e - t_be->b,
		    t_be->e - t_be->b, t_be->b);
		ErrWhere(tl, tl->t);
		return;
	}
	ep = CheckHostPort(host, "80");
	if (ep != NULL) {
		sbuf_printf(tl->sb,
		    "Backend '%*.*s': %s\n",
		    t_be->e - t_be->b,
		    t_be->e - t_be->b, t_be->b, ep);
		ErrWhere(tl, t_host);
		return;
	}
	if (port != NULL) {
		ep = CheckHostPort(host, port);
		if (ep != NULL) {
			sbuf_printf(tl->sb,
			    "Backend '%*.*s': %s\n",
			    t_be->e - t_be->b,
			    t_be->e - t_be->b, t_be->b, ep);
			ErrWhere(tl, t_port);
			return;
		}
	}
	
	NextToken(tl);
	I(tl);
	sbuf_printf(tl->fc, "}\n");
	sbuf_printf(tl->fc, "\n");
}

/*--------------------------------------------------------------------*/

static void
Function(struct tokenlist *tl)
{

	NextToken(tl);
	ExpectErr(tl, ID);
	AddDef(tl, tl->t, R_FUNC);
	sbuf_printf(tl->fh, "static void VCL_function_%*.*s (VCL_FARGS);\n",
	    tl->t->e - tl->t->b,
	    tl->t->e - tl->t->b, tl->t->b);
	I(tl);
	sbuf_printf(tl->fc, "static void\n");
	I(tl);
	sbuf_printf(tl->fc, "VCL_function_%*.*s (VCL_FARGS)\n",
	    tl->t->e - tl->t->b,
	    tl->t->e - tl->t->b, tl->t->b);
	NextToken(tl);
	L(tl, Compound(tl));
	sbuf_printf(tl->fc, "\n");
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
			Acl(tl);
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
			sbuf_printf(tl->sb,
			    "Expected 'acl', 'sub' or 'backend', found ");
			ErrToken(tl, tl->t);
			sbuf_printf(tl->sb, " at\n");
			ErrWhere(tl, tl->t);
			return;
		}
	}
}

/*--------------------------------------------------------------------
 * Add a token to the token list.
 */

static void
AddToken(struct tokenlist *tl, unsigned tok, const char *b, const char *e)
{
	struct token *t;

	t = calloc(sizeof *t, 1);
	assert(t != NULL);
	t->tok = tok;
	t->b = b;
	t->e = e;
	TAILQ_INSERT_TAIL(&tl->tokens, t, list);
	tl->t = t;
	if (0)
		fprintf(stderr, "+ %s\n", vcl_tnames[tok]);
}

/*--------------------------------------------------------------------
 * Lexical analysis and token generation
 */

static void
Lexer(struct tokenlist *tl, const char *b, const char *e)
{
	const char *p, *q;
	unsigned u;

	for (p = b; p < e; ) {

		/* Skip any whitespace */
		if (isspace(*p)) {
			p++;
			continue;
		}

		/* Skip '#.*\n' comments */
		if (*p == '#') {
			while (p < e && *p != '\n')
				p++;
			continue;
		}

		/* Skip C-style comments */
		if (*p == '/' && p[1] == '*') {
			p += 2;
			for (p += 2; p < e; p++) {
				if (*p == '*' && p[1] == '/') {
					p += 2;
					break;
				}
			}
			continue;
		}

		/* Match for the fixed tokens (see token.tcl) */
		u = vcl_fixed_token(p, &q);
		if (u != 0) {
			AddToken(tl, u, p, q);
			p = q;
			continue;
		}

		/* Match strings, with \\ and \" escapes */
		if (*p == '"') {
			for (q = p + 1; q < e; q++) {
				if (*q == '\\' && q[1] == '\\')
					q++;
				else if (*q == '\\' && q[1] == '"')
					q++;
				else if (*q == '"') {
					q++;
					break;
				}
			}
			AddToken(tl, CSTR, p, q);
			p = q;
			continue;
		}

		/* Match Identifiers */
		if (isident1(*p)) {
			for (q = p; q < e; q++)
				if (!isident(*q))
					break;
			if (isvar(*q)) {
				for (; q < e; q++)
					if (!isvar(*q))
						break;
				AddToken(tl, VAR, p, q);
			} else {
				AddToken(tl, ID, p, q);
			}
			p = q;
			continue;
		}

		/* Match numbers { [0-9]+ } */
		if (isdigit(*p)) {
			for (q = p; q < e; q++)
				if (!isdigit(*q))
					break;
			AddToken(tl, CNUM, p, q);
			p = q;
			continue;
		}
		AddToken(tl, EOI, p, p + 1);
		sbuf_printf(tl->sb, "Syntax error at\n");
		ErrWhere(tl, tl->t);
		return;
	}
	/* Add End Of Input token */
	AddToken(tl, EOI, p, p);
}

/*--------------------------------------------------------------------*/

static void
CheckRefs(struct tokenlist *tl)
{
	struct ref *r;
	const char *bug;

	TAILQ_FOREACH(r, &tl->refs, list) {
		if (r->defcnt == 0)
			bug = "Undefined ";
		else if (r->refcnt == 0)
			bug = "Unreferenced ";
		else
			continue;
		switch(r->type) {
		case R_FUNC:
			sbuf_printf(tl->sb, "%s function ", bug);
			break;
		case R_ACL:
			sbuf_printf(tl->sb, "%s acl ", bug);
			break;
		case R_BACKEND:
			sbuf_printf(tl->sb, "%s backend ", bug);
			break;
		default:
			ErrInternal(tl);
			sbuf_printf(tl->sb, "Ref ");
			ErrToken(tl, r->name);
			sbuf_printf(tl->sb, " has unknown type %d\n",
			    r->type);
			return;
		}
		ErrToken(tl, r->name);
		sbuf_cat(tl->sb, ", first mention is\n");
		ErrWhere(tl, r->name);
		return;
	}
}

/*--------------------------------------------------------------------*/

static void
LocTable(struct tokenlist *tl)
{
	struct token *t;
	unsigned lin, pos;
	const char *p;
	
	sbuf_printf(tl->fh,
	    "#define VCL_NREFS %u\n", tl->cnt + 1);
	sbuf_printf(tl->fh,
	    "static struct vcl_ref VCL_ref[VCL_NREFS];\n");
	sbuf_printf(tl->fc,
	    "static struct vcl_ref VCL_ref[VCL_NREFS] = {\n");
	lin = 1;
	pos = 0;
	p = tl->b;
	TAILQ_FOREACH(t, &tl->tokens, list) {
		if (t->cnt == 0)
			continue;
		for (;p < t->b; p++) {
			if (*p == '\n') {
				lin++;
				pos = 0;
			} else if (*p == '\t') {
				pos &= ~7;
				pos += 8;
			} else
				pos++;
		
		}
		sbuf_printf(tl->fc,
		    "%*.*s[%3u] = { %4u, %3u, 0, \"%*.*s\" },\n",
		    INDENT, INDENT, "",
		    t->cnt, lin, pos + 1,
		    t->e - t->b,
		    t->e - t->b, t->b);
	}
	sbuf_printf(tl->fc, "};\n");
}


/*--------------------------------------------------------------------*/

static void
EmitInitFunc(struct tokenlist *tl)
{
	struct ref *r;

	sbuf_printf(tl->fc,
	    "\nstatic void\n"
	    "VCL_Init(void)\n"
	    "{\n\n");
	
	TAILQ_FOREACH(r, &tl->refs, list) {
		switch(r->type) {
		case R_FUNC:
			break;
		case R_ACL:
			break;
		case R_BACKEND:
			sbuf_printf(tl->fc,
			    "\tVCL_init_backend_%*.*s(&VCL_backend_%*.*s);\n",
			    r->name->e - r->name->b,
			    r->name->e - r->name->b, r->name->b,
			    r->name->e - r->name->b,
			    r->name->e - r->name->b, r->name->b);
			break;
		}
	}
	sbuf_printf(tl->fc, "}\n");
}

/*--------------------------------------------------------------------*/

static void
EmitStruct(struct tokenlist *tl)
{

	sbuf_printf(tl->fc, "\nstruct VCL_conf VCL_conf = {\n");
	sbuf_printf(tl->fc,
	    "\t.magic = VCL_CONF_MAGIC,\n");
	sbuf_printf(tl->fc,
	    "\t.init_func = VCL_Init,\n");
	sbuf_printf(tl->fc,
	    "\t.main_func = VCL_function_main,\n");
	sbuf_printf(tl->fc,
	    "\t.default_backend = &VCL_backend_default,\n");
	sbuf_printf(tl->fc,
	    "\t.ref = VCL_ref,\n");
	sbuf_printf(tl->fc,
	    "\t.nref = VCL_NREFS,\n");
	sbuf_printf(tl->fc, "};\n");
}

/*--------------------------------------------------------------------*/

char *
VCL_Compile(struct sbuf *sb, const char *b, const char *e)
{
	struct tokenlist tokens;
	struct ref *r;
	struct token *t;
	FILE *fo;
	char *of = NULL;
	char buf[BUFSIZ];

	memset(&tokens, 0, sizeof tokens);
	TAILQ_INIT(&tokens.tokens);
	TAILQ_INIT(&tokens.refs);
	tokens.sb = sb;

	tokens.fc = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(tokens.fc != NULL);

	tokens.fh = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(tokens.fh != NULL);

	tokens.b = b;
	if (e == NULL)
		e = strchr(b, '\0');
	assert(e != NULL);
	tokens.e = e;
	Lexer(&tokens, b, e);
	if (tokens.err)
		goto done;
	tokens.t = TAILQ_FIRST(&tokens.tokens);
	Parse(&tokens);
	if (tokens.err)
		goto done;
if (0)
	CheckRefs(&tokens);
	if (tokens.err)
		goto done;
	LocTable(&tokens);

	EmitInitFunc(&tokens);

	EmitStruct(&tokens);

	of = strdup("/tmp/vcl.XXXXXXXX");
	assert(of != NULL);
	mktemp(of);

	sprintf(buf, 
	    "tee /tmp/_.c |"
	    "cc -fpic -shared -Wl,-x -o %s -x c - ", of);

	fo = popen(buf, "w");
	assert(fo != NULL);

	vcl_output_lang_h(fo);

	sbuf_finish(tokens.fh);
	fputs(sbuf_data(tokens.fh), fo);
	sbuf_delete(tokens.fh);

	sbuf_finish(tokens.fc);
	fputs(sbuf_data(tokens.fc), fo);
	sbuf_delete(tokens.fc);

	pclose(fo);
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
VCL_CompileFile(struct sbuf *sb, const char *fn)
{
	char *f, *r;
	int fd, i;
	struct stat st;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		sbuf_printf(sb, "Cannot open file '%s': %s",
		    fn, strerror(errno));
		return (NULL);
	}
	assert(0 == fstat(fd, &st));
	f = malloc(st.st_size + 1);
	assert(f != NULL);
	i = read(fd, f, st.st_size); 
	assert(i == st.st_size);
	f[i] = '\0';
	r = VCL_Compile(sb, f, NULL);
	free(f);
	return (r);
}

/*--------------------------------------------------------------------*/

void
VCL_InitCompile(void)
{
	struct var *v;

	vcl_init_tnames();
	for (v = vars; v->name != NULL; v++)
		v->len = strlen(v->name);
	for (v = be_vars; v->name != NULL; v++)
		v->len = strlen(v->name);
}
