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
 * Deal with numbers with data storage suffix scaling
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

#if ! defined(HAVE_UINT128_T) && defined(HAVE___UINT128_T)
#define uint128_t __uint128_t
#define HAVE_UINT128_T 1
#endif

#define P10_LIM 19
const int64_t p10[P10_LIM] = {
	1,
	10,
	100,
	1000,
	10000,
	100000,
	1000000,
	10000000,
	100000000,
	1000000000,
	10000000000,
	100000000000,
	1000000000000,
	10000000000000,
	100000000000000,
	1000000000000000,
	10000000000000000,
	100000000000000000,
	1000000000000000000
};

static const char err_miss_num[] = "Missing number";
static const char err_invalid_num[] = "Invalid number";
static const char err_invalid_suff[] = "Invalid suffix";
static const char err_precision[] = "Number/Unit can not be converted with"
	" adequate precision";

/**********************************************************************
 * Convert (all of!) a string to a floating point number, and if we can
 * not, return NAN.
 */

double
VNUMpfx(const char *p, const char **t)
{
	double m = 0., ee = 0.;
	double ms = 1.0;
	double es = 1.0, e = 1.0, ne = 0.0;

	AN(p);
	AN(t);
	*t = NULL;
	while (vct_issp(*p))
		p++;

	if (*p == '-' || *p == '+')
		ms = (*p++ == '-' ? -1.0 : 1.0);

	for (; *p != '\0'; p++) {
		if (vct_isdigit(*p)) {
			m *= 10.;
			m += *p - '0';
			e = ne;
			if (e)
				ne = e - 1.0;
		} else if (*p == '.' && ne == 0.0) {
			ne = -1.0;
		} else
			break;
	}
	if (e > 0.0)
		return (nan(""));		// No digits
	if (*p == 'e' || *p == 'E') {
		p++;
		if (*p == '-' || *p == '+')
			es = (*p++ == '-' ? -1.0 : 1.0);
		if (!vct_isdigit(*p))
			return (nan(""));
		for (; vct_isdigit(*p); p++)
			ee = ee * 10. + *p - '0';
	}
	while (vct_issp(*p))
		p++;
	if (*p != '\0')
		*t = p;
	return (ms * m * pow(10., e + es * ee));
}

int64_t
VNUMpfxint(const char *p, const char **t, int *scp)
{
	int sc, oflow = 0, neg = 0;
	int64_t d, r;
	int8_t m, c;

	AN(p);
	AN(t);
	*t = NULL;

	if (scp != NULL)
		sc = -1;
	else
		sc = -2;

	while (vct_issp(*p))
		p++;

	if (*p == '-') {
		neg = 1;
		p++;
	} else if (*p == '+') {
		p++;
	}

	d = neg ? - (INT64_MIN / 10) : INT64_MAX / 10;
	m = neg ? - (INT64_MIN % 10) : INT64_MAX % 10;

	for (r = 0, c = -1; *p != '\0'; p++) {
		if (*p == '.') {
			if (sc != -1)
				break;
			sc = 0;
			continue;
		}
		if (! vct_isdigit(*p))
			break;
		c = *p - '0';
		if (r > d || (r == d && c > m)) {
			oflow = 1;
			break;
		}
		r *= 10;
		r += c;

		if (sc >= 0)
			sc++;
	}

	while (vct_issp(*p))
		p++;
	if (*p != '\0')
		*t = p;

	if (c == -1) {
		errno = EINVAL;	// no digits
		return (0);
	}

	if (oflow) {
		errno = ERANGE;
		return (neg ? INT64_MIN : INT64_MAX);
	}

	if (scp) {
		assert(sc >= -1);
		if (sc == -1)
			sc = 0;
		*scp = sc;
	}
	return (neg ? -r : r);
}

double
VNUM(const char *p)
{
	const char *t;
	double r;

	r = VNUMpfx(p, &t);
	if (t != NULL)
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

	r = VNUMpfx(p, &t);

	if (isnan(r) || t == NULL)
		return (nan(""));

	return (VNUM_duration_unit(r, t, NULL));
}

