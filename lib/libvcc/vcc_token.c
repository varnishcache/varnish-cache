/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "vct.h"

/*--------------------------------------------------------------------*/

void
vcc_ErrToken(const struct vcc *tl, const struct token *t)
{

	if (t->tok == EOI)
		vcc_Complain(tl, "end of input");
	else if (t->tok == CSRC)
		vcc_Complain(tl, "C{ ... }C");
	else
		vcc_Complainf(tl, "'%.*s'", PF(t));
}

void
vcc__ErrInternal(struct vcc *tl, const char *func, unsigned line)
{

	vcc_Complainf(tl, "VCL compiler internal error at %s():%u\n",
	    func, line);
	tl->err = 1;
}

/*--------------------------------------------------------------------
 * Find start of source-line of token
 */

static void
vcc_iline(const struct token *t, const char **ll, int tail)
{
	const char *p, *b, *x;

	b = t->src->b;
	if (ll != NULL)
		*ll = b;
	x = tail ? t->e - 1 : t->b;
	for (p = b; p < x; p++) {
		if (*p == '\n') {
			if (ll != NULL)
				*ll = p + 1;
		}
	}
}

/*--------------------------------------------------------------------
 * Find and print src+line+pos of this token
 */

static void
vcc_coord(struct vsb *vsb, const struct token *t, int tail)
{
	unsigned lin, pos;
	const char *p, *b, *x;

	lin = 1;
	pos = 0;
	b = t->src->b;
	x = tail ? t->e - 1 : t->b;
	for (p = b; p < x; p++) {
		if (*p == '\n') {
			lin++;
			pos = 0;
		} else if (*p == '\t') {
			pos &= ~7;
			pos += 8;
		} else
			pos++;
	}
	VSB_cat(vsb, "(");
	if (tail < 2)
		VSB_printf(vsb, "'%s' Line %u ", t->src->name, lin);
	VSB_printf(vsb, "Pos %u)", pos + 1);
}

static void
vcc_icoord(const struct vcc *tl, const struct token *t, int tail)
{

	if (tl->snap != NULL)
		return;
	vcc_coord(tl->err_sb, t, tail);
}

/*--------------------------------------------------------------------*/

void
vcc_Coord(const struct vcc *tl, struct vsb *vsb)
{

	if (tl->snap != NULL)
		return;
	vcc_coord(vsb, tl->t, 0);
}

/*--------------------------------------------------------------------
 * Output one line of source code, starting at 'l' and ending at the
 * first NL or 'le'.
 */

static void
vcc_quoteline(const struct vcc *tl, const char *l, const char *le)
{
	const char *p;
	unsigned x, y;

	x = y = 0;
	for (p = l; p < le && *p != '\n'; p++) {
		if (*p == '\t') {
			y &= ~7;
			y += 8;
			while (x < y) {
				vcc_Complain(tl, " ");
				x++;
			}
		} else {
			x++;
			y++;
			vcc_Complainf(tl, "%c", *p);
		}
	}
	vcc_Complain(tl, "\n");
}

/*--------------------------------------------------------------------
 * Output a marker line for a sourceline starting at 'l' and ending at
 * the first NL or 'le'.  Characters between 'b' and 'e' are marked.
 */

static void
vcc_markline(const struct vcc *tl, const char *l, const char *le,
    const char *b, const char *e)
{
	const char *p;
	unsigned x, y;
	char c;

	x = y = 0;
	for (p = l; p < le && *p != '\n'; p++) {
		if (p >= b && p < e)
			c = '#';
		else
			c = '-';

		if (*p == '\t') {
			y &= ~7;
			y += 8;
		} else
			y++;
		while (x < y) {
			vcc_Complainf(tl, "%c", c);
			x++;
		}
	}
	vcc_Complain(tl, "\n");
}

/*--------------------------------------------------------------------*/

void
vcc_Complain(const struct vcc *tl, const char *s)
{

	AN(tl);
	AN(s);

	if (tl->snap != NULL)
		return;
	(void)VSB_cat(tl->err_sb, s);
}

void
vcc_Complainf(const struct vcc *tl, const char *fmt, ...)
{
	va_list ap;

	AN(tl);
	AN(fmt);

	if (tl->snap != NULL)
		return;
	va_start(ap, fmt);
	(void)VSB_vprintf(tl->err_sb, fmt, ap);
	va_end(ap);
}

void
vcc_Warn(struct vcc *tl)
{

	AN(tl);
	AN(tl->err);
	vcc_Complain(tl, "(That was just a warning)\n");
	tl->err = 0;
}

/*--------------------------------------------------------------------*/
/* XXX: should take first+last token */

