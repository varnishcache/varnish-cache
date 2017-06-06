/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <math.h>

#include "compat/daemon.h"
#include "vdef.h"
#include "vpf.h"
#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vtim.h"
#include "vas.h"
#include "miniobj.h"
#include "vcs.h"
#include "vnum.h"

#include "vut.h"

#include "vapi/voptget.h"

struct VUT VUT;

static int vut_synopsis(const struct vopt_spec *);
static int vut_options(const struct vopt_spec *);

static void
vut_vpf_remove(void)
{
	if (VUT.pfh) {
		AZ(VPF_Remove(VUT.pfh));
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

static int __match_proto__(VSLQ_dispatch_f)
vut_dispatch(struct VSL_data *vsl, struct VSL_transaction * const trans[],
    void *priv)
{
	int i;

	(void)priv;
	if (VUT.k_arg == 0)
		return (-1);	/* End of file */
	AN(VUT.dispatch_f);
	i = VUT.dispatch_f(vsl, trans, VUT.dispatch_priv);
	if (VUT.k_arg > 0)
		VUT.k_arg--;
	if (i >= 0 && VUT.k_arg == 0)
		return (-1);	/* End of file */
	return (i);
}

void
VUT_Error(int status, const char *fmt, ...)
{
	va_list ap;

	assert(status != 0);
	AN(fmt);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

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
	char *p;

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
	case 'k':
		/* Log transaction limit */
		AN(arg);
		VUT.k_arg = (int)strtol(arg, &p, 10);
		if (*p != '\0' || VUT.k_arg <= 0)
			VUT_Error(1, "-k: Invalid number '%s'", arg);
		return (1);
	case 'n':
		/* Varnish instance name */
		REPLACE(VUT.n_arg, arg);
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
	case 't':
		/* VSM connect timeout */
		AN(arg);
		if (!strcasecmp("off", arg))
			VUT.t_arg = -1.;
		else {
			VUT.t_arg = VNUM(arg);
			if (isnan(VUT.t_arg))
				VUT_Error(1, "-t: Syntax error");
			if (VUT.t_arg < 0.)
				VUT_Error(1, "-t: Range error");
		}
		return (1);
	case 'V':
		/* Print version number and exit */
		VCS_Message(VUT.progname);
		exit(0);
	default:
		AN(VUT.vsl);
		i = VSL_Arg(VUT.vsl, opt, arg);
		if (i < 0)
			VUT_Error(1, "%s", VSL_Error(VUT.vsl));
		return (i);
	}
}

void
VUT_Init(const char *progname, int argc, char * const *argv,
    const struct vopt_spec *voc)
{

	if (argc == 2 && !strcmp(argv[1], "--synopsis"))
		exit(vut_synopsis(voc));
	if (argc == 2 && !strcmp(argv[1], "--options"))
		exit(vut_options(voc));

	VUT.progname = progname;
	VUT.g_arg = VSL_g_vxid;
	AZ(VUT.vsl);
	VUT.vsl = VSL_New();
	AN(VUT.vsl);
	VUT.k_arg = -1;
	VUT.t_arg = 5.;
}

void
VUT_Setup(void)
{
	struct VSL_cursor *c;
	double t_start;
	int i;

	AN(VUT.vsl);
	AZ(VUT.vsm);
	AZ(VUT.vslq);

	/* Check input arguments (2 used for bug in FlexeLint) */
	if ((VUT.n_arg == NULL ? 0 : 2) +
	    (VUT.r_arg == NULL ? 0 : 2) > 2)
		VUT_Error(1, "Only one of -n and -r options may be used");

	/* Create and validate the query expression */
	VUT.vslq = VSLQ_New(VUT.vsl, NULL, VUT.g_arg, VUT.q_arg);
	if (VUT.vslq == NULL)
		VUT_Error(1, "Query expression error:\n%s",
		    VSL_Error(VUT.vsl));

	/* Setup input */
	if (VUT.r_arg) {
		c = VSL_CursorFile(VUT.vsl, VUT.r_arg, 0);
		if (c == NULL)
			VUT_Error(1, "%s", VSL_Error(VUT.vsl));
	} else {
		VUT.vsm = VSM_New();
		AN(VUT.vsm);
		if (VUT.n_arg && VSM_n_Arg(VUT.vsm, VUT.n_arg) <= 0)
			VUT_Error(1, "%s", VSM_Error(VUT.vsm));
		t_start = NAN;
		c = NULL;
		while (1) {
			i = VSM_Open(VUT.vsm);
			if (!i)
				c = VSL_CursorVSM(VUT.vsl, VUT.vsm,
				    (VUT.d_opt ? VSL_COPT_TAILSTOP :
					VSL_COPT_TAIL)
				    | VSL_COPT_BATCH);
			if (c)
				break;

			if (isnan(t_start) && VUT.t_arg > 0.) {
				fprintf(stderr, "Cannot open log -"
				    " retrying for %.0f seconds\n", VUT.t_arg);
				t_start = VTIM_real();
			}
			VSM_Close(VUT.vsm);
			if (VUT.t_arg <= 0.)
				break;
			if (VTIM_real() - t_start > VUT.t_arg)
				break;

			VSM_ResetError(VUT.vsm);
			VSL_ResetError(VUT.vsl);
			VTIM_sleep(0.5);
		}

		if (VUT.t_arg >= 0. && (i || !c)) {
			if (i)
				VUT_Error(1, "%s", VSM_Error(VUT.vsm));
			else
				VUT_Error(1, "%s", VSL_Error(VUT.vsl));
		} else if (!isnan(t_start))
			fprintf(stderr, "Log opened\n");
	}

	if (c)
		VSLQ_SetCursor(VUT.vslq, &c);
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
		AZ(VPF_Write(VUT.pfh));
		AZ(atexit(vut_vpf_remove));
	}
}