/**********************************************************************/

/*
 * take quantity i and scale sc, return bytes by unit
 *
 * return -1 on error
 */
int64_t
VNUM_bytes_unit(int64_t i, int sc, const char *b, const char *e)
{
	int shift = 0;
	uint64_t r, rr, t;

	assert(sc >= 0);
	assert(sc < P10_LIM);
	assert(i >= 0);

	if (e == NULL)
		e = strchr(b, '\0');

	while (b < e && vct_issp(*b))
		b++;
	if (b == e)
		return (-1);

	switch (*b) {
	case 'k': case 'K': shift = 10; b++; break;
	case 'm': case 'M': shift = 20; b++; break;
	case 'g': case 'G': shift = 30; b++; break;
	case 't': case 'T': shift = 40; b++; break;
	case 'p': case 'P': shift = 50; b++; break;
	case 'b': case 'B':
		break;
	default:
		return (-1);
	}
	if (b < e && (*b == 'b' || *b == 'B'))
		b++;

	while (b < e && vct_issp(*b))
		b++;
	if (b < e)
		return (-1);

	rr = p10[sc];

	if (shift == 0) {
		return (i / rr);
	}

	r = i << shift;

	/* simple case */
	if (r >> shift == i && r <= INT64_MAX)
		return (r / rr);

	/* lossless */
	while (shift > 0 && (rr & 1) == 0) {
		rr >>= 1;
		shift--;
	}
	r = i << shift;
	if (r >> shift == i && r <= INT64_MAX)
		return (r / rr);

	// i << shift / rr = ((i / rr) << shift) + (((i % rr) << shift) / rr)

	t = (i / rr);
	r = t << shift;
	if (r >> shift != t || t > INT64_MAX) {
		errno = EOVERFLOW;
		return (-1);
	}

	r += (((i % rr) << shift) / rr);

#if defined(NUM_C_TEST) && defined(HAVE_UINT128_T)
	// if we can, check that the above is correct
	uint128_t tr = ((uint128_t)i << shift) / rr;

	if ((uint128_t)r != tr) {
		fprintf(stderr, "r = %lu, tr = %lu\n", r,
			(uint64_t)(tr & (uint128_t)UINT64_MAX));
		assert (0);
	}
#endif

	if (r > INT64_MAX) {
		errno = EOVERFLOW;
		return (-1);
	}

	return (r);
}

const char *
VNUM_2bytes(const char *p, uintmax_t *r)
{
	int64_t rr;
	int sc;
	const char *end;

	if (p == NULL || *p == '\0')
		return (err_miss_num);

	errno = 0;
	rr = VNUMpfxint(p, &end, &sc);
	if (errno || rr < 0)
		return (err_invalid_num);

	if (end == NULL) {
		*r = (uintmax_t)rr;
		return (NULL);
	}

	rr = VNUM_bytes_unit(rr, sc, end, NULL);
	if (rr < 0) {
		if (errno == EOVERFLOW)
			return (err_precision);
		return (err_invalid_suff);
	}
	*r = (uintmax_t)rr;
	return (NULL);
}

#ifdef NUM_C_TEST
/* Compile with: "cc -o foo -DNUM_C_TEST -I../.. -I../../include num.c -lm" */

