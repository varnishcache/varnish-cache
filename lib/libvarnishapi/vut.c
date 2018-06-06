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
#include <sys/stat.h> /* for MUSL */

#include "compat/daemon.h"
#include "vdef.h"
#include "vpf.h"
#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vtim.h"
#include "vas.h"
#include "miniobj.h"
#include "vcs.h"

#include "vut.h"

#include "vapi/voptget.h"

static int vut_synopsis(const struct vopt_spec *);
static int vut_options(const struct vopt_spec *);

static struct vpf_fh	*pfh;
static unsigned		daemonized;

static struct VUT pfh_vut;

static int
vut_daemon(struct VUT *vut)
{
	if (daemonized)
		VUT_Error(vut, 1, "Already running as a daemon");
	daemonized = 1;
	return (varnish_daemon(0, 0));
}

static void
vut_vpf_remove(void)
{

	CHECK_OBJ(&pfh_vut, VUT_MAGIC);
	AN(pfh);
	AN(pfh_vut.P_arg);

	if (VPF_Remove(pfh) != 0)
		VUT_Error(&pfh_vut, 1, "Cannot remove pid file %s: %s",
		    pfh_vut.P_arg, strerror(errno));

	free(pfh_vut.P_arg);
	ZERO_OBJ(&pfh_vut, sizeof pfh_vut);
	pfh = NULL;
}

static int v_matchproto_(VSLQ_dispatch_f)
vut_dispatch(struct VSL_data *vsl, struct VSL_transaction * const trans[],
    void *priv)
{
	struct VUT *vut;
	int i;

	CAST_OBJ_NOTNULL(vut, priv, VUT_MAGIC);

	if (vut->k_arg == 0)
		return (-1);	/* End of file */
	AN(vut->dispatch_f);
	i = vut->dispatch_f(vsl, trans, vut->dispatch_priv);
	if (vut->k_arg > 0)
		vut->k_arg--;
	if (i >= 0 && vut->k_arg == 0)
		return (-1);	/* End of file */
	return (i);
}

