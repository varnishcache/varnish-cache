/*-
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Jailing processes the UNIX way, using setuid(2) etc.
 */

#include "config.h"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

static gid_t vju_mgt_gid;
static uid_t vju_uid;
static gid_t vju_gid;
static const char *vju_user;

static uid_t vju_wrkuid;
static gid_t vju_wrkgid;
static const char *vju_wrkuser;

static gid_t vju_cc_gid;
static int vju_cc_gid_set;

#ifndef VARNISH_USER
#define VARNISH_USER "varnish"
#endif

#ifndef VCACHE_USER
#define VCACHE_USER "vcache"
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
vju_getwrkuid(const char *arg)
{
	struct passwd *pw;

	pw = getpwnam(arg);
	if (pw != NULL) {
		vju_wrkuser = strdup(arg);
		AN(vju_wrkuser);
		vju_wrkuid = pw->pw_uid;
		vju_wrkgid = pw->pw_gid;
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

static int v_matchproto_(jail_init_f)
vju_init(char **args)
{
	if (args == NULL) {
		/* Autoconfig */
		if (geteuid() != 0)
			return (1);
		if (vju_getuid(VARNISH_USER))
			return (1);
	} else {

		if (geteuid() != 0)
			ARGV_ERR("Unix Jail: Must be root.\n");

		for (;*args != NULL; args++) {
			if (!strncmp(*args, "user=", 5)) {
				if (vju_getuid((*args) + 5))
					ARGV_ERR(
					    "Unix jail: %s user not found.\n",
					    (*args) + 5);
				continue;
			}
			if (!strncmp(*args, "workuser=", 9)) {
				if (vju_getwrkuid((*args) + 9))
					ARGV_ERR(
					    "Unix jail: %s user not found.\n",
					    (*args) + 9);
				continue;
			}
			if (!strncmp(*args, "ccgroup=", 8)) {
				if (vju_getccgid((*args) + 8))
					ARGV_ERR(
					    "Unix jail: %s group not found.\n",
					    (*args) + 8);
				continue;
			}
			ARGV_ERR("Unix jail: unknown sub-argument '%s'\n",
			    *args);
		}

		if (vju_user == NULL && vju_getuid(VARNISH_USER))
			ARGV_ERR("Unix jail: %s user not found.\n",
			    VARNISH_USER);
	}

	AN(vju_user);

	vju_mgt_gid = getgid();

	if (vju_wrkuser == NULL && vju_getwrkuid(VCACHE_USER)) {
		vju_wrkuid = vju_uid;
		vju_wrkgid = vju_gid;
	}

	if (vju_wrkuser != NULL && vju_wrkgid != vju_gid)
		ARGV_ERR("Unix jail: user %s and %s have "
		    "different login groups\n", vju_user, vju_wrkuser);

	/* Do an explicit JAIL_MASTER_LOW */
	AZ(setegid(vju_gid));
	AZ(seteuid(vju_uid));
	return (0);
}

static void v_matchproto_(jail_master_f)
vju_master(enum jail_master_e jme)
{
	ASSERT_JAIL_MASTER(jme);
	if (jme == JAIL_MASTER_LOW) {
		AZ(setegid(vju_gid));
		AZ(seteuid(vju_uid));
	} else {
		AZ(seteuid(0));
		AZ(setegid(vju_mgt_gid));
	}
}

static void v_matchproto_(jail_subproc_f)
vju_subproc(enum jail_subproc_e jse)
{
	int i;
	gid_t gid_list[NGID];

	ASSERT_JAIL_SUBPROC(jse);
	AZ(seteuid(0));
	if (vju_wrkuser != NULL &&
	    (jse == JAIL_SUBPROC_VCLLOAD || jse == JAIL_SUBPROC_WORKER)) {
		AZ(setgid(vju_wrkgid));
		AZ(initgroups(vju_wrkuser, vju_wrkgid));
	} else {
		AZ(setgid(vju_gid));
		AZ(initgroups(vju_user, vju_gid));
	}

	if (jse == JAIL_SUBPROC_CC && vju_cc_gid_set) {
		/* Add the optional extra group for the C-compiler access */
		i = getgroups(NGID, gid_list);
		assert(i >= 0);
		gid_list[i++] = vju_cc_gid;
		AZ(setgroups(i, gid_list));
	}

	if (vju_wrkuser != NULL &&
	    (jse == JAIL_SUBPROC_VCLLOAD || jse == JAIL_SUBPROC_WORKER)) {
		AZ(setuid(vju_wrkuid));
	} else {
		AZ(setuid(vju_uid));
	}

#ifdef __linux__
	/*
	 * On linux mucking about with uid/gid disables core-dumps,
	 * reenable them again.
	 */
	if (prctl(PR_SET_DUMPABLE, 1) != 0) {
		MGT_Complain(C_INFO,
		    "Could not set dumpable bit.  Core dumps turned off");
	}
#endif
}

static int v_matchproto_(jail_make_dir_f)
vju_make_subdir(const char *dname, const char *what, struct vsb *vsb)
{
	int e;

	AN(dname);
	AN(what);
	AZ(seteuid(0));

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
	AZ(chown(dname, vju_uid, vju_gid));
	AZ(seteuid(vju_uid));
	return (0);
}

static int v_matchproto_(jail_make_dir_f)
vju_make_workdir(const char *dname, const char *what, struct vsb *vsb)
{

	AN(dname);
	AZ(what);
	AZ(vsb);
	AZ(seteuid(0));

	if (mkdir(dname, 0755) < 0 && errno != EEXIST) {
		MGT_Complain(C_ERR, "Cannot create working directory '%s': %s",
		    dname, vstrerror(errno));
		return (1);
	}
	AZ(chown(dname, -1, vju_gid));
	AZ(seteuid(vju_uid));
	return (0);
}

static void v_matchproto_(jail_fixfd_f)
vju_fixfd(int fd, enum jail_fixfd_e what)
{
	/* Called under JAIL_MASTER_FILE */

	switch (what) {
	case JAIL_FIXFD_FILE:
		AZ(fchmod(fd, 0750));
		AZ(fchown(fd, vju_wrkuid, vju_wrkgid));
		break;
	case JAIL_FIXFD_VSMMGT:
		AZ(fchmod(fd, 0750));
		AZ(fchown(fd, vju_uid, vju_gid));
		break;
	case JAIL_FIXFD_VSMWRK:
		AZ(fchmod(fd, 0750));
		AZ(fchown(fd, vju_wrkuid, vju_wrkgid));
		break;
	default:
		WRONG("Ain't Fixin'");
	}
}

const struct jail_tech jail_tech_unix = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"unix",
	.init =		vju_init,
	.master =	vju_master,
	.make_subdir =	vju_make_subdir,
	.make_workdir =	vju_make_workdir,
	.fixfd =	vju_fixfd,
	.subproc =	vju_subproc,
};
