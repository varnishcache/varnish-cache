/*-
 * Copyright (c) 2010 Linpro AS
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/


struct symbol *
VCC_AddSymbol(struct vcc *tl, const char *name)
{
	struct symbol *sym;

	VTAILQ_FOREACH(sym, &tl->symbols, list) {
		if (strcmp(name, sym->name))
			continue;
		printf("%s <> %s\n", name, sym->name);
		WRONG("name collision");
	}
	ALLOC_OBJ(sym, SYMBOL_MAGIC);
	REPLACE(sym->name, name);
	sym->nlen = strlen(name);
	VTAILQ_INSERT_TAIL(&tl->symbols, sym, list);
	return (sym);
}

const struct symbol *
VCC_FindSymbol(const struct vcc *tl, const struct token *t)
{
	struct symbol *sym;

	assert(t->tok == ID);
	VTAILQ_FOREACH(sym, &tl->symbols, list) {
		if (!sym->wildcard) {
			if (vcc_IdIs(t, sym->name))
				return (sym);
			continue;
		}
		if (t->e - t->b <= sym->nlen)
			continue;
		if (!memcmp(sym->name, t->b, sym->nlen))
			return (sym);
	}
	return (NULL);
}
