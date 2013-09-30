/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Log tailer for Varnish
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>

#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "vcs.h"
#include "vpf.h"
#include "vsb.h"
#include "vtim.h"
#include "vut.h"

static void
usage(void)
{
	const char **opt;
	fprintf(stderr, "Usage: varnishlog <options>\n\n");
	fprintf(stderr, "Options:\n");
	for (opt = vopt_usage; *opt != NULL; opt += 2)
		fprintf(stderr, "  %-25s %s\n", *opt, *(opt + 1));
	exit(1);
}

int
main(int argc, char * const *argv)
{
	char opt;

	VUT_Init();

	while ((opt = getopt(argc, argv, vopt_optstring)) != -1) {
		switch (opt) {
		default:
			if (!VUT_Arg(opt, optarg))
				usage();
		}
	}

	if (optind != argc)
		usage();

	VUT_Setup();
	VUT_Main(NULL, NULL);
	VUT_Fini();

	exit(0);
}
