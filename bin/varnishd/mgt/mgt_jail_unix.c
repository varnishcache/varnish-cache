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

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mgt/mgt.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

static gid_t vju_mgr_gid;
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

static int __match_proto__(jail_init_f)
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

	vju_mgr_gid = getgid();

	if (vju_wrkuser == NULL)
		(void)vju_getwrkuid(VCACHE_USER);

	if (vju_wrkuser != NULL && vju_wrkgid != vju_gid)
		ARGV_ERR("Unix jail: user %s and %s have "
		    "different login groups\n", vju_user, vju_wrkuser);

	/* Do an explicit JAIL_MASTER_LOW */
	AZ(setegid(vju_gid));
	AZ(seteuid(vju_uid));
	return (0);
}

static void __match_proto__(jail_master_f)
vju_master(enum jail_master_e jme)
{
	if (jme == JAIL_MASTER_LOW) {
		AZ(setegid(vju_gid));
		AZ(seteuid(vju_uid));
	} else {
		AZ(seteuid(0));
		AZ(setegid(vju_mgr_gid));
	}
}

static void __match_proto__(jail_subproc_f)
vju_subproc(enum jail_subproc_e jse)
{
	int i;
	gid_t gid_list[NGID];

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
		MGT_complain(C_INFO,
		    "Could not set dumpable bit.  Core dumps turned off\n");
	}
#endif
}

static int __match_proto__(jail_make_dir_f)
vju_make_vcldir(const char *dname)
{
	AZ(seteuid(0));

	if (mkdir(dname, 0755) < 0 && errno != EEXIST) {
		MGT_complain(C_ERR, "Cannot create VCL directory '%s': %s",
		    dname, strerror(errno));
		return (1);
	}
	AZ(chown(dname, vju_uid, vju_gid));
	AZ(seteuid(vju_uid));
	return (0);
}


static void __match_proto__(jail_fixfile_f)
vju_vsm_file(int fd)
{
	/* Called under JAIL_MASTER_FILE */

	AZ(fchmod(fd, 0640));
	AZ(fchown(fd, 0, vju_gid));
}

static void __match_proto__(jail_fixfile_f)
vju_storage_file(int fd)
{
	/* Called under JAIL_MASTER_STORAGE */

	AZ(fchmod(fd, 0600));
	AZ(fchown(fd, vju_uid, vju_gid));
}

const struct jail_tech jail_tech_unix = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"unix",
	.init =		vju_init,
	.master =	vju_master,
	.make_vcldir =	vju_make_vcldir,
	.vsm_file =	vju_vsm_file,
	.storage_file =	vju_storage_file,
	.subproc =	vju_subproc,
};
