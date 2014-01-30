/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
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
 * Common functions for the utilities
 */

#include "config.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#include "compat/daemon.h"
#include "vpf.h"
#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vtim.h"
#include "vas.h"
#include "miniobj.h"
#include "vcs.h"

#include "vut.h"

struct VUT VUT;

static void
vut_vpf_remove(void)
{
	if (VUT.pfh) {
		VPF_Remove(VUT.pfh);
		VUT.pfh = NULL;
	}
}

static void
vut_sighup(int sig)
{
	(void)sig;
	VUT.sighup = 1;
}

static void
vut_sigint(int sig)
{
	(void)sig;
	VUT.sigint = 1;
}

static void
vut_sigusr1(int sig)
{
	(void)sig;
	VUT.sigusr1 = 1;
}

void
VUT_Error(int status, const char *fmt, ...)
{
	va_list ap;

	AN(fmt);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	if (status)
		exit(status);
}

int
VUT_g_Arg(const char *arg)
{

	VUT.g_arg = VSLQ_Name2Grouping(arg, -1);
	if (VUT.g_arg == -2)
		VUT_Error(1, "Ambiguous grouping type: %s", arg);
	else if (VUT.g_arg < 0)
		VUT_Error(1, "Unknown grouping type: %s", arg);
	return (1);
}

int
VUT_Arg(int opt, const char *arg)
{
	int i;

	switch (opt) {
	case 'd':
		/* Head */
		VUT.d_opt = 1;
		return (1);
	case 'D':
		/* Daemon mode */
		VUT.D_opt = 1;
		return (1);
	case 'g':
		/* Grouping */
		return (VUT_g_Arg(arg));
	case 'n':
		/* Varnish instance */
		if (VUT.vsm == NULL)
			VUT.vsm = VSM_New();
		AN(VUT.vsm);
		if (VSM_n_Arg(VUT.vsm, arg) <= 0)
			VUT_Error(1, "%s", VSM_Error(VUT.vsm));
		return (1);
	case 'N':
		/* Varnish stale VSM file */
		if (VUT.vsm == NULL)
			VUT.vsm = VSM_New();
		AN(VUT.vsm);
		if (VSM_N_Arg(VUT.vsm, arg) <= 0)
			VUT_Error(1, "%s", VSM_Error(VUT.vsm));
		VUT.d_opt = 1;	/* Enforces -d */
		return (1);
	case 'P':
		/* PID file */
		REPLACE(VUT.P_arg, arg);
		return (1);
	case 'q':
		/* Query to use */
		REPLACE(VUT.q_arg, arg);
		return (1);
	case 'r':
		/* Binary file input */
		REPLACE(VUT.r_arg, arg);
		return (1);
	case 'V':
		/* Print version number and exit */
		VCS_Message(VUT.progname);
		exit(1);
	default:
		AN(VUT.vsl);
		i = VSL_Arg(VUT.vsl, opt, arg);
		if (i < 0)
			VUT_Error(1, "%s", VSL_Error(VUT.vsl));
		return (i);
	}
}

void
VUT_Init(const char *progname)
{
	VUT.progname = progname;
	REPLACE(VUT.name, "");
	VUT.g_arg = VSL_g_vxid;
	AZ(VUT.vsl);
	VUT.vsl = VSL_New();
	AN(VUT.vsl);
}

