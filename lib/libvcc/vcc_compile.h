/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#include "miniobj.h"
#include "vas.h"
#include "vcl.h"
#include "vdef.h"
#include "vqueue.h"
#include "vsb.h"


#include "vcc_token_defs.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

struct vsb;
struct token;
struct sockaddr_storage;

#define isident1(c) (isalpha(c))
#define isident(c) (isalpha(c) || isdigit(c) || (c) == '_' || (c) == '-')
#define isvar(c) (isident(c) || (c) == '.')
int vcc_isCid(const struct token *t);
unsigned vcl_fixed_token(const char *p, const char **q);
extern const char * const vcl_tnames[256];
void vcl_output_lang_h(struct vsb *sb);

#define PF(t)	(int)((t)->e - (t)->b), (t)->b

#define INDENT		2

struct acl_e;
struct proc;
struct expr;
struct vcc;
struct symbol;

enum var_type {
#define VCC_TYPE(foo)		foo,
#include "tbl/vcc_types.h"
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

enum symkind {
#define VCC_SYMB(uu, ll)	SYM_##uu,
#include "tbl/symbol_kind.h"
#undef VCC_SYMB
};

typedef void sym_expr_t(struct vcc *tl, struct expr **,
    const struct symbol *sym);
typedef struct symbol *sym_wildcard_t(struct vcc *tl, const struct token *t,
    const struct symbol *sym);

struct symbol {
	unsigned			magic;
#define SYMBOL_MAGIC			0x3368c9fb
	VTAILQ_ENTRY(symbol)		list;

	char				*name;
	unsigned			nlen;
	sym_wildcard_t			*wildcard;
	enum symkind			kind;

	const struct token		*def_b, *def_e;

	enum var_type			fmt;

	sym_expr_t			*eval;
	void				*eval_priv;

	/* xref.c */
	struct proc			*proc;
	unsigned			nref, ndef;

	/* SYM_PROC, SYM_FUNC */
	const char			*cfunc;
	const char			*extra;
	const char			*args;

	/* SYM_VAR */
	const struct var		*var;
	unsigned			r_methods;
};

VTAILQ_HEAD(tokenhead, token);

struct inifin {
	unsigned		magic;
#define INIFIN_MAGIC		0x583c274c
	unsigned		n;
	struct vsb		*ini;
	struct vsb		*fin;
	VTAILQ_ENTRY(inifin)	list;
};

VTAILQ_HEAD(inifinhead, inifin);

struct vcc {
	unsigned		magic;
#define VCC_MAGIC		0x24ad719d

	/* Parameter/Template section */
	char			*builtin_vcl;
	char			*vcl_dir;
	char			*vmod_dir;

	const struct var	*vars;
	VTAILQ_HEAD(, symbol)	symbols;

	struct inifinhead	inifin;
	unsigned		ninifin;

	/* Instance section */
	struct tokenhead	tokens;
	VTAILQ_HEAD(, source)	sources;
	VTAILQ_HEAD(, membit)	membits;
	unsigned		nsources;
	struct source		*src;
	struct token		*t;
	int			indent;
	int			hindent;
	unsigned		cnt;

	struct vsb		*fc;		/* C-code */
	struct vsb		*fh;		/* H-code (before C-code) */
	struct vsb		*fb;		/* Body of current sub
						 * NULL otherwise
						 */
	struct vsb		*fm[VCL_MET_MAX];	/* Method bodies */
	struct vsb		*sb;
	int			err;
	int			ndirector;
	struct proc		*curproc;
	struct proc		*mprocs[VCL_MET_MAX];

	VTAILQ_HEAD(, acl_e)	acl;

	int			nprobe;

	int			defaultdir;
	struct token		*t_defaultdir;

	unsigned		unique;

