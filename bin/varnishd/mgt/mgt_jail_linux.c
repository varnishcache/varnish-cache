/*-
 * Copyright (c) 2006-2020 Varnish Software AS
 * All rights reserved.
 *
 * Author: Marco Benatto <mbenatto@redhat.com>
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
 * Restricts the syscalls only to necessary ones using seccomp and still lower
 * privilleges by using setuid() as done on UNIX jail.
 *
 * The setup is pretty much the same as on UNIX jail, however besides drop
 * privilleges we also block any syscall we don't really use using seccomp(2).
 * If, eventually, someone manage to inject code or trick the mgt ou cld the
 * one won't be able to execute any locked syscall (let's say make someone
 * exec() arbitrary code)
 */


#include "config.h"

#include "mgt/mgt.h"
#include "common/heritage.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <linux/seccomp.h>
#include <seccomp.h>
#include <pwd.h>
#include <grp.h>

#ifndef VARNISH_USER
#define VARNISH_USER "varnish"
#endif

#ifndef VCACHE_USER
#define VCACHE_USER "vcache"
#endif

#ifndef NGID
#define NGID 2000
#endif

#ifndef SYSCALL_MAX_SIZE
#define SYSCALL_MAX_SIZE 255
#endif


static gid_t vjl_mgt_gid;
static gid_t vjl_wrkuid;
static gid_t vjl_wrkgid;
static char *vjl_wrkuser;

static gid_t vjl_uid;
static gid_t vjl_gid;
static char *vjl_user;

static gid_t vjl_cc_gid;
static int vjl_cc_gid_set;

/* syscall list file path */
static const char *vjl_mgtf_path;
static const char *vjl_vccf_path;
static const char *vjl_wrk_path;
uint32_t vjl_seccomp_mode;
static int vjl_enable_seccomp;
scmp_filter_ctx vjl_ctx;

static int vjl_add_rules(scmp_filter_ctx ctx, const char *vjl_filter_path);

static void vjl_sig_handler(int sig, siginfo_t *si, void *unused)
{
	if (sig == SIGSYS) {
		MGT_Complain(C_ERR, "[%u]syscall: %d not permitted at: %p\n", si->si_pid,
												si->si_syscall, si->si_call_addr);

	}
	(void)unused;
}

static int vjl_seccomp_reset(const char *vjl_filter_path)
{
	int ret;

	ret = 0;

	ret = seccomp_reset(vjl_ctx, SCMP_ACT_LOG);
	/* We reload the empty context here to make sure all previous rules
	 * are cleared. This is needed when a CLD task is launched as it inherits
	 * its parent's filters.
	 */
	AZ(seccomp_load(vjl_ctx));

	if (ret == 0)
		ret = vjl_add_rules(vjl_ctx, vjl_filter_path);

	AZ(ret);

	ret = seccomp_load(vjl_ctx);

	return ret;
}

/*
 * We don't release vjl_ctx here as we need it during all
 * execution time.
 */
static int vjl_seccomp_init(const char *vjl_filter_path)
{
	struct sigaction sa;
	int ret;

	ret = 0;

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = vjl_sig_handler;
	AZ(sigaction(SIGSYS, &sa, NULL));

	vjl_ctx = seccomp_init(vjl_seccomp_mode);

	AN(vjl_ctx);

	ret = vjl_add_rules(vjl_ctx, vjl_filter_path);
	AZ(ret);

	ret = seccomp_load(vjl_ctx);

	return ret;
}


static int vjl_add_rules(scmp_filter_ctx ctx, const char *vjl_filter_path)
{
	FILE *f;
	char line[SYSCALL_MAX_SIZE];
	int rc, ret, err, syscall;

	ret = 0;

	f = fopen(vjl_filter_path, "r");

	if (f == NULL) {
		MGT_Complain(C_INFO,
			"Could not open syscall file: %s [errno: %d]\n",
                                     vjl_filter_path, errno);
		return -errno;
	}


	while (fgets(line, SYSCALL_MAX_SIZE, f) != NULL) {
		AN(line);
		assert(strlen(line) <= SYSCALL_MAX_SIZE);

		syscall = atoi(line);

		rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW,
								syscall, 0);
		if (rc != 0) {
			err = errno;
			MGT_Complain(C_INFO,
				"SECCOMP returned %d for syscall %d [errno: %d]\n",
												 rc, syscall, err);

			ret = -err;
		}
	}

	fclose(f);

	return ret;
}

