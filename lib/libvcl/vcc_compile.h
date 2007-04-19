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

#include "queue.h"
#include "vcl_returns.h"

#define INDENT		2

struct membit {
	TAILQ_ENTRY(membit)	list;
	void			*ptr;
};

struct source {
	TAILQ_ENTRY(source)	list;
	char			*name;
	const char		*b;
	const char		*e;
	unsigned		idx;
};

struct token {
	unsigned		tok;
	const char		*b;
	const char		*e;
	struct source		*src;
	TAILQ_ENTRY(token)	list;
	unsigned		cnt;
	char			*dec;
};

TAILQ_HEAD(tokenhead, token);

struct tokenlist {
	struct tokenhead	tokens;
	TAILQ_HEAD(, source)	sources;
	TAILQ_HEAD(, membit)	membits;
	unsigned		nsources;
	struct source		*src;
	struct token		*t;
	int			indent;
	unsigned		cnt;
	struct vsb		*fc, *fh, *fi, *ff, *fb;
	struct vsb		*fm[N_METHODS];
	TAILQ_HEAD(, ref)	refs;
	struct vsb		*sb;
	int			err;
	int			nbackend;
	TAILQ_HEAD(, proc)	procs;
	struct proc		*curproc;
	struct proc		*mprocs[N_METHODS];

	unsigned		recnt;
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
	PORTNAME,
	HEADER
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

struct var {
	const char		*name;
	enum var_type		fmt;
	unsigned		len;
	const char		*rname;
	const char		*lname;
};

struct method {
	const char		*name;
	unsigned		returns;
};

struct proccall {
	TAILQ_ENTRY(proccall)	list;
	struct proc		*p;
	struct token		*t;
};

struct proc {
	TAILQ_ENTRY(proc)	list;
	TAILQ_HEAD(,proccall)	calls;
	struct token		*name;
	unsigned		returns;
	unsigned		exists;
	unsigned		called;
	unsigned		active;
	struct token		*returnt[VCL_RET_MAX];
};


/*--------------------------------------------------------------------*/

/* vcc_acl.c */

void vcc_Acl(struct tokenlist *tl);
void vcc_Cond_Ip(struct var *vp, struct tokenlist *tl);

/* vcc_compile.c */
void Fh(struct tokenlist *tl, int indent, const char *fmt, ...);
void Fc(struct tokenlist *tl, int indent, const char *fmt, ...);
void Fb(struct tokenlist *tl, int indent, const char *fmt, ...);
void Fi(struct tokenlist *tl, int indent, const char *fmt, ...);
void Ff(struct tokenlist *tl, int indent, const char *fmt, ...);
unsigned UintVal(struct tokenlist *tl);
void AddDef(struct tokenlist *tl, struct token *t, enum ref_type type);
void AddRef(struct tokenlist *tl, struct token *t, enum ref_type type);
void EncToken(struct vsb *sb, struct token *t);
struct var *FindVar(struct tokenlist *tl, struct token *t, struct var *vl);
void AddCall(struct tokenlist *tl, struct token *t);
struct proc *AddProc(struct tokenlist *tl, struct token *t, int def);
int IsMethod(struct token *t);
void *TlAlloc(struct tokenlist *tl, unsigned len);

/* vcc_obj.c */
extern struct var vcc_be_vars[];
extern struct var vcc_vars[];

/* vcc_parse.c */
void vcc_Parse(struct tokenlist *tl);

/* vcc_token.c */
void vcc_ErrToken(struct tokenlist *tl, struct token *t);
void vcc_ErrWhere(struct tokenlist *tl, struct token *t);
void vcc__Expect(struct tokenlist *tl, unsigned tok, int line);
int vcc_Teq(struct token *t1, struct token *t2);
int vcc_IdIs(struct token *t, const char *p);
void vcc_Lexer(struct tokenlist *tl, struct source *sp);
void vcc_NextToken(struct tokenlist *tl);
void vcc__ErrInternal(struct tokenlist *tl, const char *func, unsigned line);
void vcc_AddToken(struct tokenlist *tl, unsigned tok, const char *b, const char *e);
void vcc_FreeToken(struct token *t);

#define ERRCHK(tl)      do { if ((tl)->err) return; } while (0)
#define ErrInternal(tl) vcc__ErrInternal(tl, __func__, __LINE__)
#define Expect(a, b) vcc__Expect(a, b, __LINE__)
#define ExpectErr(a, b) do { vcc__Expect(a, b, __LINE__); ERRCHK(a);} while (0)