void
vcc_ErrWhere2(struct vcc *tl, const struct token *t, const struct token *t2)
{
	const char  *l1, *l2, *l3;

	if (t == NULL) {
		vcc_ErrWhere(tl, t2);
		return;
	}
	vcc_iline(t, &l1, 0);
	t2 = VTAILQ_PREV(t2, tokenhead, list);
	vcc_iline(t2, &l2, 1);


	if (l1 == l2) {
		vcc_icoord(tl, t, 0);
		vcc_Complain(tl, " -- ");
		vcc_icoord(tl, t2, 2);
		vcc_Complain(tl, "\n");
		/* Two tokens on same line */
		vcc_quoteline(tl, l1, t->src->e);
		vcc_markline(tl, l1, t->src->e, t->b, t2->e);
	} else {
		/* Two tokens different lines */
		l3 = strchr(l1, '\n');
		AN(l3);
		/* XXX: t had better be before t2 */
		vcc_icoord(tl, t, 0);
		if (l3 + 1 == l2) {
			vcc_Complain(tl, " -- ");
			vcc_icoord(tl, t2, 1);
		}
		vcc_Complain(tl, "\n");
		vcc_quoteline(tl, l1, t->src->e);
		vcc_markline(tl, l1, t->src->e, t->b, t2->e);
		if (l3 + 1 != l2) {
			vcc_Complain(tl, "[...]\n");
			vcc_icoord(tl, t2, 1);
			vcc_Complain(tl, "\n");
		}
		vcc_quoteline(tl, l2, t->src->e);
		vcc_markline(tl, l2, t->src->e, t->b, t2->e);
	}
	vcc_Complain(tl, "\n");
	tl->err = 1;
}

void
vcc_ErrWhere(struct vcc *tl, const struct token *t)
{
	const char  *l1;

	vcc_iline(t, &l1, 0);
	vcc_icoord(tl, t, 0);
	vcc_Complain(tl, "\n");
	vcc_quoteline(tl, l1, t->src->e);
	vcc_markline(tl, l1, t->src->e, t->b, t->e);
	vcc_Complain(tl, "\n");
	tl->err = 1;
}

/*--------------------------------------------------------------------*/

void
vcc_NextToken(struct vcc *tl)
{

	tl->t = VTAILQ_NEXT(tl->t, list);
	if (tl->t == NULL) {
		vcc_Complain(tl,
		    "Ran out of input, something is missing or"
		    " maybe unbalanced (...) or {...}\n");
		tl->err = 1;
		return;
	}
}

