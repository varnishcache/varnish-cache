/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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
 * In the highly unlikely event of performance trouble, handbuilt versions
 * would likely be faster than relying on the OS time functions.
 *
 * We must parse three different formats:
 *       000000000011111111112222222222
 *       012345678901234567890123456789
 *       ------------------------------
 *	"Sun, 06 Nov 1994 08:49:37 GMT"		RFC822 & RFC1123
 *	"Sunday, 06-Nov-94 08:49:37 GMT"	RFC850
 *	"Sun Nov  6 08:49:37 1994"		ANSI-C asctime()
 *
 * And always output the RFC1123 format.
 *
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/time.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#include "libvarnish.h"

/*
 * Note on Solaris: for some reason, clock_gettime(CLOCK_MONOTONIC, &ts) is not
 * implemented in assembly, but falls into a syscall, while gethrtime() doesn't,
 * so we save a syscall by using gethrtime() if it is defined.
 */

double
TIM_mono(void)
{
#ifdef HAVE_GETHRTIME
	return (gethrtime() * 1e-9);
#elif  HAVE_CLOCK_GETTIME
	struct timespec ts;

	assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
	return (ts.tv_sec + 1e-9 * ts.tv_nsec);
#else
	struct timeval tv;

	assert(gettimeofday(&tv, NULL) == 0);
	return (tv.tv_sec + 1e-6 * tv.tv_usec);
#endif
}

double
TIM_real(void)
{
#ifdef HAVE_CLOCK_GETTIME
	struct timespec ts;

	assert(clock_gettime(CLOCK_REALTIME, &ts) == 0);
	return (ts.tv_sec + 1e-9 * ts.tv_nsec);
#else
	struct timeval tv;

	assert(gettimeofday(&tv, NULL) == 0);
	return (tv.tv_sec + 1e-6 * tv.tv_usec);
#endif
}

void
TIM_format(double t, char *p)
{
	struct tm tm;
	time_t tt;

	tt = (time_t) t;
	(void)gmtime_r(&tt, &tm);
	AN(strftime(p, TIM_FORMAT_SIZE, "%a, %d %b %Y %T GMT", &tm));
}

/* XXX: add statistics ? */
static const char *fmts[] = {
	"%a, %d %b %Y %T GMT",	/* RFC 822 & RFC 1123 */
	"%A, %d-%b-%y %T GMT",	/* RFC 850 */
	"%a %b %d %T %Y",	/* ANSI-C asctime() */
	"%F %T",		/* ISO 8601 */
	NULL
};

time_t
TIM_parse(const char *p)
{
	time_t t;
	struct tm tm;
	const char **r;

	for (r = fmts; *r != NULL; r++) {
		memset(&tm, 0, sizeof tm);
		if (strptime(p, *r, &tm) != NULL) {
			/*
			 * Make sure this is initialized on the off-chance
			 * that some raving loonie would apply DST to UTC.
			 */
			tm.tm_isdst = -1;
#if defined(HAVE_TIMEGM)
			t = timegm(&tm);
#else
			/*
			 * Ahh, another POSIX_STUPIDITY, how unexpected.
			 * Instead of, as would have been logical, to have
			 * tm_timezone element, mktime() is standardized as
			 * always working in localtime.  This brilliant idea
			 * came from the same people who said "leap-seconds ?
			 * Naah, screw it!".
			 *
			 * On broken systems without a working timegm(),
			 * it is the responsibility of the calling program
			 * to set the timezone to UTC.  We check that.
			 */
			t = mktime(&tm);
			assert(!strcmp(tzname[0], "UTC"));
#endif
			return (t);
		}
	}
	return (0);
}

void
TIM_sleep(double t)
{
#ifdef HAVE_NANOSLEEP
	struct timespec ts;

	ts = TIM_timespec(t);

	(void)nanosleep(&ts, NULL);
#else
	if (t >= 1.) {
		(void)sleep(floor(t);
		t -= floor(t);
	}
	/* XXX: usleep() is not mandated to be thread safe */
	t *= 1e6;
	if (t > 0)
		(void)usleep(floor(t));
#endif
}

struct timeval
TIM_timeval(double t)
{
	struct timeval tv;

	tv.tv_sec = (time_t)trunc(t);
	tv.tv_usec = (int)(1e6 * (t - tv.tv_sec));
	return (tv);
}

struct timespec
TIM_timespec(double t)
{
	struct timespec tv;

	tv.tv_sec = (time_t)trunc(t);
	tv.tv_nsec = (int)(1e9 * (t - tv.tv_sec));
	return (tv);
}


#ifdef TEST_DRIVER

#include <stdlib.h>

/*
 * Compile with:
 *  cc -o foo -DTEST_DRIVER -I../.. -I../../include time.c assert.c
 * (Solaris)
 *  cc -o foo -DTEST_DRIVER -I../.. -I../../include -lm time.c assert.c
 * Test with:
 *  env TZ=UTC ./foo
 *  env TZ=CET ./foo
 */

static void
tst(const char *s, time_t good)
{
	time_t t;
	char buf[BUFSIZ];

	t = TIM_parse(s);
	TIM_format(t, buf);
	printf("%-30s -> %12jd -> %s\n", s, (intmax_t)t, buf);
	if (t != good) {
		printf("Parse error! Got: %jd should have %jd diff %jd\n",
		    (intmax_t)t, (intmax_t)good, (intmax_t)(t - good));
		exit (2);
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

	r_begin = TIM_real();
	m_begin = TIM_mono();
	TIM_sleep(ref);
	r_end = TIM_real();
	m_end = TIM_mono();

	err += tst_delta_check("TIM_mono", m_begin, m_end, ref);
	err += tst_delta_check("TIM_real", r_begin, r_end, ref);

	if (err) {
		printf("%d time delta test errrors\n", err);
		exit (2);
	}
}

int
main(int argc, char **argv)
{
	time_t t;
	char buf[BUFSIZ];

	time(&t);
	memset(buf, 0x55, sizeof buf);
	TIM_format(t, buf);
	printf("scan = %d <%s>\n", TIM_parse(buf), buf);

	/* Examples from RFC2616 section 3.3.1 */
	tst("Sun, 06 Nov 1994 08:49:37 GMT", 784111777);
	tst("Sunday, 06-Nov-94 08:49:37 GMT", 784111777);
	tst("Sun Nov  6 08:49:37 1994", 784111777);

	tst_delta();

	return (0);
}
#endif
