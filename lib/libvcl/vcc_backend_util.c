/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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
 */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------
 * Helper functions to complain about duplicate and missing fields
 *
 * XXX: idea: add groups to check for exclusivity, such that
 * XXX:    ("!foo", "?bar", "!{", "this", "that", "}", NULL)
 * XXX: means exactly one of "this" or "that", and
 * XXX:    ("!foo", "?bar", "?{", "this", "that", "}", NULL)
 * XXX: means at most one of "this" or "that".
 */

struct fld_spec {
	const char	*name;
	struct token	*found;
};

void
vcc_ResetFldSpec(struct fld_spec *f)
{

	for (; f->name != NULL; f++)
		f->found = NULL;
}

struct fld_spec *
vcc_FldSpec(struct vcc *tl, const char *first, ...)
{
	struct fld_spec f[100], *r;
	int n = 0;
	va_list ap;
	const char *p;

	f[n++].name = first;
	va_start(ap, first);
	while (1) {
		p = va_arg(ap, const char *);
		if (p == NULL)
			break;
		f[n++].name = p;
		assert(n < 100);
	}
	va_end(ap);
	f[n++].name = NULL;

	vcc_ResetFldSpec(f);

	r = TlAlloc(tl, sizeof *r * n);
	memcpy(r, f, n * sizeof *r);
	return (r);
}

void
vcc_IsField(struct vcc *tl, struct token **t, struct fld_spec *fs)
{
	struct token *t_field;

	SkipToken(tl, '.');
	ExpectErr(tl, ID);
	t_field = tl->t;
	*t = t_field;
	vcc_NextToken(tl);
	SkipToken(tl, '=');

	for (; fs->name != NULL; fs++) {
		if (!vcc_IdIs(t_field, fs->name + 1))
			continue;
		if (fs->found == NULL) {
			fs->found = t_field;
			return;
		}
		VSB_printf(tl->sb, "Field ");
		vcc_ErrToken(tl, t_field);
		VSB_printf(tl->sb, " redefined at:\n");
		vcc_ErrWhere(tl, t_field);
		VSB_printf(tl->sb, "\nFirst defined at:\n");
		vcc_ErrWhere(tl, fs->found);
		return;
	}
	VSB_printf(tl->sb, "Unknown field: ");
	vcc_ErrToken(tl, t_field);
	VSB_printf(tl->sb, " at\n");
	vcc_ErrWhere(tl, t_field);
	return;
}

void
vcc_FieldsOk(struct vcc *tl, const struct fld_spec *fs)
{

	for (; fs->name != NULL; fs++) {
		if (*fs->name == '!' && fs->found == NULL) {
			VSB_printf(tl->sb,
			    "Mandatory field '%s' missing.\n", fs->name + 1);
			tl->err = 1;
		}
	}
}
