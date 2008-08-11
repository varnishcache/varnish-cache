/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
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

#include <sys/time.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "libvarnish.h"

double
TIM_mono(void)
{
#ifdef HAVE_CLOCK_GETTIME
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
	AN(strftime(p, 30, "%a, %d %b %Y %T GMT", &tm));
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
	struct tm tm;
	const char **r;

	for (r = fmts; *r != NULL; r++) {
		memset(&tm, 0, sizeof tm);
		if (strptime(p, *r, &tm) != NULL)
			return (mktime(&tm));
	}
	return (0);
}

#ifdef TEST_DRIVER
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
	printf("scan = %d\n", TIM_parse("Sun, 06 Nov 1994 08:49:37 GMT"));
	printf("scan = %d\n", TIM_parse("Sunday, 06-Nov-94 08:49:37 GMT"));
	printf("scan = %d\n", TIM_parse("Sun Nov  6 08:49:37 1994"));

	return (0);
}
#endif
