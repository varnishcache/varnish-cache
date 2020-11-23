/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Log tailer for Varnish
 */

#include "config.h"

#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#define VOPT_DEFINITION
#define VOPT_INC "varnishlog_options.h"

#include "vdef.h"

#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "vut.h"
#include "miniobj.h"

static struct VUT *vut;

static struct log {
	/* Options */
	int		a_opt;
	int		A_opt;
	int		u_opt;
	char		*w_arg;

	/* State */
	FILE		*fo;
} LOG;

static void
openout(int append)
{

	AN(LOG.w_arg);
	if (LOG.A_opt)
		LOG.fo = fopen(LOG.w_arg, append ? "a" : "w");
	else
		LOG.fo = VSL_WriteOpen(vut->vsl, LOG.w_arg, append, LOG.u_opt);
	if (LOG.fo == NULL)
		VUT_Error(vut, 2, "Cannot open output file (%s)",
		    LOG.A_opt ? strerror(errno) : VSL_Error(vut->vsl));
	vut->dispatch_priv = LOG.fo;
}

static int v_matchproto_(VUT_cb_f)
rotateout(struct VUT *v)
{

	assert(v == vut);
	AN(LOG.w_arg);
	AN(LOG.fo);
	(void)fclose(LOG.fo);
	openout(1);
	AN(LOG.fo);
	return (0);
}

static int v_matchproto_(VUT_cb_f)
flushout(struct VUT *v)
{

	assert(v == vut);
	AN(LOG.fo);
	if (fflush(LOG.fo))
		return (-5);
	return (0);
}

int
main(int argc, char * const *argv)
{
	int opt;

	vut = VUT_InitProg(argc, argv, &vopt_spec);
	AN(vut);
	memset(&LOG, 0, sizeof LOG);

	while ((opt = getopt(argc, argv, vopt_spec.vopt_optstring)) != -1) {
		switch (opt) {
		case 'a':
			/* Append to file */
			LOG.a_opt = 1;
			break;
		case 'A':
			/* Text output */
			LOG.A_opt = 1;
			break;
		case 'h':
			/* Usage help */
			VUT_Usage(vut, &vopt_spec, 0);
		case 'u':
			/* Unbuffered output */
			LOG.u_opt = 1;
			break;
		case 'w':
			/* Write to file */
			REPLACE(LOG.w_arg, optarg);
			break;
		default:
			if (!VUT_Arg(vut, opt, optarg))
				VUT_Usage(vut, &vopt_spec, 1);
		}
	}

	if (optind != argc)
		VUT_Usage(vut, &vopt_spec, 1);

	if (vut->D_opt && !LOG.w_arg)
		VUT_Error(vut, 1, "Missing -w option");

	/* Setup output */
	if (LOG.A_opt || !LOG.w_arg) {
		vut->dispatch_f = VSL_PrintTransactions;
	} else {
		vut->dispatch_f = VSL_WriteTransactions;
		/*
		 * inefficient but not crossing API layers
		 * first x argument avoids initial suppression of all tags
		 */
		AN(VUT_Arg(vut, 'x', "Link"));
		AN(VUT_Arg(vut, 'i', "Link"));
		AN(VUT_Arg(vut, 'i', "Begin"));
		AN(VUT_Arg(vut, 'i', "End"));
	}
	if (LOG.w_arg) {
		openout(LOG.a_opt);
		AN(LOG.fo);
		if (vut->D_opt)
			vut->sighup_f = rotateout;
	} else
		LOG.fo = stdout;
	vut->idle_f = flushout;

	VUT_Setup(vut);
	(void)VUT_Main(vut);
	VUT_Fini(&vut);

	(void)flushout(NULL);

	exit(0);
}
