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
#include "miniobj.h"

#include "vxp.h"

static void
vxp_ErrToken(const struct vxp *vxp, const struct token *t)
{

	if (t->tok == EOI)
		VSB_printf(vxp->sb, "end of input");
	else
		VSB_printf(vxp->sb, "'%.*s'", PF(t));
}

static void
vxp_Pos(const struct vxp *vxp, struct vsb *vsb, const struct token *t,
    int tokoff)
{
	unsigned pos;

	AN(vxp);
	AN(vsb);
	AN(t);
	assert(t->b >= vxp->b);
	pos = (unsigned)(t->b - vxp->b);
	if (tokoff > 0)
		pos += tokoff;
	VSB_printf(vsb, "(Pos %u)", pos + 1);
}

static void
vxp_quote(const struct vxp *vxp, const char *b, const char *e, int tokoff)
{
	const char *p;
	char c;

	assert(b <= e);
	assert(b >= vxp->b);
	assert(e <= vxp->e);
	for (p = vxp->b; p < vxp->e; p++) {
		if (isspace(*p))
			VSB_bcat(vxp->sb, " ", 1);
		else
			VSB_bcat(vxp->sb, p, 1);
	}
	VSB_putc(vxp->sb, '\n');
	for (p = vxp->b; p < vxp->e; p++) {
		if (p >= b && p < e) {
			if (p - b == tokoff)
				c = '^';
			else
				c = '#';
		} else
			c = '-';
		VSB_putc(vxp->sb, c);
	}
	VSB_putc(vxp->sb, '\n');
}

void
vxp_ErrWhere(struct vxp *vxp, const struct token *t, int tokoff)
{

	AN(vxp);
	AN(t);
	vxp_Pos(vxp, vxp->sb, t, tokoff);
	VSB_putc(vxp->sb, '\n');
	vxp_quote(vxp, t->b, t->e, tokoff);
	VSB_putc(vxp->sb, '\n');
	vxp->err = 1;
}

void
vxp_NextToken(struct vxp *vxp)
{

	AN(vxp->t);
	vxp->t = VTAILQ_NEXT(vxp->t, list);
	if (vxp->t == NULL) {
		VSB_printf(vxp->sb,
		    "Ran out of input, something is missing or"
		    " maybe unbalanced parenthesis\n");
		vxp->err = 1;
	}
}

void
vxp__Expect(struct vxp *vxp, unsigned tok)
{

	if (vxp->t->tok == tok)
		return;
	VSB_printf(vxp->sb, "Expected %s got ", vxp_tnames[tok]);
	vxp_ErrToken(vxp, vxp->t);
	VSB_putc(vxp->sb, ' ');
	vxp_ErrWhere(vxp, vxp->t, -1);
}

static void
vxp_DoFree(struct vxp *vxp, void *p)
{
	struct membit *mb;

	mb = calloc(sizeof *mb, 1);
	AN(mb);
	mb->ptr = p;
	VTAILQ_INSERT_TAIL(&vxp->membits, mb, list);
}

void *
vxp_Alloc(struct vxp *vxp, unsigned len)
{
	void *p;

	p = calloc(len, 1);
	AN(p);
	vxp_DoFree(vxp, p);
	return (p);
}

static struct vxp *
vxp_New(struct vsb *sb)
{
	struct vxp *vxp;

	AN(sb);

	ALLOC_OBJ(vxp, VXP_MAGIC);
	AN(vxp);
	VTAILQ_INIT(&vxp->membits);
	VTAILQ_INIT(&vxp->tokens);
	vxp->sb = sb;

	return (vxp);
}

static void
vxp_Delete(struct vxp **pvxp)
{
	struct vxp *vxp;
	struct membit *mb;

	AN(pvxp);
	vxp = *pvxp;
	*pvxp = NULL;
	CHECK_OBJ_NOTNULL(vxp, VXP_MAGIC);

	while (!VTAILQ_EMPTY(&vxp->membits)) {
		mb = VTAILQ_FIRST(&vxp->membits);
		VTAILQ_REMOVE(&vxp->membits, mb, list);
		free(mb->ptr);
		free(mb);
	}

	FREE_OBJ(vxp);
}

struct vex *
vex_New(const char *query, struct vsb *sb, unsigned options)
{
	struct vxp *vxp;
	struct vex *vex;

	AN(query);
	AN(sb);
	vxp = vxp_New(sb);
	vxp->b = query;
	vxp->e = query + strlen(query);
	vxp->vex_options = options;
	if (options & VEX_OPT_CASELESS)
		vxp->vre_options |= VRE_CASELESS;

	vxp_Lexer(vxp);

#ifdef VXP_DEBUG
	vxp_PrintTokens(vxp);
#endif

	if (vxp->err) {
		vxp_Delete(&vxp);
		AZ(vxp);
		return (NULL);
	}

	vex = vxp_Parse(vxp);

#ifdef VXP_DEBUG
	if (vex != NULL)
		vex_PrintTree(vex);
#endif

	vxp_Delete(&vxp);
	AZ(vxp);

	return (vex);
}
