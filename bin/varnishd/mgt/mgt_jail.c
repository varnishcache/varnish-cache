/*-
 * Copyright (c) 2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Jailing
 *
 */

#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mgt/mgt.h"
#include "common/heritage.h"
#include "vav.h"

/**********************************************************************
 * A "none" jail implementation which doesn't do anything.
 */

static int v_matchproto_(jail_init_f)
vjn_init(char **args)
{
	if (args != NULL && *args != NULL)
		ARGV_ERR("-jnone takes no arguments.\n");
	return (0);
}

static void v_matchproto_(jail_master_f)
vjn_master(enum jail_master_e jme)
{
	(void)jme;
}

static void v_matchproto_(jail_subproc_f)
vjn_subproc(enum jail_subproc_e jse)
{
	(void)jse;
}

static const struct jail_tech jail_tech_none = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"none",
	.init =		vjn_init,
	.master =	vjn_master,
	.subproc =	vjn_subproc,
};

/**********************************************************************/

static const struct jail_tech *vjt;

static const struct choice vj_choice[] = {
#ifdef HAVE_SETPPRIV
	{ "solaris",	&jail_tech_solaris },
#endif
	{ "unix",	&jail_tech_unix },
	{ "none",	&jail_tech_none },
#ifdef WITH_LINUX_JAIL
	{ "linux_experimental",	&jail_tech_linux},
#endif
	{ NULL,		NULL },
};

void
VJ_Init(const char *j_arg)
{
	char **av;
	int i;

	if (j_arg != NULL) {
		av = VAV_Parse(j_arg, NULL, ARGV_COMMA);
		AN(av);
		if (av[0] != NULL)
			ARGV_ERR("-j argument: %s\n", av[0]);
		if (av[1] == NULL)
			ARGV_ERR("-j argument is empty\n");
		vjt = MGT_Pick(vj_choice, av[1], "jail");
		CHECK_OBJ_NOTNULL(vjt, JAIL_TECH_MAGIC);
		(void)vjt->init(av + 2);
		VAV_Free(av);
	} else {
		/*
		 * Go through list of jail technologies until one
		 * succeeds, falling back to "none".
		 */
		for (i = 0; vj_choice[i].name != NULL; i++) {
			vjt = vj_choice[i].ptr;
			CHECK_OBJ_NOTNULL(vjt, JAIL_TECH_MAGIC);
			if (!vjt->init(NULL))
				break;
		}
	}
	VSB_printf(vident, ",-j%s", vjt->name);
}

void
VJ_master(enum jail_master_e jme)
{
	CHECK_OBJ_NOTNULL(vjt, JAIL_TECH_MAGIC);
	vjt->master(jme);
}

void
VJ_subproc(enum jail_subproc_e jse)
{
	CHECK_OBJ_NOTNULL(vjt, JAIL_TECH_MAGIC);
	vjt->subproc(jse);
}

int
VJ_make_workdir(const char *dname)
{
	int i;

	AN(dname);
	CHECK_OBJ_NOTNULL(vjt, JAIL_TECH_MAGIC);
	if (vjt->make_workdir != NULL) {
		i = vjt->make_workdir(dname, NULL, NULL);
		if (i)
			return (i);
		VJ_master(JAIL_MASTER_FILE);
	} else {
		VJ_master(JAIL_MASTER_FILE);
		if (mkdir(dname, 0755) < 0 && errno != EEXIST)
			ARGV_ERR("Cannot create working directory '%s': %s\n",
				 dname, vstrerror(errno));
	}

	if (chdir(dname) < 0)
		ARGV_ERR("Cannot change to working directory '%s': %s\n",
		    dname, vstrerror(errno));

	i = open("_.testfile", O_RDWR|O_CREAT|O_EXCL, 0600);
	if (i < 0)
		ARGV_ERR("Cannot create test-file in %s (%s)\n"
		    "Check permissions (or delete old directory)\n",
		    dname, vstrerror(errno));
	closefd(&i);
	AZ(unlink("_.testfile"));
	VJ_master(JAIL_MASTER_LOW);
	return (0);
}

int
VJ_make_subdir(const char *dname, const char *what, struct vsb *vsb)
{
	int e;

	AN(dname);
	AN(what);
	CHECK_OBJ_NOTNULL(vjt, JAIL_TECH_MAGIC);
	if (vjt->make_subdir != NULL)
		return (vjt->make_subdir(dname, what, vsb));

	VJ_master(JAIL_MASTER_FILE);
	if (mkdir(dname, 0755) < 0 && errno != EEXIST) {
		e = errno;
		if (vsb != NULL) {
			VSB_printf(vsb,
			    "Cannot create %s directory '%s': %s\n",
			    what, dname, vstrerror(e));
		} else {
			MGT_Complain(C_ERR,
			    "Cannot create %s directory '%s': %s",
			    what, dname, vstrerror(e));
		}
		return (1);
	}
	VJ_master(JAIL_MASTER_LOW);
	return (0);
}

void
VJ_unlink(const char *fname)
{
	VJ_master(JAIL_MASTER_FILE);
	if (unlink(fname)) {
		fprintf(stderr, "Could not delete '%s': %s\n",
		    fname, strerror(errno));
	}
	VJ_master(JAIL_MASTER_LOW);
}

void
VJ_rmdir(const char *dname)
{
	VJ_master(JAIL_MASTER_FILE);
	if (rmdir(dname)) {
		fprintf(stderr, "Could not rmdir '%s': %s\n",
		    dname, strerror(errno));
	}
	VJ_master(JAIL_MASTER_LOW);
}

void
VJ_fix_fd(int fd, enum jail_fixfd_e what)
{

	CHECK_OBJ_NOTNULL(vjt, JAIL_TECH_MAGIC);
	if (vjt->fixfd != NULL)
		vjt->fixfd(fd, what);
}
