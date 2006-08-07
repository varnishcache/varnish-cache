/*
 * $Id$
 *
 */

#include <stdarg.h>
#include <stdio.h>

#include "compat.h"

#ifndef HAVE_ASPRINTF
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
