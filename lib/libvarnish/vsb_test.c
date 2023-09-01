/*-
 * Copyright (c) 2020-2021 Varnish Software AS
 * All rights reserved.
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

#ifdef VSB_TEST

#include <stdio.h>
#include <string.h>

#include "vdef.h"
#include "vas.h"
#include "vsb.h"

struct tc {
	int		how;
	int		inlen;
	const char	*in;
	const char	*out;
};

static struct tc tcs[] = {
	{
		VSB_QUOTE_HEX,
		5, "\x00\n\x7e\x7f\xff",
		"PFX0x000a7e7fff"
	},
	{
		VSB_QUOTE_HEX,
		5, "\0\0\0\0\0",
		"PFX0x0...0"
	},
	{
		VSB_QUOTE_HEX | VSB_QUOTE_NONL,
		5, "\x00\n\x7e\x7f\xff",
		"PFX0x000a7e7fff\n"
	},
	{
		VSB_QUOTE_ESCHEX,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\x00\\n~\\x7f\\xff",
	},
	{
		0,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\000\\n~\\177\\377",
	},
	{
		VSB_QUOTE_UNSAFE,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\000\nPFX~\\177\\377\n",
	},
	{
		VSB_QUOTE_UNSAFE,
		-1, "\n\"\\\t",
		"PFX\nPFX\"\\\\t\n"
	},
	{
		VSB_QUOTE_CSTR | VSB_QUOTE_ESCHEX,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\"\\x00\\n\"\nPFX\"~\\x7f\\xff\"",
	},
	/*
	 * VSB_QUOTE_VJSON puts >0x7e literally
	 */
	{
		VSB_QUOTE_VJSON,
		4, "\xf0\x9f\x90\xb0",
		"PFX\xf0\x9f\x90\xb0",
	},
	/*
	 * VSB_QUOTE_VJSON encodes <0x20 as \u00XX, which looks like
	 * unicode codepoints in the JSON standard, but are just
	 * individual bytes
	 */
	{
		VSB_QUOTE_VJSON,
		5, "\"\x01\x02\x03\x04",
		"PFX\\\"\\u0001\\u0002\\u0003\\u0004",
	},
	{
		VSB_QUOTE_VJSON,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\u0000\\n~\x7f\xff",
	},
	{
		VSB_QUOTE_VJSON | VSB_QUOTE_NONL,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\u0000\\n~\x7f\xff\n",
	},
	{
		VSB_QUOTE_CSTR,
		-1, "",
		"PFX\"\""
	},
	{
		VSB_QUOTE_CSTR,
		-1, "?",
		"PFX\"\\?\""
	},
	{
		VSB_QUOTE_NONL,
		-1, "\n\t",
		"PFX\nPFX\\t\n"
	},
	{
		0, -1, NULL, NULL
	}
};

int
main(int argc, char *argv[])
{
	int err = 0;
	struct tc *tc;
	struct vsb *vsb;
	struct vsb *vsbo;

	(void)argc;
	(void)argv;
	vsb = VSB_new_auto();
	AN(vsb);
	vsbo = VSB_new_auto();
	AN(vsbo);

	for (tc = tcs; tc->in; tc++) {
		AZ(VSB_quote_pfx(vsb, "PFX", tc->in, tc->inlen, tc->how, NULL));
		assert(VSB_finish(vsb) == 0);

		VSB_clear(vsbo);
		VSB_printf(vsbo, "0x%02x: ", tc->how);
		AZ(VSB_quote(vsbo, tc->in, tc->inlen, VSB_QUOTE_HEX));
		VSB_cat(vsbo, " -> ");
		AZ(VSB_quote(vsbo, VSB_data(vsb), -1, VSB_QUOTE_HEX));
		VSB_cat(vsbo, " (");
		AZ(VSB_quote(vsbo, tc->out, -1, VSB_QUOTE_ESCHEX));
		VSB_cat(vsbo, ")");
		if (strcmp(VSB_data(vsb), tc->out)) {
			VSB_cat(vsbo, "\nShould have been:\n\t");
			AZ(VSB_quote(vsbo, tc->out, -1, VSB_QUOTE_HEX));
			VSB_cat(vsbo, "\nThat's:\n\t");
			AZ(VSB_quote(vsbo, VSB_data(vsb), -1, VSB_QUOTE_ESCHEX));
			VSB_cat(vsbo, "\nvs:\n\t");
			AZ(VSB_quote(vsbo, tc->out, -1, VSB_QUOTE_ESCHEX));
			VSB_printf(vsbo, "\nFlags 0x%02x = ", tc->how);
			if (!tc->how)
				VSB_cat(vsbo, "\n\t0");
			if (tc->how & VSB_QUOTE_NONL)
				VSB_cat(vsbo, "\n\tVSB_QUOTE_NONL");
			if (tc->how & VSB_QUOTE_VJSON)
				VSB_cat(vsbo, "\n\tVSB_QUOTE_VJSON");
			if (tc->how & VSB_QUOTE_HEX)
				VSB_cat(vsbo, "\n\tVSB_QUOTE_HEX");
			if (tc->how & VSB_QUOTE_CSTR)
				VSB_cat(vsbo, "\n\tVSB_QUOTE_CSTR");
			if (tc->how & VSB_QUOTE_UNSAFE)
				VSB_cat(vsbo, "\n\tVSB_QUOTE_UNSAFE");
			if (tc->how & VSB_QUOTE_ESCHEX)
				VSB_cat(vsbo, "\n\tVSB_QUOTE_ESCHEX");
			VSB_cat(vsbo, "\n\n");
			err = 1;
		}
		AZ(VSB_finish(vsbo));
		printf("%s\n", VSB_data(vsbo));
		VSB_clear(vsb);
	}
	VSB_destroy(&vsb);
	VSB_destroy(&vsbo);
	printf("error is %i\n", err);
	return (err);
}

#endif /* VSB_TEST */
