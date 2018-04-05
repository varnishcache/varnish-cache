/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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
 */

#include "config.h"

#include <sys/wait.h>

#include <errno.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_SYS_PERSONALITY_H
#  include <sys/personality.h>
#endif

#include "vtc.h"

#include "vnum.h"
#include "vre.h"
#include "vtim.h"

/* SECTION: varnishtest varnishtest
 *
 * This should be the first command in your vtc as it will identify the test
 * case with a short yet descriptive sentence. It takes exactly one argument, a
 * string, eg::
 *
 *         varnishtest "Check that varnishtest is actually a valid command"
 *
 * It will also print that string in the log.
 */

void v_matchproto_(cmd_f)
cmd_varnishtest(CMD_ARGS)
{

	(void)priv;
	(void)cmd;
	(void)vl;

	if (av == NULL)
		return;
	AZ(strcmp(av[0], "varnishtest"));

	vtc_log(vl, 1, "TEST %s", av[1]);
	AZ(av[2]);
}

/* SECTION: shell shell
 *
 * Pass the string given as argument to a shell. If you have multiple
 * commands to run, you can use curly barces to describe a multi-lines
 * script, eg::
 *
 *         shell {
 *                 echo begin
 *                 cat /etc/fstab
 *                 echo end
 *         }
 *
 * By default a zero exit code is expected, otherwise the vtc will fail.
 *
 * Notice that the commandstring is prefixed with "exec 2>&1;" to join
 * stderr and stdout back to the varnishtest process.
 *
 * Optional arguments:
 *
 * \-err
 *	Expect non-zero exit code.
 *
 * \-exit N
 *	Expect exit code N instead of zero.
 *
 * \-expect STRING
 *	Expect string to be found in stdout+err.
 *
 * \-match REGEXP
 *	Expect regexp to match the stdout+err output.
 */
/* SECTION: client-server.spec.shell shell
 *
 * Same as for the top-level shell.
 */

static void
cmd_shell_engine(struct vtclog *vl, int ok, const char *cmd,
    const char *expect, const char *re)
{
	struct vsb *vsb;
	FILE *fp;
	vre_t *vre = NULL;
	const char *errptr;
	int r, c;
	int err;

	AN(vl);
	AN(cmd);
	vsb = VSB_new_auto();
	AN(vsb);
	if (re != NULL) {
		vre = VRE_compile(re, 0, &errptr, &err);
		if (vre == NULL)
			vtc_fatal(vl, "shell_match invalid regexp (\"%s\")",
			    re);
	}
	VSB_printf(vsb, "exec 2>&1 ; %s", cmd);
	AZ(VSB_finish(vsb));
	vtc_dump(vl, 4, "shell_cmd", VSB_data(vsb), -1);
	fp = popen(VSB_data(vsb), "r");
	if (fp == NULL)
		vtc_fatal(vl, "popen fails: %s", strerror(errno));
	VSB_clear(vsb);
	do {
		c = getc(fp);
		if (c != EOF)
			VSB_putc(vsb, c);
	} while (c != EOF);
	r = pclose(fp);
	AZ(VSB_finish(vsb));
	vtc_dump(vl, 4, "shell_out", VSB_data(vsb), VSB_len(vsb));
	vtc_log(vl, 4, "shell_status = 0x%04x", WEXITSTATUS(r));
	if (WIFSIGNALED(r))
		vtc_log(vl, 4, "shell_signal = %d", WTERMSIG(r));

	if (ok < 0 && !WEXITSTATUS(r) && !WIFSIGNALED(r))
		vtc_fatal(vl, "shell did not fail as expected");
	else if (ok >= 0 && WEXITSTATUS(r) != ok)
		vtc_fatal(vl, "shell_exit not as expected: "
		    "got 0x%04x wanted 0x%04x", WEXITSTATUS(r), ok);

	if (expect != NULL) {
		if (strstr(VSB_data(vsb), expect) == NULL)
			vtc_fatal(vl,
			    "shell_expect not found: (\"%s\")", expect);
		else
			vtc_log(vl, 4, "shell_expect found");
	} else if (vre != NULL) {
		if (VRE_exec(vre, VSB_data(vsb), VSB_len(vsb), 0, 0,
		    NULL, 0, NULL) < 1)
			vtc_fatal(vl,
			    "shell_match failed: (\"%s\")", re);
		else
			vtc_log(vl, 4, "shell_match succeeded");
		VRE_free(&vre);
	}
	VSB_destroy(&vsb);
}


