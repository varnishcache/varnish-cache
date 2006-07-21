/*
 * $Id$
 */

#include "queue.h"
#include "vcl_returns.h"

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
	int			nbackend;
	TAILQ_HEAD(, proc)	procs;
	struct proc		*curproc;
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
	int			len;
	const char		*rname;
	const char		*lname;
};

static struct method {
	const char		*name;
	const char		*defname;
	unsigned		returns;
} method_tab[] = {
#define VCL_RET_MAC(a,b,c)
#define VCL_MET_MAC(a,b,c)	{ "vcl_"#a, "default_vcl_"#a, c },
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC
	{ NULL, 0U }
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

/* vcc_compile.c */
extern const char *vcc_default_vcl_b, *vcc_default_vcl_e;

/* vcc_obj.c */
extern struct var vcc_be_vars[];
extern struct var vcc_vars[];

/* vcc_token.c */
void vcc_ErrToken(struct tokenlist *tl, struct token *t);
void vcc_ErrWhere(struct tokenlist *tl, struct token *t);
void vcc__Expect(struct tokenlist *tl, unsigned tok, int line);
int vcc_Teq(struct token *t1, struct token *t2);
int vcc_IdIs(struct token *t, const char *p);
void vcc_Lexer(struct tokenlist *tl, const char *b, const char *e);
void vcc_NextToken(struct tokenlist *tl);
void vcc__ErrInternal(struct tokenlist *tl, const char *func, unsigned line);
void vcc_AddToken(struct tokenlist *tl, unsigned tok, const char *b, const char *e);
