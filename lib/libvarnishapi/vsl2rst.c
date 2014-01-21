/*-
 * Copyright (c) 2011-2014 Varnish Software AS
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
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vapi/vsl.h"

struct SLT {
	unsigned	tag;
	unsigned	flags;
	const char	*name;
	const char	*sdesc;
	const char	*ldesc;
};

static struct SLT tags[SLT__MAX] = {
#define SLTM(name, flags, sdesc, ldesc)				\
	[SLT_##name] = { SLT_##name, flags, #name, sdesc, ldesc },
#include "tbl/vsl_tags.h"
#undef SLTM
};

static int
ptag_cmp(const void *va, const void *vb)
{
	const struct SLT *a, *b;

	a = *(const struct SLT * const *)va;
	b = *(const struct SLT * const *)vb;
	if (a->name == NULL && b->name != NULL)
		return (1);
	else if (a->name != NULL && b->name == NULL)
		return (-1);
	else if (a->name == NULL && b->name == NULL)
		return (0);
	return (strcmp(a->name, b->name));
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

int
main(int argc, char *argv[])
{
	int i;
	struct SLT *ptags[SLT__MAX];

	(void)argc;
	(void)argv;

	for (i = 0; i < SLT__MAX; i++)
		ptags[i] = &tags[i];

	qsort(ptags, SLT__MAX, sizeof *ptags, ptag_cmp);

	for (i = 0; i < SLT__MAX; i++) {
		if (ptags[i]->name == NULL || !strcmp(ptags[i]->name, ""))
			continue;
		if (ptags[i]->flags & SLT_F_UNUSED)
			continue;
		printf("%s", ptags[i]->name);
		if (ptags[i]->sdesc != NULL && strcmp(ptags[i]->sdesc, ""))
			printf(" - %s", ptags[i]->sdesc);
		printf("\n");
		if (ptags[i]->ldesc != NULL && strcmp(ptags[i]->ldesc, "")) {
			print_tabbed(ptags[i]->ldesc, 1);
		}
		printf("\n\n");
	}

	return (0);
}