static struct test_case {
	const char *str;
	uintmax_t val;
	const char *err;
} test_cases[] = {
	{ "1",			(uintmax_t)1 },
	{ "1B",			(uintmax_t)1<<0 },
	{ "1 B",		(uintmax_t)1<<0 },
	{ "1.3B",		(uintmax_t)1 },
	{ "1.7B",		(uintmax_t)1 },

	{ "1024",		(uintmax_t)1024 },
	{ "1k",			(uintmax_t)1<<10 },
	{ "1kB",		(uintmax_t)1<<10 },
	{ "1.3kB",		(uintmax_t)1331 },
	{ "1.7kB",		(uintmax_t)1740 },

	{ "1048576",		(uintmax_t)1048576 },
	{ "1M",			(uintmax_t)1<<20 },
	{ "1MB",		(uintmax_t)1<<20 },
	{ "1.3MB",		(uintmax_t)1363148 },
	{ "1.7MB",		(uintmax_t)1782579 },

	{ "1073741824",		(uintmax_t)1073741824 },
	{ "1G",			(uintmax_t)1<<30 },
	{ "1GB",		(uintmax_t)1<<30 },
	{ "1.3GB",		(uintmax_t)1395864371 },
	{ "1.7GB",		(uintmax_t)1825361100 },

	{ "1099511627776",	(uintmax_t)1099511627776ULL },
	{ "1T",			(uintmax_t)1<<40 },
	{ "1TB",		(uintmax_t)1<<40 },
	{ "1.3TB",		(uintmax_t)1429365116108ULL },
	{ "1.7\tTB",		(uintmax_t)1869169767219ULL },

	{ "1125899906842624",	(uintmax_t)1125899906842624ULL},
	{ "1P\t",		(uintmax_t)1125899906842624ULL},
	{ "1PB ",		(uintmax_t)1125899906842624ULL},
	{ "1.3 PB",		(uintmax_t)1463669878895411ULL},

	// highest integers not rounded for double conversion
	{ "9007199254740988",	(uintmax_t)9007199254740988ULL},
	{ "9007199254740989",	(uintmax_t)9007199254740989ULL},
	{ "9007199254740990",	(uintmax_t)9007199254740990ULL},
	{ "9007199254740991",	(uintmax_t)9007199254740991ULL},

	// near INT64_MAX
	{ "9223372036854775807B", (uintmax_t)9223372036854775807ULL},
	{ "9223372036854775808B", 0, err_invalid_num },
	{ "9.223372036854775807B", (uintmax_t)9ULL},
	{ "922337203685.4775807B", (uintmax_t)922337203685ULL},
	{ "922337203685.477KB", (uintmax_t)944473296573928ULL},
	{ "337203685.4775807KB", (uintmax_t)345296573929ULL},

	{ "9007199254740991.99KB", (uintmax_t)9223372036854775797ULL},
	{ "8796093022207.9999MB", (uintmax_t)9223372036854775703ULL},

	{ "8796093022208.0001MB", 0, err_precision },
	{ "8796093022207.9999GB", 0, err_precision },

	/* Check the error checks */
	{ "",			0,	err_miss_num },
	{ "m",			0,	err_invalid_num },
	{ "-5B",			0,	err_invalid_num },
	{ "4%",			0,	err_invalid_suff },
	{ "3*",			0,	err_invalid_suff },

	/* TODO: add more */

	{ 0, 0, 0},
};

