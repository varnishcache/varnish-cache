/*
 * $Id$
 *
 */

#ifndef HAVE_ASPRINTF

#include <stdarg.h>
#include <stdio.h>

#include "compat/asprintf.h"
#include "compat/vasprintf.h"

int
asprintf(char **strp, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vasprintf(strp, fmt, ap);
	va_end(ap);
	return (ret);
}
#endif
