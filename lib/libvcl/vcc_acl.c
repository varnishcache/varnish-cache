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

#include <stdio.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"

void
vcc_Cond_Ip(struct var *vp, struct tokenlist *tl)
{

	(void)vp;	/* only client.ip at this time */

	switch (tl->t->tok) {
	case '~':
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		vcc_AddRef(tl, tl->t, R_ACL);
		Fb(tl, 1, "VRT_acl_match(sp, \"%.*s\", acl_%.*s)\n",
		    PF(tl->t), PF(tl->t));
		vcc_NextToken(tl);
		break;
	default:
		vsb_printf(tl->sb, "Illegal condition ");
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
	unsigned mask, para, not;
	struct token *t, *an;

	vcc_NextToken(tl);

	ExpectErr(tl, ID);
	an = tl->t;
	vcc_NextToken(tl);

	vcc_AddDef(tl, an, R_ACL);
	Fh(tl, 0, "static struct vrt_acl acl_%.*s[];\n", PF(an));
	Fc(tl, 1, "static struct vrt_acl acl_%.*s[] = {\n", PF(an));

	tl->indent += INDENT;

	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	while (tl->t->tok != '}') {

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
		Fc(tl, 1, "{ %u, %u, %u, ", not, mask, para);
		EncToken(tl->fc, t);
		Fc(tl, 0, ", \"");
		if (para)
			Fc(tl, 0, "(");
		if (not)
			Fc(tl, 0, "!");
		Fc(tl, 0, "\\\"\" ");
		EncToken(tl->fc, t);
		Fc(tl, 0, " \"\\\"");
		if (mask)
			Fc(tl, 0, "/%u", mask);
		if (para)
			Fc(tl, 0, ")");
		Fc(tl, 0, "\" },\n");

		if (para) {
			ExpectErr(tl, ')');
			vcc_NextToken(tl);
		}
		ExpectErr(tl, ';');
		vcc_NextToken(tl);
	}
	Fc(tl, 1, "{ 0, 0, 0, (void*)0, ""}\n", 0, 0);
	tl->indent -= INDENT;
	Fc(tl, 1, "};\n\n");

	ExpectErr(tl, '}');
	vcc_NextToken(tl);

	Fi(tl, 1, "\tVRT_acl_init(acl_%.*s);\n", PF(an));
	Ff(tl, 1, "\tVRT_acl_fini(acl_%.*s);\n", PF(an));
}
