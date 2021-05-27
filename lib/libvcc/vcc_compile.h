/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include <stdio.h>
#include <stdint.h>

#include "miniobj.h"
#include "vdef.h"
#include "vas.h"
#include "vrt.h"
#include "vcl.h"
#include "vqueue.h"
#include "vsb.h"

#include "vcc_token_defs.h"

/*---------------------------------------------------------------------
 * VCL version stuff
 */

#define VCL_LOW		40		// Lowest VCC supports
#define VCL_HIGH	41		// Highest VCC supports

// Specific VCL versions
#define	VCL_40		40
#define	VCL_41		41

/*---------------------------------------------------------------------*/

struct acl;
struct acl_e;
struct expr;
struct method;
struct proc;
struct sockaddr_storage;
struct symbol;
struct symtab;
struct token;
struct vcc;
struct vjsn_val;
struct vmod_obj;
struct vsb;

unsigned vcl_fixed_token(const char *p, const char **q);
extern const char * const vcl_tnames[256];
void vcl_output_lang_h(struct vsb *sb);

#define PF(t)	(int)((t)->e - (t)->b), (t)->b

#define INDENT		2

struct source {
	unsigned		magic;
#define SOURCE_MAGIC		0xf756fe82
	VTAILQ_ENTRY(source)	list;
	const char		*kind;
	char			*name;
	const char		*b;
	const char		*e;
	unsigned		idx;
	const struct source	*parent;
	const struct token	*parent_tok;
	VTAILQ_HEAD(, token)	src_tokens;
};

struct token {
	unsigned		tok;
	const char		*b;
	const char		*e;
	const struct source	*src;
	VTAILQ_ENTRY(token)	list;
	VTAILQ_ENTRY(token)	src_list;
	unsigned		cnt;
	char			*dec;
	double			num;
};

/*---------------------------------------------------------------------*/

typedef const struct type	*vcc_type_t;

struct type {
	unsigned		magic;
#define TYPE_MAGIC		0xfae932d9

	const char		*name;
	const struct vcc_method	*methods;

	const char		*global_pfx;
	const char		*tostring;
	vcc_type_t		multype;
	int			stringform;
};

#define VCC_TYPE(UC, lc)	extern const struct type UC[1];
#include "vcc_types.h"

/*---------------------------------------------------------------------*/

typedef const struct kind	*vcc_kind_t;

struct kind {
	unsigned		magic;
#define KIND_MAGIC		0xfad72443

	const char		*name;
};

#define VCC_KIND(U,l)	extern const struct kind SYM_##U[1];
#include "tbl/symbol_kind.h"

/*---------------------------------------------------------------------*/

typedef const struct vcc_namespace *const vcc_ns_t;

#define VCC_NAMESPACE(U, l)	extern vcc_ns_t SYM_##U;
#include "vcc_namespace.h"

enum vcc_namespace_e {
#define VCC_NAMESPACE(U, l)	VCC_NAMESPACE_##U,
#include "vcc_namespace.h"
	VCC_NAMESPACE__MAX
};

/*---------------------------------------------------------------------*/

typedef void sym_expr_t(struct vcc *tl, struct expr **,
    struct token *, struct symbol *sym, vcc_type_t);
typedef void sym_wildcard_t(struct vcc *, struct symbol *, struct symbol *);

typedef void sym_act_f(struct vcc *, struct token *, struct symbol *);

struct symbol {
	unsigned			magic;
#define SYMBOL_MAGIC			0x3368c9fb
	VTAILQ_ENTRY(symbol)		list;
	VTAILQ_ENTRY(symbol)		sideways;

	const char			*name;

	int				lorev;
	int				hirev;

	const struct symtab		*symtab;
	const char			*vmod_name;

	sym_wildcard_t			*wildcard;
	vcc_kind_t			kind;

	sym_act_f			*action;
	unsigned			action_mask;

	const struct token		*def_b, *def_e, *ref_b;

	vcc_type_t			type;

