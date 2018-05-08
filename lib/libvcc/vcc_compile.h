/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

struct vsb;
struct token;
struct sockaddr_storage;
struct method;

unsigned vcl_fixed_token(const char *p, const char **q);
extern const char * const vcl_tnames[256];
void vcl_output_lang_h(struct vsb *sb);

#define PF(t)	(int)((t)->e - (t)->b), (t)->b

#define INDENT		2

struct acl_e;
struct proc;
struct expr;
struct vcc;
struct vjsn_val;
struct symbol;

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

/*---------------------------------------------------------------------*/
typedef const struct type	*vcc_type_t;

struct type {
	unsigned		magic;
#define TYPE_MAGIC		0xfae932d9

	const char		*name;
	const char		*tostring;
	vcc_type_t		multype;
};

#define VCC_TYPE(foo)		extern const struct type foo[1];
#include "tbl/vcc_types.h"

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

typedef void sym_expr_t(struct vcc *tl, struct expr **,
    struct token *, struct symbol *sym, vcc_type_t);
typedef void sym_wildcard_t(struct vcc *, struct symbol *, struct symbol *);

typedef void sym_act_f(struct vcc *, struct token *, struct symbol *);

struct symbol {
	unsigned			magic;
#define SYMBOL_MAGIC			0x3368c9fb
	VTAILQ_ENTRY(symbol)		list;
	VTAILQ_HEAD(,symbol)		children;

	char				*name;
	unsigned			nlen;

	unsigned			lorev;
	unsigned			hirev;

	struct symbol			*parent;
	const char			*vmod;

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

struct proc {
	unsigned		magic;
#define PROC_MAGIC		0xd1d98499
	const struct method	*method;
	VTAILQ_HEAD(,proccall)	calls;
	VTAILQ_HEAD(,procuse)	uses;
	VTAILQ_ENTRY(proc)	list;
	struct token		*name;
	unsigned		ret_bitmap;
	unsigned		called;
	unsigned		active;
	struct token		*return_tok[VCL_RET_MAX];
	struct vsb		*cname;
	struct vsb		*prologue;
	struct vsb		*body;
};

struct inifin {
	unsigned		magic;
#define INIFIN_MAGIC		0x583c274c
	unsigned		n;
	struct vsb		*ini;
	struct vsb		*fin;
	struct vsb		*event;
	VTAILQ_ENTRY(inifin)	list;
};

VTAILQ_HEAD(inifinhead, inifin);

struct vcc {
	unsigned		magic;
#define VCC_MAGIC		0x24ad719d
	int			syntax;

	char			*builtin_vcl;
	struct vfil_path	*vcl_path;
	struct vfil_path	*vmod_path;
	unsigned		err_unref;
	unsigned		allow_inline_c;
	unsigned		unsafe_path;

	struct symbol		*symbols;

	struct inifinhead	inifin;
	unsigned		ninifin;

	/* Instance section */
	struct tokenhead	tokens;
	VTAILQ_HEAD(, source)	sources;
	unsigned		nsources;
	struct source		*src;
	struct token		*t;
	int			indent;
	int			hindent;
	unsigned		cnt;

	struct vsb		*fi;		/* VCC info to MGT */
	struct vsb		*fc;		/* C-code */
	struct vsb		*fh;		/* H-code (before C-code) */
	struct vsb		*fb;		/* Body of current sub
						 * NULL otherwise
						 */
	struct vsb		*sb;
	int			err;
	struct proc		*curproc;
	VTAILQ_HEAD(, proc)	procs;

	VTAILQ_HEAD(, acl_e)	acl;

	int			nprobe;

	int			ndirector;
	struct symbol		*first_director;
	const char		*default_director;
	const char		*default_probe;

