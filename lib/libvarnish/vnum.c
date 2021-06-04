/*-
 * Copyright (c) 2008-2009 Varnish Software AS
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
 * Deal with numbers.
 *
 */

#include "config.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"

#include "vnum.h"
#include "vas.h"
#include "vct.h"

/* The distinction between these two is used internally */
static const char err_invalid_num[] = "Invalid number";
static const char err_no_digits[] = "Invalid number";

static const char err_fatnum[] = "Too many digits";

static const char err_unknown_bytes[] =
    "Unknown BYTES unit of measurement ([KMGTP][B])";

static const char err_fractional_bytes[] = "Fractional BYTES not allowed";

#define BAIL(txt)						\
	do {							\
		if (errtxt != NULL)				\
			*errtxt = (txt);			\
		errno = EINVAL;					\
		return (retval);				\
	} while (0)

/*
 * Internal function for parsing an integer with a limited
 * number of digits.
 */

static int64_t
sf_parse_int(const char **ipp, const char **errtxt, int *sign, int maxdig)
{
	int64_t retval = 0;
	int ndig = 0;

	AN(ipp);
	AN(*ipp);
	if (errtxt != NULL)
		*errtxt = NULL;
	*sign = 1;
	errno = 0;
	while (vct_isows(*(*ipp)))
		(*ipp)++;
	if(*(*ipp) == '-') {
		*sign = -1;
		(*ipp)++;
	}
	while (vct_isdigit(*(*ipp))) {
		ndig++;
		if (ndig > maxdig)
			BAIL(err_fatnum);
		retval *= 10;
		retval += *(*ipp)++ - 0x30;
	}
	if (ndig == 0)
		BAIL(err_no_digits);
	while (vct_isows(*(*ipp)))
		(*ipp)++;
	return (retval);
}

/**********************************************************************
 * Parse a RFC8941 `sf-integer`.
 *
 * If `errno` is non-zero the conversion failed.
 * If `errtxt` is provided it summarily tells why.
 * The input argument points to the first character not consumed.
 */

int64_t
SF_Parse_Integer(const char **ipp, const char **errtxt)
{
	int64_t retval;
	int sign;

	retval = sf_parse_int(ipp, errtxt, &sign, 15);
	return(retval * sign);
}

/**********************************************************************
 * Parse either a RFC8941 `sf-integer` or `sf-decimal`.
 *
 * If `errno` is non-zero the conversion failed.
 * If `errtxt` is provided it summarily tells why.
 * The input argument points to the first character not consumed.
 */

double
SF_Parse_Number(const char **ipp, int strict, const char **errtxt)
{
	double retval, scale = 1;
	int sign, ndig;

	retval = (double)sf_parse_int(ipp, errtxt, &sign, 15);
	if (strict && errno)
		return (0);
	if (*(*ipp) != '.')
		return (retval * sign);
	if (retval < VRT_DECIMAL_MIN || retval > VRT_DECIMAL_MAX)
		BAIL(err_fatnum);
	if (*errtxt == err_no_digits && (!vct_isdigit((*ipp)[1])))
		BAIL(err_no_digits);
	*errtxt = NULL;
	errno = 0;
	do {
		(*ipp)++;
		for(ndig = 0; ndig < 3; ndig++) {
			scale *= .1;
			if (!vct_isdigit(*(*ipp)))
				break;
			retval += scale * (*(*ipp)++ - 0x30);
		}
		if (strict && ndig == 0)
			BAIL(err_invalid_num);
		if (strict && vct_isdigit(*(*ipp)))
			BAIL(err_fatnum);
		while (vct_isdigit(*(*ipp)))
			(*ipp)++;
	} while (0);
	while (vct_isows(*(*ipp)))
		(*ipp)++;
	return (retval * sign);
}

/**********************************************************************
 * Parse a RFC8941 `sf-decimal`.
 *
 * If `errno` is non-zero the conversion failed.
 * If `errtxt` is provided it summarily tells why.
 * The input argument points to the first character not consumed.
 */

