/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "vqueue.h"

#include "vsb.h"

#include "libvarnish.h"
#include "vcc_priv.h"
#include "vcc_compile.h"

/*--------------------------------------------------------------------*/

void
vcc_ErrToken(const struct tokenlist *tl, const struct token *t)
{

	if (t->tok == EOI)
		vsb_printf(tl->sb, "end of input");
	else if (t->tok == CSRC)
		vsb_printf(tl->sb, "C{ ... }C");
	else
		vsb_printf(tl->sb, "'%.*s'", PF(t));
}

void
vcc__ErrInternal(struct tokenlist *tl, const char *func, unsigned line)
{

	vsb_printf(tl->sb, "VCL compiler internal error at %s():%u\n",
	    func, line);
	tl->err = 1;
}

static void
vcc_icoord(struct vsb *vsb, const struct token *t, const char **ll)
{
	unsigned lin, pos;
	const char *p, *b;
	struct source *sp;

	lin = 1;
	pos = 0;
	sp = t->src;
	b = sp->b;
	if (ll != NULL)
		*ll = b;
	for (p = b; p < t->b; p++) {
		if (*p == '\n') {
			lin++;
			pos = 0;
			if (ll != NULL)
				*ll = p + 1;
		} else if (*p == '\t') {
			pos &= ~7;
			pos += 8;
		} else
			pos++;
	}
	vsb_printf(vsb, "(%s Line %d Pos %d)", sp->name, lin, pos + 1);
}

void
vcc_Coord(const struct tokenlist *tl, struct vsb *vsb, const struct token *t)
{

	if (t == NULL)
		t = tl->t;
	vcc_icoord(vsb, t, NULL);
}

void
vcc_ErrWhere(struct tokenlist *tl, const struct token *t)
{
	unsigned x, y;
	const char *p, *l, *e;

	vcc_icoord(tl->sb, t, &l);
	vsb_printf(tl->sb, "\n");
	
	x = y = 0;
	e = t->src->e;
	for (p = l; p < e && *p != '\n'; p++) {
		if (*p == '\t') {
			y &= ~7;
			y += 8;
			while (x < y) {
				vsb_bcat(tl->sb, " ", 1);
				x++;
			}
		} else {
			x++;
			y++;
			vsb_bcat(tl->sb, p, 1);
		}
	}
	vsb_cat(tl->sb, "\n");
	x = y = 0;
	for (p = l; p < e && *p != '\n'; p++) {
		if (p >= t->b && p < t->e) {
			vsb_bcat(tl->sb, "#", 1);
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
			vsb_bcat(tl->sb, "-", 1);
			x++;
		}
	}
	vsb_cat(tl->sb, "\n");
	tl->err = 1;
}

/*--------------------------------------------------------------------*/

void
vcc_NextToken(struct tokenlist *tl)
{

	tl->t = VTAILQ_NEXT(tl->t, list);
	if (tl->t == NULL) {
		vsb_printf(tl->sb,
		    "Ran out of input, something is missing or"
		    " maybe unbalanced (...) or {...}\n");
		tl->err = 1;
		return;
	}
}

