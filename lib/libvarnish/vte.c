/*-
 * Copyright (c) 2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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
 *
 */

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> /* for MUSL (ssize_t) */

#include "vdef.h"
#include "miniobj.h"

#include "vas.h"
#include "vsb.h"
#include "vte.h"

#define MINSEP 1
#define MAXSEP 3

struct vte {
	unsigned	magic;
#define VTE_MAGIC	0xedf42b97
	struct vsb	*vsb;
	int		c_off;		/* input char offset */
	int		l_sz;		/* input line size */
	int		l_maxsz;	/* maximum input line size */
	int		o_sz;		/* output sz */
	int		o_sep;		/* output field separators */
	int		f_off;		/* input field offset */
	int		f_sz;		/* input field size */
	int		f_cnt;		/* actual number of fields */
	int		f_maxcnt;	/* maximum number of fields */
	int		f_maxsz[]
	    v_counted_by_(f_cnt);	/* maximum size per field */
};

struct vte *
VTE_new(int maxfields, int width)
{
	struct vte *vte;

	assert(maxfields > 0);
	assert(width > 0);

	ALLOC_FLEX_OBJ(vte, f_maxsz, maxfields, VTE_MAGIC);
	if (vte != NULL) {
		vte->o_sz = width;
		vte->f_maxcnt = maxfields;
		vte->vsb = VSB_new_auto();
		AN(vte->vsb);
	}
	return (vte);
}

void
VTE_destroy(struct vte **vtep)
{
	struct vte *vte;

	TAKE_OBJ_NOTNULL(vte, vtep, VTE_MAGIC);
	AN(vte->vsb);
	VSB_destroy(&vte->vsb);
	FREE_OBJ(vte);
}

static int
vte_update(struct vte *vte)
{
	const char *p, *q;
	int len, fno;

	AZ(vte->o_sep);

	len = VSB_len(vte->vsb);
	assert(len >= vte->c_off);

	p = vte->vsb->s_buf + vte->c_off;
	q = vte->vsb->s_buf + len;
	for (; p < q; p++) {
		if (vte->f_off < 0) {
			while (p < q && *p != '\n')
				p++;
		}
		if (vte->l_sz == 0 && *p == ' ') {
			vte->f_off = -1;
			continue;
		}
		if (vte->f_off >= 0 && vte->f_sz == 0 && *p == '\v')
			p++;
		if (*p == '\t' || *p == '\n') {
			fno = vte->f_off;
			if (fno >= 0 && vte->f_sz > vte->f_maxsz[fno])
				vte->f_maxsz[fno] = vte->f_sz;
			fno++;
			assert(fno <= vte->f_maxcnt);
			if (*p == '\t' && fno == vte->f_maxcnt) {
				errno = EOVERFLOW;
				vte->o_sep = -1;
				return (-1);
			}
			vte->f_off = fno;
			vte->f_sz = 0;
		}
		if (*p == '\n') {
			vte->f_cnt = vmax(vte->f_cnt, vte->f_off);
			vte->l_maxsz = vmax(vte->l_maxsz, vte->l_sz);
			vte->f_off = 0;
			vte->f_sz = 0;
			vte->l_sz = 0;
		} else if (*p != '\t') {
			vte->f_sz++;
			vte->l_sz++;
		}
	}

	vte->c_off = len;
	return (0);
}

int
VTE_putc(struct vte *vte, char c)
{

	CHECK_OBJ_NOTNULL(vte, VTE_MAGIC);
	AN(c);

	if (vte->o_sep != 0)
		return (-1);

	if (VSB_putc(vte->vsb, c) < 0) {
		vte->o_sep = -1;
		return (-1);
	}

	return (vte_update(vte));
}

int
VTE_cat(struct vte *vte, const char *s)
{

	CHECK_OBJ_NOTNULL(vte, VTE_MAGIC);
	AN(s);

	if (vte->o_sep != 0)
		return (-1);

	if (VSB_cat(vte->vsb, s) < 0) {
		vte->o_sep = -1;
		return (-1);
	}

	return (vte_update(vte));
}

int
VTE_printf(struct vte *vte, const char *fmt, ...)
{
	va_list ap;
	int res;

	CHECK_OBJ_NOTNULL(vte, VTE_MAGIC);
	AN(fmt);

	if (vte->o_sep != 0)
		return (-1);

	va_start(ap, fmt);
	res = VSB_vprintf(vte->vsb, fmt, ap);
	va_end(ap);

	if (res < 0) {
		vte->o_sep = -1;
		return (-1);
	}

	return (vte_update(vte));
}

