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
 *
 * $Id$
 */

#include "vqueue.h"

#include "miniobj.h"
#include "vcl.h"

#define INDENT		2

struct acl_e;

enum var_type {
#define VCC_TYPE(foo)	foo,
#include "vcc_types.h"
#undef VCC_TYPE
};

struct membit {
	VTAILQ_ENTRY(membit)	list;
	void			*ptr;
};

struct source {
	VTAILQ_ENTRY(source)	list;
	char			*name;
	const char		*b;
	const char		*e;
	unsigned		idx;
	char			*freeit;
};

struct token {
	unsigned		tok;
	const char		*b;
	const char		*e;
	struct source		*src;
	VTAILQ_ENTRY(token)	list;
	unsigned		cnt;
	char			*dec;
};

struct symbol {
	unsigned			magic;
#define SYMBOL_MAGIC			0x3368c9fb
	VTAILQ_ENTRY(symbol)		list;
	char				*name;
	unsigned			nlen;
	unsigned			wildcard;
	const struct var		*var;
	enum var_type			fmt;
	unsigned			r_methods;
};

VTAILQ_HEAD(tokenhead, token);

struct vcc {
	unsigned		magic;
#define VCC_MAGIC		0x24ad719d

	/* Parameter/Template section */
	char			*default_vcl;
	char			*vcl_dir;
	char			*vmod_dir;

	const struct var	*vars;
	VTAILQ_HEAD(, symbol)	symbols;

	/* Instance section */
	struct tokenhead	tokens;
	VTAILQ_HEAD(, source)	sources;
	VTAILQ_HEAD(, membit)	membits;
	VTAILQ_HEAD(, host)	hosts;
	unsigned		nsources;
	struct source		*src;
	struct token		*t;
	int			indent;
	int			hindent;
	int			iindent;
	int			findent;
	unsigned		cnt;

	struct vsb		*fc;		/* C-code */
	struct vsb		*fh;		/* H-code (before C-code) */
	struct vsb		*fi;		/* Init func code */
	struct vsb		*ff;		/* Finish func code */
	struct vsb		*fb;		/* Body of current sub
						 * NULL otherwise
						 */
	struct vsb		*fm[VCL_MET_MAX];	/* Method bodies */
	VTAILQ_HEAD(, ref)	refs;
	struct vsb		*sb;
	int			err;
	int			ndirector;
	VTAILQ_HEAD(, proc)	procs;
	struct proc		*curproc;
	struct proc		*mprocs[VCL_MET_MAX];

	VTAILQ_HEAD(, acl_e)	acl;

	int			nprobe;

	int			defaultdir;
	struct token		*t_defaultdir;
	struct token		*t_dir;
	struct token		*t_policy;

	unsigned		recnt;
	unsigned		nsockaddr;
};

enum ref_type {
	R_SUB,
	R_ACL,
	R_BACKEND,
	R_PROBE
};

struct ref {
	enum ref_type		type;
	struct token		*name;
	unsigned		defcnt;
	unsigned		refcnt;
	VTAILQ_ENTRY(ref)	list;
};

struct var {
	const char		*name;
	enum var_type		fmt;
	unsigned		len;
	const char		*rname;
	unsigned		r_methods;
	const char		*lname;
	unsigned		w_methods;
	const char		*hdr;
};

struct method {
	const char		*name;
	unsigned		ret_bitmap;
	unsigned		bitval;
};

/*--------------------------------------------------------------------*/

/* vcc_acl.c */

void vcc_Acl(struct vcc *tl);
void vcc_Cond_Ip(struct vcc *tl, const char *a1);

/* vcc_action.c */
int vcc_ParseAction(struct vcc *tl);

/* vcc_backend.c */
struct fld_spec;
typedef void parsedirector_f(struct vcc *tl);

void vcc_ParseProbe(struct vcc *tl);
void vcc_ParseDirector(struct vcc *tl);
void vcc_ParseBackendHost(struct vcc *tl, int serial, char **nm);
struct fld_spec * vcc_FldSpec(struct vcc *tl, const char *first, ...);
void vcc_ResetFldSpec(struct fld_spec *f);
void vcc_IsField(struct vcc *tl, struct token **t, struct fld_spec *fs);
void vcc_FieldsOk(struct vcc *tl, const struct fld_spec *fs);

