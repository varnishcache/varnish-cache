/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "vapi/voptget.h"

static void
print_nobrackets(const char *s)
{
	const char *e;

	/* Remove whitespace */
	while (isspace(*s))
		s++;
	e = s + strlen(s);
	while (e > s && isspace(e[-1]))
		e--;

	/* Remove outer layer brackets if present */
	if (e > s && *s == '[' && e[-1] == ']') {
		s++;
		e--;
	}

	printf("%.*s", (int)(e - s), s);
}

static void
print_tabbed(const char *string, int tabs)
{
	int i;
	const char *c;

	for (c = string; *c; c++) {
		if (c == string || *(c - 1) == '\n')
			for (i = 0; i < tabs; i++)
				printf("\t");
		printf("%c", *c);
	}
}

static void
print_opt(const struct vopt_list *opt)
{
	print_nobrackets(opt->synopsis);
	printf("\n\n");
	print_tabbed(opt->ldesc, 1);
	printf("\n\n");
}

static void
usage(void)
{
	fprintf(stderr, "Usage: opt2rst {synopsis|options}\n");
	exit(1);
}

int
main(int argc, char * const *argv)
{
	int i;

	if (argc != 2)
		usage();
	if (!strcmp(argv[1], "synopsis"))
		printf(".. |synopsis| replace:: %s\n", vopt_synopsis);
	else if (!strcmp(argv[1], "options"))
		for (i = 0; i < vopt_list_n; i++)
			print_opt(&vopt_list[i]);
	else
		usage();

	return (0);
}
