/*
 * $Id$
 */

#ifndef COMPAT_ASPRINTF_H_INCLUDED
#define COMPAT_ASPRINTF_H_INCLUDED

#ifndef HAVE_ASPRINTF
int asprintf(char **strp, const char *fmt, ...);
#endif

#endif
