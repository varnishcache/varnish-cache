/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/

static struct var *
HeaderVar(struct tokenlist *tl, const struct token *t, const struct var *vh)
{
	char *p;
	struct var *v;
	int i;

	(void)tl;

	v = TlAlloc(tl, sizeof *v);
	assert(v != NULL);
	i = t->e - t->b;
	p = TlAlloc(tl, i + 1);
	assert(p != NULL);
	memcpy(p, t->b, i);
	p[i] = '\0';
	v->name = p;
	v->access = V_RW;
	v->fmt = STRING;
	v->hdr = vh->hdr;
	v->methods = vh->methods;
	asprintf(&p, "VRT_GetHdr(sp, %s, \"\\%03o%s:\")", v->hdr,
	    (unsigned)(strlen(v->name + vh->len) + 1), v->name + vh->len);
	AN(p);
	TlFree(tl, p);
	v->rname = p;
	asprintf(&p, "VRT_SetHdr(sp, %s, \"\\%03o%s:\", ", v->hdr,
	    (unsigned)(strlen(v->name + vh->len) + 1), v->name + vh->len);
	AN(p);
	TlFree(tl, p);
	v->lname = p;
	return (v);
}

/*--------------------------------------------------------------------*/

struct var *
vcc_FindVar(struct tokenlist *tl, const struct token *t, struct var *vl)
{
	struct var *v;

	for (v = vl; v->name != NULL; v++) {
		if (v->fmt == HEADER  && (t->e - t->b) <= v->len)
			continue;
		if (v->fmt != HEADER  && t->e - t->b != v->len)
			continue;
		if (memcmp(t->b, v->name, v->len))
			continue;
		vcc_AddUses(tl, v);
		if (v->fmt != HEADER)
			return (v);
		return (HeaderVar(tl, t, v));
	}
	vsb_printf(tl->sb, "Unknown variable ");
	vcc_ErrToken(tl, t);
	vsb_cat(tl->sb, "\nAt: ");
	vcc_ErrWhere(tl, t);
	return (NULL);
}

