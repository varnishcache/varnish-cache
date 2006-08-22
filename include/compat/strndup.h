/*
 * $Id$
 */

#ifndef COMPAT_STRNDUP_H_INCLUDED
#define COMPAT_STRNDUP_H_INCLUDED

#ifndef HAVE_STRNDUP
char *strndup(const char *str, size_t len);
#endif

#endif
