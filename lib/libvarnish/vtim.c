/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <sys/time.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vas.h"
#include "vtim.h"

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
	31, 29, 31,  30, 31, 30,  31, 31, 30,  31, 30, 31,
};

static const int days_before_month[] = {
	0, 31, 59,  90, 120, 151,  181, 212, 243,  273, 304, 334
};

/*
 * Note on Solaris: for some reason, clock_gettime(CLOCK_MONOTONIC, &ts) is not
 * implemented in assembly, but falls into a syscall, while gethrtime() doesn't,
 * so we save a syscall by using gethrtime() if it is defined.
 */

double
VTIM_mono(void)
{
#ifdef HAVE_GETHRTIME
	return (gethrtime() * 1e-9);
#elif  HAVE_CLOCK_GETTIME
	struct timespec ts;

	AZ(clock_gettime(CLOCK_MONOTONIC, &ts));
	return (ts.tv_sec + 1e-9 * ts.tv_nsec);
#else
	struct timeval tv;

	AZ(gettimeofday(&tv, NULL));
	return (tv.tv_sec + 1e-6 * tv.tv_usec);
#endif
}

double
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
VTIM_format(double t, char *p)
{
	struct tm tm;
	time_t tt;

	tt = (time_t) t;
	(void)gmtime_r(&tt, &tm);
	AN(snprintf(p, VTIM_FORMAT_SIZE, "%s, %02d %s %4d %02d:%02d:%02d GMT",
	    weekday_name[tm.tm_wday], tm.tm_mday, month_name[tm.tm_mon],
	    tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec));
}

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
		fld += (*p - '0') * mult;			\
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
		for(i = 0; i < 7; i++) {			\
			if (!memcmp(p, weekday_name[i], 3)) {	\
				weekday = i;			\
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
		for(i = 0; i < 12; i++) {			\
			if (!memcmp(p, month_name[i], 3)) {	\
				month = i + 1;			\
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

double
VTIM_parse(const char *p)
{
	double t;
	int month = 0, year = 0, weekday = -1, mday = 0;
	int hour = 0, min = 0, sec = 0;
	int d, leap;

	while (*p == ' ')
		p++;

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
		if (*p == ',') {
			/* RFC822 & RFC1123 - "Sun, 06 Nov 1994 08:49:37 GMT" */
			p++;
			MUSTBE(' ');
			DIGIT(10, mday);
			DIGIT(1, mday);
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
		} else if (!memcmp(p,
			    more_weekday[weekday],
			    strlen(more_weekday[weekday]))) {
			/* RFC850 -- "Sunday, 06-Nov-94 08:49:37 GMT" */
			p += strlen(more_weekday[weekday]);
			MUSTBE(',');
			MUSTBE(' ');
			DIGIT(10, mday);
			DIGIT(1, mday);
			MUSTBE('-');
			MONTH();
			MUSTBE('-');
			DIGIT(10, year);
			DIGIT(1, year);
			year += 1900;
			if (year < 1969)
				year += 100;
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

	if (sec < 0 || sec > 60)	// Leapseconds!
		FAIL();
	if (min < 0 || min > 59)
		FAIL();
	if (hour < 0 || hour > 23)
		FAIL();
	if (month < 1 || month > 12)
		FAIL();
	if (mday < 1 || mday > days_in_month[month - 1])
		FAIL();
	if (year < 1899)
		FAIL();

	leap =
	    ((year) % 4) == 0 && (((year) % 100) != 0 || ((year) % 400) == 0);

	if (month == 2 && mday > 28 && !leap)
		FAIL();

	if (sec == 60)		// Ignore Leapseconds
		sec--;

	t = ((hour * 60.) + min) * 60. + sec;

	d = (mday - 1) + days_before_month[month - 1];

	if (month > 2 && leap)
		d++;

	d += (year % 100) * 365;	/* There are 365 days in a year */

	if ((year % 100) > 0)		/* And a leap day every four years */
		d += (((year % 100) - 1)/4);

	d += ((year / 100) - 20) *	/* Days relative to y2000 */
	    (100 * 365 + 24);		/* 24 leapdays per year in a century */

	d += ((year-1) / 400) - 4;	/* And one more every 400 years */

	/*
	 * Now check weekday, if we have one.
	 * 6 is because 2000-01-01 was a saturday.
	 * 10000 is to make sure the modulus argument is always positive
	 */
	if (weekday != -1 && (d + 6 + 7 * 10000) % 7 != weekday)
		FAIL();

	t += d * 86400.;

	t += 10957. * 86400.;		/* 10957 days frm UNIX epoch to y2000 */

	return (t);
}

void
VTIM_sleep(double t)
{
#ifdef HAVE_NANOSLEEP
	struct timespec ts;

	ts = VTIM_timespec(t);

	(void)nanosleep(&ts, NULL);
#else
	if (t >= 1.) {
		(void)sleep(floor(t));
		t -= floor(t);
	}
	/* XXX: usleep() is not mandated to be thread safe */
	t *= 1e6;
	if (t > 0)
		(void)usleep(floor(t));
#endif
}

struct timeval
VTIM_timeval(double t)
{
	struct timeval tv;

	tv.tv_sec = (time_t)trunc(t);
	tv.tv_usec = (int)(1e6 * (t - tv.tv_sec));
	return (tv);
}

struct timespec
VTIM_timespec(double t)
{
	struct timespec tv;

	tv.tv_sec = (time_t)trunc(t);
	tv.tv_nsec = (int)(1e9 * (t - tv.tv_sec));
	return (tv);
}


#ifdef TEST_DRIVER

/**********************************************************************
 * Compile with:
 *	cc -o foo -DTEST_DRIVER -I../.. -I../../include vtim.c vas.c -lm
 * Test with:
 *	./foo
 */

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

static int
tst_delta_check(const char *name, double begin, double end, double ref)
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
tst_delta()
{
	double m_begin, m_end;
	double r_begin, r_end;
	const double ref = 1;
	int err = 0;

	r_begin = VTIM_real();
	m_begin = VTIM_mono();
	VTIM_sleep(ref);
	r_end = VTIM_real();
	m_end = VTIM_mono();

	err += tst_delta_check("VTIM_mono", m_begin, m_end, ref);
	err += tst_delta_check("VTIM_real", r_begin, r_end, ref);

	if (err) {
		printf("%d time delta test errrors\n", err);
		exit(4);
	}
}

int
main(int argc, char **argv)
{
	time_t t;
	struct tm tm;
	double tt;
	char buf[BUFSIZ];
	char buf1[BUFSIZ];

	AZ(setenv("TZ", "UTC", 1));
	assert(sizeof t >= 8);

	/* Brute force test against libc version */
	for (t = -2209852800; t < 20000000000; t += 3599) {
		gmtime_r(&t, &tm);
		strftime(buf1, sizeof buf1, "%a, %d %b %Y %T GMT", &tm);
		VTIM_format(t, buf);
		if (strcmp(buf, buf1)) {
			printf("libc: <%s> Vtim <%s> %jd\n",
			    buf1, buf, (intmax_t)t);
			exit(2);
		}
		tt = VTIM_parse(buf1);
		if (tt != t) {
			VTIM_format(tt, buf);
			printf("  fm: %12jd <%s>\n", (intmax_t)t, buf1);
			printf("  to: %12.0f <%s>\n", tt, buf);
			exit(2);
		}

		strftime(buf1, sizeof buf1, "%a %b %e %T %Y", &tm);
		tt = VTIM_parse(buf1);
		if (tt != t) {
			VTIM_format(tt, buf);
			printf("  fm: %12jd <%s>\n", (intmax_t)t, buf1);
			printf("  to: %12.0f <%s>\n", tt, buf);
			exit(2);
		}

		strftime(buf1, sizeof buf1, "%Y-%m-%dT%T", &tm);
		tt = VTIM_parse(buf1);
		if (tt != t) {
			VTIM_format(tt, buf);
			printf("  fm: %12jd <%s>\n", (intmax_t)t, buf1);
			printf("  to: %12.0f <%s>\n", tt, buf);
			exit(2);
		}

		if (tm.tm_year >= 69 && tm.tm_year < 169) {
			strftime(buf1, sizeof buf1, "%A, %d-%b-%y %T GMT", &tm);
			tt = VTIM_parse(buf1);
			if (tt != t) {
				VTIM_format(tt, buf);
				printf("  fm: %12jd <%s>\n", (intmax_t)t, buf1);
				printf("  to: %12.0f <%s>\n", tt, buf);
				exit(2);
			}
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
