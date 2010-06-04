/*-
 * Copyright (c) 2008-2009 Linpro AS
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
 *
 * Deal with numbers with data storage suffix scaling
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <libvarnish.h>

static const char err_miss_num[] = "Missing number";
static const char err_invalid_num[] = "Invalid number";
static const char err_abs_req[] = "Absolute number required";
static const char err_invalid_suff[] = "Invalid suffix";

const char *
str2bytes(const char *p, uintmax_t *r, uintmax_t rel)
{
	double fval;
	char *end;

	if (p == NULL || *p == '\0')
		return (err_miss_num);

	fval = strtod(p, &end);
	if (end == p || !isfinite(fval))
		return (err_invalid_num);

	if (*end == '\0') {
		*r = (uintmax_t)fval;
		return (NULL);
	}

	if (end[0] == '%' && end[1] == '\0') {
		if (rel == 0)
			return (err_abs_req);
		fval *= rel / 100.0;
	} else {
		/* accept a space before the multiplier */
		if (end[0] == ' ' && end[1] != '\0')
			++end;

		switch (end[0]) {
		case 'k': case 'K':
			fval *= (uintmax_t)1 << 10;
			++end;
			break;
		case 'm': case 'M':
			fval *= (uintmax_t)1 << 20;
			++end;
			break;
		case 'g': case 'G':
			fval *= (uintmax_t)1 << 30;
			++end;
			break;
		case 't': case 'T':
			fval *= (uintmax_t)1 << 40;
			++end;
			break;
		case 'p': case 'P':
			fval *= (uintmax_t)1 << 50;
			++end;
			break;
		case 'e': case 'E':
			fval *= (uintmax_t)1 << 60;
			++end;
			break;
		default:
			break;
		}

		/* [bB] is a generic suffix of no effect */
		if (end[0] == 'b' || end[0] == 'B')
			end++;

		if (end[0] != '\0')
			return (err_invalid_suff);
	}

	*r = (uintmax_t)round(fval);
	return (NULL);
}

#ifdef NUM_C_TEST
/* Compile with: "cc -o foo -DNUM_C_TEST -I../.. -I../../include num.c -lm" */
#include "vas.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

struct test_case {
	const char *str;
	uintmax_t rel;
	uintmax_t val;
	const char *err;
} test_cases[] = {
	{ "1",			(uintmax_t)0,	(uintmax_t)1 },
	{ "1B",			(uintmax_t)0,	(uintmax_t)1<<0 },
	{ "1 B",		(uintmax_t)0,	(uintmax_t)1<<0 },
	{ "1.3B",		(uintmax_t)0,	(uintmax_t)1 },
	{ "1.7B",		(uintmax_t)0,	(uintmax_t)2 },

	{ "1024",		(uintmax_t)0,	(uintmax_t)1024 },
	{ "1k",			(uintmax_t)0,	(uintmax_t)1<<10 },
	{ "1kB",		(uintmax_t)0,	(uintmax_t)1<<10 },
	{ "1.3kB",		(uintmax_t)0,	(uintmax_t)1331 },
	{ "1.7kB",		(uintmax_t)0,	(uintmax_t)1741 },

	{ "1048576",		(uintmax_t)0,	(uintmax_t)1048576 },
	{ "1M",			(uintmax_t)0,	(uintmax_t)1<<20 },
	{ "1MB",		(uintmax_t)0,	(uintmax_t)1<<20 },
	{ "1.3MB",		(uintmax_t)0,	(uintmax_t)1363149 },
	{ "1.7MB",		(uintmax_t)0,	(uintmax_t)1782579 },

	{ "1073741824",		(uintmax_t)0,	(uintmax_t)1073741824 },
	{ "1G",			(uintmax_t)0,	(uintmax_t)1<<30 },
	{ "1GB",		(uintmax_t)0,	(uintmax_t)1<<30 },
	{ "1.3GB",		(uintmax_t)0,	(uintmax_t)1395864371 },
	{ "1.7GB",		(uintmax_t)0,	(uintmax_t)1825361101 },

	{ "1099511627776",	(uintmax_t)0,	(uintmax_t)1099511627776ULL },
	{ "1T",			(uintmax_t)0,	(uintmax_t)1<<40 },
	{ "1TB",		(uintmax_t)0,	(uintmax_t)1<<40 },
	{ "1.3TB",		(uintmax_t)0,	(uintmax_t)1429365116109ULL },
	{ "1.7TB",		(uintmax_t)0,	(uintmax_t)1869169767219ULL },

	{ "1%",			(uintmax_t)1024,	(uintmax_t)10 },
	{ "2%",			(uintmax_t)1024,	(uintmax_t)20 },
	{ "3%",			(uintmax_t)1024,	(uintmax_t)31 },

	/* Check the error checks */
	{ "",			0,	0,	err_miss_num },
	{ "m",			0,	0,	err_invalid_num },
	{ "4%",			0,	0,	err_abs_req },
	{ "3*",			0,	0,	err_invalid_suff },

	/* TODO: add more */

	{ 0, 0, 0 },
};

int
main(int argc, char *argv[])
{
	struct test_case *tc;
	uintmax_t val;
	int ec;
	const char *e;

	(void)argc;
	for (ec = 0, tc = test_cases; tc->str; ++tc) {
		e = str2bytes(tc->str, &val, tc->rel);
		if (e != tc->err) {
			printf("%s: str2bytes(\"%s\", %ju) (%s) != (%s)\n",
			    *argv, tc->str, tc->rel, tc->err, e);
			++ec;
		} else if (e == NULL && val != tc->val) {
			printf("%s: str2bytes(\"%s\", %ju) %ju != %ju (%s)\n",
			    *argv, tc->str, tc->rel, val, tc->val, e);
			++ec;
		}
	}
	/* TODO: test invalid strings */
	if (!ec)
		printf("OK\n");
	return (ec > 0);
}
#endif
