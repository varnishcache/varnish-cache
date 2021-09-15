/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Display a standardized version message.
 */

#include "config.h"

#include <stdio.h>

#include "vdef.h"
#include "vas.h"
#include "vcs.h"
#include "vcs_version.h"

void
VCS_Message(const char *progname)
{
	fprintf(stderr, "%s %s", progname, VCS_String("M"));
}

const char *
VCS_String(const char *which)
{
	AN(which);
	assert(which[1] == '\0');

	switch(*which) {
	case 'T':
		return (PACKAGE_TARNAME);
	case 'P':
		return (PACKAGE_VERSION);
	case 'R':
		return (VCS_Version);
	case 'V':
		return (
		    PACKAGE_TARNAME
		    "-" PACKAGE_VERSION
		    " revision " VCS_Version
		);
	case 'M':
		return (
		    "("
		    PACKAGE_TARNAME
		    "-" PACKAGE_VERSION
		    " revision " VCS_Version
		    ")"
		    "\n"
		    "Copyright (c) 2006 Verdens Gang AS\n"
		    "Copyright (c) 2006-2021 Varnish Software\n"
		);
	default:
		WRONG("Wrong argument to VCS_String");
	}
	NEEDLESS(return (0));
}
