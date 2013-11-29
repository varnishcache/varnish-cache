/*-
 * Copyright (c) 2013 Varnish Software AS
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
 * Test what VSL_Name2Tag and VSL_Glob2Tags produces
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vapi/vsl.h"

static void
cb(int tag, void *priv)
{
	(void)priv;

	printf("\t%d (%s)\n", tag, VSL_tags[tag]);
}

int
main(int argc, char * const *argv)
{
	int i;

	if (argc != 2) {
		fprintf(stderr, "vsl_glob_test <tagname/glob>\n");
		exit(1);
	}

	i = VSL_Name2Tag(argv[1], -1);
	printf("VSL_Name2Tag returns %d", i);
	if (i >= 0)
		printf(" (%s)", VSL_tags[i]);
	printf("\n");

	printf("VSL_Glob2Tags:\n");
	i = VSL_Glob2Tags(argv[1], -1, cb, NULL);
	printf("VSL_Glob2Tags returns %d\n", i);

	printf("VSL_List2Tags:\n");
	i = VSL_List2Tags(argv[1], -1, cb, NULL);
	printf("VSL_List2Tags returns %d\n", i);

	return (0);
}