	sym_expr_t			*eval;
	const void			*eval_priv;

	/* xref.c */
	struct proc			*proc;
	unsigned			noref, nref, ndef;

	const char			*extra;

	/* SYM_VAR */
	const char			*rname;
	unsigned			r_methods;
	const char			*lname;
	unsigned			w_methods;
	const char			*uname;
	unsigned			u_methods;
};

VTAILQ_HEAD(tokenhead, token);
VTAILQ_HEAD(procprivhead, procpriv);

struct proc {
	unsigned		magic;
#define PROC_MAGIC		0xd1d98499
	const struct method	*method;
	VTAILQ_HEAD(,proccall)	calls;
	VTAILQ_HEAD(,procuse)	uses;
	struct procprivhead	priv_tasks;
	struct procprivhead	priv_tops;
	VTAILQ_ENTRY(proc)	list;
	struct token		*name;
	unsigned		ret_bitmap;
	unsigned		called;
	unsigned		active;
	unsigned		okmask;
	unsigned		calledfrom;
	struct token		*return_tok[VCL_RET_MAX];
	struct vsb		*cname;
	struct vsb		*prologue;
	struct vsb		*body;
	struct symbol		*sym;
};

struct inifin {
	unsigned		magic;
#define INIFIN_MAGIC		0x583c274c
	unsigned		n;
	unsigned		ignore_errors;
	struct vsb		*ini;
	struct vsb		*fin;
	struct vsb		*final;
	struct vsb		*event;
	VTAILQ_ENTRY(inifin)	list;
};

VTAILQ_HEAD(inifinhead, inifin);

struct syntax {
#define SYNTAX_MAGIC		0x42397890
	const struct source	*src;
	int			version;
	VTAILQ_ENTRY(syntax)	list;
};

VTAILQ_HEAD(syntaxhead, syntax);

struct vcc {
	unsigned		magic;
#define VCC_MAGIC		0x24ad719d
	struct syntaxhead	vcl_syntax;
	int			syntax;
	int			esyntax;	/* effective syntax */

	char			*builtin_vcl;
	struct vfil_path	*vcl_path;
	struct vfil_path	*vmod_path;
#define MGT_VCC(t, n, cc) t n;
#include <tbl/mgt_vcc.h>

	struct symtab		*syms[VCC_NAMESPACE__MAX];

	struct inifinhead	inifin;
	unsigned		ninifin;

	/* Instance section */
	struct tokenhead	tokens;
	VTAILQ_HEAD(, source)	sources;
	unsigned		nsources;
	struct token		*t;
	int			indent;
	int			hindent;
	unsigned		cnt;

	struct vsb		*symtab;	/* VCC info to MGT */
	struct vsb		*fc;		/* C-code */
	struct vsb		*fh;		/* H-code (before C-code) */
	struct vsb		*fb;		/* Body of current sub
						 * NULL otherwise
						 */
	struct vsb		*sb;
	int			err;
	unsigned		nsub;
	unsigned		subref;	// SUB arguments present
	struct proc		*curproc;
	VTAILQ_HEAD(, proc)	procs;

	struct acl		*acl;

	int			nprobe;

	int			ndirector;
	struct symbol		*first_director;
	const char		*default_director;
	const char		*default_probe;

	VTAILQ_HEAD(, symbol)	sym_objects;
	VTAILQ_HEAD(, symbol)	sym_vmods;
	VTAILQ_HEAD(, vmod_obj)	vmod_objects;

	unsigned		unique;
	unsigned		vmod_count;
};

extern struct vcc *vcc_builtin;

struct method {
	const char		*name;
	unsigned		ret_bitmap;
	unsigned		bitval;
};

/*--------------------------------------------------------------------*/

/* vcc_acl.c */

void vcc_ParseAcl(struct vcc *tl);

/* vcc_action.c */
void vcc_Action_Init(struct vcc *);

/* vcc_backend.c */
struct fld_spec;

