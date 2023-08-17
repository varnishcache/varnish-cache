/*-
 * Copyright (c) 2019 Varnish Software AS
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
 *
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> /* for MUSL (ssize_t) */

#include "vdef.h"
#include "vqueue.h"
#include "miniobj.h"

#include "vas.h"
#include "vcli_serve.h"
#include "vsb.h"
#include "vte.h"

#define MAXCOL 10

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
	int		f_maxsz[];	/* maximum size per field */
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
		} else {
			vte->f_sz++;
			vte->l_sz++;
		}
	}

	vte->c_off = len;
	return (0);
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

void
VCLI_VTE(struct cli *cli, struct vsb **src, int width)
{
	int w_col[MAXCOL];
	int n_col = 0;
	int w_ln = 0;
	int cc = 0;
	int wc = 0;
	int wl = 0;
	int nsp;
	const char *p;
	char *s;

	AN(cli);
	AN(src);
	AN(*src);
	AZ(VSB_finish(*src));
	if (VSB_len(*src) == 0) {
		VSB_destroy(src);
		return;
	}
	s = VSB_data(*src);
	AN(s);
	memset(w_col, 0, sizeof w_col);
	for (p = s; *p ; p++) {
		if (wl == 0 && *p == ' ') {
			while (p[1] != '\0' && *p != '\n')
				p++;
			continue;
		}
		if (*p == '\t' || *p == '\n') {
			if (wc > w_col[cc])
				w_col[cc] = wc;
			cc++;
			assert(cc < MAXCOL);
			wc = 0;
		}
		if (*p == '\n') {
			n_col = vmax(n_col, cc);
			w_ln = vmax(w_ln, wl);
			cc = 0;
			wc = 0;
			wl = 0;
		} else {
			wc++;
			wl++;
		}
	}

	if (n_col == 0)
		return;
	AN(n_col);

	nsp = vlimit_t(int, (width - (w_ln)) / n_col, 1, 3);

	cc = 0;
	wc = 0;
	for (p = s; *p ; p++) {
		if (wc == 0 && cc == 0 && *p == ' ') {
			while (p[1] != '\0') {
				VCLI_Out(cli, "%c", *p);
				if (*p == '\n')
					break;
				p++;
			}
			continue;
		}
		if (*p == '\t') {
			while (wc++ < w_col[cc] + nsp)
				VCLI_Out(cli, " ");
			cc++;
			wc = 0;
		} else if (*p == '\n') {
			VCLI_Out(cli, "%c", *p);
			cc = 0;
			wc = 0;
		} else {
			VCLI_Out(cli, "%c", *p);
			wc++;
		}
	}
	VSB_destroy(src);
}

