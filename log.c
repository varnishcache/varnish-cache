/*
 * $Id$
 */

#include <sys/signal.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "varnish.h"

static void
emit(const char *fmt, va_list ap)
{
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