double
SF_Parse_Decimal(const char **ipp, int strict, const char **errtxt)
{
	double retval;

	retval = SF_Parse_Number(ipp, strict, errtxt);
	if (errno)
		return(retval);
	if (retval < VRT_DECIMAL_MIN || retval > VRT_DECIMAL_MAX)
		BAIL(err_fatnum);
	return (retval);
}

/**********************************************************************
 * Parse a "Varnish number".
 *
 * Varnish numbers are the union of RFC8941 sf-integer and sf-decimal.
 * If `errno` is non-zero the conversion failed and NAN is returned.
 */

double
VNUM(const char *p)
{
	const char *t;
	double r;

	r = SF_Parse_Number(&p, 0, &t);
	if (errno || *p != '\0')
		r = nan("");
	return (r);
}

/**********************************************************************/

vtim_dur
VNUM_duration_unit(vtim_dur r, const char *b, const char *e)
{
	double sc;

	if (e == NULL)
		e = strchr(b, '\0');

	while (b < e && vct_issp(*b))
		b++;
	if (b == e)
		return (nan(""));

	switch (*b++) {
	case 's':
		sc = 1.0;
		break;
	case 'm':
		if (b < e && *b == 's') {
			sc = 1e-3;
			b++;
		} else
			sc = 60.0;
		break;
	case 'h':
		sc = 60.0 * 60.0;
		break;
	case 'd':
		sc = 60.0 * 60.0 * 24.0;
		break;
	case 'w':
		sc = 60.0 * 60.0 * 24.0 * 7.0;
		break;
	case 'y':
		sc = 60.0 * 60.0 * 24.0 * 365.0;
		break;
	default:
		return (nan(""));
	}

	while (b < e && vct_issp(*b))
		b++;

	if (b < e)
		return (nan(""));

	return (r * sc);
}

vtim_dur
VNUM_duration(const char *p)
{
	const char *t;
	vtim_dur r;

	if (p == NULL)
		return (nan(""));

	r = SF_Parse_Number(&p, 0, &t);

	if (errno)
		return (nan(""));

	return (VNUM_duration_unit(r, p, NULL));
}

/**********************************************************************/

int64_t
VNUM_bytes_unit(double r, const char *b, const char *e, uintmax_t rel,
    const char **errtxt)
{
	double sc = 1.0, tmp;

	AN(b);
	AN(errtxt);
	errno = 0;
	if (e == NULL)
		e = strchr(b, '\0');

	while (b < e && vct_issp(*b))
		b++;
	if (b == e) {
		if (modf(r, &tmp) != 0.0) {
			*errtxt = err_fractional_bytes;
			errno = EINVAL;
		}
		return ((int64_t)trunc(sc * r));
	}

	if (rel != 0 && *b == '%') {
		r *= rel * 0.01;
		b++;
	} else {
		switch (*b) {
		case 'k': case 'K': sc = exp2(10); b++; break;
		case 'm': case 'M': sc = exp2(20); b++; break;
		case 'g': case 'G': sc = exp2(30); b++; break;
		case 't': case 'T': sc = exp2(40); b++; break;
		case 'p': case 'P': sc = exp2(50); b++; break;
		case 'b': case 'B':
			if (modf(r, &tmp) != 0.0) {
				*errtxt = err_fractional_bytes;
				errno = EINVAL;
				return (0);
			}
			break;
		default:
			*errtxt = err_unknown_bytes;
			errno = EINVAL;
			return (0);
		}
		if (b < e && (*b == 'b' || *b == 'B'))
			b++;
	}
	while (b < e && vct_issp(*b))
		b++;
	if (b < e) {
		*errtxt = err_unknown_bytes;
		errno = EINVAL;
		return (0);
	}
	return ((int64_t)trunc(sc * r));
}

const char *
VNUM_2bytes(const char *p, uintmax_t *r, uintmax_t rel)
{
	double fval;
	const char *errtxt;

	if (p == NULL || *p == '\0')
		return (err_invalid_num);

	fval = SF_Parse_Number(&p, 1, &errtxt);
	if (errno)
		return(errtxt);
	if (fval < 0)
		return(err_invalid_num);

	fval = VNUM_bytes_unit(fval, p, NULL, rel, &errtxt);
	if (errno)
		return (errtxt);
	*r = (uintmax_t)round(fval);
	return (NULL);
}

