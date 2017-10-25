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

#ifndef __FLEXELINT__

#include <fnmatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vapi/vsl.h"
#include "vdef.h"
#include "vas.h"

#ifndef FNM_CASEFOLD
#  define FNM_CASEFOLD FNM_IGNORECASE
#endif

static void
cb(int tag, void *priv)
{
	printf("\t%d (%s)\n", tag, VSL_tags[tag]);
	if (priv != NULL)
		assert(!fnmatch(priv, VSL_tags[tag], FNM_CASEFOLD));
}

static int
tst_one_glob(const char *p)
{
	int i;

	printf("Test <%s>\n", p);
	i = VSL_Glob2Tags(p, -1, cb, TRUST_ME(p));
	printf("  -> %d\n", i);
	return (i);
}

int
main(int argc, char * const *argv)
{
	int i, j;

	if (argc == 1) {
		i = tst_one_glob("Req*");
		assert(i == 10);
		j = tst_one_glob("reQ*");
		assert(i == j);
		assert(tst_one_glob("*Header") > 0);
		assert(tst_one_glob("Req*eader") == 1);
		assert(tst_one_glob("xyz*y") == -1);
		assert(tst_one_glob("*") > 0);
		assert(tst_one_glob("a*b*c") == -3);
		assert(tst_one_glob("**") == -3);
		assert(tst_one_glob("_") == -1);
		assert(tst_one_glob("") == -1);
		assert(VSL_Glob2Tags("", 0, cb, NULL) == -1);

		assert(VSL_List2Tags("Req*,Resp*",-1,cb,NULL) > 0);
		assert(VSL_List2Tags(",,,",-1,cb,NULL) == -1);
		assert(VSL_List2Tags("xyzzy,,xyzzy",-1,cb,NULL) == -1);
		return (0);
	}
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

#endif // __FLEXELINT__
