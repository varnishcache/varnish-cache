/*
 * $Id$
 */

#ifndef COMPAT_H_INCLUDED
#define COMPAT_H_INCLUDED

#ifndef HAVE_VASPRINTF
#ifdef va_start /* make sure <stdarg.h> is in scope */
int vasprintf(char **strp, const char *fmt, va_list ap);
#endif
#endif

#ifndef HAVE_ASPRINTF
int asprintf(char **strp, const char *fmt, ...);
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size);
#endif

#endif