#ifdef NUM_C_TEST
/*
 * Compile with:
 *     cc -o foo -DNUM_C_TEST -DTEST_VERBOSE \
 *	   -I../.. -I../../include vnum.c vas.c vct.c -lm
 */

static const struct test_sf_parse_int {
	const char *input;
	int maxdig;
	int64_t retval;
	int consumed;
	int sign;
	const char *errtxt;
} test_sf_parse_int[] = {
	{ "1234",	3,  123, 3,  1, err_fatnum },
	{ "1234",	4, 1234, 4,  1, NULL },
	{ "1234",	5, 1234, 4,  1, NULL },
	{ "-",		5,    0, 1, -1, err_no_digits },
	{ "  ",		5,    0, 2,  1, err_no_digits },
	{ "-1234",	3,  123, 4, -1, err_fatnum },
	{ "-1234",	4, 1234, 5, -1, NULL },
	{ "-1234",	5, 1234, 5, -1, NULL },
	{ " -1234",	5, 1234, 6, -1, NULL },
	{ " -1234 ",	5, 1234, 7, -1, NULL },
	{ " -12 34 ",	5,   12, 5, -1, NULL },
	{ " - 12 34 ",	5,    0, 2, -1, err_no_digits },
	{ NULL},
};

static const struct test_sf_parse_number {
	const char *input;
	int strict;
	double retval;
	int consumed;
	const char *errtxt;
} test_sf_parse_number[] = {
	{ "1234",		1,          1234.000,  4, NULL },
	{ " 1234",		1,          1234.000,  5, NULL },
	{ " 1234 ",		1,          1234.000,  6, NULL },
	{ " 1234. ",		1,          1234.000,  6, err_invalid_num },
	{ " 123456789012.0 ",	1,  123456789012.000, 16, NULL },
	{ " 1234567890123.0 ",	1, 1234567890123.000, 14, err_fatnum },
	{ " 123456789012.123 ",	1,  123456789012.123, 18, NULL },
	{ " 123456789012.1234 ",1,  123456789012.123, 17, err_fatnum },
	{ " -0.123456 ",	1,		.123,  7, err_fatnum },
	{ " -.123456 ",		1,	       0.,     2, err_no_digits },
	{ " .123456 ",		1,             0.,     1, err_no_digits },
	{ " 0. ",		1,             0.,     3, err_invalid_num },
	{ " .0 ",		1,             0.,     1, err_no_digits },

	{ " 123456789012.1234 ",0,  123456789012.123, 19, NULL },
	{ " -0.123456 ",	0,	       -.123, 11, NULL },
	{ " -.123456 ",		0,	       -.123, 10, NULL },
	{ " .123456 ",		0,		.123,  9, NULL },
	{ " 0. ",		0,             0.,     4, NULL },
	{ " .0 ",		0,             0.,     4, NULL },
	{ " -0. ",		0,            -0.,     5, NULL },
	{ " -.0 ",		0,            -0.,     5, NULL },
	{ " - ",		0,            -0.,     2, err_no_digits },
	{ " -. ",		0,             0.,     2, err_no_digits },
	{ " . ",		0,             0.,     1, err_no_digits },
	{ NULL},
};