//lint -sem(vut_error, r_no)
static void v_noreturn_ v_matchproto_(VUT_error_f)
vut_error(struct VUT *vut, int status, const char *fmt, va_list ap)
{

	CHECK_OBJ_NOTNULL(vut, VUT_MAGIC);
	AN(fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");

	exit(status);
}

void
VUT_Error(struct VUT *vut, int status, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(vut, VUT_MAGIC);
	AN(vut->error_f);
	AN(status);

	va_start(ap, fmt);
	vut->error_f(vut, status, fmt, ap);
	va_end(ap);
	exit(2);
}

int
VUT_Arg(struct VUT *vut, int opt, const char *arg)
{
	int i;
	char *p;

	switch (opt) {
	case 'd':
		/* Head */
		vut->d_opt = 1;
		return (1);
	case 'D':
		/* Daemon mode */
		vut->D_opt = 1;
		return (1);
	case 'g':
		/* Grouping */
		AN(arg);
		vut->g_arg = VSLQ_Name2Grouping(arg, -1);
		if (vut->g_arg == -2)
			VUT_Error(vut, 1, "Ambiguous grouping type: %s", arg);
		else if (vut->g_arg < 0)
			VUT_Error(vut, 1, "Unknown grouping type: %s", arg);
		return (1);
	case 'k':
		/* Log transaction limit */
		AN(arg);
		vut->k_arg = (int)strtol(arg, &p, 10);
		if (*p != '\0' || vut->k_arg <= 0)
			VUT_Error(vut, 1, "-k: Invalid number '%s'", arg);
		return (1);
	case 'n':
		/* Varnish instance name */
		AN(arg);
		REPLACE(vut->n_arg, arg);
		return (1);
	case 'P':
		/* PID file */
		AN(arg);
		REPLACE(vut->P_arg, arg);
		return (1);
	case 'q':
		/* Query to use */
		AN(arg);
		REPLACE(vut->q_arg, arg);
		return (1);
	case 'r':
		/* Binary file input */
		AN(arg);
		REPLACE(vut->r_arg, arg);
		return (1);
	case 't':
		/* VSM connect timeout */
		REPLACE(vut->t_arg, arg);
		return (1);
	case 'V':
		/* Print version number and exit */
		VCS_Message(vut->progname);
		exit(0);
	default:
		AN(vut->vsl);
		i = VSL_Arg(vut->vsl, opt, arg);
		if (i < 0)
			VUT_Error(vut, 1, "%s", VSL_Error(vut->vsl));
		return (i);
	}
}

struct VUT *
VUT_Init(const char *progname, int argc, char * const *argv,
    const struct vopt_spec *voc)
{
	struct VUT *vut;

	AN(progname);
	AN(argv);
	AN(voc);

	ALLOC_OBJ(vut, VUT_MAGIC);
	AN(vut);

	if (argc == 2 && !strcmp(argv[1], "--synopsis"))
		exit(vut_synopsis(voc));
	if (argc == 2 && !strcmp(argv[1], "--options"))
		exit(vut_options(voc));
	if (argc == 2 && !strcmp(argv[1], "--optstring")) {
		(void)printf("%s\n", voc->vopt_optstring);
		exit(0);
	}

	vut->progname = progname;
	vut->g_arg = VSL_g_vxid;
	vut->k_arg = -1;
	vut->error_f = vut_error;
	AZ(vut->vsl);
	vut->vsl = VSL_New();
	AN(vut->vsl);
	return (vut);
}

void
VUT_Signal(VUT_sighandler_f sig_cb)
{

	AN(sig_cb);
	(void)signal(SIGHUP, sig_cb);
	(void)signal(SIGINT, sig_cb);
	(void)signal(SIGTERM, sig_cb);
	(void)signal(SIGUSR1, sig_cb);
}

void
VUT_Signaled(struct VUT *vut, int sig)
{

	CHECK_OBJ_NOTNULL(vut, VUT_MAGIC);
	vut->sighup |= (int)(sig == SIGHUP);
	vut->sigint |= (int)(sig == SIGINT || sig == SIGTERM);
	vut->sigusr1 |= (int)(sig == SIGUSR1);
}

void
VUT_Setup(struct VUT *vut)
{
	struct VSL_cursor *c;

	CHECK_OBJ_NOTNULL(vut, VUT_MAGIC);
	AN(vut->vsl);
	AZ(vut->vsm);
	AZ(vut->vslq);

	/* Check input arguments (2 used for bug in FlexeLint) */
	if ((vut->n_arg == NULL ? 0 : 2) +
	    (vut->r_arg == NULL ? 0 : 2) > 2)
		VUT_Error(vut, 1, "Only one of -n and -r options may be used");

	/* Create and validate the query expression */
	vut->vslq = VSLQ_New(vut->vsl, NULL,
	    (enum VSL_grouping_e)vut->g_arg, vut->q_arg);
	if (vut->vslq == NULL)
		VUT_Error(vut, 1, "Query expression error:\n%s",
		    VSL_Error(vut->vsl));

	/* Setup input */
	if (vut->r_arg) {
		c = VSL_CursorFile(vut->vsl, vut->r_arg, 0);
		if (c == NULL)
			VUT_Error(vut, 1, "%s", VSL_Error(vut->vsl));
		VSLQ_SetCursor(vut->vslq, &c);
		AZ(c);
	} else {
		vut->vsm = VSM_New();
		AN(vut->vsm);
		if (vut->n_arg && VSM_Arg(vut->vsm, 'n', vut->n_arg) <= 0)
			VUT_Error(vut, 1, "%s", VSM_Error(vut->vsm));
		if (vut->t_arg && VSM_Arg(vut->vsm, 't', vut->t_arg) <= 0)
			VUT_Error(vut, 1, "%s", VSM_Error(vut->vsm));
		if (VSM_Attach(vut->vsm, STDERR_FILENO))
			VUT_Error(vut, 1, "VSM: %s", VSM_Error(vut->vsm));
		// Cursor is handled in VUT_Main()
	}

	/* Open PID file */
	if (vut->P_arg) {
		if (pfh != NULL)
			VUT_Error(vut, 1, "PID file already created");
		pfh = VPF_Open(vut->P_arg, 0644, NULL);
		if (pfh == NULL)
			VUT_Error(vut, 1, "%s: %s", vut->P_arg, strerror(errno));
	}

	/* Daemon mode */
	if (vut->D_opt && vut_daemon(vut) == -1)
		VUT_Error(vut, 1, "Daemon mode: %s", strerror(errno));

	/* Write PID and setup exit handler */
	if (vut->P_arg) {
		AN(pfh);
		AZ(VPF_Write(pfh));

		/* NB: move ownership to a global pseudo-VUT. */
		INIT_OBJ(&pfh_vut, VUT_MAGIC);
		pfh_vut.P_arg = vut->P_arg;
		pfh_vut.error_f = vut->error_f;
		vut->P_arg = NULL;

		AZ(atexit(vut_vpf_remove));
	}
}

void
VUT_Fini(struct VUT **vutp)
{
	struct VUT *vut;

	TAKE_OBJ_NOTNULL(vut, vutp, VUT_MAGIC);
	AN(vut->progname);

	free(vut->n_arg);
	free(vut->q_arg);
	free(vut->r_arg);
	free(vut->t_arg);
	AZ(vut->P_arg);

	if (vut->vslq)
		VSLQ_Delete(&vut->vslq);
	if (vut->vsl)
		VSL_Delete(vut->vsl);
	if (vut->vsm)
		VSM_Destroy(&vut->vsm);

	memset(vut, 0, sizeof *vut);
	FREE_OBJ(vut);
}

int
VUT_Main(struct VUT *vut)
{
	struct VSL_cursor *c;
	int i = -1;
	int hascursor = -1;

	CHECK_OBJ_NOTNULL(vut, VUT_MAGIC);
	AN(vut->vslq);

	while (!vut->sigint) {
		if (vut->sighup && vut->sighup_f) {
			/* sighup callback */
			vut->sighup = 0;
			i = vut->sighup_f(vut);
			if (i)
				break;
		}

		if (vut->sigusr1) {
			/* Flush and report any incomplete records */
			vut->sigusr1 = 0;
			(void)VSLQ_Flush(vut->vslq, vut_dispatch, vut);
		}

		/* We must repeatedly call VSM_Status() when !hascursor
		 * to make VSM discover our segment.
		 *
		 * XXX consider moving the error handling to VSLQ_Dispatch.
		 * or some other VSL utility function
		 * Reasons:
		 *
		 * - it does not seem to make much sense to call VSM_StillValid
		 *   in vsl if that can only detect invalid segments after
		 *   VSM_Status has run, so it appears both should be
		 *   consolidated
		 *
		 * - not all VSL Clients will use VUT, yet the log abandoned/
		 *   overrun situation will be occur for all of them.
		 */

		if (vut->vsm != NULL &&
		    (VSM_Status(vut->vsm) & VSM_WRK_RESTARTED)) {
			if (hascursor < 1) {
				fprintf(stderr, "Log abandoned (vsm)\n");
				VSLQ_SetCursor(vut->vslq, NULL);
				hascursor = 0;
			}
		}
		if (vut->vsm != NULL && hascursor < 1) {
			/* Reconnect VSM */
			AZ(vut->r_arg);
			VTIM_sleep(0.1);
			c = VSL_CursorVSM(vut->vsl, vut->vsm,
			    (vut->d_opt ? VSL_COPT_TAILSTOP : VSL_COPT_TAIL)
			    | VSL_COPT_BATCH);
			if (c == NULL) {
				VSL_ResetError(vut->vsl);
				continue;
			}
			if (hascursor >= 0)
				fprintf(stderr, "Log reacquired\n");
			hascursor = 1;
			VSLQ_SetCursor(vut->vslq, &c);
			AZ(c);
		}

		i = VSLQ_Dispatch(vut->vslq, vut_dispatch, vut);
		if (i == vsl_more)
			continue;
		else if (i == vsl_end) {
			if (vut->idle_f) {
				i = vut->idle_f(vut);
				if (i)
					break;
			}
			VTIM_sleep(0.01);
			continue;
		} else if (i == vsl_e_eof)
			break;

		if (vut->vsm == NULL)
			break;

		/* XXX: Make continuation optional */

		(void)VSLQ_Flush(vut->vslq, vut_dispatch, vut);

		if (i == vsl_e_abandon) {
			fprintf(stderr, "Log abandoned (vsl)\n");
			VSLQ_SetCursor(vut->vslq, NULL);
			hascursor = 0;
		} else if (i == vsl_e_overrun) {
			fprintf(stderr, "Log overrun\n");
			VSLQ_SetCursor(vut->vslq, NULL);
			hascursor = 0;
		} else
			fprintf(stderr, "Error %d from VSLQ_Dispatch()", i);
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
	printf("--optstring\n"
	    "\tPrint the optstring parameter to ``getopt(3)`` to help"
	    " writing wrapper scripts.\n\n");
	return (0);
}
