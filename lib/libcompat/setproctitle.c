/*
 * $Id$
 */

#ifndef HAVE_SETPROCTITLE

#include <stdarg.h>

#include "compat/setproctitle.h"

void
setproctitle(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	/* XXX */
	va_end(ap);
}
#endif