static struct test_case {
	const char *str;
	uintmax_t rel;
	uintmax_t val;
	const char *err;
} test_cases[] = {
	{ "1",			(uintmax_t)0,	(uintmax_t)1 },
	{ "1B",			(uintmax_t)0,	(uintmax_t)1<<0 },
	{ "1 B",		(uintmax_t)0,	(uintmax_t)1<<0 },
	{ "1.3B",		0,	0,	err_fractional_bytes },
	{ "1.7B",		0,	0,	err_fractional_bytes },

	{ "1024",		(uintmax_t)0,	(uintmax_t)1024 },
	{ "1k",			(uintmax_t)0,	(uintmax_t)1<<10 },
	{ "1kB",		(uintmax_t)0,	(uintmax_t)1<<10 },
	{ "0.75kB",		(uintmax_t)0,	(uintmax_t)768 },
	{ "1.3kB",		(uintmax_t)0,	(uintmax_t)1331 },
	{ "1.70kB",		(uintmax_t)0,	(uintmax_t)1740 },

	{ "1048576",		(uintmax_t)0,	(uintmax_t)1048576 },
	{ "1M",			(uintmax_t)0,	(uintmax_t)1<<20 },
	{ "1MB",		(uintmax_t)0,	(uintmax_t)1<<20 },
	{ "1.3MB",		(uintmax_t)0,	(uintmax_t)1363148 },
	{ "1.700MB",		(uintmax_t)0,	(uintmax_t)1782579 },

	{ "1073741824",		(uintmax_t)0,	(uintmax_t)1073741824 },
	{ "1G",			(uintmax_t)0,	(uintmax_t)1<<30 },
	{ "1GB",		(uintmax_t)0,	(uintmax_t)1<<30 },
	{ "1.3GB",		(uintmax_t)0,	(uintmax_t)1395864371 },
	{ "1.7GB",		(uintmax_t)0,	(uintmax_t)1825361100 },

	{ "1099511627776",	(uintmax_t)0,	(uintmax_t)1099511627776ULL },
	{ "1T",			(uintmax_t)0,	(uintmax_t)1<<40 },
	{ "1TB",		(uintmax_t)0,	(uintmax_t)1<<40 },
	{ "1.3TB",		(uintmax_t)0,	(uintmax_t)1429365116108ULL },
	{ "1.7\tTB",		(uintmax_t)0,	(uintmax_t)1869169767219ULL },

	{ "999999999999999",	(uintmax_t)0,	(uintmax_t)999999999999999ULL},

	{ "1125899906842624",	0,	0,	err_fatnum },
	{ "1P\t",		(uintmax_t)0,	(uintmax_t)1125899906842624ULL},
	{ "1PB ",		(uintmax_t)0,	(uintmax_t)1125899906842624ULL},
	{ "1.3 PB",		(uintmax_t)0,	(uintmax_t)1463669878895411ULL},

	{ "1.5%",		(uintmax_t)1024,	(uintmax_t)15 },
	{ "1.501%",		(uintmax_t)1024,	(uintmax_t)15 },
	{ "2%",			(uintmax_t)1024,	(uintmax_t)20 },
	{ "3%",			(uintmax_t)1024,	(uintmax_t)30 },

	/* Check the error checks */
	{ "",			0,	0,	err_invalid_num },
	{ "-1",			0,	0,	err_invalid_num },
	{ "1.3",		0,	0,	err_fractional_bytes},
	{ "1.5011%",		0,	0,	err_fatnum },
	{ "-",			0,	0,	err_no_digits },
	{ "m",			0,	0,	err_no_digits },
	{ "4%",			0,	0,	err_unknown_bytes },
	{ "3*",			0,	0,	err_unknown_bytes },

	/* TODO: add more */

	{ 0, 0, 0 },
};

static const char *vec[] = {
	" 1",
	" 12",
	" 12.",
	" 12.3",
	" 12.34",
	"N12.34e-3",
	"N12.34e3",
	"N12.34e+3",
	"N+12.34e-3",
	"N-12.34e3",
	"N.",
	"N.12.",
	"N12..",
	"N12.,",
	"N12e,",
	"N12e+,",
	"N12ee,",
	"N1..2",
	"NA",
	"N1A",
	"Ne-3",
	NULL
};

