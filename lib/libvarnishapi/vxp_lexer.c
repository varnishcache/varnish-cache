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

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "vsb.h"
#include "vas.h"

#include "vxp.h"

static void
vxp_add_token(struct vxp *vxp, unsigned tok, const char *b, const char *e)
{
	struct token *t;

	t = vxp_Alloc(vxp, sizeof *t);
	AN(t);
	t->tok = tok;
	t->b = b;
	t->e = e;
	if (vxp->t != NULL)
		VTAILQ_INSERT_AFTER(&vxp->tokens, vxp->t, t, list);
	else
		VTAILQ_INSERT_TAIL(&vxp->tokens, t, list);
	vxp->t = t;
}

static int
vxp_decstr(struct vxp *vxp, int quoted)
{
	const char *b, *e, *p;
	char *s;
	unsigned l;
	int esc = 0;

	assert(vxp->t->tok == VAL);

	b = vxp->t->b;
	e = vxp->t->e;
	if (quoted) {
		assert(e - b >= 2);
		b++;
		e--;
	}
	l = e - b;
	s = vxp->t->dec = vxp_Alloc(vxp, l + 1);
	AN(vxp->t->dec);
	for (p = b; p < e; p++) {
		if (!esc && *p == '\\') {
			esc = 1;
			continue;
		}
		esc = 0;
		*s++ = *p;
	}
	*s = '\0';
	if (esc || p != e) {
		VSB_printf(vxp->sb, "Syntax error ");
		vxp_ErrWhere(vxp, vxp->t, -1);
		return (1);
	}
	return (0);
}

/*
 * Build a token list
 */

void
vxp_Lexer(struct vxp *vxp)
{
	const char *p, *q;
	unsigned u;
	char quote;

	for (p = vxp->b; p < vxp->e; ) {

		/* Skip any whitespace */
		if (isspace(*p)) {
			p++;
			continue;
		}

		/* Match for the fixed tokens */
		u = vxp_fixed_token(p, &q);
		if (u != 0) {
			AN(q);
			vxp_add_token(vxp, u, p, q);
			p = q;
			continue;
		}

		/* Match quoted strings */
		if (*p == '"' || *p == '\'') {
			quote = *p;
			for (q = p + 1; q < vxp->e; q++) {
				if (q[-1] == '\\')
					continue;
				if (*q == quote) {
					q++;
					quote = '\0';
					break;
				}
			}
			vxp_add_token(vxp, VAL, p, q);
			if (quote != '\0') {
				VSB_printf(vxp->sb, "Unterminated string ");
				vxp_ErrWhere(vxp, vxp->t, q - p - 1);
				return;
			}
			if (vxp_decstr(vxp, 1))
				return;
			p = q;
			continue;
		}

		/* Match bareword */
		if (isword(*p)) {
			for (q = p; q < vxp->e; q++)
				if (!isword(*q))
					break;
			vxp_add_token(vxp, VAL, p, q);
			if (vxp_decstr(vxp, 0))
				return;
			p = q;
			continue;
		}

		/* Error */
		vxp_add_token(vxp, EOI, p, p + 1);
		VSB_printf(vxp->sb, "Syntax error ");
		vxp_ErrWhere(vxp, vxp->t, q - p);
		return;
	}

	/* Finished */
	vxp_add_token(vxp, EOI, vxp->e, vxp->e);
}

#ifdef VXP_DEBUG
void
vxp_PrintTokens(const struct vxp *vxp)
{
	struct token *t;

	fprintf(stderr, "Token list:\n");
	fprintf(stderr, "  %-5s %-20s %s\n", "TOK", "SUBSTR", "DECODED");
	VTAILQ_FOREACH(t, &vxp->tokens, list) {
		fprintf(stderr, "  ");
		fprintf(stderr, "%-5s", vxp_tnames[t->tok]);
		fprintf(stderr, " %-20.*s", (unsigned)(t->e - t->b), t->b);
		if (t->dec)
			fprintf(stderr, " '%s'", t->dec);
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n");
}
#endif /* VXP_DEBUG */