	unsigned		err_unref;
	unsigned		allow_inline_c;
	unsigned		unsafe_path;
};

struct var {
	const char		*name;
	enum var_type		fmt;
	unsigned		len;
	const char		*rname;
	unsigned		r_methods;
	const char		*lname;
	unsigned		w_methods;
};

struct method {
	const char		*name;
	unsigned		ret_bitmap;
	unsigned		bitval;
};

/*--------------------------------------------------------------------*/

/* vcc_acl.c */

void vcc_ParseAcl(struct vcc *tl);
void vcc_Acl_Hack(struct vcc *tl, char *b);

/* vcc_action.c */
int vcc_ParseAction(struct vcc *tl);

/* vcc_backend.c */
#define MAX_BACKEND_NAME	64
struct fld_spec;

void vcc_ParseProbe(struct vcc *tl);
void vcc_ParseBackend(struct vcc *tl);
struct fld_spec * vcc_FldSpec(struct vcc *tl, const char *first, ...);
void vcc_IsField(struct vcc *tl, struct token **t, struct fld_spec *fs);
void vcc_FieldsOk(struct vcc *tl, const struct fld_spec *fs);

/* vcc_compile.c */
extern struct method method_tab[];
struct inifin *New_IniFin(struct vcc *tl);

/*
 * H -> Header, before the C code
 * C -> C-code
 * B -> Body of function, ends up in C once function is completed
 * I -> Initializer function
 * F -> Finish function
 */
void Fh(const struct vcc *tl, int indent, const char *fmt, ...)
    __printflike(3, 4);
void Fc(const struct vcc *tl, int indent, const char *fmt, ...)
    __printflike(3, 4);
void Fb(const struct vcc *tl, int indent, const char *fmt, ...)
    __printflike(3, 4);
void EncToken(struct vsb *sb, const struct token *t);
int IsMethod(const struct token *t);
void *TlAlloc(struct vcc *tl, unsigned len);
char *TlDup(struct vcc *tl, const char *s);
char *TlDupTok(struct vcc *tl, const struct token *tok);

void EncString(struct vsb *sb, const char *b, const char *e, int mode);

/* vcc_expr.c */
double vcc_DoubleVal(struct vcc *tl);
void vcc_Duration(struct vcc *tl, double *);
unsigned vcc_UintVal(struct vcc *tl);
void vcc_Expr(struct vcc *tl, enum var_type typ);
void vcc_Expr_Call(struct vcc *tl, const struct symbol *sym);
void vcc_Expr_Init(struct vcc *tl);
sym_expr_t vcc_Eval_Var;
sym_expr_t vcc_Eval_SymFunc;
void vcc_Eval_Func(struct vcc *tl, const char *cfunc, const char *extra,
    const char *name, const char *args);
sym_expr_t vcc_Eval_Backend;

/* vcc_obj.c */
extern const struct var vcc_vars[];

/* vcc_parse.c */
void vcc_Parse(struct vcc *tl);

/* vcc_storage.c */
sym_wildcard_t vcc_Stv_Wildcard;

/* vcc_utils.c */
const char *vcc_regexp(struct vcc *tl);
void Resolve_Sockaddr(struct vcc *tl, const char *host, const char *port, \
    const char **ipv4, const char **ipv4_ascii, const char **ipv6, \
    const char **ipv6_ascii, const char **p_ascii, int maxips,
    const struct token *t_err, const char *errid);

/* vcc_symb.c */
struct symbol *VCC_AddSymbolStr(struct vcc *tl, const char *name, enum symkind);
struct symbol *VCC_AddSymbolTok(struct vcc *tl, const struct token *t,
    enum symkind kind);
struct symbol *VCC_GetSymbolTok(struct vcc *tl, const struct token *tok,
    enum symkind);
struct symbol *VCC_FindSymbol(struct vcc *tl,
    const struct token *t, enum symkind kind);
const char * VCC_SymKind(struct vcc *tl, const struct symbol *s);
typedef void symwalk_f(struct vcc *tl, const struct symbol *s);
void VCC_WalkSymbols(struct vcc *tl, symwalk_f *func, enum symkind kind);

/* vcc_token.c */
void vcc_Coord(const struct vcc *tl, struct vsb *vsb,
    const struct token *t);
void vcc_ErrToken(const struct vcc *tl, const struct token *t);
void vcc_ErrWhere(struct vcc *, const struct token *);
void vcc_ErrWhere2(struct vcc *, const struct token *, const struct token *);

void vcc__Expect(struct vcc *tl, unsigned tok, unsigned line);
int vcc_IdIs(const struct token *t, const char *p);
void vcc_ExpectCid(struct vcc *tl);
void vcc_Lexer(struct vcc *tl, struct source *sp);
void vcc_NextToken(struct vcc *tl);
void vcc__ErrInternal(struct vcc *tl, const char *func,
    unsigned line);
void vcc_AddToken(struct vcc *tl, unsigned tok, const char *b,
    const char *e);

/* vcc_var.c */
sym_wildcard_t vcc_Var_Wildcard;
const struct var *vcc_FindVar(struct vcc *tl, const struct token *t,
    int wr_access, const char *use);

/* vcc_vmod.c */
void vcc_ParseImport(struct vcc *tl);

/* vcc_xref.c */
int vcc_AddDef(struct vcc *tl, const struct token *t, enum symkind type);
void vcc_AddRef(struct vcc *tl, const struct token *t, enum symkind type);
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