static const char *vec[] = {
	" 1",
	" 12",
	" 12.",
	" 12.3",
	" 12.34",
	" 12.34e-3",
	" 12.34e3",
	" 12.34e+3",
	" +12.34e-3",
	" -12.34e3",
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

/*
 * add one to a numerical string. First digit must be < 9
 * returns pointer to last digit;
 */
static const char *
strint_inc(char *buf)
{
	char *b;
	const char *e;

	assert(strlen(buf) >= 1);
	e = b = buf + strlen(buf) - 1;
	while (*b == '9' && b >= buf) {
		if (*b == '.')
			b--;
		else
			*b-- = '0';
	}
	assert(b >= buf);
	if (*b < '9')
		(*b)++;
	return (e);
}

/*
 * insert a period at n digits from the right
 */
static void
strint_sc(char *buf, int n, size_t sz)
{
	int l = strlen(buf);

	assert(sz > l + 1);
	assert(n < l);
	memmove(buf + l - n + 1, buf + l - n, n + 1);
	buf[l - n] = '.';
}

int
main(int argc, char *argv[])
{
	int ec = 0;
	struct test_case *tc;
	uintmax_t val;
	const char **p;
	const char *e, *ee;
	double d1, d2;
	char buf[64];
	int64_t r;
	int sc;

	(void)argc;

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
		e = VNUM_2bytes(tc->str, &val);
		if (e != NULL)
			val = 0;
		if (e == tc->err && val == tc->val)
			continue;
		++ec;
		printf("%s: VNUM_2bytes(\"%s\")\n",
		   *argv, tc->str);
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
	if (d1 < 31556925.2159 || d1 > 31556925.2161) {
		printf("%s: VNUM_Duration() wrong: %g\n", *argv, d1);
		++ec;
	}

	/* pos in range */
	snprintf(buf, sizeof(buf), "%jd", (intmax_t) INT64_MAX);
	errno = 0;
	e = NULL;
	r = VNUMpfxint(buf, &e, NULL);
	if (errno != 0 || r != INT64_MAX || (e != NULL && *e != '\0')) {
		printf("VNUMpfxint(%s) -> %jd errno %d e \"%s\"\n",
		    buf, (intmax_t)r, errno, e ? e : "(null)");
		++ec;
	}

	/* pos scale */
	strint_sc(buf, 3, sizeof(buf));
	errno = 0;
	e = NULL;
	r = VNUMpfxint(buf, &e, &sc);
	if (errno != 0 || r != INT64_MAX || (e != NULL && *e != '\0') ||
	    sc != 3) {
		printf("VNUMpfxint(%s) -> %jd errno %d e \"%s\" sc %d\n",
		       buf, (intmax_t)r, errno, e ? e : "(null)", sc);
		++ec;
	}

	/* pos scale ERANGE */
	ee = strint_inc(buf);
	sc = 0;
	errno = 0;
	e = NULL;
	r = VNUMpfxint(buf, &e, &sc);
	if (errno != ERANGE || r != INT64_MAX || e == NULL || e != ee) {
		printf("VNUMpfxint(%s) -> %jd errno %d e \"%s\" sc %d\n",
		       buf, (intmax_t)r, errno, e ? e : "(null)", sc);
		++ec;
	}

	/* pos ERANGE */
	snprintf(buf, sizeof(buf), "%jd", (intmax_t) INT64_MAX);
	ee = strint_inc(buf);
	errno = 0;
	e = NULL;
	r = VNUMpfxint(buf, &e, NULL);
	if (errno != ERANGE || r != INT64_MAX || e == NULL || e != ee) {
		printf("VNUMpfxint(%s) -> %jd errno %d e \"%s\"\n",
		    buf, (intmax_t)r, errno, e ? e : "(null)");
		++ec;
	}

	/* neg in range */
	snprintf(buf, sizeof(buf), "%jd", (intmax_t) INT64_MIN);
	errno = 0;
	e = NULL;
	r = VNUMpfxint(buf, &e, NULL);
	if (errno != 0 || r != INT64_MIN || (e != NULL && *e != '\0')) {
		printf("VNUMpfxint(%s) -> %jd errno %d e \"%s\"\n",
		    buf, (intmax_t)r, errno, e ? e : "(null)");
		++ec;
	}

	/* neg ERANGE */
	errno = 0;
	e = NULL;
	assert(*buf == '-');
	ee = strint_inc(buf + 1);
	r = VNUMpfxint(buf, &e, NULL);
	if (errno != ERANGE || r != INT64_MIN || e == NULL || e != ee) {
		printf("VNUMpfxint(%s) -> %jd errno %d e \"%s\"\n",
		    buf, (intmax_t)r, errno, e ? e : "(null)");
		++ec;
	}

	/* two dots */
	errno = 0;
	e = NULL;
	assert(*buf == '-');
	strint_sc(buf, 5, sizeof(buf));
	strint_sc(buf, 3, sizeof(buf));
	r = VNUMpfxint(buf, &e, &sc);
	if (errno != 0 || r != INT64_MIN / 1000 || e == NULL ||
	    e != buf + strlen(buf) - 4) {
		printf("VNUMpfxint(%s) -> %jd errno %d e \"%s\" sc %d\n",
		       buf, (intmax_t)r, errno, e ? e : "(null)", sc);
		++ec;
	}
	/* TODO: test invalid strings */
	if (!ec)
		printf("OK\n");
	return (ec > 0);
}
#endif
