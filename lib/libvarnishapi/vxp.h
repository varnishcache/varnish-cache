/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include "vqueue.h"
#include "vre.h"

#include "vxp_tokens.h"

#define isword(c)  (isalpha(c) || isdigit(c) || (c) == '_' || (c) == '-' || \
	    (c) == '+' || (c) == '.' || (c) == '*')

#define PF(t)	(int)((t)->e - (t)->b), (t)->b

/* From vex_fixed_token.c */
unsigned vxp_fixed_token(const char *p, const char **q);
extern const char * const vxp_tnames[256];

struct membit {
	VTAILQ_ENTRY(membit)	list;
	void			*ptr;
};

struct token {
	unsigned		tok;
	const char		*b;
	const char		*e;
	VTAILQ_ENTRY(token)	list;
	unsigned		cnt;
	char			*dec;
};

struct vxp {
	unsigned		magic;
#define VXP_MAGIC		0x59C7F6AC

	const char		*src;
	const char		*b;
	const char		*e;

	VTAILQ_HEAD(, token)	tokens;
	VTAILQ_HEAD(, membit)	membits;
	struct token		*t;

	unsigned		vex_options;
	int			vre_options;

	struct vsb		*sb;
	int			err;
};

struct vex;

struct vex_lhs {
	/* Left-hand-side of a vex expression. Stores the information
	   about which records and what parts of those records the
	   expression should be applied to */
	unsigned		magic;
#define VEX_LHS_MAGIC		0x1AD3D78D
	struct vbitmap		*tags;
	char			*prefix;
	int			prefixlen;
	int			field;
	int			level;
	int			level_pm;
};

enum vex_rhs_e {
	VEX__UNSET,
	VEX_INT,
	VEX_FLOAT,
	VEX_STRING,
	VEX_REGEX,
};

struct vex_rhs {
	/* Right-hand-side of a vex expression. Stores the value that the
	   records from LHS should be matched against */
	unsigned		magic;
#define VEX_RHS_MAGIC		0x3F109965
	enum vex_rhs_e		type;
	long long		val_int;
	double			val_float;
	char			*val_string;
	size_t			val_stringlen;
	vre_t			*val_regex;
};

struct vex {
	unsigned		magic;
#define VEX_MAGIC		0xC7DB792D
	unsigned		tok;
	unsigned		options;
	struct vex		*a, *b;
	struct vex_lhs		*lhs;
	struct vex_rhs		*rhs;
};

/* VXP internals */

#define ERRCHK(tl)	do { if ((tl)->err) return; } while (0)
#define Expect(a, b)	vxp__Expect(a, b)
#define ExpectErr(a, b)	\
    do { vxp__Expect(a, b); ERRCHK(a); } while (0)
#define SkipToken(a, b) \
    do { vxp__Expect(a, b); ERRCHK(a); vxp_NextToken(a); } while (0)

void vxp__Expect(struct vxp *vxp, unsigned tok);
void vxp_ErrWhere(struct vxp *vxp, const struct token *t, int tokoff);
void vxp_NextToken(struct vxp *vxp);
void * vxp_Alloc(struct vxp *vxp, unsigned len);
void vxp_Lexer(struct vxp *vxp);
struct vex * vxp_Parse(struct vxp *vxp);

/* API internal interface */
#define VEX_OPT_CASELESS	(1 << 0)
struct vex * vex_New(const char *query, struct vsb *sb, unsigned options);
void vex_Free(struct vex **pvex);

/* Debug routines */
#ifdef VXP_DEBUG
void vxp_PrintTokens(const struct vxp *vxp);
void vex_PrintTree(const struct vex *vex);
#endif /* VXP_DEBUG */
