/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vcc_compile.h"

#include "vfil.h"

struct source *
vcc_new_source(const char *src, const char *name)
{
	struct source *sp;

	AN(src);
	AN(name);
	sp = calloc(1, sizeof *sp);
	AN(sp);
	REPLACE(sp->name, name);
	sp->b = src;
	sp->e = strchr(src, '\0');
	return (sp);
}

/*--------------------------------------------------------------------*/

static void
vcc_destroy_source(struct source **spp)
{
	struct source *sp;

	AN(spp);
	sp = *spp;
	*spp = NULL;

	AN(sp);
	free(sp->name);
	free(sp->freeit);
	free(sp);
}

/*--------------------------------------------------------------------*/

struct source *
vcc_file_source(const struct vcc *tl, const char *fn)
{
	char *f, *fnp;
	struct source *sp;

	if (!tl->unsafe_path && strchr(fn, '/') != NULL) {
		VSB_printf(tl->sb, "VCL filename '%s' is unsafe.\n", fn);
		return (NULL);
	}
	f = NULL;
	if (VFIL_searchpath(tl->vcl_path, NULL, &f, fn, &fnp) || f == NULL) {
		VSB_printf(tl->sb, "Cannot read file '%s' (%s)\n",
		    fnp != NULL ? fnp : fn, strerror(errno));
		free(fnp);
		return (NULL);
	}
	sp = vcc_new_source(f, fnp);
	free(fnp);
	sp->freeit = f;
	return (sp);
}

/*--------------------------------------------------------------------*/

void
vcc_resolve_includes(struct vcc *tl)
{
	struct token *t, *t1, *t2;
	struct source *sp;
	const struct source *sp1;
	struct vsb *vsb;
	const char *p;

	VTAILQ_FOREACH(t, &tl->tokens, list) {
		if (t->tok != ID || !vcc_IdIs(t, "include"))
			continue;

		t1 = VTAILQ_NEXT(t, list);
		AN(t1);			/* There's always an EOI */
		if (t1->tok != CSTR) {
			VSB_cat(tl->sb,
			    "include not followed by string constant.\n");
			vcc_ErrWhere(tl, t1);
			return;
		}
		t2 = VTAILQ_NEXT(t1, list);
		AN(t2);			/* There's always an EOI */

		if (t2->tok != ';') {
			VSB_cat(tl->sb,
			    "include <string> not followed by semicolon.\n");
			vcc_ErrWhere(tl, t1);
			return;
		}

		if (t1->dec[0] == '.' && t1->dec[1] == '/') {
			/*
			 * Nested include filenames, starting with "./" are
			 * resolved relative to the VCL file which contains
			 * the include directive.
			 */
			if (t1->src->name[0] != '/') {
				VSB_cat(tl->sb,
				    "include \"./xxxxx\"; needs absolute "
				    "filename of including file.\n");
				vcc_ErrWhere(tl, t1);
				return;
			}
			vsb = VSB_new_auto();
			AN(vsb);
			p = strrchr(t1->src->name, '/');
			AN(p);
			VSB_bcat(vsb, t1->src->name, p - t1->src->name);
			VSB_cat(vsb, t1->dec + 1);
			AZ(VSB_finish(vsb));
			sp = vcc_file_source(tl, VSB_data(vsb));
			VSB_destroy(&vsb);
		} else {
			sp = vcc_file_source(tl, t1->dec);
		}
		if (sp == NULL) {
			vcc_ErrWhere(tl, t1);
			return;
		}

		for (sp1 = t->src; sp1 != NULL; sp1 = sp1->parent)
			if (!strcmp(sp1->name, sp->name))
				break;
		if (sp1 != NULL) {
			VSB_printf(tl->sb,
			    "Recursive include of \"%s\"\n\n", sp->name);
			vcc_ErrWhere(tl, t1);
			for (sp1 = t->src; sp1 != NULL; sp1 = sp1->parent) {
				if (sp1->parent_tok)
					vcc_ErrWhere(tl, sp1->parent_tok);
			}
			vcc_destroy_source(&sp);
			return;
		}
		sp->parent = t->src;
		sp->parent_tok = t1;
		VTAILQ_INSERT_TAIL(&tl->sources, sp, list);
		sp->idx = tl->nsources++;
		tl->t = t2;
		vcc_Lexer(tl, sp, 0);

		VTAILQ_REMOVE(&tl->tokens, t, list);
		VTAILQ_REMOVE(&tl->tokens, t1, list);
		VTAILQ_REMOVE(&tl->tokens, t2, list);
		if (!tl->err)
			vcc_resolve_includes(tl);
		return;
	}
}