void
vcc__Expect(struct tokenlist *tl, unsigned tok, int line)
{
	if (tl->t->tok == tok)
		return;
	vsb_printf(tl->sb, "Expected %s got ", vcl_tnames[tok]);
	vcc_ErrToken(tl, tl->t);
	vsb_printf(tl->sb, "\n(program line %u), at\n", line);
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------
 * Compare token to token
 */

int
vcc_Teq(const struct token *t1, const struct token *t2)
{
	if (t1->e - t1->b != t2->e - t2->b)
		return (0);
	return (!memcmp(t1->b, t2->b, t1->e - t1->b));
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
 * Check that we have a C-identifier
 */

static int
vcc_isCid(const struct token *t)
{
	const char *q;

	assert(t->tok == ID);
	for (q = t->b; q < t->e; q++) {
		if (!isalnum(*q) && *q != '_') 
			return (0);
	}
	return (1);
}

void
vcc_ExpectCid(struct tokenlist *tl)
{

	ExpectErr(tl, ID);
	ERRCHK(tl);
	if (vcc_isCid(tl->t))
		return;
	vsb_printf(tl->sb, "Identifier ");
	vcc_ErrToken(tl, tl->t);
	vsb_printf(tl->sb,
	    " contains illegal characters, use [0-9a-zA-Z_] only.\n");
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------
 * Decode %xx in a string
 */

static int8_t
vcc_xdig(const char c)
{
	static const char *xdigit =
	    "0123456789abcdef"
	    "0123456789ABCDEF";
	const char *p;

	p = strchr(xdigit, c);
	assert(p != NULL);
	return ((p - xdigit) % 16);
}

static int
vcc_decstr(struct tokenlist *tl)
{
	const char *p;
	char *q;
	unsigned char u;

	assert(tl->t->tok == CSTR);
	tl->t->dec = TlAlloc(tl, (tl->t->e - tl->t->b) - 1);
	assert(tl->t->dec != NULL);
	q = tl->t->dec;
	for (p = tl->t->b + 1; p < tl->t->e - 1; ) {
		if (*p != '%') {
			*q++ = *p++;
			continue;
		}
		if (p + 4 > tl->t->e) {
			vcc_AddToken(tl, CSTR, p, tl->t->e);
			vsb_printf(tl->sb,
			    "Incomplete %%xx escape\n");
			vcc_ErrWhere(tl, tl->t);
			return(1);
		}
		if (!isxdigit(p[1]) || !isxdigit(p[2])) {
			vcc_AddToken(tl, CSTR, p, p + 3);
			vsb_printf(tl->sb,
			    "Invalid hex char in %%xx escape\n");
			vcc_ErrWhere(tl, tl->t);
			return(1);
		}
		u = (vcc_xdig(p[1]) * 16 + vcc_xdig(p[2])) & 0xff;
		if (!isgraph(u)) {
			vcc_AddToken(tl, CSTR, p, p + 3);
			vsb_printf(tl->sb,
			    "Control character in %%xx escape\n");
			vcc_ErrWhere(tl, tl->t);
			return(1);
		}
		*q++ = u;
		p += 3;
	}
	*q++ = '\0';
	return (0);
}

/*--------------------------------------------------------------------
 * Add a token to the token list.
 */

void
vcc_AddToken(struct tokenlist *tl, unsigned tok, const char *b, const char *e)
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
vcc_Lexer(struct tokenlist *tl, struct source *sp)
{
	const char *p, *q;
	unsigned u;

	tl->src = sp;
	for (p = sp->b; p < sp->e; ) {

		/* Skip any whitespace */
		if (isspace(*p)) {
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
				if (*q == '*' && q[1] == '/') {
					p = q + 2;
					break;
				}
			}
			if (q < sp->e)
				continue;
			vcc_AddToken(tl, EOI, p, p + 2);
			vsb_printf(tl->sb,
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
			vsb_printf(tl->sb,
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
				u -= 4; 	/* {" ... "} */
				tl->t->dec = TlAlloc(tl, u + 1 );
				AN(tl->t->dec);
				memcpy(tl->t->dec, tl->t->b + 2, u);
				tl->t->dec[u] = '\0';
				continue;
			}
			vcc_AddToken(tl, EOI, p, p + 2);
			vsb_printf(tl->sb,
			    "Unterminated long-string, starting at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}

		/* Match for the fixed tokens (see token.tcl) */
		u = vcl_fixed_token(p, &q);
		if (u != 0) {
			vcc_AddToken(tl, u, p, q);
			p = q;
			continue;
		}

		/* Match strings, with \\ and \" escapes */
		if (*p == '"') {
			for (q = p + 1; q < sp->e; q++) {
				if (*q == '"') {
					q++;
					break;
				}
				if (*q == '\r' || *q == '\n') {
					vcc_AddToken(tl, EOI, p, q);
					vsb_printf(tl->sb,
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
		if (isident1(*p)) {
			for (q = p; q < sp->e; q++)
				if (!isident(*q))
					break;
			if (isvar(*q)) {
				for (; q < sp->e; q++)
					if (!isvar(*q))
						break;
				vcc_AddToken(tl, VAR, p, q);
			} else {
				vcc_AddToken(tl, ID, p, q);
			}
			p = q;
			continue;
		}

		/* Match numbers { [0-9]+ } */
		if (isdigit(*p)) {
			for (q = p; q < sp->e; q++)
				if (!isdigit(*q))
					break;
			vcc_AddToken(tl, CNUM, p, q);
			p = q;
			continue;
		}
		vcc_AddToken(tl, EOI, p, p + 1);
		vsb_printf(tl->sb, "Syntax error at\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}