static void v_matchproto_(jail_subproc_f)
vjl_subproc(enum jail_subproc_e jse) {
	int i, ret;
	gid_t gid_list[NGID];

	ret = 0;

	AZ(seteuid(0));
	if (vjl_wrkuser != NULL &&
	    (jse == JAIL_SUBPROC_VCLLOAD || jse == JAIL_SUBPROC_WORKER)) {
		AZ(setgid(vjl_wrkgid));
		AZ(initgroups(vjl_wrkuser, vjl_wrkgid));
	} else {
		if (vjl_enable_seccomp) {
			if (vjl_vccf_path != NULL) {
				ret = vjl_seccomp_reset(vjl_vccf_path);
				AZ(ret);
			} else {
				MGT_Complain(C_SECURITY,
				 "VCC-Compiler filter not found, falling back to manager's filter.\n");
			}
		}
		AZ(setgid(vjl_gid));
		AZ(initgroups(vjl_user, vjl_gid));
	}

	if (jse == JAIL_SUBPROC_CC && vjl_cc_gid_set) {
		/* Add the optional extra group for the C-compiler access */
		i = getgroups(NGID, gid_list);
		assert(i >= 0);
		gid_list[i++] = vjl_cc_gid;
		AZ(setgroups(i, gid_list));
	}

	if (vjl_wrkuser != NULL &&
	    (jse == JAIL_SUBPROC_VCLLOAD || jse == JAIL_SUBPROC_WORKER)) {
		if (vjl_enable_seccomp) {
			if (vjl_wrk_path != NULL) {
				ret = vjl_seccomp_reset(vjl_wrk_path);
				AZ(ret);
			} else {
				MGT_Complain(C_SECURITY,
				 "Worker seccomp filter not found, falling back to manager's filter.\n");
			}
		}
		AZ(setuid(vjl_wrkuid));
	} else {
		AZ(setuid(vjl_uid));
	}

	/*
	 * On linux mucking about with uid/gid disables core-dumps,
	 * reenable them again.
	 */
	if (prctl(PR_SET_DUMPABLE, 1) != 0) {
		MGT_Complain(C_INFO,
		    "Could not set dumpable bit.  Core dumps turned off");
	}
}


static void v_matchproto_(jail_master_f)
vjl_master(enum jail_master_e jme)
{
	(void)jme;
}

static int vjl_getuid(const char *user, uid_t *uid, gid_t *gid,
												 char **ptr_user)
{
	struct passwd *pwd;

	pwd = getpwnam(user);
	if (pwd) {
		(*ptr_user) = strdup(user);
		AN(ptr_user);
		(*uid) = pwd->pw_uid;
		(*gid) = pwd->pw_gid;
	}
	endpwent();

	return (pwd == NULL ? -1 : 0);
}

static int
vjl_getccgid(const char *arg)
{
	struct group *gr;

	gr = getgrnam(arg);
	if (gr != NULL) {
		vjl_cc_gid_set = 1;
		vjl_cc_gid = gr->gr_gid;
	}
	endgrent();

	return(gr == NULL ? -1 : 0);
}

