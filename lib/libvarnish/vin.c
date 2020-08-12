/*-
 * Copyright (c) 2007-2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"

#include "vas.h"
#include "vin.h"
#include "vsb.h"

int
VIN_n_Arg(const char *n_arg, char **dir)
{
	char nm[PATH_MAX];
	char dn[PATH_MAX];
	struct vsb vsb[1];
	int i;

	AN(dir);

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

	AN(VSB_init(vsb, dn, sizeof dn));

	if (*nm == '/')
		i = VSB_printf(vsb, "%s/", nm);
	else
		i = VSB_printf(vsb, "%s/%s/", VARNISH_STATE_DIR, nm);

	if (i != 0) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	AZ(VSB_finish(vsb));

	*dir = strdup(VSB_data(vsb));
	if (*dir == NULL)
		return (-1);

	VSB_fini(vsb);
	return (0);
}
