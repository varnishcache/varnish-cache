/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Semi-trivial functions to handle HTTP header timestamps according to
 * RFC 2616 section 3.3.
 *
 * We must parse four different formats:
 *       000000000011111111112222222222
 *       012345678901234567890123456789
 *       ------------------------------
 *	"Sun, 06 Nov 1994 08:49:37 GMT"		RFC822 & RFC1123
 *	"Sunday, 06-Nov-94 08:49:37 GMT"	RFC850
 *	"Sun Nov  6 08:49:37 1994"		ANSI-C asctime()
 *	"1994-11-06T08:49:37"			ISO 8601
 *
 * And always output the RFC1123 format.
 *
 * So why are these functions hand-built ?
 *
 * Because the people behind POSIX were short-sighted morons who didn't think
 * anybody would ever need to deal with timestamps in multiple different
 * timezones at the same time -- for that matter, convert timestamps to
 * broken down UTC/GMT time.
 *
 * We could, and used to, get by by smashing our TZ variable to "UTC" but
 * that ruins the LOCALE for VMODs.
 *
 */

#include "config.h"

#include <time.h>
#include <sys/time.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __MACH__
#include <mach/mach_time.h>
#endif

#include "vdef.h"

#include "vas.h"
#include "vtim.h"

/* relax vtim parsing */
unsigned VTIM_postel = 0;