static int v_matchproto_(jail_init_f)
vjl_init(char **args)
{
	vjl_seccomp_mode = SCMP_ACT_TRAP;
	vjl_enable_seccomp = 0;

	if (args == NULL) {
		/* We are basically on the same mode as UNIX jail */
		if (geteuid() != 0)
			return 1;
		if (vjl_getuid(VARNISH_USER, &vjl_uid, &vjl_gid, &vjl_user))
			return 1;
	} else {
		if (geteuid() != 0 )
			ARGV_ERR("Linux Jail: Must be root.\n");

		for(;*args != NULL; args++) {
			if(!strncmp(*args, "user=", 5)) {
				if (vjl_getuid((*args) + 5, &vjl_uid, &vjl_gid, &vjl_user)) {
					ARGV_ERR(
						"Linux jail: %s user not found.\n", (*args) + 5);
					continue;
				}
			}
			if (!strncmp(*args, "workuser=", 9)) {
				if (vjl_getuid((*args) + 9, &vjl_wrkuid, &vjl_wrkgid,
														 &vjl_wrkuser)){
					ARGV_ERR(
					    "Unix jail: %s user not found.\n",
					    (*args) + 9);
					continue;
				}
			}
			if (!strncmp(*args, "ccgroup=", 8)) {
				if (vjl_getccgid((*args) + 8))
					ARGV_ERR(
					    "Unix jail: %s group not found.\n",
					    (*args) + 8);
				continue;
			}
			if (!strncmp(*args, "mgt_filter=", 11)) {
				vjl_enable_seccomp = 1;
				vjl_mgtf_path = strdup((*args) + 11);
				AN(vjl_mgtf_path);
				continue;
			}
			if (!strncmp(*args, "vcc_filter=",11)) {
				vjl_enable_seccomp = 1;
				vjl_vccf_path = strdup((*args) + 11);
				AN(vjl_vccf_path);
				continue;
			}
			if (!strncmp(*args, "wrk_filter=",11)) {
				vjl_enable_seccomp = 1;
				vjl_wrk_path = strdup((*args) + 11);
				AN(vjl_wrk_path);
				continue;
			}
			if (!strncmp(*args, "audit", 5)) {
				vjl_seccomp_mode = SCMP_ACT_LOG;
				continue;
			}
		}

		if (vjl_user == NULL && vjl_getuid(VARNISH_USER, &vjl_uid,
												 &vjl_gid, &vjl_user)) {
			ARGV_ERR("Linux jail: %s user not found.\n", VARNISH_USER);
		}
	}

	AN(vjl_user);

	if (vjl_enable_seccomp)
		AZ(vjl_seccomp_init(vjl_mgtf_path));

	vjl_mgt_gid = getgid();

	if (vjl_wrkuser == NULL && vjl_getuid(VCACHE_USER, &vjl_wrkuid,
											&vjl_wrkgid, &vjl_wrkuser)) {
		vjl_wrkuid = vjl_uid;
		vjl_wrkgid = vjl_gid;
	}

	if (vjl_wrkuser != NULL && vjl_wrkgid != vjl_gid)
		ARGV_ERR("Unix jail: user %s and %s have "
		    "different login groups\n", vjl_user, vjl_wrkuser);

	/* Drop the privilleges now */
	AZ(setegid(vjl_gid));
	AZ(seteuid(vjl_uid));

	return 0;
}

static int v_matchproto_(jail_make_dir_f)
vjl_make_workdir(const char *dname, const char *what, struct vsb *vsb)
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
	AZ(chown(dname, vjl_uid, vjl_gid));
	AZ(seteuid(vjl_uid));
	return (0);
}

static void v_matchproto_(jail_fixfd_f)
vjl_fixfd(int fd, enum jail_fixfd_e what)
{
	/* Called under JAIL_MASTER_FILE */

	switch (what) {
	case JAIL_FIXFD_FILE:
		AZ(fchmod(fd, 0750));
		AZ(fchown(fd, vjl_wrkuid, vjl_wrkgid));
		break;
	case JAIL_FIXFD_VSMMGT:
		AZ(fchmod(fd, 0750));
		AZ(fchown(fd, vjl_uid, vjl_gid));
		break;
	case JAIL_FIXFD_VSMWRK:
		AZ(fchmod(fd, 0750));
		AZ(fchown(fd, vjl_wrkuid, vjl_wrkgid));
		break;
	default:
		WRONG("Ain't Fixin'");
	}
}

static int v_matchproto_(jail_make_dir_f)
vjl_make_subdir(const char *dname, const char *what, struct vsb *vsb)
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
	AZ(chown(dname, vjl_uid, vjl_gid));
	AZ(seteuid(vjl_uid));
	return (0);
}

const struct jail_tech jail_tech_linux = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"linux_experimental",
	.init =		vjl_init,
	.master =	vjl_master,
	.subproc =	vjl_subproc,
	.make_workdir = vjl_make_workdir,
	.fixfd =	vjl_fixfd,
	.make_subdir = vjl_make_subdir,
};
