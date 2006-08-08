/*
 * $Id$
 */

#ifndef COMPAT_VASPRINTF_H_INCLUDED
#define COMPAT_VASPRINTF_H_INCLUDED

#ifndef HAVE_VASPRINTF
int vasprintf(char **strp, const char *fmt, va_list ap);
#endif

#endif
