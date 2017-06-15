/*-
 * Copyright (c) 2008-2017 Varnish Software AS
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

#include <sys/types.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "vct.h"
#include "vnum.h"
#include "vre.h"
#include "vtc.h"

struct vsb *
vtc_hex_to_bin(struct vtclog *vl, const char *arg)
{
	struct vsb *vsb;
	unsigned sh = 4;
	unsigned c, b = 0;

	vsb = VSB_new_auto();
	AN(vsb);
	for (; *arg != '\0'; arg++) {
		if (vct_issp(*arg) || *arg == '\n')
			continue;
		c = (uint8_t)*arg;
		if (c >= '0' && c <= '9')
			b |= (c - 48U) << sh;
		else if (c >= 'A' && c <= 'F')
			b |= (c - 55U) << sh;
		else if (c >= 'a' && c <= 'f')
			b |= (c - 87U) << sh;
		else
			vtc_fatal(vl,"Illegal hex string");
		sh = 4 - sh;
		if (sh == 4) {
			VSB_putc(vsb, b);
			b = 0;
		}
	}
	if (sh != 4)
		VSB_putc(vsb, b);
	AZ(VSB_finish(vsb));
	return (vsb);
}

void
vtc_expect(struct vtclog *vl,
    const char *olhs, const char *lhs,
    const char *cmp,
    const char *orhs, const char *rhs)
{
	vre_t *vre;
	const char *error;
	int erroroffset;
	int i, j, retval = -1;
	double fl, fr;

	j = lhs == NULL || rhs == NULL;
	if (lhs == NULL)
		lhs = "<undef>";
	if (rhs == NULL)
		rhs = "<undef>";

	if (!strcmp(cmp, "~") || !strcmp(cmp, "!~")) {
		vre = VRE_compile(rhs, 0, &error, &erroroffset);
		if (vre == NULL)
			vtc_fatal(vl, "REGEXP error: %s (@%d) (%s)",
			    error, erroroffset, rhs);
		i = VRE_exec(vre, lhs, strlen(lhs), 0, 0, NULL, 0, 0);
		retval = (i >= 0 && *cmp == '~') || (i < 0 && *cmp == '!');
		VRE_free(&vre);
	} else if (!strcmp(cmp, "==")) {
		retval = strcmp(lhs, rhs) == 0;
	} else if (!strcmp(cmp, "!=")) {
		retval = strcmp(lhs, rhs) != 0;
	} else if (j) {
		// fail inequality comparisons if either side is undef'ed
		retval = 0;
	} else {
		fl = VNUM(lhs);
		fr = VNUM(rhs);
		if (!strcmp(cmp, "<"))
			retval = isless(fl, fr);
		else if (!strcmp(cmp, ">"))
			retval = isgreater(fl, fr);
		else if (!strcmp(cmp, "<="))
			retval = islessequal(fl, fr);
		else if (!strcmp(cmp, ">="))
			retval = isgreaterequal(fl, fr);
	}

	if (retval == -1)
		vtc_fatal(vl,
		    "EXPECT %s (%s) %s %s (%s) test not implemented",
		    olhs, lhs, cmp, orhs, rhs);
	else if (retval == 0)
		vtc_fatal(vl, "EXPECT %s (%s) %s \"%s\" failed",
		    olhs, lhs, cmp, rhs);
	else
		vtc_log(vl, 4, "EXPECT %s (%s) %s \"%s\" match",
		    olhs, lhs, cmp, rhs);
}
