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

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

#include "vrt.h"

/*--------------------------------------------------------------------*/

char *
vcc_regexp(struct tokenlist *tl, int sub)
{
	char buf[32], *p;

	Expect(tl, CSTR);
	if (VRT_re_test(tl->sb, tl->t->dec, sub)) {
		vcc_ErrWhere(tl, tl->t);
		return (NULL);
	}
	sprintf(buf, "VGC_re_%u", tl->recnt++);
	p = TlAlloc(tl, strlen(buf) + 1);
	strcpy(p, buf);

	Fh(tl, 0, "static void *%s;\n", buf);
	Fi(tl, 0, "\tVRT_re_init(&%s, ",buf);
	EncToken(tl->fi, tl->t);
	Fi(tl, 0, ", %d);\n", sub);
	Ff(tl, 0, "\tVRT_re_fini(%s);\n", buf);
	return (p);
}

/*--------------------------------------------------------------------*/

static int
vcc_regsub(struct tokenlist *tl, int all)
{
	char *p;

	vcc_NextToken(tl);

	Fb(tl, 0, "VRT_regsub(sp, %d, ", all);

	Expect(tl, '(');
	vcc_NextToken(tl);

	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return (0);
	}

	Expect(tl, ',');
	vcc_NextToken(tl);
	
	Expect(tl, CSTR);
	p = vcc_regexp(tl, 1);
	vcc_NextToken(tl);
	Fb(tl, 0, ", %s, ", p);

	Expect(tl, ',');
	vcc_NextToken(tl);
	
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return (0);
	}

	Expect(tl, ')');
	vcc_NextToken(tl);
	Fb(tl, 0, ")");

	return (1);
}

/*--------------------------------------------------------------------
 * Parse a string value and emit something that results in a usable
 * "const char *".
 * There are three possible outcomes:
 *	tl->err != 0 means something bad happened and a message is emitted.
 *	return (0) means "could not use this token"
 *	return (1) means "done"
 */

int
vcc_StringVal(struct tokenlist *tl) 
{
	struct var *vp;

	if (tl->t->tok == CSTR) {
		EncToken(tl->fb, tl->t);
		vcc_NextToken(tl);
		return (1);
	}
	if (tl->t->tok == ID && vcc_IdIs(tl->t, "regsub"))
		return (vcc_regsub(tl, 0));
	if (tl->t->tok == ID && vcc_IdIs(tl->t, "regsuball"))
		return (vcc_regsub(tl, 1));
	if (tl->t->tok == VAR) {
		vp = vcc_FindVar(tl, tl->t, vcc_vars);
		if (tl->err)
			return (0);
		assert(vp != NULL);
		switch (vp->fmt) {
		case STRING:
			Fb(tl, 0, "%s", vp->rname);
			break;
		case IP:
			Fb(tl, 0, "VRT_IP_string(sp, %s)", vp->rname);
			break;
		case INT:
			Fb(tl, 0, "VRT_int_string(sp, %s)", vp->rname);
			break;
		case FLOAT:
			Fb(tl, 0, "VRT_double_string(sp, %s)", vp->rname);
			break;
		default:
			vsb_printf(tl->sb,
			    "String representation of '%s' not implemented yet.\n",
				vp->name);
			vcc_ErrWhere(tl, tl->t);
			return (0);
		}
		vcc_NextToken(tl);
		return (1);
	}
	return (0);
}

void
vcc_ExpectedStringval(struct tokenlist *tl)
{

	if (!tl->err) {
		vsb_printf(tl->sb, "Expected string variable or constant\n");
		vcc_ErrWhere(tl, tl->t);
	}
}
