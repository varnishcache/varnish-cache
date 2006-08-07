/*
 * $Id$
 *
 */

#include <stdarg.h>
#include <stdio.h>

#include "compat.h"

#ifndef HAVE_VASPRINTF
int
asprintf(char **strp, const char *fmt, va_list ap)
{
	va_list ap, aq;
	int ret;

	va_copy(aq, ap);
	ret = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);
	if ((*strp = malloc(ret + 1)) == NULL)
		return (-1);
	ret = vsnprintf(*strp, ret + 1, fmt, ap);
	return (ret);
}
#endif