static const char * const weekday_name[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char * const more_weekday[] = {
	"day", "day", "sday", "nesday", "rsday", "day", "urday"
};

static const char * const month_name[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const int days_in_month[] = {
	31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const int days_before_month[] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

#ifdef __MACH__
// http://stackoverflow.com/a/21352348
static uint64_t mt_base;
static double   mt_scale;

static void
mach_time_init(void)
{
	mach_timebase_info_data_t timebase;

	mt_base = mach_absolute_time();

	AZ(mach_timebase_info(&timebase));
	mt_scale = (double)timebase.numer / (double)timebase.denom * 1e-9;
}

static __attribute__((constructor)) void
init(void)
{
	mach_time_init();
}
#endif

/*
 * On older Solaris-incarnations, gethrtime() was faster than
 * clock_gettime(CLOCK_MONOTONIC). Our configure script prefers
 * clock_gettime if it is consistently at least twice as fast as
 * gethrtime(), which is the case on modern Solaris descendents.
 */

vtim_mono
VTIM_mono(void)
{
#if defined(HAVE_GETHRTIME) && defined(USE_GETHRTIME)
	return (gethrtime() * 1e-9);
#else
	struct timespec ts;

	AZ(clock_gettime(CLOCK_MONOTONIC, &ts));
	return (ts.tv_sec + 1e-9 * ts.tv_nsec);
#endif
}

vtim_real
VTIM_real(void)
{
#ifdef HAVE_CLOCK_GETTIME
	struct timespec ts;

	AZ(clock_gettime(CLOCK_REALTIME, &ts));
	return (ts.tv_sec + 1e-9 * ts.tv_nsec);
#else
	struct timeval tv;

	AZ(gettimeofday(&tv, NULL));
	return (tv.tv_sec + 1e-6 * tv.tv_usec);
#endif
}

void
VTIM_format(vtim_real t, char p[VTIM_FORMAT_SIZE])
{
	struct tm tm;
	time_t tt;

	AN(p);
	*p = '\0';

	if (t < (vtim_real)INTMAX_MIN || t > (vtim_real)INTMAX_MAX)
		return;

	tt = (time_t)(intmax_t)t;
	if (gmtime_r(&tt, &tm) == NULL)
		return;

	AN(snprintf(p, VTIM_FORMAT_SIZE,
	    "%s, %02d %s %4d %02d:%02d:%02d GMT",
	    weekday_name[tm.tm_wday],
	    tm.tm_mday, month_name[tm.tm_mon], tm.tm_year + 1900,
	    tm.tm_hour, tm.tm_min, tm.tm_sec));
}

struct vtim {
	int	year;
	int	month;
	int	mday;
	int	weekday;
	int	hour;
	int	min;
	int	sec;
};

#define VTIM_INIT(vtim)			\
	do {				\
		(vtim)->year = 0;	\
		(vtim)->month = 0;	\
		(vtim)->mday = 0;	\
		(vtim)->weekday = -1;	\
		(vtim)->hour = 0;	\
		(vtim)->min = 0;	\
		(vtim)->sec = 0;	\
	} while (0)

#ifdef TEST_DRIVER
#define FAIL()	\
	do { printf("\nFAIL <<%d>>\n", __LINE__); return (0); } while (0)
#else
#define FAIL()	\
	do { return (0); } while (0)
#endif

#define DIGIT(mult, fld)					\
	do {							\
		if (*p < '0' || *p > '9')			\
			FAIL();					\
		vtim->fld += (*p - '0') * mult;			\
		p++;						\
	} while(0)

#define MUSTBE(chr)						\
	do {							\
		if (*p != chr)					\
			FAIL();					\
		p++;						\
	} while(0)

#define WEEKDAY()						\
	do {							\
		int i;						\
		for (i = 0; i < 7; i++) {			\
			if (!memcmp(p, weekday_name[i], 3)) {	\
				vtim->weekday = i;		\
				break;				\
			}					\
		}						\
		if (i == 7)					\
			FAIL();					\
		p += 3;						\
	} while(0)


#define MONTH()							\
	do {							\
		int i;						\
		for (i = 0; i < 12; i++) {			\
			if (!memcmp(p, month_name[i], 3)) {	\
				vtim->month = i + 1;		\
				break;				\
			}					\
		}						\
		if (i == 12)					\
			FAIL();					\
		p += 3;						\
	} while(0)

#define TIMESTAMP()						\
	do {							\
		DIGIT(10, hour);				\
		DIGIT(1, hour);					\
		MUSTBE(':');					\
		DIGIT(10, min);					\
		DIGIT(1, min);					\
		MUSTBE(':');					\
		DIGIT(10, sec);					\
		DIGIT(1, sec);					\
	} while(0)

vtim_real
VTIM_parse(const char *p)
{
	struct vtim vtim[1];
	vtim_real t;
	int d, leap;

	if (p == NULL || *p == '\0')
		FAIL();

	while (*p == ' ')
		p++;

	VTIM_INIT(vtim);

	if (*p >= '0' && *p <= '9') {
		/* ISO8601 -- "1994-11-06T08:49:37" */
		DIGIT(1000, year);
		DIGIT(100, year);
		DIGIT(10, year);
		DIGIT(1, year);
		MUSTBE('-');
		DIGIT(10, month);
		DIGIT(1, month);
		MUSTBE('-');
		DIGIT(10, mday);
		DIGIT(1, mday);
		MUSTBE('T');
		TIMESTAMP();
	} else {
		WEEKDAY();
		assert(vtim->weekday >= 0 && vtim->weekday <= 6);
		if (*p == ',') {
			/* RFC822 & RFC1123 - "Sun, 06 Nov 1994 08:49:37 GMT" */
			p++;
			MUSTBE(' ');
			if (VTIM_postel && *p && p[1] == ' ')
				DIGIT(1, mday);
			else {
				DIGIT(10, mday);
				DIGIT(1, mday);
			}
			MUSTBE(' ');
			MONTH();
			MUSTBE(' ');
			DIGIT(1000, year);
			DIGIT(100, year);
			DIGIT(10, year);
			DIGIT(1, year);
			MUSTBE(' ');
			TIMESTAMP();
			MUSTBE(' ');
			MUSTBE('G');
			MUSTBE('M');
			MUSTBE('T');
		} else if (*p == ' ') {
			/* ANSI-C asctime() -- "Sun Nov  6 08:49:37 1994" */
			p++;
			MONTH();
			MUSTBE(' ');
			if (*p != ' ')
				DIGIT(10, mday);
			else
				p++;
			DIGIT(1, mday);
			MUSTBE(' ');
			TIMESTAMP();
			MUSTBE(' ');
			DIGIT(1000, year);
			DIGIT(100, year);
			DIGIT(10, year);
			DIGIT(1, year);
		} else if (!memcmp(p, more_weekday[vtim->weekday],
		    strlen(more_weekday[vtim->weekday]))) {
			/* RFC850 -- "Sunday, 06-Nov-94 08:49:37 GMT" */
			p += strlen(more_weekday[vtim->weekday]);
			MUSTBE(',');
			MUSTBE(' ');
			DIGIT(10, mday);
			DIGIT(1, mday);
			MUSTBE('-');
			MONTH();
			MUSTBE('-');
			DIGIT(10, year);
			DIGIT(1, year);
			vtim->year += 1900;
			if (vtim->year < 1969)
				vtim->year += 100;
			MUSTBE(' ');
			TIMESTAMP();
			MUSTBE(' ');
			MUSTBE('G');
			MUSTBE('M');
			MUSTBE('T');
		} else
			FAIL();
	}

	while (*p == ' ')
		p++;

	if (*p != '\0')
		FAIL();

	if (vtim->sec < 0 || vtim->sec > 60)	/* Leapseconds! */
		FAIL();
	if (vtim->min < 0 || vtim->min > 59)
		FAIL();
	if (vtim->hour < 0 || vtim->hour > 23)
		FAIL();
	if (vtim->month < 1 || vtim->month > 12)
		FAIL();
	if (vtim->mday < 1 || vtim->mday > days_in_month[vtim->month - 1])
		FAIL();
	if (vtim->year < 1899)
		FAIL();

	leap = ((vtim->year) % 4) == 0 &&
	    (((vtim->year) % 100) != 0 || ((vtim->year) % 400) == 0);

	if (vtim->month == 2 && vtim->mday > 28 && !leap)
		FAIL();

	if (vtim->sec == 60)			/* Ignore Leapseconds */
		vtim->sec--;

	t = ((vtim->hour * 60.) + vtim->min) * 60. + vtim->sec;

	d = (vtim->mday - 1) + days_before_month[vtim->month - 1];

	if (vtim->month > 2 && leap)
		d++;

	d += (vtim->year % 100) * 365;	/* There are 365 days in a year */

	if ((vtim->year % 100) > 0)	/* And a leap day every four years */
		d += (((vtim->year % 100) - 1) / 4);

	d += ((vtim->year / 100) - 20) *	/* Days relative to y2000 */
	    (100 * 365 + 24);		/* 24 leapdays per year in a century */

	d += ((vtim->year - 1) / 400) - 4;	/* One more every 400 years */

	/*
	 * Now check weekday, if we have one.
	 * 6 is because 2000-01-01 was a saturday.
	 * 10000 is to make sure the modulus argument is always positive
	 */
	if (vtim->weekday != -1 && (d + 6 + 7 * 10000) % 7 != vtim->weekday)
		FAIL();

	t += d * 86400.;

	t += 10957. * 86400.;	/* 10957 days frm UNIX epoch to y2000 */

	return (t);
}

void
VTIM_sleep(vtim_dur t)
{
	struct timespec ts;

	ts = VTIM_timespec(t);

	(void)nanosleep(&ts, NULL);
}

/*
 * VTIM_timeval and VTIM_timespec may need variants with different signatures
 * when vtim_real / vtim_mono typedefs are changed
 */

struct timeval
VTIM_timeval(vtim_dur t)
{
	struct timeval tv;

	AZ(isnan(t));
	tv.tv_sec = (time_t)trunc(t);
	tv.tv_usec = (int)(1e6 * (t - tv.tv_sec));
	return (tv);
}

struct timespec
VTIM_timespec(vtim_dur t)
{
	struct timespec tv;

	AZ(isnan(t));
	tv.tv_sec = (time_t)trunc(t);
	tv.tv_nsec = (int)(1e9 * (t - tv.tv_sec));
	return (tv);
}

struct timeval
VTIM_timeval_sock(vtim_dur t)
{

	return (VTIM_timeval(isinf(t) ? 0. : vmax(t, 1e-3)));
}

int
VTIM_poll_tmo(vtim_dur tmo)
{

	if (isinf(tmo))
		return (-1);
	assert(!isnan(tmo));
	return (vmax_t(int, 0, ((int)(tmo * 1e3))));
}

#ifdef TEST_DRIVER

#pragma GCC diagnostic ignored "-Wformat-y2k"

#include <stdint.h>

static void
tst(const char *s, time_t good)
{
	time_t t;
	char buf[BUFSIZ];

	t = VTIM_parse(s);
	VTIM_format(t, buf);
	printf("%-30s -> %12jd -> %s\n", s, (intmax_t)t, buf);
	if (t != good) {
		printf("Parse error! Got: %jd should have %jd diff %jd\n",
		    (intmax_t)t, (intmax_t)good, (intmax_t)(t - good));
		exit(4);
	}
}

/* XXX keep as double for the time being */
static int
tst_delta_check(const char *name, double begin, double end, vtim_dur ref)
{
	const double tol_max = 1.1;
	const double tol_min = 1;

	printf("%s delta for %fs sleep: %f\n", name, ref, (end - begin));

	if ((end - begin) > tol_max * ref) {
		printf("%s delta above tolerance: ((%f - %f) = %f) > %f\n",
		    name, end, begin, (end - begin), tol_max);
		return (1);
	} else if ((end - begin) < tol_min * ref) {
		printf("%s delta below tolerance: ((%f - %f) = %f) < %f\n",
		    name, end, begin, (end - begin), tol_min);
		return (1);
	}
	return (0);
}

static void
tst_delta(void)
{
	vtim_mono m_begin, m_end;
	vtim_real r_begin, r_end;
	const vtim_dur ref = 1;
	int err = 0;

	r_begin = VTIM_real();
	m_begin = VTIM_mono();
	VTIM_sleep(ref);
	r_end = VTIM_real();
	m_end = VTIM_mono();

	err += tst_delta_check("VTIM_mono", m_begin, m_end, ref);
	err += tst_delta_check("VTIM_real", r_begin, r_end, ref);

	if (err) {
		printf("%d time delta test errors\n", err);
		exit(4);
	}
}

static void
bench(void)
{
	vtim_mono s, e;
	vtim_mono t_m;
	vtim_real t_r;
	unsigned long t_i;
	int i;
	char buf[64];

	t_m = 0;
	t_r = 0;
	s = VTIM_mono();
	for (i=0; i<100000; i++)
		t_r += VTIM_real();
	e = VTIM_mono();
	printf("real: %fs / %d = %fns - tst val %f\n",
	    e - s, i, 1e9 * (e - s) / i, t_r);

	t_i = 0;
	s = VTIM_mono();
	for (i=0; i<100000; i++)
		t_m += VTIM_mono();
	e = VTIM_mono();
	printf("mono: %fs / %d = %fns - tst val %f\n",
	    e - s, i, 1e9 * (e - s) / i, t_m);

	t_i = 0;
	s = VTIM_mono();
	for (i=0; i<100000; i++) {
		snprintf(buf, sizeof(buf), "%.6f", s);
		t_i += buf[4];
	}
	e = VTIM_mono();
	printf("printf %%.6f: %fs / %d = %fns - tst val %lu %s\n",
	    e - s, i, 1e9 * (e - s) / i, t_i, buf);

	t_i = 0;
	s = VTIM_mono();
	for (i=0; i<100000; i++) {
		snprintf(buf, sizeof(buf), "%ju.%06ju",
		    (uintmax_t)floor(s),
		    (uintmax_t)floor((s * 1e6)) % 1000000UL);
		t_i += buf[4];
	}
	e = VTIM_mono();
	printf("printf %%ju.%%06ju: %fs / %d = %fns - tst val %lu %s\n",
	    e - s, i, 1e9 * (e - s) / i, t_i, buf);
}

static void
parse_check(time_t t, const char *s)
{
	vtim_real tt;
	char buf[BUFSIZ];

	tt = VTIM_parse(s);
	if (tt != t) {
		VTIM_format(tt, buf);
		printf("  fm: %12jd <%s>\n", (intmax_t)t, s);
		printf("  to: %12.0f <%s>\n", tt, buf);
		exit(2);
	}
}

#define TTEST_MIN (sizeof(time_t) >= 8 ? -2209852800LL : INT32_MIN)
#define TTEST_MAX (sizeof(time_t) >= 8 ? 20000000000LL : INT32_MAX)

int
main(int argc, char **argv)
{
	time_t t;
	intmax_t iter;
	struct tm tm;
	char buf[BUFSIZ];
	char buf1[BUFSIZ];

	(void)argc;
	(void)argv;

	AZ(setenv("TZ", "UTC", 1));

	bench();

	/* Brute force test against libc version */
	for (iter = TTEST_MIN; iter < TTEST_MAX; iter += 3599) {
		t = (time_t)iter;
		gmtime_r(&t, &tm);
		strftime(buf1, sizeof buf1, "%a, %d %b %Y %T GMT", &tm);
		VTIM_format(t, buf);
		if (strcmp(buf, buf1)) {
			printf("libc: <%s> Vtim <%s> %jd\n",
			    buf1, buf, (intmax_t)t);
			exit(2);
		}
		parse_check(t, buf1);

		strftime(buf1, sizeof buf1, "%a %b %e %T %Y", &tm);
		parse_check(t, buf1);

		strftime(buf1, sizeof buf1, "%Y-%m-%dT%T", &tm);
		parse_check(t, buf1);

		if (tm.tm_year >= 69 && tm.tm_year < 169) {
			strftime(buf1, sizeof buf1, "%A, %d-%b-%y %T GMT", &tm);
			parse_check(t, buf1);
		}
	}

	/* Examples from RFC2616 section 3.3.1 */
	tst("Sun, 06 Nov 1994 08:49:37 GMT", 784111777);
	tst("Sunday, 06-Nov-94 08:49:37 GMT", 784111777);
	tst("Sun Nov  6 08:49:37 1994", 784111777);

	tst("1994-11-06T08:49:37", 784111777);

	tst_delta();

	return (0);
}

#endif