void
cmd_shell(CMD_ARGS)
{
	const char *expect = NULL;
	const char *re = NULL;
	int n;
	int ok = 0;

	(void)priv;
	(void)cmd;

	if (av == NULL)
		return;
	for (n = 1; av[n] != NULL; n++) {
		if (!strcmp(av[n], "-err")) {
			ok = -1;
		} else if (!strcmp(av[n], "-exit")) {
			n += 1;
			ok = atoi(av[n]);
		} else if (!strcmp(av[n], "-expect")) {
			if (re != NULL)
				vtc_fatal(vl,
				    "Cannot use -expect with -match");
			n += 1;
			expect = av[n];
		} else if (!strcmp(av[n], "-match")) {
			if (expect != NULL)
				vtc_fatal(vl,
				    "Cannot use -match with -expect");
			n += 1;
			re = av[n];
		} else {
			break;
		}
	}
	AN(av[n]);
	cmd_shell_engine(vl, ok, av[n], expect, re);
}

/* SECTION: err_shell err_shell
 *
 * This is very similar to the the ``shell`` command, except it takes a first
 * string as argument before the command::
 *
 *         err_shell "foo" "echo foo"
 *
 * err_shell expect the shell command to fail AND stdout to match the string,
 * failing the test case otherwise.
 */

void v_matchproto_(cmd_f)
cmd_err_shell(CMD_ARGS)
{
	(void)priv;
	(void)cmd;

	if (av == NULL)
		return;
	AN(av[1]);
	AN(av[2]);
	AZ(av[3]);
	vtc_log(vl, 1,
	    "NOTICE: err_shell is deprecated, use 'shell -err -expect'");
	cmd_shell_engine(vl, -1, av[2], av[1], NULL);
}

/* SECTION: setenv setenv
 *
 * Set or change an environment variable::
 *
 *         setenv FOO "bar baz"
 *
 * The above will set the environment variable $FOO to the value
 * provided. There is also an ``-ifunset`` argument which will only
 * set the value if the the environment variable does not already
 * exist::
 *
 *        setenv -ifunset FOO quux
 */

void v_matchproto_(cmd_f)
cmd_setenv(CMD_ARGS)
{
	int r;
	int force;

	(void)priv;
	(void)cmd;

	if (av == NULL)
		return;
	AN(av[1]);
	AN(av[2]);

	force = 1;
	if (strcmp("-ifunset", av[1]) == 0) {
		force = 0;
		av++;
		AN(av[2]);
	}
	if (av[3] != NULL)
		vtc_fatal(vl, "CMD setenv: Unexpected argument '%s'", av[3]);
	r = setenv(av[1], av[2], force);
	if (r != 0)
		vtc_log(vl, 0, "CMD setenv %s=\"%s\" failed: %s",
		    av[1], av[2], strerror(errno));
}

/* SECTION: delay delay
 *
 * Sleep for the number of seconds specified in the argument. The number
 * can include a fractional part, e.g. 1.5.
 */
/* SECTION: stream.spec.delay delay
 *
 * Same as for the top-level delay.
 */
void
cmd_delay(CMD_ARGS)
{
	double f;

	(void)priv;
	(void)cmd;
	if (av == NULL)
		return;
	AN(av[1]);
	AZ(av[2]);
	f = VNUM(av[1]);
	if (isnan(f))
		vtc_fatal(vl, "Syntax error in number (%s)", av[1]);
	vtc_log(vl, 3, "delaying %g second(s)", f);
	VTIM_sleep(f);
}