void
VUT_Fini(void)
{
	free(VUT.n_arg);
	free(VUT.r_arg);
	free(VUT.P_arg);

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
			i = VUT.sighup_f();
			if (i)
				break;
		}

		if (VUT.sigusr1) {
			/* Flush and report any incomplete records */
			VUT.sigusr1 = 0;
			(void)VSLQ_Flush(VUT.vslq, vut_dispatch, NULL);
		}

		if (VUT.vsm != NULL && !VSM_IsOpen(VUT.vsm)) {
			/* Reconnect VSM */
			AZ(VUT.r_arg);
			VTIM_sleep(0.1);
			if (VSM_Open(VUT.vsm)) {
				VSM_ResetError(VUT.vsm);
				continue;
			}
			c = VSL_CursorVSM(VUT.vsl, VUT.vsm,
			    (VUT.d_opt ? VSL_COPT_TAILSTOP : VSL_COPT_TAIL)
			    | VSL_COPT_BATCH);
			if (c == NULL) {
				VSL_ResetError(VUT.vsl);
				VSM_Close(VUT.vsm);
				continue;
			}
			VSLQ_SetCursor(VUT.vslq, &c);
			AZ(c);
			fprintf(stderr, "Log reacquired\n");
		}

		i = VSLQ_Dispatch(VUT.vslq, vut_dispatch, NULL);
		if (i == 1)
			/* Call again */
			continue;
		else if (i == 0) {
			/* Nothing to do but wait */
			if (VUT.idle_f) {
				i = VUT.idle_f();
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

		(void)VSLQ_Flush(VUT.vslq, vut_dispatch, NULL);

		if (i == -2)
			/* Abandoned */
			fprintf(stderr, "Log abandoned\n");
		else if (i < -2)
			/* Overrun */
			fprintf(stderr, "Log overrun\n");

		VSM_Close(VUT.vsm);
	}

	return (i);
}

/**********************************************************************/


static void
print_nobrackets(const char *s)
{
	const char *e;

	/* Remove whitespace */
	while (isspace(*s))
		s++;
	e = s + strlen(s);
	while (e > s && isspace(e[-1]))
		e--;

	/* Remove outer layer brackets if present */
	if (e > s && *s == '[' && e[-1] == ']') {
		s++;
		e--;
	}

	printf("%.*s", (int)(e - s), s);
}

static void
print_tabbed(const char *string, int tabs)
{
	int i;
	const char *c;

	for (c = string; *c; c++) {
		if (c == string || *(c - 1) == '\n')
			for (i = 0; i < tabs; i++)
				printf("\t");
		printf("%c", *c);
	}
}

static void
print_opt(const struct vopt_list *opt)
{
	print_nobrackets(opt->synopsis);
	printf("\n\n");
	print_tabbed(opt->ldesc, 1);
	printf("\n\n");
}

static int
vut_synopsis(const struct vopt_spec *voc)
{
	printf(".. |synopsis| replace:: %s\n", voc->vopt_synopsis);
	return (0);
}

static int
vut_options(const struct vopt_spec *voc)
{
	int i;

	for (i = 0; i < voc->vopt_list_n; i++)
		print_opt(&voc->vopt_list[i]);
	return (0);
}