int
main(int argc, char *argv[])
{
	int ec = 0;
	struct test_case *tc;
	uintmax_t val;
	const char **p;
	const char *e;
	double d1, d2;
	const struct test_sf_parse_int *tspi;
	const struct test_sf_parse_number *tspn;
	int64_t i64;
	volatile double dbl;
	int sign, consumed;
	const char *errtxt;
	const char *input;
	char buf1[30];
	char buf2[30];

	(void)argc;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	for (tspi = test_sf_parse_int; tspi->input != NULL; tspi++) {
		errtxt = "(unset)";
		input = tspi->input;
		i64 = sf_parse_int(&input, &errtxt, &sign, tspi->maxdig);
		consumed = input - tspi->input;
		if (i64 != tspi->retval ||
		    sign != tspi->sign ||
		    consumed != tspi->consumed ||
		    errtxt != tspi->errtxt) {
			ec++;
			printf("sf_parse_int(%s, maxdig=%d) failed\n",
			    tspi->input, tspi->maxdig);
#ifdef TEST_VERBOSE
			printf("    retval\texpected %jd\tgot %jd\n",
			    (intmax_t)tspi->retval, (intmax_t)i64);
			printf("    sign\texpected %d\tgot %d\n",
			    tspi->sign, sign);
			printf("    consumed\texpected %d\tgot %d\n",
			    tspi->consumed, consumed);
			printf("    errtxt\texpected %p\tgot %p\n",
			    tspi->errtxt, errtxt);
			printf("    errtxt\texpected %s\tgot %s\n",
			    tspi->errtxt, errtxt);
#endif
		}
	}

	for (tspn = test_sf_parse_number; tspn->input != NULL; tspn++) {
		errtxt = "(unset)";
		input = tspn->input;
		dbl = SF_Parse_Number(&input, tspn->strict, &errtxt);
		consumed = input - tspn->input;
		bprintf(buf1, "%.4f", dbl);
		bprintf(buf2, "%.4f", tspn->retval);
		if (strcmp(buf1, buf2) ||
		    consumed != tspn->consumed ||
		    errtxt != tspn->errtxt) {
			ec++;
			printf("sf_parse_number(%s, strict=%d) failed\n",
			    tspn->input, tspn->strict);
#ifdef TEST_VERBOSE
			printf("    retval\texpected %.4f\tgot %.4f\t(%e)\n",
			    tspn->retval, dbl, dbl - tspn->retval);
			printf("    retval\texpected %a\tgot %a\n",
			    tspn->retval, dbl);
			printf("    retval\texpected %s\tgot %s\n",
			    buf2, buf1);
			printf("    retval\tdelta %e\n",
			    dbl - tspn->retval);
			printf("    consumed\texpected %d\tgot %d\n",
			    tspn->consumed, consumed);
			printf("    errtxt\texpected %p\tgot %p\n",
			    tspn->errtxt, errtxt);
			printf("    errtxt\texpected %s\tgot %s\n",
			    tspn->errtxt, errtxt);
#endif
		}
	}

	for (p = vec; *p != NULL; p++) {
		e = *p;
		d1 = VNUM(e + 1);
		if (*e == 'N') {
			if (!isnan(d1)) {
				ec++;
				printf("VNUM(%s) not NAN (%g)\n", e + 1, d1);
			}
		} else {
			d2 = atof(e + 1);
			if (isnan(d1)) {
				printf("VNUM(%s) is NAN (%g)\n", e + 1, d1);
				ec++;
			} else if (fabs((d1 - d2) / d2) > 1e-15) {
				printf("VNUM(%s) differs from atof() (%g)\n",
				    e + 1, d1);
				ec++;
			}
		}
	}

	for (tc = test_cases; tc->str; ++tc) {
		e = VNUM_2bytes(tc->str, &val, tc->rel);
		if (e != NULL)
			val = 0;
		if (e == tc->err && val == tc->val)
			continue;
		++ec;
		printf("%s: VNUM_2bytes(\"%s\", %ju)\n",
		   *argv, tc->str, tc->rel);
		printf("\tExpected:\tstatus %s - value %ju\n",
		    tc->err ? tc->err : "Success", tc->val);
		printf("\tGot:\t\tstatus %s - value %ju\n",
		    e ? e : "Success", val);
	}
	if (!isnan(VNUM_duration(NULL))) {
		printf("%s: VNUM_Duration(NULL) fail\n", *argv);
		++ec;
	}
	d1 = VNUM_duration(" 365.24219d ");
	if (d1 != 31556908.8) {
		printf("%s: VNUM_Duration() wrong, %.3f delta = %e\n",
		    *argv, d1, d1 - 31556908.8);
		++ec;
	}
	/* TODO: test invalid strings */
	if (!ec)
		printf("OK\n");
	return (ec > 0);
}
#endif
