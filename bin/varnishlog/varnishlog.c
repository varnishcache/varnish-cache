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
	fprintf(stderr, "\n");

	if (status)
		exit(status);
}

static void
usage(void)
{
	fprintf(stderr, "usage: varnishlog ...\n");
	exit(1);
}

int
main(int argc, char * const *argv)
{
	char optchar;
	int d_opt = 0;

	struct VSL_data *vsl;
	struct VSM_data *vsm;
	struct VSL_cursor *c;
	struct VSLQ *q;
	int grouping = VSL_g_vxid;
	int i;

	vsl = VSL_New();
	AN(vsl);
	vsm = VSM_New();
	AN(vsm);

	while ((optchar = getopt(argc, argv, "dg:n:r:v")) != -1) {
		switch (optchar) {
		case 'd':
			d_opt = 1;
			break;
		case 'g':
			/* Grouping mode */
			grouping = VSLQ_Name2Grouping(optarg, -1);
			if (grouping == -2)
				error(1, "Ambiguous grouping type: %s", optarg);
			else if (grouping < 0)
				error(1, "Unknown grouping type: %s", optarg);
			break;
		case 'n':
			/* Instance name */
			if (VSM_n_Arg(vsm, optarg) > 0)
				break;
		default:
			if (!VSL_Arg(vsl, optchar, optarg))
				usage();
		}
	}
	assert(grouping >= 0 && grouping <= VSL_g_session);

	/* Create cursor */
	if (VSM_Open(vsm))
		error(1, "VSM_Open: %s", VSM_Error(vsm));
	c = VSL_CursorVSM(vsl, vsm, !d_opt);
	if (c == NULL)
		error(1, "VSL_CursorVSM: %s", VSL_Error(vsl));

	/* Create query */
	q = VSLQ_New(vsl, &c, grouping, argv[optind]);
	if (q == NULL)
		error(1, "VSLQ_New: %s", VSL_Error(vsl));
	AZ(c);

	while (1) {
		while (q == NULL) {
			VTIM_sleep(0.1);
			if (VSM_Open(vsm)) {
				VSM_ResetError(vsm);
				continue;
			}
			c = VSL_CursorVSM(vsl, vsm, 1);
			if (c == NULL) {
				VSL_ResetError(vsl);
				continue;
			}
			q = VSLQ_New(vsl, &c, grouping, argv[optind]);
			AN(q);
			AZ(c);
		}

		i = VSLQ_Dispatch(q, VSL_PrintTransactions, stdout);
		if (i == 0) {
			/* Nothing to do but wait */
			VTIM_sleep(0.01);
		} else if (i == -1) {
			/* EOF */
			break;
		} else if (i <= -2) {
			/* XXX: Make continuation optional */
			VSLQ_Flush(q, VSL_PrintTransactions, stdout);
			VSLQ_Delete(&q);
			AZ(q);
			if (i == -2) {
				/* Abandoned */
				error(0, "Log abandoned - reopening");
				VSM_Close(vsm);
			} else if (i < -2) {
				/* Overrun */
				error(0, "Log overrun");
			}
		} else {
			error(1, "Unexpected: %d", i);
		}
	}

	if (q != NULL) {
		VSLQ_Flush(q, VSL_PrintTransactions, stdout);
		VSLQ_Delete(&q);
		AZ(q);
	}
	VSL_Delete(vsl);
	VSM_Delete(vsm);

	exit(0);
}
