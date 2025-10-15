/*-
 * Copyright 2025 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
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
 * Wrapper to call vtest with additional extensions
 *
 * The problem solved by this code can easily also be solved with a two-liner
 * shell script, except that we would need to implement all the mechanics to
 * locate libvtc_varnish.so while it is still uninstalled. But for this simple
 * program, libtool handles it all.
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"

const char *addargs[]= {
	"vtest",
	"-E", "libvtc_varnish.so",
	"-E", "libvtc_builtwith.so"
};

int
main(int argc, char **argv)
{
	size_t n = vcountof(addargs);
	char **narg = calloc(argc + n, sizeof *narg);
	int r;

	if (narg == NULL)
		return (ENOMEM);
	memcpy(&narg[0], &addargs[0], n * sizeof *narg);
	memcpy(&narg[n], &argv[1], (argc - 1) * sizeof *narg);
	r = execvp(narg[0], narg);
	fprintf(stderr, "execvp(%s): %d (%s)\n", narg[0], errno,
		strerror(errno));
	return (r);
}
