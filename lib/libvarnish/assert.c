/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "libvarnish.h"

void
lbv_xxxassert(const char *func, const char *file, int line, const char *cond, int err)
{

	fprintf(stderr,
	    "Missing errorhandling code in %s(), %s line %d:\n"
	    "  Condition(%s) not true.\n"
	    "  errno = %d (%s)\n", func, file, line, cond, err, strerror(err));
	abort();
}

void
lbv_assert(const char *func, const char *file, int line, const char *cond, int err)
{

	fprintf(stderr,
	    "Assert error in %s(), %s line %d:\n"
	    "  Condition(%s) not true.\n"
	    "  errno = %d (%s)\n", func, file, line, cond, err, strerror(err));
	abort();
}

