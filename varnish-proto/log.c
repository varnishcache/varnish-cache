/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/time.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef TIME_WITH_SYS_TIME
#include <time.h>
#endif
#include <unistd.h>

#include "varnish.h"
#include "log.h"
#include "system.h"

static void
timestamp(void)
{
	struct timeval now;

	if (gettimeofday(&now, NULL) == -1)
		now.tv_sec = now.tv_usec = 0;
	fprintf(stderr, "%lu.%06lu [%lu] ",
	    (unsigned long)now.tv_sec,
	    (unsigned long)now.tv_usec,
	    (unsigned long)sys.pid);
}

static void
emit(const char *fmt, va_list ap)
{
	timestamp();
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

static void
sysemit(const char *fmt, va_list ap)
{
	char errstr[64];

#if defined(HAVE_STRERROR_R)
	strerror_r(errno, errstr, sizeof errstr);
#else
	snprintf(errstr, sizeof errstr, "%s", strerror(errno));
#endif
	timestamp();
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ": %s\n", errstr);
}

static void
panic(void)
{
	signal(SIGABRT, SIG_DFL);
	kill(getpid(), SIGABRT);
}

void
log_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	emit(fmt, ap);
	va_end(ap);
}

void
log_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	emit(fmt, ap);
	va_end(ap);
}

void
log_syserr(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	sysemit(fmt, ap);
	va_end(ap);
}

void
log_panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	emit(fmt, ap);
	va_end(ap);
	panic();
}

void
log_syspanic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	sysemit(fmt, ap);
	va_end(ap);
	panic();
}