	unsigned		unique;

};

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
void vcc_IsField(struct vcc *tl, struct token **t, struct fld_spec *fs);
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
void vcc_Expr_Init(struct vcc *tl);
sym_expr_t vcc_Eval_Var;
sym_expr_t vcc_Eval_Handle;
sym_expr_t vcc_Eval_SymFunc;
void vcc_Eval_Func(struct vcc *, const struct vjsn_val *,
    const char *, const struct symbol *);
void VCC_GlobalSymbol(struct symbol *, vcc_type_t fmt, const char *pfx);
struct symbol *VCC_HandleSymbol(struct vcc *, vcc_type_t , const char *);

/* vcc_obj.c */
void vcc_Var_Init(struct vcc *);

/* vcc_parse.c */
void vcc_Parse(struct vcc *);
void vcc_Parse_Init(struct vcc *);
sym_act_f vcc_Act_If;

/* vcc_utils.c */
void vcc_regexp(struct vcc *tl, struct vsb *vgc_name);
void Resolve_Sockaddr(struct vcc *tl, const char *host, const char *defport,
    const char **ipv4, const char **ipv4_ascii, const char **ipv6,
    const char **ipv6_ascii, const char **p_ascii, int maxips,
    const struct token *t_err, const char *errid);
void Emit_UDS_Path(struct vcc *tl, const struct token *t_path,
    const char *errid);
double vcc_TimeUnit(struct vcc *);
void vcc_ByteVal(struct vcc *, double *);
void vcc_NumVal(struct vcc *, double *, int *);
void vcc_Duration(struct vcc *tl, double *);
unsigned vcc_UintVal(struct vcc *tl);

/* vcc_storage.c */
void vcc_stevedore(struct vcc *vcc, const char *stv_name);

/* vcc_symb.c */
void VCC_PrintCName(struct vsb *vsb, const char *b, const char *e);
struct symbol *VCC_MkSym(struct vcc *tl, const char *b, vcc_kind_t,
    unsigned, unsigned);
extern const char XREF_NONE[];
extern const char XREF_DEF[];
extern const char XREF_REF[];
extern const char SYMTAB_NOERR[];
extern const char SYMTAB_CREATE[];
struct symbol *VCC_SymbolGet(struct vcc *, vcc_kind_t, const char *,
    const char *);
typedef void symwalk_f(struct vcc *tl, const struct symbol *s);
void VCC_WalkSymbols(struct vcc *tl, symwalk_f *func, vcc_kind_t);

/* vcc_token.c */
void vcc_Coord(const struct vcc *tl, struct vsb *vsb,
    const struct token *t);
void vcc_ErrToken(const struct vcc *tl, const struct token *t);
void vcc_ErrWhere(struct vcc *, const struct token *);
void vcc_ErrWhere2(struct vcc *, const struct token *, const struct token *);

void vcc__Expect(struct vcc *tl, unsigned tok, unsigned line);
int vcc_IdIs(const struct token *t, const char *p);
void vcc_ExpectVid(struct vcc *tl, const char *what);
void vcc_Lexer(struct vcc *tl, struct source *sp);
void vcc_NextToken(struct vcc *tl);
void vcc__ErrInternal(struct vcc *tl, const char *func,
    unsigned line);
void vcc_AddToken(struct vcc *tl, unsigned tok, const char *b,
    const char *e);

/* vcc_types.c */
vcc_type_t VCC_Type(const char *p);

/* vcc_var.c */
sym_wildcard_t vcc_Var_Wildcard;

/* vcc_vmod.c */
void vcc_ParseImport(struct vcc *tl);
sym_act_f vcc_Act_New;

/* vcc_xref.c */
int vcc_CheckReferences(struct vcc *tl);
void VCC_XrefTable(struct vcc *);

void vcc_AddCall(struct vcc *, struct symbol *);
void vcc_ProcAction(struct proc *p, unsigned action, struct token *t);
int vcc_CheckAction(struct vcc *tl);
void vcc_AddUses(struct vcc *, const struct token *, const struct token *,
     unsigned mask, const char *use);
int vcc_CheckUses(struct vcc *tl);

#define ERRCHK(tl)      do { if ((tl)->err) return; } while (0)
#define ErrInternal(tl) vcc__ErrInternal(tl, __func__, __LINE__)
#define Expect(a, b) vcc__Expect(a, b, __LINE__)
#define ExpectErr(a, b) \
    do { vcc__Expect(a, b, __LINE__); ERRCHK(a);} while (0)
#define SkipToken(a, b) \
    do { vcc__Expect(a, b, __LINE__); ERRCHK(a); vcc_NextToken(a); } while (0)

#define ACL_SYMBOL_PREFIX "vrt_acl_named"