void
vcc__Expect(struct vcc *tl, unsigned tok, unsigned line)
{
	if (tl->t->tok == tok)
		return;
	vcc_Complainf(tl, "Expected %s got ", vcl_tnames[tok]);
	vcc_ErrToken(tl, tl->t);
	vcc_Complainf(tl, "\n(program line %u), at\n", line);
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------
 * Compare ID token to string, return true of match
 */

int
vcc_IdIs(const struct token *t, const char *p)
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

/*--------------------------------------------------------------------
 * Check that we have a Varnish identifier
 */

void
vcc_ExpectVid(struct vcc *tl, const char *what)
{
	const char *bad = NULL;
	struct token *t2, *t3;

	ExpectErr(tl, ID);
	ERRCHK(tl);

	t2 = VTAILQ_NEXT(tl->t, list);
	while (t2->tok == '.') {
		bad = ".";
		t2 = VTAILQ_NEXT(t2, list);
		if (t2->tok != ID)
			break;
		t2 = VTAILQ_NEXT(t2, list);
	}
	if (bad == NULL)
		bad = VCT_invalid_name(tl->t->b, tl->t->e);
	if (bad != NULL) {
		vcc_Complainf(tl, "Name of %s, '", what);
		for (t3 = tl->t; t3 != t2; t3 = VTAILQ_NEXT(t3, list))
			vcc_Complainf(tl, "%.*s", PF(t3));
		vcc_Complainf(tl,
		    "', contains illegal character '%c'\n", *bad);
		vcc_ErrWhere2(tl, tl->t, t2);
		return;
	}
}

/*--------------------------------------------------------------------
 * Decode a string
 */

static int
vcc_decstr(struct vcc *tl)
{
	char *q;
	unsigned int l;

	assert(tl->t->tok == CSTR);
	l = (tl->t->e - tl->t->b) - 2;
	tl->t->dec = TlAlloc(tl, l + 1);
	assert(tl->t->dec != NULL);
	q = tl->t->dec;
	memcpy(q, tl->t->b + 1, l);
	q[l] = '\0';
	return (0);
}

/*--------------------------------------------------------------------
 * Add a token to the token list.
 */

void
vcc_AddToken(struct vcc *tl, unsigned tok, const char *b, const char *e)
{
	struct token *t;

	t = TlAlloc(tl, sizeof *t);
	assert(t != NULL);
	t->tok = tok;
	t->b = b;
	t->e = e;
	t->src = tl->src;
	if (tl->t != NULL)
		VTAILQ_INSERT_AFTER(&tl->tokens, tl->t, t, list);
	else
		VTAILQ_INSERT_TAIL(&tl->tokens, t, list);
	tl->t = t;
}

/*--------------------------------------------------------------------
 * Lexical analysis and token generation
 */

void
vcc_Lexer(struct vcc *tl, struct source *sp)
{
	const char *p, *q;
	unsigned u;

	tl->src = sp;
	for (p = sp->b; p < sp->e; ) {

		/* Skip any whitespace */
		if (vct_isspace(*p)) {
			p++;
			continue;
		}

		/* Skip '#.*\n' comments */
		if (*p == '#') {
			while (p < sp->e && *p != '\n')
				p++;
			continue;
		}

		/* Skip C-style comments */
		if (*p == '/' && p[1] == '*') {
			for (q = p + 2; q < sp->e; q++) {
				if (*q == '/' && q[1] == '*') {
					vcc_Complain(tl,
					    "/* ... */ comment contains /*\n");
					vcc_AddToken(tl, EOI, p, p + 2);
					vcc_ErrWhere(tl, tl->t);
					vcc_AddToken(tl, EOI, q, q + 2);
					vcc_ErrWhere(tl, tl->t);
					return;
				}
				if (*q == '*' && q[1] == '/') {
					p = q + 2;
					break;
				}
			}
			if (q < sp->e)
				continue;
			vcc_AddToken(tl, EOI, p, p + 2);
			vcc_Complain(tl,
			    "Unterminated /* ... */ comment, starting at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}

		/* Skip C++-style comments */
		if (*p == '/' && p[1] == '/') {
			while (p < sp->e && *p != '\n')
				p++;
			continue;
		}

		/* Recognize inline C-code */
		if (*p == 'C' && p[1] == '{') {
			for (q = p + 2; q < sp->e; q++) {
				if (*q == '}' && q[1] == 'C') {
					vcc_AddToken(tl, CSRC, p, q + 2);
					break;
				}
			}
			if (q < sp->e) {
				p = q + 2;
				continue;
			}
			vcc_AddToken(tl, EOI, p, p + 2);
			vcc_Complain(tl,
			    "Unterminated inline C source, starting at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}

		/* Recognize long-strings */
		if (*p == '{' && p[1] == '"') {
			for (q = p + 2; q < sp->e; q++) {
				if (*q == '"' && q[1] == '}') {
					vcc_AddToken(tl, CSTR, p, q + 2);
					break;
				}
			}
			if (q < sp->e) {
				p = q + 2;
				u = tl->t->e - tl->t->b;
				u -= 4;		/* {" ... "} */
				tl->t->dec = TlAlloc(tl, u + 1 );
				AN(tl->t->dec);
				memcpy(tl->t->dec, tl->t->b + 2, u);
				tl->t->dec[u] = '\0';
				continue;
			}
			vcc_AddToken(tl, EOI, p, p + 2);
			vcc_Complain(tl,
			    "Unterminated long-string, starting at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}

		/* Match for the fixed tokens (see generate.py) */
		u = vcl_fixed_token(p, &q);
		if (u != 0) {
			vcc_AddToken(tl, u, p, q);
			p = q;
			continue;
		}

		/* Match strings */
		if (*p == '"') {
			for (q = p + 1; q < sp->e; q++) {
				if (*q == '"') {
					q++;
					break;
				}
				if (*q == '\r' || *q == '\n') {
					vcc_AddToken(tl, EOI, p, q);
					vcc_Complain(tl,
					    "Unterminated string at\n");
					vcc_ErrWhere(tl, tl->t);
					return;
				}
			}
			vcc_AddToken(tl, CSTR, p, q);
			if (vcc_decstr(tl))
				return;
			p = q;
			continue;
		}

		/* Match Identifiers */
		if (vct_isident1(*p)) {
			for (q = p; q < sp->e; q++)
				if (!vct_isident(*q))
					break;
			vcc_AddToken(tl, ID, p, q);
			p = q;
			continue;
		}

		/* Match numbers { [0-9]+ } */
		if (vct_isdigit(*p)) {
			for (q = p; q < sp->e; q++)
				if (!vct_isdigit(*q))
					break;
			if (*q != '.') {
				vcc_AddToken(tl, CNUM, p, q);
				p = q;
				continue;
			}
			for (++q; q < sp->e; q++)
				if (!vct_isdigit(*q))
					break;
			vcc_AddToken(tl, FNUM, p, q);
			p = q;
			continue;
		}
		vcc_AddToken(tl, EOI, p, p + 1);
		vcc_Complain(tl, "Syntax error at\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}