/* SECTION: feature feature
 *
 * Test that the required feature(s) for a test are available, and skip
 * the test otherwise; or change the interpretation of the test, as
 * documented below. feature takes any number of arguments from this list:
 *
 * SO_RCVTIMEO_WORKS
 *        The SO_RCVTIMEO socket option is working
 * 64bit
 *        The environment is 64 bits
 * !OSX
 *        The environment is not OSX
 * dns
 *        DNS lookups are working
 * topbuild
 *        varnishtest has been started with '-i'
 * root
 *        varnishtest has been invoked by the root user
 * user_varnish
 *        The varnish user is present
 * user_vcache
 *        The vcache user is present
 * group_varnish
 *        The varnish group is present
 * cmd <command-line>
 *        A command line that should execute with a zero exit status
 * ignore_unknown_macro
 *        Do not fail the test if a string of the form ${...} is not
 *        recognized as a macro.
 *
 * persistent_storage
 *        Varnish was built with the deprecated persistent storage.
 *
 * Be careful with ignore_unknown_macro, because it may cause a test with a
 * misspelled macro to fail silently. You should only need it if you must
 * run a test with strings of the form "${...}".
 */

#if WITH_PERSISTENT_STORAGE
static const unsigned with_persistent_storage = 1;
#else
static const unsigned with_persistent_storage = 0;
#endif

void v_matchproto_(cmd_f)
cmd_feature(CMD_ARGS)
{
	int r;
	int good;

	(void)priv;
	(void)cmd;

	if (av == NULL)
		return;

#define FEATURE(nm, tst)				\
	do {						\
		if (!strcmp(*av, nm)) {			\
			if (tst) {			\
				good = 1;		\
			} else {			\
				vtc_stop = 2;		\
			}				\
		}					\
	} while (0)

	for (av++; *av != NULL; av++) {
		good = 0;
		if (!strcmp(*av, "SO_RCVTIMEO_WORKS")) {
#ifdef SO_RCVTIMEO_WORKS
			good = 1;
#else
			vtc_stop = 2;
#endif
		}

		if (!strcmp(*av, "!OSX")) {
#if !defined(__APPLE__) || !defined(__MACH__)
			good = 1;
#else
			vtc_stop = 2;
#endif
		}
		FEATURE("pcre_jit", VRE_has_jit);
		FEATURE("64bit", sizeof(void*) == 8);
		FEATURE("dns", feature_dns);
		FEATURE("topbuild", iflg);
		FEATURE("root", !geteuid());
		FEATURE("user_varnish", getpwnam("varnish") != NULL);
		FEATURE("user_vcache", getpwnam("vcache") != NULL);
		FEATURE("group_varnish", getgrnam("varnish") != NULL);
		FEATURE("persistent_storage", with_persistent_storage);

		if (!strcmp(*av, "disable_aslr")) {
			good = 1;
#ifdef HAVE_SYS_PERSONALITY_H
			r = personality(0xffffffff);
			r = personality(r | ADDR_NO_RANDOMIZE);
			if (r < 0) {
				good = 0;
				vtc_stop = 2;
			}
#endif
		} else if (!strcmp(*av, "cmd")) {
			av++;
			if (*av == NULL)
				vtc_fatal(vl, "Missing the command-line");
			r = system(*av);
			if (WEXITSTATUS(r) == 0)
				good = 1;
			else
				vtc_stop = 2;
		} else if (!strcmp(*av, "ignore_unknown_macro")) {
			ign_unknown_macro = 1;
			good = 1;
		}
		if (good)
			continue;

		if (!vtc_stop)
			vtc_fatal(vl, "FAIL test, unknown feature: %s", *av);
		else
			vtc_log(vl, 1,
			    "SKIPPING test, lacking feature: %s", *av);
		return;
	}
}