void Emit_Sockaddr(struct vcc *tl, const struct token *t_host, const char *port);
void vcc_EmitBeIdent(const struct vcc *tl, struct vsb *v,
	 int serial, const struct token *first, const struct token *last);

/* vcc_compile.c */
extern struct method method_tab[];
/*
 * H -> Header, before the C code
 * C -> C-code
 * B -> Body of function, ends up in C once function is completed
 * I -> Initializer function
 * F -> Finish function
 */
void Fh(const struct vcc *tl, int indent, const char *fmt, ...);
void Fc(const struct vcc *tl, int indent, const char *fmt, ...);
void Fb(const struct vcc *tl, int indent, const char *fmt, ...);
void Fi(const struct vcc *tl, int indent, const char *fmt, ...);
void Ff(const struct vcc *tl, int indent, const char *fmt, ...);
void EncToken(struct vsb *sb, const struct token *t);
int IsMethod(const struct token *t);
void *TlAlloc(struct vcc *tl, unsigned len);

/* vcc_dir_random.c */
parsedirector_f vcc_ParseRandomDirector;

/* vcc_dir_round_robin.c */
parsedirector_f vcc_ParseRoundRobinDirector;

/* vcc_expr.c */
void vcc_RTimeVal(struct vcc *tl, double *);
void vcc_TimeVal(struct vcc *tl, double *);
unsigned vcc_UintVal(struct vcc *tl);
double vcc_DoubleVal(struct vcc *tl);
void vcc_Expr(struct vcc *tl, enum var_type fmt);

/* vcc_dir_dns.c */
parsedirector_f vcc_ParseDnsDirector;

/* vcc_obj.c */
extern const struct var vcc_vars[];

/* vcc_parse.c */
void vcc_Parse(struct vcc *tl);

/* vcc_string.c */
char *vcc_regexp(struct vcc *tl);
int vcc_StringVal(struct vcc *tl);
void vcc_ExpectedStringval(struct vcc *tl);

/* vcc_symbol */
struct symbol *VCC_AddSymbol(struct vcc *tl, const char *name);
const struct symbol *VCC_FindSymbol(const struct vcc *tl,
    const struct token *t);

/* vcc_token.c */
void vcc_Coord(const struct vcc *tl, struct vsb *vsb,
    const struct token *t);
void vcc_ErrToken(const struct vcc *tl, const struct token *t);
void vcc_ErrWhere(struct vcc *tl, const struct token *t);
void vcc__Expect(struct vcc *tl, unsigned tok, int line);
int vcc_Teq(const struct token *t1, const struct token *t2);
int vcc_IdIs(const struct token *t, const char *p);
void vcc_ExpectCid(struct vcc *tl);
void vcc_Lexer(struct vcc *tl, struct source *sp);
void vcc_NextToken(struct vcc *tl);
void vcc__ErrInternal(struct vcc *tl, const char *func,
    unsigned line);
void vcc_AddToken(struct vcc *tl, unsigned tok, const char *b,
    const char *e);

/* vcc_var.c */
const struct var *vcc_FindVar(struct vcc *tl, const struct token *t,
    int wr_access, const char *use);
void vcc_VarVal(struct vcc *tl, const struct var *vp,
    const struct token *vt);

/* vcc_xref.c */
void vcc_AddDef(struct vcc *tl, struct token *t, enum ref_type type);
void vcc_AddRef(struct vcc *tl, struct token *t, enum ref_type type);
int vcc_CheckReferences(struct vcc *tl);

void vcc_AddCall(struct vcc *tl, struct token *t);
struct proc *vcc_AddProc(struct vcc *tl, struct token *t);
void vcc_ProcAction(struct proc *p, unsigned action, struct token *t);
int vcc_CheckAction(struct vcc *tl);
void vcc_AddUses(struct vcc *tl, const struct token *t, unsigned mask,
    const char *use);
int vcc_CheckUses(struct vcc *tl);

#define ERRCHK(tl)      do { if ((tl)->err) return; } while (0)
#define ErrInternal(tl) vcc__ErrInternal(tl, __func__, __LINE__)
#define Expect(a, b) vcc__Expect(a, b, __LINE__)
#define ExpectErr(a, b) \
    do { vcc__Expect(a, b, __LINE__); ERRCHK(a);} while (0)
#define SkipToken(a, b) \
    do { vcc__Expect(a, b, __LINE__); ERRCHK(a); vcc_NextToken(a); } while (0)
