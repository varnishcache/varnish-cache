/*-
 * Copyright (c) 2024 Varnish Software AS
 * All rights reserved.
 *
 * Author: Thibaut Artis <thibaut.artis@varnish-software.com>
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
 */

#include "config.h"

#ifdef __linux__

#include <fcntl.h>
#include <grp.h>
#include <linux/magic.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/vfs.h>

#include "mgt/mgt.h"

static int
vjl_set_thp(const char *arg, struct vsb *vsb)
{
	int r, val, must;

	if (!strcmp(arg, "ignore"))
		return (0);
	must = 1;
	if (!strcmp(arg, "enable"))
		val = 0;
	else if (!strcmp(arg, "disable"))
		val = 1;
	else if (!strcmp(arg, "try-disable")) {
		arg = "disable";
		val = 1;
		must = 0;
	}
	else {
		VSB_printf(vsb, "linux jail: unknown value '%s' for argument"
		    " transparent_hugepage.", arg);
		return (1);
	}
	r = prctl(PR_SET_THP_DISABLE, val, 0, 0, 0);
	if (r) {
		VSB_printf(vsb, "linux jail: Could not %s "
		    "Transparent Hugepage: %s (%d)",
		    arg, VAS_errtxt(errno), errno);
	}
	return (r && must);
}

static int
vjl_init(char **args)
{
	struct vsb *vsb;
	char **unix_args;
	const char *val;
	int seen = 0, ret = 0;
	size_t i;

	(void)args;

	vsb = VSB_new_auto();
	AN(vsb);

	if (args == NULL) {
		/* Autoconfig */
		AZ(vjl_set_thp("try-disable", vsb));
		MGT_ComplainVSB(C_INFO, vsb);
		VSB_destroy(&vsb);
		return (jail_tech_unix.init(NULL));
	}

	i = 0;
	while (args[i] != NULL)
		i++;

	unix_args = calloc(i + 1, sizeof *unix_args);
	AN(unix_args);

	i = 0;
	for (; *args != NULL && ret == 0; args++) {
		val = keyval(*args, "transparent_hugepage=");
		if (val == NULL) {
			unix_args[i++] = *args;
			continue;
		}

		ret |= vjl_set_thp(val, vsb);
		seen++;
	}

	if (seen == 0)
		AZ(vjl_set_thp("try-disable", vsb));

	MGT_ComplainVSB(ret ? C_ERR : C_INFO, vsb);
	VSB_destroy(&vsb);

	if (ret == 0)
		ret = jail_tech_unix.init(unix_args);
	free(unix_args);
	return (ret);
}

static void
vjl_master(enum jail_master_e jme)
{

	jail_tech_unix.master(jme);
}

static void
vjl_subproc(enum jail_subproc_e jse)
{

	jail_tech_unix.subproc(jse);
	/*
	 * On linux mucking about with uid/gid disables core-dumps,
	 * reenable them again.
	 */
	if (prctl(PR_SET_DUMPABLE, 1) != 0) {
		MGT_Complain(C_INFO,
		    "Could not set dumpable bit.  Core dumps turned off");
	}
}

static int
vjl_make_subdir(const char *dname, const char *what, struct vsb *vsb)
{

	return jail_tech_unix.make_subdir(dname, what, vsb);
}

static int
vjl_make_workdir(const char *dname, const char *what, struct vsb *vsb)
{
	struct statfs info;

	AN(vsb);
	if (jail_tech_unix.make_workdir(dname, what, vsb) != 0)
		return (1);

	vjl_master(JAIL_MASTER_FILE);
	if (statfs(dname, &info) != 0) {
		VSB_printf(vsb, "Could not stat working directory '%s':"
		    " %s (%d)\n", dname, VAS_errtxt(errno), errno);
		return (1);
	}
	if (info.f_type != TMPFS_MAGIC) {
		VSB_printf(vsb, "Working directory not mounted on"
		    " tmpfs partition\n");
	}
	vjl_master(JAIL_MASTER_LOW);
	return (0);
}

static void
vjl_fixfd(int fd, enum jail_fixfd_e what)
{

	jail_tech_unix.fixfd(fd, what);
}

const struct jail_tech jail_tech_linux = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"linux",
	.init =		vjl_init,
	.master =	vjl_master,
	.make_subdir =	vjl_make_subdir,
	.make_workdir =	vjl_make_workdir,
	.fixfd =	vjl_fixfd,
	.subproc =	vjl_subproc,
};

#endif /* __linux__ */
