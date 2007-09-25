/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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

#include <stdio.h>
#include <stdlib.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

static void
vcc_acl_top(struct tokenlist *tl, const char *acln)
{

	Fh(tl, 1, "\nstatic const struct vrt_acl acl_%s[] = {\n", acln);
	tl->hindent += INDENT;

}

static void
vcc_acl_entry(struct tokenlist *tl)
{
	unsigned mask, para, not;
	struct token *t;

	not = para = mask = 0;

	if (tl->t->tok == '(') {
		para = 1;
		vcc_NextToken(tl);
	}

	if (tl->t->tok == '!') {
		not = 1;
		vcc_NextToken(tl);
	}

	ExpectErr(tl, CSTR);
	/* XXX: try to look it up, warn if failure */
	t = tl->t;
	vcc_NextToken(tl);
	if (tl->t->tok == '/') {
		vcc_NextToken(tl);
		ExpectErr(tl, CNUM);
		mask = vcc_UintVal(tl);
	}
	Fh(tl, 1, "{ %u, %u, %u, ", not, mask, para);
	EncToken(tl->fh, t);
	Fh(tl, 0, ", \"");
	if (para)
		Fh(tl, 0, "(");
	if (not)
		Fh(tl, 0, "!");
	Fh(tl, 0, "\\\"\" ");
	EncToken(tl->fh, t);
	Fh(tl, 0, " \"\\\"");
	if (mask)
		Fh(tl, 0, "/%u", mask);
	if (para)
		Fh(tl, 0, ")");
	Fh(tl, 0, "\" },\n");

	if (para) {
		ExpectErr(tl, ')');
		vcc_NextToken(tl);
	}
}

static void
vcc_acl_bot(struct tokenlist *tl, const char *acln)
{

	Fh(tl, 1, "{ 0, 0, 0, (void*)0, ""}\n", 0, 0);
	tl->hindent -= INDENT;
	Fh(tl, 1, "};\n");
	Fi(tl, 1, "\tVRT_acl_init(acl_%s);\n", acln);
	Ff(tl, 1, "\tVRT_acl_fini(acl_%s);\n", acln);
}

void
vcc_Cond_Ip(const struct var *vp, struct tokenlist *tl)
{
	unsigned tcond;
	char *acln;

	switch (tl->t->tok) {
	case '~':
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		vcc_AddRef(tl, tl->t, R_ACL);
		Fb(tl, 1, "VRT_acl_match(sp, %s, \"%.*s\", acl_%.*s)\n",
		    vp->rname, PF(tl->t), PF(tl->t));
		vcc_NextToken(tl);
		break;
	case T_EQ:
	case T_NEQ:
		tcond = tl->t->tok;
		vcc_NextToken(tl);
		asprintf(&acln, "acl_%u", tl->cnt);
		assert(acln != NULL);
		vcc_acl_top(tl, acln);
		vcc_acl_entry(tl);
		vcc_acl_bot(tl, acln);
		Fb(tl, 1, "%sVRT_acl_match(sp, %s, 0, acl_%s)\n",
		    (tcond == T_NEQ ? "!" : ""), vp->rname, acln);
		free(acln);
		break;
	default:
		vsb_printf(tl->sb, "Invalid condition ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, " on IP number variable\n");
		vsb_printf(tl->sb, "  only '==', '!=' and '~' are legal\n");
		vcc_ErrWhere(tl, tl->t);
		break;
	}
}

void
vcc_Acl(struct tokenlist *tl)
{
	struct token *an;
	char *acln;

	vcc_NextToken(tl);

	ExpectErr(tl, ID);
	an = tl->t;
	vcc_NextToken(tl);

	vcc_AddDef(tl, an, R_ACL);
	asprintf(&acln, "%.*s", PF(an));
	assert(acln != NULL);

	vcc_acl_top(tl, acln);

	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	while (tl->t->tok != '}') {
		vcc_acl_entry(tl);
		ERRCHK(tl);
		ExpectErr(tl, ';');
		vcc_NextToken(tl);
	}
	ExpectErr(tl, '}');
	vcc_NextToken(tl);

	vcc_acl_bot(tl, acln);

	free(acln);
}