int
VTE_finish(struct vte *vte)
{
	int sep;

	CHECK_OBJ_NOTNULL(vte, VTE_MAGIC);

	if (vte->o_sep != 0)
		return (-1);

	if (VSB_finish(vte->vsb) < 0) {
		vte->o_sep = -1;
		return (-1);
	}

	if (vte->f_cnt == 0) {
		vte->o_sep = INT_MAX;
		return (0);
	}

	sep = (vte->o_sz - vte->l_maxsz) / vte->f_cnt;
	vte->o_sep = vlimit_t(int, sep, MINSEP, MAXSEP);
	return (0);
}

#define VTE_FORMAT(func, priv, ...)			\
	do {						\
		if (func(priv, __VA_ARGS__) < 0)	\
			return (-1);			\
	} while (0)

int
VTE_format(const struct vte *vte, VTE_format_f *func, void *priv)
{
	int fno, fsz, nsp, just_left;
	const char *p, *q, *sep;

	CHECK_OBJ_NOTNULL(vte, VTE_MAGIC);
	AN(func);

	if (vte->o_sep <= 0)
		return (-1);

	nsp = vte->o_sep;
	p = VSB_data(vte->vsb);
	AN(p);
	q = p;

	fno = 0;
	sep = "";
	just_left = 0;
	while (*p != 0) {
		if (*p == '\v') {
			if (p > q) {
				VTE_FORMAT(func, priv, "%.*s%s",
				    (int)(p - q), q, sep);
			}
			q = ++p;
			just_left = 1;
		}
		if (!just_left && fno == 0 && *p == ' ')
			fsz = strcspn(p, "\n");
		else
			fsz = strcspn(p, "\t\n");
		p += fsz;
		if (*p == '\t') {
			assert(vte->f_maxsz[fno] + nsp > fsz);
			if (just_left) {
				VTE_FORMAT(func, priv, "%*s%.*s%*s",
				    vte->f_maxsz[fno] - fsz, "",
				    (int)(p - q), q,
				    nsp, "");
				just_left = 0;
			} else {
				VTE_FORMAT(func, priv, "%.*s%*s",
				    (int)(p - q), q,
				    vte->f_maxsz[fno] + nsp - fsz, "");
			}
			fno++;
			q = ++p;
			sep = "";
		} else if (*p == '\n') {
			fno = 0;
			p++;
			sep = "\n";
		}
	}

	if (q < p)
		VTE_FORMAT(func, priv, "%s", q);
	return (0);
}

#ifdef TEST_DRIVER

#include <stdio.h>

static const char *test_vte =
    "name\tref\tcomment\n"
    "foo\t\v1\tthe foo\n"
    "bar\t\v10\tthe bars\n"
    "baz\t\v0\t\n"
    "qux\t\v-1\tno eol";

static const char *test_fmt =
    "name  ref  comment\n"
    "foo     1  the foo\n"
    "bar    10  the bars\n"
    "baz     0  \n"
    "qux    -1  no eol";

static int
test_vsb_format(void *priv, const char *fmt, ...)
{
	struct vsb *vsb;
	va_list ap;
	int res;

	CAST_OBJ_NOTNULL(vsb, priv, VSB_MAGIC);
	AN(fmt);

	va_start(ap, fmt);
	res = VSB_vprintf(vsb, fmt, ap);
	va_end(ap);

	return (res);
}

int
main(int argc, char **argv)
{
	struct vte *vte;
	struct vsb *vsb;
	int err = 0;

	(void)argc;
	(void)argv;

	vte = VTE_new(3, 20);
	AN(vte);
	AZ(VTE_cat(vte, test_vte));
	AZ(VTE_finish(vte));

	vsb = VSB_new_auto();
	AN(vsb);
	AZ(VTE_format(vte, test_vsb_format, vsb));
	AZ(VSB_finish(vsb));

	assert(vte->o_sep == 2);
	assert(vte->f_maxsz[0] == 4);
	assert(vte->f_maxsz[1] == 3);
	assert(vte->f_maxsz[2] == 8);

	if (strcmp(VSB_data(vsb), test_fmt)) {
		fprintf(stderr,
		    "Error: VTE output mismatch\n"
		    "<<<<<<<\n"
		    "%s\n"
		    "=======\n"
		    "%s\n"
		    ">>>>>>>\n"
		    "FAIL\n",
		    VSB_data(vsb), test_fmt);
		err = 1;
	}

	VSB_destroy(&vsb);
	VTE_destroy(&vte);
	if (!err)
		printf("PASS\n");
	return (err);
}

#endif
