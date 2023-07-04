/*-
 * Copyright (c) 2023 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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
 */

#ifdef VTEST_WITH_VTC_VSM

#include "config.h"

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "vapi/vsm.h"

#include "vtc.h"
#include "vav.h"

struct vtc_vsm {
	unsigned			magic;
#define VTC_VSM_MAGIC			0x5ca77a36
	VTAILQ_ENTRY(vtc_vsm)		list;

	char				*name;
	char				*n_arg;
	struct vtclog			*vl;
	struct vsm			*vsm;
};

static VTAILQ_HEAD(, vtc_vsm) vsms = VTAILQ_HEAD_INITIALIZER(vsms);

static struct vtc_vsm *
vsm_new(const char *name)
{
	struct vtc_vsm *m;

	ALLOC_OBJ(m, VTC_VSM_MAGIC);
	AN(m);
	REPLACE(m->name, name);
	REPLACE(m->n_arg, "${v1_name}");
	m->vl = vtc_logopen("%s", name);

	VTAILQ_INSERT_TAIL(&vsms, m, list);
	return (m);
}

static void
vsm_detach(struct vtc_vsm *m)
{

	CHECK_OBJ_NOTNULL(m, VTC_VSM_MAGIC);
	if (m->vsm == NULL)
		vtc_fatal(m->vl, "Cannot detach unattached VSM");
	vtc_log(m->vl, 3, "Detaching from VSM");
	VSM_Destroy(&m->vsm);
	AZ(m->vsm);
}

static void
vsm_attach(struct vtc_vsm *m)
{
	struct vsb *n_arg;

	CHECK_OBJ_NOTNULL(m, VTC_VSM_MAGIC);
	if (m->vsm != NULL)
		vsm_detach(m);

	n_arg = macro_expandf(m->vl, "%s", m->n_arg);
	if (n_arg == NULL)
		vtc_fatal(m->vl, "Could not expand -n argument");
	vtc_log(m->vl, 3, "Attaching to VSM: %s", VSB_data(n_arg));

	m->vsm = VSM_New();
	AN(m->vsm);

	if (VSM_Arg(m->vsm, 'n', VSB_data(n_arg)) <= 0)
		vtc_fatal(m->vl, "-n argument error: %s", VSM_Error(m->vsm));
	if (VSM_Attach(m->vsm, -1))
		vtc_fatal(m->vl, "VSM_Attach: %s", VSM_Error(m->vsm));

	VSB_destroy(&n_arg);
}

static void
vsm_delete(struct vtc_vsm *m)
{

	CHECK_OBJ_NOTNULL(m, VTC_VSM_MAGIC);
	if (m->vsm != NULL)
		vsm_detach(m);
	REPLACE(m->name, NULL);
	REPLACE(m->n_arg, NULL);
	vtc_logclose(m->vl);
	FREE_OBJ(m);
}

#define STATUS_BITS()					\
	STATUS_BIT(VSM_MGT_RUNNING,	mgt-running);	\
	STATUS_BIT(VSM_MGT_CHANGED,	mgt-changed);	\
	STATUS_BIT(VSM_MGT_RESTARTED,	mgt-restarted);	\
	STATUS_BIT(VSM_WRK_RUNNING,	wrk-running);	\
	STATUS_BIT(VSM_WRK_CHANGED,	wrk-changed);	\
	STATUS_BIT(VSM_WRK_RESTARTED,	wrk-restarted);

