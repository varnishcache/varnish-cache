/*
 * $Id$
 */

#ifndef COMPAT_H_INCLUDED
#define COMPAT_H_INCLUDED

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size);
#endif

#endif
