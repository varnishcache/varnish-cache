/*-
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Sandboxing child processes
 *
 * The worker/manager process border is one of the major security barriers
 * in Varnish, and therefore subject to whatever restrictions we have access
 * to under the given operating system.
 *
 * Unfortunately there is no consensus on APIs for this purpose, so each
 * operating system will require its own methods.
 *
 * This sourcefile tries to encapsulate the resulting mess on place.
 *
 * TODO:
 *	Unix:	chroot
 *	FreeBSD: jail
 *	FreeBSD: capsicum
 */

#include "config.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/params.h"
#include "mgt/mgt_param.h"

#include <vsub.h>

mgt_sandbox_f *mgt_sandbox;

/*--------------------------------------------------------------------
 * XXX: slightly magic.  We want to initialize to "nobody" (XXX: shouldn't
 * XXX: that be something autocrap found for us ?) but we don't want to
 * XXX: fail initialization if that user doesn't exists, even though we
 * XXX: do want to fail it, in subsequent sets.
 * XXX: The magic init string is a hack for this.
 */

static int
tweak_user(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	struct passwd *pw;

	(void)par;
	if (arg != NULL) {
		pw = getpwnam(arg);
		if (pw == NULL) {
			VSB_printf(vsb, "Unknown user '%s'", arg);
			return(-1);
		}
		REPLACE(mgt_param.user, pw->pw_name);
		mgt_param.uid = pw->pw_uid;
		endpwent();
	} else if (mgt_param.user) {
		VSB_printf(vsb, "%s (%d)", mgt_param.user, (int)mgt_param.uid);
	} else {
		VSB_printf(vsb, "UID %d", (int)mgt_param.uid);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * XXX: see comment for tweak_user, same thing here.
 */

static int
tweak_group(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		gr = getgrnam(arg);
		if (gr == NULL) {
			VSB_printf(vsb, "Unknown group '%s'", arg);
			return(-1);
		}
		REPLACE(mgt_param.group, gr->gr_name);
		mgt_param.gid = gr->gr_gid;
		endgrent();
	} else if (mgt_param.group) {
		VSB_printf(vsb, "%s (%d)", mgt_param.group, (int)mgt_param.gid);
	} else {
		VSB_printf(vsb, "GID %d", (int)mgt_param.gid);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * XXX: see comment for tweak_user, same thing here.
 */

static int
tweak_group_cc(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if (*arg != '\0') {
			gr = getgrnam(arg);
			if (gr == NULL) {
				VSB_printf(vsb, "Unknown group");
				return(-1);
			}
			REPLACE(mgt_param.group_cc, gr->gr_name);
			mgt_param.gid_cc = gr->gr_gid;
		} else {
			REPLACE(mgt_param.group_cc, "");
			mgt_param.gid_cc = 0;
		}
	} else if (strlen(mgt_param.group_cc) > 0) {
		VSB_printf(vsb, "%s (%d)",
		    mgt_param.group_cc, (int)mgt_param.gid_cc);
	} else {
		VSB_printf(vsb, "<not set>");
	}
	return (0);
}

/*--------------------------------------------------------------------
 */

static struct parspec mgt_parspec_sandbox[] = {
	{ "user", tweak_user, NULL, NULL, NULL,
		"The unprivileged user to run as.",
		MUST_RESTART | ONLY_ROOT,
		"" },
	{ "group", tweak_group, NULL, NULL, NULL,
		"The unprivileged group to run as.",
		MUST_RESTART | ONLY_ROOT,
		"" },
	{ "group_cc", tweak_group_cc, NULL, NULL, NULL,
		"On some systems the C-compiler is restricted so not"
		" everybody can run it.  This parameter makes it possible"
		" to add an extra group to the sandbox process which runs the"
		" cc_command, in order to gain access to such a restricted"
		" C-compiler.",
		ONLY_ROOT,
		"" },
	{ NULL, NULL, NULL }
};

/*--------------------------------------------------------------------*/

static void __match_proto__(mgt_sandbox_f)
mgt_sandbox_null(enum sandbox_e who)
{
	(void)who;
}

/*--------------------------------------------------------------------*/

#ifndef HAVE_SETPPRIV
static void __match_proto__(mgt_sandbox_f)
mgt_sandbox_unix(enum sandbox_e who)
{
#define NGID 2000
	int i;
	gid_t gid, gid_list[NGID];
	uid_t uid;

	if (who == SANDBOX_TESTING) {
		/*
		 * Test if sandboxing is going to work.
		 * Do not assert on failure here, but simply exit non-zero.
		 */
		gid = getgid();
		gid += 1;
		if (setgid(gid))
			exit(1);
		uid = getuid();
		uid += 1;
		if (setuid(uid))
			exit(2);
		exit(0);
	}

	/*
	 * Do the real thing, assert if we fail
	 */

	AZ(setgid(mgt_param.gid));
	AZ(initgroups(mgt_param.user, mgt_param.gid));

	if (who == SANDBOX_CC && strlen(mgt_param.group_cc) > 0) {
		/* Add the optional extra group for the C-compiler access */
		i = getgroups(NGID, gid_list);
		assert(i >= 0);
		gid_list[i++] = mgt_param.gid_cc;
		AZ(setgroups(i, gid_list));
	}

	AZ(setuid(mgt_param.uid));

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
#endif

/*--------------------------------------------------------------------*/

static void __match_proto__(sub_func_f)
run_sandbox_test(void *priv)
{

	(void)priv;
	mgt_sandbox(SANDBOX_TESTING);
}

/*--------------------------------------------------------------------*/

void
mgt_sandbox_init(void)
{
	struct passwd *pwd;
	struct group *grp;
	struct vsb *sb;
	unsigned subs;

	/* Pick a sandbox */

#ifdef HAVE_SETPPRIV
	mgt_sandbox = mgt_sandbox_solaris;
#else
	mgt_sandbox = mgt_sandbox_unix;
#endif

	/* Test it */

	sb = VSB_new_auto();
	subs = VSUB_run(sb, run_sandbox_test, NULL, "SANDBOX-test", 10);
	VSB_delete(sb);
	if (subs) {
		REPORT0(LOG_INFO, "Warning: init of platform-specific sandbox "
		    "failed - sandboxing disabled");
		REPORT0(LOG_INFO, "Warning: Varnish might run with elevated "
		    "privileges");
		mgt_sandbox = mgt_sandbox_null;
	}

	MCF_AddParams(mgt_parspec_sandbox);

	/*
	 * If we have nobody/nogroup, use them as defaults for sandboxes,
	 * else fall back to whoever we run as.
	 */
	if (getpwnam("nobody") != NULL) {
		MCF_SetDefault("user", "nobody");
	} else {
		pwd = getpwuid(getuid());
		if (pwd == NULL)
			ARGV_ERR("Neither user 'nobody' or my uid (%jd)"
			    " found in password database.\n",
			    (intmax_t)getuid());
		MCF_SetDefault("user", pwd->pw_name);
	}
	endpwent();

	if (getgrnam("nogroup") != NULL) {
		MCF_SetDefault("group", "nogroup");
	} else {
		grp = getgrgid(getgid());
		if (grp == NULL)
			ARGV_ERR("Neither group 'nogroup' or my gid (%jd)"
			    " found in password database.\n",
			    (intmax_t)getgid());
		MCF_SetDefault("group", grp->gr_name);
	}
	endgrent();
}
