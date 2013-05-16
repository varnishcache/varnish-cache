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
#include "vut.h"

static void
usage(void)
{
	fprintf(stderr, "usage: varnishlog ...\n");
	exit(1);
}

int
main(int argc, char * const *argv)
{
	char opt;

	struct VUT *vut;
	struct VSL_data *vsl;
	struct VSM_data *vsm;
	struct VSL_cursor *c;
	struct VSLQ *q;
	int i;

	vut = VUT_New();
	AN(vut);
	vsl = VSL_New();
	AN(vsl);
	vsm = VSM_New();
	AN(vsm);

	while ((opt = getopt(argc, argv, "dg:n:r:v")) != -1) {
		switch (opt) {
		case 'n':
			/* Instance name */
			if (VSM_n_Arg(vsm, optarg) > 0)
				break;
		default:
			if (!VSL_Arg(vsl, opt, optarg) &&
			    !VUT_Arg(vut, opt, optarg))
				usage();
		}
	}

	/* Create cursor */
	if (VSM_Open(vsm))
		VUT_Error(1, "VSM_Open: %s", VSM_Error(vsm));
	c = VSL_CursorVSM(vsl, vsm, !vut->d_opt);
	if (c == NULL)
		VUT_Error(1, "VSL_CursorVSM: %s", VSL_Error(vsl));

	/* Create query */
	q = VSLQ_New(vsl, &c, vut->g_arg, argv[optind]);
	if (q == NULL)
		VUT_Error(1, "VSLQ_New: %s", VSL_Error(vsl));
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
			q = VSLQ_New(vsl, &c, vut->g_arg, argv[optind]);
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
				VUT_Error(0, "Log abandoned - reopening");
				VSM_Close(vsm);
			} else if (i < -2) {
				/* Overrun */
				VUT_Error(0, "Log overrun");
			}
		} else {
			VUT_Error(1, "Unexpected: %d", i);
		}
	}

	if (q != NULL) {
		VSLQ_Flush(q, VSL_PrintTransactions, stdout);
		VSLQ_Delete(&q);
		AZ(q);
	}
	VSL_Delete(vsl);
	VSM_Delete(vsm);
	VUT_Delete(&vut);

	exit(0);
}
