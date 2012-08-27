/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "common/params.h"
#include "mgt/mgt.h"
#include "mgt/mgt_param.h"

#include "vav.h"
#include "vcli.h"
#include "vcli_common.h"
#include "vcli_priv.h"

#include "vapi/vsl_int.h"

static const char * const VSL_tags[256] = {
#  define SLTM(foo,sdesc,ldesc)       [SLT_##foo] = #foo,
#  include "tbl/vsl_tags.h"
#  undef SLTM
};

enum bit_do {BSET, BCLR, BTST};

static int
vsl_bit(unsigned no, enum bit_do act)
{
	volatile uint8_t *bm = &mgt_param.vsl_mask[0];
	uint8_t b;

	assert(no < (unsigned)SLT_Reserved);

	bm += (no >> 3);
	b = (0x80 >> (no & 7));
	if (act == BSET)
		*bm |= b;
	else if (act == BCLR)
		*bm &= ~b;
	return (*bm & b);
}

static void
tweak_vsl_mask(struct cli *cli, const struct parspec *par, const char *arg)
{
	int i, n;
	unsigned j;
	const char *s;
	char **av;
	(void)par;

	if (arg != NULL) {
		if (!strcmp(arg, "default")) {
			(void)vsl_bit(SLT_VCL_trace, BSET);
			(void)vsl_bit(SLT_WorkThread, BSET);
			(void)vsl_bit(SLT_Hash, BSET);
		} else if (*arg != 0) {
			av = VAV_Parse(arg, &n, ARGV_COMMA);
			if (av[0] != NULL) {
				VCLI_Out(cli, "Cannot parse: %s\n", av[0]);
				VCLI_SetResult(cli, CLIS_PARAM);
				return;
			}
			for (i = 1; av[i] != NULL; i++) {
				s = av[i];
				if (*s != '-' && *s != '+') {
					VCLI_Out(cli,
					    "Missing '+' or '-' (%s)\n", s);
					VCLI_SetResult(cli, CLIS_PARAM);
					VAV_Free(av);
					return;
				}
				for (j = 0; j < 256; j++)
					if (VSL_tags[j] != NULL &&
					    !strcasecmp(s + 1, VSL_tags[j]))
						break;
				if (j == 256) {
					VCLI_Out(cli,
					    "Unknown VSL tag (%s)\n", s);
					VCLI_SetResult(cli, CLIS_PARAM);
					VAV_Free(av);
					return;
				}
				if (s[0] == '+')
					(void)vsl_bit(j, BCLR);
				else
					(void)vsl_bit(j, BSET);
			}
			VAV_Free(av);
		}
	} else {
		s = "";
		for (j = 0; j < (unsigned)SLT_Reserved; j++) {
			if (vsl_bit(j, BTST)) {
				VCLI_Out(cli, "%s-%s", s, VSL_tags[j]);
				s = ",";
			}
		}
		if (*s == '\0')
			VCLI_Out(cli, "(all enabled)");
	}
}

const struct parspec VSL_parspec[] = {
	{ "vsl_mask", tweak_vsl_mask, NULL, 0, 0,
		"Mask individual VSL messages from being logged",
		0, "default", "" },
	{ NULL, NULL, NULL }
};
