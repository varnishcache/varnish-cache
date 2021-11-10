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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h> /* for MUSL (ssize_t) */

#include "vdef.h"
#include "vqueue.h"

#include "vas.h"
#include "vcli_serve.h"
#include "vsb.h"

#define MAXCOL 10

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

