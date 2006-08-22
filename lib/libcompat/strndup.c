/*
 * $Id$
 *
 */

#ifndef HAVE_STRNDUP

#include <stdlib.h>
#include <string.h>

#include "compat/strndup.h"

char *
strndup(const char *str, size_t len)
{
	char *dup;

	/* wasteful if len is large and str is short */
	if ((dup = calloc(len + 1, 1)) != NULL)
		strncpy(dup, str, len);
	return (dup);
}

#endif
