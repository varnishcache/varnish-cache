/*-
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Jailing processes the UNIX way, using setuid(2) etc.
 */

#include "config.h"

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mgt/mgt.h"

#ifdef __linux__
#include <syslog.h>
#include <sys/prctl.h>
#endif

static uid_t vju_uid;
static gid_t vju_gid;
static const char *vju_user;
static gid_t vju_cc_gid;
static int vju_cc_gid_set;

#ifndef JAIL_USER
#define JAIL_USER "varnish"
#endif

#ifndef NGID
#define NGID 2000
#endif

static int
vju_getuid(const char *arg)
{
	struct passwd *pw;

	pw = getpwnam(arg);
	if (pw != NULL) {
		vju_user = strdup(arg);
		AN(vju_user);
		vju_uid = pw->pw_uid;
		vju_gid = pw->pw_gid;
	}
	endpwent();
	return (pw == NULL ? -1 : 0);
}

static int
vju_getccgid(const char *arg)
{
	struct group *gr;

	gr = getgrnam(arg);
	if (gr != NULL) {
		vju_cc_gid_set = 1;
		vju_cc_gid = gr->gr_gid;
	}
	endgrent();
	return (gr == NULL ? -1 : 0);
}

/**********************************************************************
 */

static int __match_proto__(jail_init_f)
vju_init(char **args)
{
	if (args == NULL) {
		/* Autoconfig */
		if (geteuid() != 0)
			return (1);
		if (vju_getuid(JAIL_USER))
			return (1);
		return (0);
	}

	if (geteuid() != 0)
		ARGV_ERR("Unix Jail: Must be root.\n");

	for (;*args != NULL; args++) {
		if (!strncmp(*args, "user=", 5)) {
			if (vju_getuid((*args) + 5)) {
				ARGV_ERR("Unix jail: %s user not found.\n",
				    (*args) + 5);
			}
			continue;
		}
		if (!strncmp(*args, "ccgroup=", 8)) {
			if (vju_getccgid((*args) + 8)) {
				ARGV_ERR("Unix jail: %s group not found.\n",
				    (*args) + 8);
			}
			continue;
		}
		ARGV_ERR("Unix jail: unknown sub-argument '%s'\n", *args);
	}

	if (vju_user == NULL && vju_getuid(JAIL_USER))
		ARGV_ERR("Unix jail: %s user not found.\n", JAIL_USER);

	return (0);
}

static void __match_proto__(jail_master_f)
vju_master(enum jail_master_e jme)
{
	(void)jme;
}

static void __match_proto__(jail_subproc_f)
vju_subproc(enum jail_subproc_e jse)
{
	int i;
	gid_t gid_list[NGID];

	AZ(setgid(vju_gid));
	AZ(initgroups(vju_user, vju_gid));

	if (jse == JAIL_SUBPROC_CC && vju_cc_gid_set) {
		/* Add the optional extra group for the C-compiler access */
		i = getgroups(NGID, gid_list);
		assert(i >= 0);
		gid_list[i++] = vju_cc_gid;
		AZ(setgroups(i, gid_list));
	}

	AZ(setuid(vju_uid));

#ifdef __linux__
	/*
	 * On linux mucking about with uid/gid disables core-dumps,			 * reenable them again.
	 */
	if (prctl(PR_SET_DUMPABLE, 1) != 0) {
		REPORT0(LOG_INFO,
		    "Could not set dumpable bit.  Core dumps turned off\n");
	}
#endif
}

const struct jail_tech jail_tech_unix = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"unix",
	.init =		vju_init,
	.master =	vju_master,
	.subproc =	vju_subproc,
};
