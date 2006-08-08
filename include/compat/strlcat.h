/*
 * $Id$
 */

#ifndef COMPAT_STRLCAT_H_INCLUDED
#define COMPAT_STRLCAT_H_INCLUDED

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size);
#endif

#endif