const char *vcc_default_probe(struct vcc *);
void vcc_Backend_Init(struct vcc *tl);
void vcc_ParseProbe(struct vcc *tl);
void vcc_ParseBackend(struct vcc *tl);
struct fld_spec * vcc_FldSpec(struct vcc *tl, const char *first, ...);
void vcc_IsField(struct vcc *tl, const struct token **t, struct fld_spec *fs);
void vcc_FieldsOk(struct vcc *tl, const struct fld_spec *fs);

/* vcc_compile.c */
struct inifin *New_IniFin(struct vcc *);
struct proc *vcc_NewProc(struct vcc*, struct symbol*);

/*
 * H -> Header, before the C code
 * C -> C-code
 * B -> Body of function, ends up in C once function is completed
 * I -> Initializer function
 * F -> Finish function
 */
void Fh(const struct vcc *tl, int indent, const char *fmt, ...)
    v_printflike_(3, 4);
void Fc(const struct vcc *tl, int indent, const char *fmt, ...)
    v_printflike_(3, 4);
void Fb(const struct vcc *tl, int indent, const char *fmt, ...)
    v_printflike_(3, 4);
void EncToken(struct vsb *sb, const struct token *t);
void *TlAlloc(struct vcc *tl, unsigned len);
char *TlDup(struct vcc *tl, const char *s);

/* vcc_expr.c */
void vcc_Expr(struct vcc *tl, vcc_type_t typ);
sym_act_f vcc_Act_Call;
sym_act_f vcc_Act_Obj;
void vcc_Expr_Init(struct vcc *tl);
sym_expr_t vcc_Eval_Var;
sym_expr_t vcc_Eval_Handle;
sym_expr_t vcc_Eval_SymFunc;
sym_expr_t vcc_Eval_TypeMethod;
void vcc_Eval_Func(struct vcc *, const struct vjsn_val *,
    const char *, struct symbol *);
void VCC_GlobalSymbol(struct symbol *, vcc_type_t fmt);
struct symbol *VCC_HandleSymbol(struct vcc *, vcc_type_t);
void VCC_SymName(struct vsb *, const struct symbol *);

/* vcc_obj.c */
void vcc_Var_Init(struct vcc *);

/* vcc_parse.c */
void vcc_Parse(struct vcc *);
void vcc_Parse_Init(struct vcc *);
sym_act_f vcc_Act_If;

/* vcc_source.c */
struct source * vcc_new_source(const char *src, const char *kind,
    const char *name);
struct source *vcc_file_source(struct vcc *tl, const char *fn);
void vcc_lex_source(struct vcc *tl, struct source *sp, int eoi);
void vcc_IncludePush(struct vcc *, struct token *);
void vcc_IncludePop(struct vcc *, struct token *);

/* vcc_storage.c */
void vcc_stevedore(struct vcc *vcc, const char *stv_name);

/* vcc_symb.c */
void VCC_PrintCName(struct vsb *vsb, const char *b, const char *e);
struct symbol *VCC_MkSym(struct vcc *tl, const char *b, vcc_ns_t, vcc_kind_t,
    int, int);

struct symxref { const char *name; };
extern const struct symxref XREF_NONE[1];
extern const struct symxref XREF_DEF[1];
extern const struct symxref XREF_REF[1];

struct symmode {
	const char	*name;
	unsigned	noerr;
	unsigned	partial;
};
extern const struct symmode SYMTAB_NOERR[1];
extern const struct symmode SYMTAB_CREATE[1];
extern const struct symmode SYMTAB_EXISTING[1];
extern const struct symmode SYMTAB_PARTIAL[1];
extern const struct symmode SYMTAB_PARTIAL_NOERR[1];

struct symbol *VCC_SymbolGet(struct vcc *, vcc_ns_t, vcc_kind_t,
    const struct symmode *, const struct symxref *);

struct symbol *VCC_TypeSymbol(struct vcc *, vcc_kind_t, vcc_type_t);

typedef void symwalk_f(struct vcc *tl, const struct symbol *s);
void VCC_WalkSymbols(struct vcc *tl, symwalk_f *func, vcc_ns_t, vcc_kind_t);

