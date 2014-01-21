/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
#include "vpf.h"
#include "vsb.h"
#include "vut.h"
#include "miniobj.h"

static const char progname[] = "varnishlog";

struct log {
	/* Options */
	int		a_opt;
	int		B_opt;
	char		*w_arg;

	/* State */
	FILE		*fo;
} LOG;

static void
usage(int status)
{
	const char **opt;
	fprintf(stderr, "Usage: %s <options>\n\n", progname);
	fprintf(stderr, "Options:\n");
	for (opt = vopt_usage; *opt != NULL; opt += 2)
		fprintf(stderr, "  %-25s %s\n", *opt, *(opt + 1));
	exit(status);
}

static void
openout(int append)
{

	AN(LOG.w_arg);
	if (LOG.B_opt)
		LOG.fo = VSL_WriteOpen(VUT.vsl, LOG.w_arg, append, 0);
	else
		LOG.fo = fopen(LOG.w_arg, append ? "a" : "w");
	if (LOG.fo == NULL)
		VUT_Error(1, "Can't open output file (%s)",
		    LOG.B_opt ? VSL_Error(VUT.vsl) : strerror(errno));
	VUT.dispatch_priv = LOG.fo;
}

static int __match_proto__(VUT_cb_f)
rotateout(void)
{

	AN(LOG.w_arg);
	AN(LOG.fo);
	fclose(LOG.fo);
	openout(1);
	AN(LOG.fo);
	return (0);
}

static int __match_proto__(VUT_cb_f)
flushout(void)
{

	AN(LOG.fo);
	if (fflush(LOG.fo))
		return (-5);
	return (0);
}

int
main(int argc, char * const *argv)
{
	int opt;

	memset(&LOG, 0, sizeof LOG);
	VUT_Init(progname);

	while ((opt = getopt(argc, argv, vopt_optstring)) != -1) {
		switch (opt) {
		case 'a':
			/* Append to file */
			LOG.a_opt = 1;
			break;
		case 'B':
			/* Binary output */
			LOG.B_opt = 1;
			break;
		case 'h':
			/* Usage help */
			usage(0);
			break;
		case 'w':
			/* Write to file */
			REPLACE(LOG.w_arg, optarg);
			break;
		default:
			if (!VUT_Arg(opt, optarg))
				usage(1);
		}
	}

	if (optind != argc)
		usage(1);

	/* Setup output */
	if (LOG.B_opt)
		VUT.dispatch_f = VSL_WriteTransactions;
	else
		VUT.dispatch_f = VSL_PrintTransactions;
	if (LOG.w_arg) {
		openout(LOG.a_opt);
		AN(LOG.fo);
		VUT.sighup_f = rotateout;
	} else
		LOG.fo = stdout;
	VUT.idle_f = flushout;

	VUT_Setup();
	VUT_Main();
	VUT_Fini();

	(void)flushout();

	exit(0);
}
