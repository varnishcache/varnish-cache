/*
 * $Id$
 */

#ifndef COMPAT_STRLCPY_H_INCLUDED
#define COMPAT_STRLCPY_H_INCLUDED

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size);
#endif

#endif
