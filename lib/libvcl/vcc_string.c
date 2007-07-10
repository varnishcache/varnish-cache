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
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

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