static void
vsm_expect_status(struct vtc_vsm *m, const char *exp)
{
	struct vsb *stat;
	const char *sep;
	char **av;
	unsigned bstat, bexp, bfound;
	int argc, i;

	CHECK_OBJ_NOTNULL(m, VTC_VSM_MAGIC);
	if (exp == NULL)
		vtc_fatal(m->vl, "Missing expected status");

	if (m->vsm == NULL)
		vsm_attach(m);

	av = VAV_Parse(exp, &argc, ARGV_COMMA|ARGV_NOESC);
	AN(av);

	bexp = 0;
	for (i = 1; i < argc; i++) {
#define STATUS_BIT(b, s)				\
		if (!strcasecmp(#s, av[i])) {		\
			bexp |= b;			\
			continue;			\
		}
		STATUS_BITS()
#undef STATUS_BIT
		vtc_fatal(m->vl, "Unknown status bit: %s", av[i]);
	}
	VAV_Free(av);

	bfound = 0;
	bstat = VSM_Status(m->vsm);
	stat = VSB_new_auto();
	AN(stat);
	sep = "";
#define STATUS_BIT(b, s)				\
	if (bstat & b) {				\
		VSB_cat(stat, sep);			\
		VSB_cat(stat, #s);			\
		bfound |= b;				\
		sep = ",";				\
	}
	STATUS_BITS();
#undef STATUS_BIT

	if (bstat != bfound) {
		vtc_fatal(m->vl, "VSM status bits not handled: %x",
		    bstat & ~bfound);
	}

	if (bstat != bexp) {
		AZ(VSB_finish(stat));
		vtc_fatal(m->vl, "Expected VSM status '%s' got '%s'",
		    exp, VSB_data(stat));
	}

	VSB_destroy(&stat);
	vtc_log(m->vl, 4, "Found expected VSM status");
}

/* SECTION: vsm vsm
 *
 * Interact with the shared memory of a varnish instance.
 *
 * To define a VSM consumer, use this syntax::
 *
 *     vsm mNAME [-n STRING]
 *
 * Arguments:
 *
 * mNAME
 *         Identify the VSM consumer, it must starts with 'm'.
 *
 * \-n STRING
 *         Choose the working directory of the varnish instance. By default
 *         a VSM consumer connects to ``${v1_name}``.
 *
 * \-attach
 *         Attach to a new varnish instance. Implicitly detach from the
 *         current varnish instance if applicable.
 *
 * \-detach
 *         Detach from the current varnish instance.
 *
 * \-expect-status STRING
 *         Check that the status of VSM matches the list of status flags from
 *         STRING. The expected status is represented as a comma-separated
 *         list of flags. The list of flags in STRING is not sensitive to the
 *         order of flags.
 *
 *         The available flags are:
 *
 *         - ``mgt-running``
 *         - ``mgt-changed``
 *         - ``mgt-restarted``
 *         - ``wrk-running``
 *         - ``wrk-changed``
 *         - ``wrk-restarted``
 *
 *         Expecting a status automatically attaches to the varnish instance
 *         if that was not already the case.
 */

void
cmd_vsm(CMD_ARGS)
{
	struct vtc_vsm *m, *m2;

	(void)priv;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(m, &vsms, list, m2) {
			CHECK_OBJ_NOTNULL(m, VTC_VSM_MAGIC);
			VTAILQ_REMOVE(&vsms, m, list);
			vsm_delete(m);
		}
		return;
	}

	AZ(strcmp(av[0], "vsm"));
	av++;

	VTC_CHECK_NAME(vl, av[0], "VSM", 'm');
	VTAILQ_FOREACH(m, &vsms, list) {
		if (!strcmp(m->name, av[0]))
			break;
	}

	if (m == NULL) {
		m = vsm_new(*av);
		AN(m);
	}
	av++;

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;
		if (!strcmp(*av, "-attach")) {
			vsm_attach(m);
			continue;
		}
		if (!strcmp(*av, "-detach")) {
			vsm_detach(m);
			continue;
		}
		if (!strcmp(*av, "-expect-status")) {
			vsm_expect_status(m, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-n")) {
			if (av[1] == NULL)
				vtc_fatal(m->vl, "Missing -n argument");
			REPLACE(m->n_arg, av[1]);
			av++;
			continue;
		}
		vtc_fatal(vl, "Unknown VSM argument: %s", *av);
	}
}

#endif /* VTEST_WITH_VTC_VSM */
