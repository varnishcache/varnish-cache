/*
 * $Id$
 *
 * Display a standardized version message.
 */

#include <stdio.h>

#include "libvarnish.h"

void
varnish_version(const char *progname)
{
	fprintf(stderr, "%s (%s-%s)\n", progname,
	    PACKAGE_TARNAME, PACKAGE_VERSION);
	fprintf(stderr, "Copyright (c) 2006 Linpro AS / Verdens Gang AS\n");
}
