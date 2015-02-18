/*-
 * Copyright (c) 2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mgt/mgt.h"
#include "vav.h"

/**********************************************************************
 * A "none" jail implementation which doesn't do anything.
 */

static int __match_proto__(jail_init_f)
vjn_init(char **args)
{
	if (args != NULL && *args != NULL)
		ARGV_ERR("-jnone takes no arguments.\n");
	return (0);
}

static void __match_proto__(jail_master_f)
vjn_master(enum jail_master_e jme)
{
	(void)jme;
}

static void __match_proto__(jail_subproc_f)
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
			ARGV_ERR("-j argument is emtpy\n");
		vjt = pick(vj_choice, av[1], "jail");
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

void
VJ_make_workdir(const char *dname)
{
	int fd;

	AN(dname);
	CHECK_OBJ_NOTNULL(vjt, JAIL_TECH_MAGIC);
	if (vjt->make_workdir != NULL) {
		vjt->make_workdir(dname);
		return;
	}

	if (mkdir(dname, 0755) < 0 && errno != EEXIST)
		ARGV_ERR("Cannot create working directory '%s': %s\n",
		    dname, strerror(errno));

	if (chdir(dname) < 0)
		ARGV_ERR("Cannot change to working directory '%s': %s\n",
		    dname, strerror(errno));

	fd = open("_.testfile", O_RDWR|O_CREAT|O_EXCL, 0600);
	if (fd < 0)
		ARGV_ERR("Error: Cannot create test-file in %s (%s)\n"
		    "Check permissions (or delete old directory)\n",
		    dname, strerror(errno));
	AZ(close(fd));
	AZ(unlink("_.testfile"));
}
