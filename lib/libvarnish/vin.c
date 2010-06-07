/*-
 * Copyright (c) 2007-2009 Linpro AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * XXX: NB: also used in libvarnishapi
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "libvarnish.h"
#include "shmlog.h"

int
vin_n_arg(const char *n_arg, char **name, char **dir, char **vsl)
{
	char nm[PATH_MAX];
	char dn[PATH_MAX];


	/* First: determine the name */

	if (n_arg == NULL || *n_arg == '\0') {
		if (gethostname(nm, sizeof nm) != 0)
			return (-1);
	} else if (strlen(n_arg) >= sizeof nm) {
		/* preliminary length check to avoid overflowing nm */
		errno = ENAMETOOLONG;
		return (-1);
	} else
		bprintf(nm, "%s", n_arg);


	/* Second: find the directory name */

	if (*nm == '/')
		strcpy(dn, nm);
	else if (strlen(VARNISH_STATE_DIR) + 1 + strlen(nm) >= sizeof dn){
		/* preliminary length check to avoid overflowing dm */
		errno = ENAMETOOLONG;
		return (-1);
	} else {
		bprintf(dn, "%s/%s", VARNISH_STATE_DIR, nm);
	}

	/* Definitive length check */
	if (strlen(dn) + 1 + strlen(SHMLOG_FILENAME) >= sizeof dn) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	strcat(dn, "/");

	if (name != NULL) {
		*name = strdup(nm);
		if (*name == NULL)
			return (-1);
	}
	if (dir != NULL) {
		*dir = strdup(dn);
		if (*dir == NULL)
			return (-1);
	}
	if (vsl != NULL) {
		bprintf(nm, "%s%s", dn, SHMLOG_FILENAME);
		*vsl = strdup(nm);
		if (*vsl == NULL)
			return (-1);
	}
	return (0);
}

char *
vin_L_arg(unsigned L_arg)
{
	char p[PATH_MAX];

	assert(VIN_L_OK(L_arg));
	bprintf(p, "/tmp/varnish_L_%u", L_arg);
	return (strdup(p));
}
