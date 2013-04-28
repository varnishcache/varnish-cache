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
#include "vas.h"
#include "vcs.h"
#include "vpf.h"
#include "vsb.h"
#include "vtim.h"

#include "compat/daemon.h"

static void error(int status, const char *fmt, ...)
	__printflike(2, 3);

static void
error(int status, const char *fmt, ...)
{
	va_list ap;

	AN(fmt);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap); /* XXX: syslog on daemon */
	va_end(ap);

	if (status)
		exit(status);
}

static struct VSL_cursor *
cursor_vsm(struct VSL_data *vsl, struct VSM_data *vsm, int status, int front)
{
	struct VSL_cursor *c;

	AN(vsm);
	if (VSM_Open(vsm)) {
		if (status)
			error(status, "VSM: %s", VSM_Error(vsm));
		VSM_ResetError(vsm);
		return (NULL);
	}
	c = VSL_CursorVSM(vsl, vsm, front);
	if (c == NULL) {
		if (status)
			error(status, "VSL: %s", VSL_Error(vsl));
		VSL_ResetError(vsl);
		return (NULL);
	}
	return (c);
}

static void
usage(void)
{
	fprintf(stderr, "usage: varnishlog ...\n");
	exit(1);
}

static void
do_raw(struct VSL_data *vsl, struct VSM_data *vsm)
{
	struct VSL_cursor *c;
	int i;

	c = cursor_vsm(vsl, vsm, 1, 1);
	AN(c);

	i = 0;
	while (1) {
		while (c == NULL) {
			c = cursor_vsm(vsl, vsm, 0, 1);
			if (c != NULL) {
				error(0, "Log reopened\n");
				break;
			}
			VTIM_sleep(1.);
		}

		i = VSL_Next(c);
		if (i == 1) {
			/* Got new record */
			if (VSL_Match(vsl, c))
				(void)VSL_Print(vsl, c, stdout);
		} else if (i == 0) {
			/* Nothing to do but wait */
			VTIM_sleep(0.01);
		} else if (i == -1) {
			/* EOF */
			break;
		} else if (i == -2) {
			/* Abandoned - try reconnect */
			error(0, "Log abandoned, reopening\n");
			VSL_DeleteCursor(c);
			c = NULL;
			VSM_Close(vsm);
		} else if (i < -2) {
			/* Overrun - bail */
			error(1, "Log overrun\n");
		}
	}

	VSL_DeleteCursor(c);
}

int
main(int argc, char * const *argv)
{
	struct VSL_data *vsl;
	struct VSM_data *vsm;
	char c;
	char *r_arg = NULL;

	vsl = VSL_New();
	AN(vsl);
	vsm = VSM_New();
	AN(vsm);

	while ((c = getopt(argc, argv, "n:r:")) != -1) {
		switch (c) {
		case 'r':
			r_arg = optarg;
			break;
		case 'n':
			if (VSM_n_Arg(vsm, optarg) > 0)
				break;
		default:
			usage();
		}
	}

	if (r_arg) {
		/* XXX */
	} else {
		if (VSM_Open(vsm))
			error(1, VSM_Error(vsm));
	}

	do_raw(vsl, vsm);

	VSL_Delete(vsl);
	if (vsm)
		VSM_Delete(vsm);

	exit(0);
}