void
VUT_Setup(void)
{
	struct VSL_cursor *c;

	AN(VUT.vsl);

	/* Input */
	if (VUT.r_arg && VUT.vsm)
		VUT_Error(1, "Can't have both -n and -r options");
	if (VUT.r_arg) {
		REPLACE(VUT.name, VUT.r_arg);
		c = VSL_CursorFile(VUT.vsl, VUT.r_arg, 0);
	} else {
		if (VUT.vsm == NULL)
			/* Default uses VSM with n=hostname */
			VUT.vsm = VSM_New();
		AN(VUT.vsm);
		if (VSM_Open(VUT.vsm))
			VUT_Error(1, "Can't open VSM file (%s)",
			    VSM_Error(VUT.vsm));
		REPLACE(VUT.name, VSM_Name(VUT.vsm));
		c = VSL_CursorVSM(VUT.vsl, VUT.vsm,
		    (VUT.d_opt ? VSL_COPT_TAILSTOP : VSL_COPT_TAIL)
		    | VSL_COPT_BATCH);
	}
	if (c == NULL)
		VUT_Error(1, "Can't open log (%s)", VSL_Error(VUT.vsl));

	/* Create query */
	VUT.vslq = VSLQ_New(VUT.vsl, &c, VUT.g_arg, VUT.q_arg);
	if (VUT.vslq == NULL)
		VUT_Error(1, "Query expression error:\n%s", VSL_Error(VUT.vsl));
	AZ(c);

	/* Signal handlers */
	(void)signal(SIGHUP, vut_sighup);
	(void)signal(SIGINT, vut_sigint);
	(void)signal(SIGTERM, vut_sigint);
	(void)signal(SIGUSR1, vut_sigusr1);

	/* Open PID file */
	if (VUT.P_arg) {
		AZ(VUT.pfh);
		VUT.pfh = VPF_Open(VUT.P_arg, 0644, NULL);
		if (VUT.pfh == NULL)
			VUT_Error(1, "%s: %s", VUT.P_arg, strerror(errno));
	}

	/* Daemon mode */
	if (VUT.D_opt && varnish_daemon(0, 0) == -1)
		VUT_Error(1, "Daemon mode: %s", strerror(errno));

	/* Write PID and setup exit handler */
	if (VUT.pfh != NULL) {
		VPF_Write(VUT.pfh);
		AZ(atexit(vut_vpf_remove));
	}
}

void
VUT_Fini(void)
{
	free(VUT.r_arg);
	free(VUT.P_arg);
	free(VUT.name);

	vut_vpf_remove();
	AZ(VUT.pfh);

	if (VUT.vslq)
		VSLQ_Delete(&VUT.vslq);
	if (VUT.vsl)
		VSL_Delete(VUT.vsl);
	if (VUT.vsm)
		VSM_Delete(VUT.vsm);

	memset(&VUT, 0, sizeof VUT);
}

int
VUT_Main(void)
{
	struct VSL_cursor *c;
	int i = -1;

	AN(VUT.vslq);

	while (!VUT.sigint) {
		if (VUT.sighup && VUT.sighup_f) {
			/* sighup callback */
			VUT.sighup = 0;
			i = (VUT.sighup_f)();
			if (i)
				break;
		}

		if (VUT.sigusr1) {
			/* Flush and report any incomplete records */
			VUT.sigusr1 = 0;
			if (VUT.vslq != NULL)
				VSLQ_Flush(VUT.vslq, VUT.dispatch_f,
				    VUT.dispatch_priv);
		}

		if (VUT.vslq == NULL) {
			/* Reconnect VSM */
			AZ(VUT.r_arg);
			AN(VUT.vsm);
			VTIM_sleep(0.1);
			if (VSM_Open(VUT.vsm)) {
				VSM_ResetError(VUT.vsm);
				continue;
			}
			c = VSL_CursorVSM(VUT.vsl, VUT.vsm,
			    VSL_COPT_TAIL | VSL_COPT_BATCH);
			if (c == NULL) {
				VSL_ResetError(VUT.vsl);
				VSM_Close(VUT.vsm);
				continue;
			}
			VUT.vslq = VSLQ_New(VUT.vsl, &c, VUT.g_arg, VUT.q_arg);
			AN(VUT.vslq);
			AZ(c);
			VUT_Error(0, "Log reaquired");
		}

		i = VSLQ_Dispatch(VUT.vslq, VUT.dispatch_f, VUT.dispatch_priv);
		if (i == 1)
			/* Call again */
			continue;
		else if (i == 0) {
			/* Nothing to do but wait */
			if (VUT.idle_f) {
				i = (VUT.idle_f)();
				if (i)
					break;
			}
			VTIM_sleep(0.01);
			continue;
		} else if (i == -1) {
			/* EOF */
			break;
		}

		if (VUT.vsm == NULL)
			break;

		/* XXX: Make continuation optional */

		VSLQ_Flush(VUT.vslq, VUT.dispatch_f, VUT.dispatch_priv);
		VSLQ_Delete(&VUT.vslq);
		AZ(VUT.vslq);

		if (i == -2) {
			/* Abandoned */
			VUT_Error(0, "Log abandoned");
			VSM_Close(VUT.vsm);
		} else if (i < -2) {
			/* Overrun */
			VUT_Error(0, "Log overrun");
		}
	}

	return (i);
}
