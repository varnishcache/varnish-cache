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

#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vcc_compile.h"

#include "vfil.h"

struct source *
vcc_new_source(const char *src, const char *kind, const char *name)
{
	struct source *sp;

	AN(src);
	AN(name);
	ALLOC_OBJ(sp, SOURCE_MAGIC);
	AN(sp);
	REPLACE(sp->name, name);
	sp->kind = kind;
	sp->b = src;
	sp->e = strchr(src, '\0');
	VTAILQ_INIT(&sp->src_tokens);
	return (sp);
}

/*--------------------------------------------------------------------*/

struct source *
vcc_file_source(struct vcc *tl, const char *fn)
{
	char *f, *fnp;
	struct source *sp;

	if (!tl->unsafe_path && strchr(fn, '/') != NULL) {
		VSB_printf(tl->sb, "VCL filename '%s' is unsafe.\n", fn);
		tl->err = 1;
		return (NULL);
	}
	f = NULL;
	if (VFIL_searchpath(tl->vcl_path, NULL, &f, fn, &fnp) || f == NULL) {
		VSB_printf(tl->sb, "Cannot read file '%s' (%s)\n",
		    fnp != NULL ? fnp : fn, strerror(errno));
		free(fnp);
		tl->err = 1;
		return (NULL);
	}
	sp = vcc_new_source(f, "file", fnp);
	free(fnp);
	return (sp);
}

/*--------------------------------------------------------------------*/

static void
vcc_include_file(struct vcc *tl, const struct source *src_sp,
    const char *filename, const struct token *parent_token)
{
	struct source *sp;

	sp = vcc_file_source(tl, filename);
	if (sp == NULL)
		return;

	sp->parent = src_sp;
	sp->parent_tok = parent_token;
	vcc_lex_source(tl, sp, 0);
}

/*--------------------------------------------------------------------*/

static void
vcc_include_glob_file(struct vcc *tl, const struct source *src_sp,
    const char *filename, const struct token *parent_token)
{
	glob_t	g[1];
	unsigned u;
	int i;

	memset(g, 0, sizeof g);
	i = glob(filename, 0, NULL, g);
	switch (i) {
	case 0:
		for (u = 0; !tl->err && u < g->gl_pathc; u++) {
			vcc_include_file(
			    tl, src_sp, g->gl_pathv[u], parent_token);
		}
		break;
	case GLOB_NOMATCH:
		VSB_printf(tl->sb, "glob pattern matched no files.\n");
		tl->err = 1;
		break;
	default:
		VSB_printf(tl->sb, "glob(3) expansion failed (%d)\n", i);
		tl->err = 1;
		break;
	}
	globfree(g);
}

/*--------------------------------------------------------------------
 * NB: We cannot use vcc_ErrWhere2() on tokens which are not on the
 * NB: tl->tokens list.
 */

static struct token *
vcc_lex_include(struct vcc *tl, const struct source *src_sp, struct token *t)
{
	struct token *tok1;
	int i, glob_flag = 0;
	struct vsb *vsb = NULL;
	const char *filename;
	const char *p;

	assert(vcc_IdIs(t, "include"));

	tok1 = VTAILQ_NEXT(t, src_list);
	AN(tok1);

	while (1) {
		t = VTAILQ_NEXT(tok1, src_list);
		AN(t);
		i = vcc_IsFlagRaw(tl, tok1, t);
		if (i < 0)
			break;
		if (vcc_IdIs(t, "glob")) {
			glob_flag = i;
		} else {
			VSB_cat(tl->sb, "Unknown include flag:\n");
			vcc_ErrWhere(tl, t);
			return(t);
		}
		tok1 = VTAILQ_NEXT(t, src_list);
		AN(tok1);
	}

	if (tok1->tok != CSTR) {
		VSB_cat(tl->sb,
		    "include not followed by string constant.\n");
		vcc_ErrWhere(tl, tok1);
		return (t);
	}
	t = VTAILQ_NEXT(tok1, src_list);
	AN(t);

	if (t->tok != ';') {
		VSB_cat(tl->sb,
		    "include <string> not followed by semicolon.\n");
		vcc_ErrWhere(tl, tok1);
		return (t);
	}

	filename = tok1->dec;

	if (filename[0] == '.' && filename[1] == '/') {
		/*
		 * Nested include filenames, starting with "./" are
		 * resolved relative to the VCL file which contains
		 * the include directive.
		 */
		if (src_sp->name[0] != '/') {
			VSB_cat(tl->sb,
			    "include \"./xxxxx\"; needs absolute "
			    "filename of including file.\n");
			vcc_ErrWhere(tl, tok1);
			return(t);
		}
		vsb = VSB_new_auto();
		AN(vsb);
		p = strrchr(src_sp->name, '/');
		AN(p);
		VSB_bcat(vsb, src_sp->name, p - src_sp->name);
		VSB_cat(vsb, filename + 1);
		AZ(VSB_finish(vsb));
		filename = VSB_data(vsb);
	}

	if (glob_flag)
		vcc_include_glob_file(tl, src_sp, filename, tok1);
	else
		vcc_include_file(tl, src_sp, filename, tok1);
	if (vsb != NULL)
		VSB_destroy(&vsb);
	if (tl->err)
		vcc_ErrWhere(tl, tok1);
	return (t);
}

void
vcc_lex_source(struct vcc *tl, struct source *src_sp, int eoi)
{
	struct token *t;
	const struct source *sp1;

	CHECK_OBJ_NOTNULL(src_sp, SOURCE_MAGIC);

	for (sp1 = src_sp->parent; sp1 != NULL; sp1 = sp1->parent) {
		if (!strcmp(sp1->name, src_sp->name) &&
		    !strcmp(sp1->kind, src_sp->kind)) {
			VSB_printf(tl->sb,
			    "Recursive use of %s \"%s\"\n\n",
			    src_sp->kind, src_sp->name);
			tl->err = 1;
			return;
		}
	}

	VTAILQ_INSERT_TAIL(&tl->sources, src_sp, list);
	src_sp->idx = tl->nsources++;

	vcc_Lexer(tl, src_sp);
	if (tl->err)
		return;
	VTAILQ_FOREACH(t, &src_sp->src_tokens, src_list) {
		if (!eoi && t->tok == EOI)
			break;

		if (t->tok == ID && vcc_IdIs(t, "include")) {
			t = vcc_lex_include(tl, src_sp, t);
		} else {
			VTAILQ_INSERT_TAIL(&tl->tokens, t, list);
		}
		if (tl->err)
			return;
	}
}

/*--------------------------------------------------------------------*/

void
vcc_IncludePush(struct vcc *tl, struct token *t)
{
	struct syntax *s;

	CHECK_OBJ_NOTNULL(tl, VCC_MAGIC);
	AN(t);
	assert(t->tok == INC_PUSH);

	s = calloc(1, sizeof *s);
	AN(s);
	s->version = tl->esyntax;
	s->src = t->src;
	VTAILQ_INSERT_TAIL(&tl->vcl_syntax, s, list);
}

void
vcc_IncludePop(struct vcc *tl, struct token *t)
{
	struct syntax *s;

	CHECK_OBJ_NOTNULL(tl, VCC_MAGIC);
	AN(t);
	assert(t->tok == INC_POP);

	s = VTAILQ_LAST(&tl->vcl_syntax, syntaxhead);
	AN(s);
	tl->esyntax = s->version;
	VTAILQ_REMOVE(&tl->vcl_syntax, s, list);
	free(s);
}