/* vcc_token.c */
void vcc_Coord(const struct vcc *tl, struct vsb *vsb,
    const struct token *t);
void vcc_ErrToken(const struct vcc *tl, const struct token *t);
void vcc_ErrWhere(struct vcc *, const struct token *);
void vcc_ErrWhere2(struct vcc *, const struct token *, const struct token *);
void vcc_Warn(struct vcc *);

void vcc__Expect(struct vcc *tl, unsigned tok, unsigned line);
int vcc_IdIs(const struct token *t, const char *p);
void vcc_PrintTokens(struct vcc *tl, const struct token *tb,
    const struct token *te);
void vcc_ExpectVid(struct vcc *tl, const char *what);
void vcc_Lexer(struct vcc *tl, struct source *sp);
void vcc_NextToken(struct vcc *tl);
struct token * vcc_NextTokenFrom(struct vcc *tl, const struct token *t);
struct token * vcc_PeekToken(struct vcc *tl);
struct token * vcc_PeekTokenFrom(struct vcc *tl, const struct token *t);
void vcc__ErrInternal(struct vcc *tl, const char *func,
    unsigned line);

/* vcc_types.c */
vcc_type_t VCC_Type(const char *p);
const char * VCC_Type_EvalMethod(struct vcc *, const struct symbol *);
void vcc_Type_Init(struct vcc *tl);

/* vcc_utils.c */
void vcc_regexp(struct vcc *tl, struct vsb *vgc_name);
void Resolve_Sockaddr(struct vcc *tl, const char *host, const char *defport,
    const char **ipv4, const char **ipv4_ascii, const char **ipv6,
    const char **ipv6_ascii, const char **p_ascii, int maxips,
    const struct token *t_err, const char *errid);
double vcc_DurationUnit(struct vcc *);
void vcc_ByteVal(struct vcc *, VCL_INT *);
void vcc_Duration(struct vcc *tl, double *);
uint64_t vcc_UintVal(struct vcc *tl);
int vcc_IsFlag(struct vcc *tl);
int vcc_IsFlagRaw(struct vcc *, const struct token *, const struct token *);
char *vcc_Dup_be(const char *b, const char *e);
int vcc_Has_vcl_prefix(const char *b);

/* vcc_var.c */
sym_wildcard_t vcc_Var_Wildcard;

/* vcc_vmod.c */
void vcc_ParseImport(struct vcc *tl);
sym_act_f vcc_Act_New;

/* vcc_xref.c */
int vcc_CheckReferences(struct vcc *tl);
void VCC_InstanceInfo(struct vcc *tl);
void VCC_XrefTable(struct vcc *);

void vcc_AddCall(struct vcc *, struct token *, struct symbol *);
void vcc_ProcAction(struct proc *, unsigned, unsigned, struct token *);
int vcc_CheckAction(struct vcc *tl);


struct xrefuse { const char *name, *err; };
extern const struct xrefuse XREF_READ[1];
extern const struct xrefuse XREF_WRITE[1];
extern const struct xrefuse XREF_UNSET[1];
extern const struct xrefuse XREF_ACTION[1];

void vcc_AddUses(struct vcc *, const struct token *, const struct token *,
    const struct symbol *sym, const struct xrefuse *use);
int vcc_CheckUses(struct vcc *tl);
const char *vcc_MarkPriv(struct vcc *, struct procprivhead *,
    const char *);

#define ERRCHK(tl)      do { if ((tl)->err) return; } while (0)
#define ErrInternal(tl) vcc__ErrInternal(tl, __func__, __LINE__)
#define Expect(a, b) vcc__Expect(a, b, __LINE__)
#define ExpectErr(a, b) \
    do { vcc__Expect(a, b, __LINE__); ERRCHK(a);} while (0)
#define SkipToken(a, b) \
    do { vcc__Expect(a, b, __LINE__); ERRCHK(a); vcc_NextToken(a); } while (0)
