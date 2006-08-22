/*
 * $Id$
 *
 */

#ifndef HAVE_STRNDUP

#include <stdlib.h>
#include <string.h>

#ifndef HAVE_STRLCPY
#include "compat/strlcpy.h"
#endif

#include "compat/strndup.h"

char *
strndup(const char *str, size_t len)
{
	char *dup;

	/* wasteful if len is large and str is short */
	if ((dup = malloc(len + 1)) != NULL)
		strlcpy(dup, str, len + 1);
	return (dup);
}

#endif
