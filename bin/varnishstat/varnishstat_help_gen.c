/*-
 * Copyright (c) 2020 Varnish Software AS
 * All rights reserved.
 *
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
 */

#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include <vdef.h>
#include <vas.h>
#include <vsb.h>

static const char help[] = "\n\n"
#define BINDING_KEY(key, name, next) "<" name ">" next
#define BINDING(name, desc) "\n\n" desc "\n\n"
#include "varnishstat_bindings.h"
;

int
main(void)
{
	struct vsb *vsb;
	const char *p, *n;
	unsigned u;

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_cat(vsb,
	    "/*\n"
	    " * NB:  This file is machine generated, DO NOT EDIT!\n"
	    " *\n"
	    " * Edit varnishstat_bindings.h and run make instead\n"
	    " */\n"
	    "\n"
	    "#include <stddef.h>\n"
	    "#include \"vdef.h\"\n"
	    "#include \"varnishstat.h\"\n"
	    "\n"
	    "const char *const bindings_help[] = {\n");

	n = help;
	u = 0;
	do {
		p = n + 1;
		n = strchr(p, '\n');
		if (n != NULL && n > p) {
			VSB_putc(vsb, '\t');
			AZ(VSB_quote(vsb, p, (int)(n - p), VSB_QUOTE_CSTR));
			VSB_cat(vsb, ",\n");
			u++;
		}
	} while (n != NULL);

	VSB_printf(vsb,
	    "\tNULL\n"
	    "};\n"
	    "\n"
	    "const int bindings_help_len = %u;\n", u);
	AZ(VSB_finish(vsb));
	AZ(VSB_tofile(vsb, STDOUT_FILENO));
	VSB_destroy(&vsb);
	return (0);
}
