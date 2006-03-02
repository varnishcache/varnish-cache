/*
 * $Id$
 */

#include "config.h"

#include <sys/types.h>

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

#include <varnishapi.h>

void
varnish_panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, fmt, ap);
	va_end(ap);
	signal(SIGABRT, SIG_DFL);
	raise(SIGABRT);
}
