/*-
 * Copyright (c) 2006 Verdens Gang AS
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
 * Functions for tweaking parameters
 *
 */

#include "config.h"

#include <grp.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/params.h"

#include "mgt/mgt_param.h"

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

struct parspec mgt_parspec_sandbox[] = {
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
