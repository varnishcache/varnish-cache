/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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
 */

#include "config.h"

#include <sys/wait.h>
#include <sys/socket.h>

#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef HAVE_SYS_PERSONALITY_H
#  include <sys/personality.h>
#endif

#include "vtc.h"

#include "vfil.h"
#include "vnum.h"
#include "vre.h"
#include "vtcp.h"
#include "vsa.h"
#include "vss.h"
#include "vtim.h"
#include "vus.h"

/* SECTION: vtest vtest
 *
 * This should be the first command in your vtc as it will identify the test
 * case with a short yet descriptive sentence. It takes exactly one argument, a
 * string, eg::
 *
 *         vtest "Check that vtest is actually a valid command"
 *
 * It will also print that string in the log.
 */

void v_matchproto_(cmd_f)
cmd_vtest(CMD_ARGS)
{

	(void)priv;
	(void)vl;

	if (av == NULL)
		return;
	AZ(strcmp(av[0], "vtest"));

	vtc_log(vl, 1, "VTEST %s", av[1]);
	AZ(av[2]);
}

/* SECTION: varnishtest varnishtest
 *
 * Alternate name for 'vtest', see above.
 *
 */

void v_matchproto_(cmd_f)
cmd_varnishtest(CMD_ARGS)
{

	(void)priv;
	(void)vl;

	if (av == NULL)
		return;
	AZ(strcmp(av[0], "varnishtest"));

	vtc_log(vl, 1, "VTEST %s", av[1]);
	AZ(av[2]);
}

/* SECTION: shell shell
 *
 * NOTE: This command is available everywhere commands are given.
 *
 * Pass the string given as argument to a shell. If you have multiple
 * commands to run, you can use curly brackets to describe a multi-lines
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
 * Notice that the commandstring is prefixed with "exec 2>&1;" to combine
 * stderr and stdout back to the test process.
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
/* SECTION: client-server.spec.shell
 *
 * shell
 *	Same as for the top-level shell.
 */

static void
cmd_shell_engine(struct vtclog *vl, int ok, const char *cmd,
    const char *expect, const char *re)
{
	struct vsb *vsb, re_vsb[1];
	FILE *fp;
	vre_t *vre = NULL;
	int r, c;
	int err, erroff;
	char errbuf[VRE_ERROR_LEN];

	AN(vl);
	AN(cmd);
	vsb = VSB_new_auto();
	AN(vsb);
	if (re != NULL) {
		vre = VRE_compile(re, 0, &err, &erroff, 1);
		if (vre == NULL) {
			AN(VSB_init(re_vsb, errbuf, sizeof errbuf));
			AZ(VRE_error(re_vsb, err));
			AZ(VSB_finish(re_vsb));
			VSB_fini(re_vsb);
			vtc_fatal(vl,
			    "shell_match invalid regexp (\"%s\" at %d)",
			    errbuf, erroff);
		}
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
		if (VRE_match(vre, VSB_data(vsb), VSB_len(vsb), 0, NULL) < 1)
			vtc_fatal(vl, "shell_match failed: (\"%s\")", re);
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

/* SECTION: filewrite filewrite
 *
 * Write strings to file
 *
 *         filewrite [-a] /somefile "Hello" " " "World\n"
 *
 * The -a flag opens the file in append mode.
 *
 */

void v_matchproto_(cmd_f)
cmd_filewrite(CMD_ARGS)
{
	FILE *fo;
	int n;
	const char *mode = "w";

	(void)priv;

	if (av == NULL)
		return;
	if (av[1] != NULL && !strcmp(av[1], "-a")) {
		av++;
		mode = "a";
	}
	if (av[1] == NULL)
		vtc_fatal(vl, "Need filename");
	fo = fopen(av[1], mode);
	if (fo == NULL)
		vtc_fatal(vl, "Cannot open %s: %s", av[1], strerror(errno));
	for (n = 2; av[n] != NULL; n++)
		(void)fputs(av[n], fo);
	AZ(fclose(fo));
}

/* SECTION: setenv setenv
 *
 * Set or change an environment variable::
 *
 *         setenv FOO "bar baz"
 *
 * The above will set the environment variable $FOO to the value
 * provided. There is also an ``-ifunset`` argument which will only
 * set the value if the environment variable does not already
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
 * NOTE: This command is available everywhere commands are given.
 *
 * Sleep for the number of seconds specified in the argument. The number
 * can include a fractional part, e.g. 1.5.
 */
void
cmd_delay(CMD_ARGS)
{
	double f;

	(void)priv;
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

/* SECTION: include include
 *
 * Executes a vtc fragment::
 *
 *         include FILE [...]
 *
 * Open a file and execute it as a VTC fragment. This command is available
 * everywhere commands are given.
 *
 */
void
cmd_include(CMD_ARGS)
{
	char *spec;
	unsigned i;

	if (av == NULL)
		return;

	if (av[1] == NULL)
		vtc_fatal(vl, "CMD include: At least 1 argument required");

	for (i = 1; av[i] != NULL; i++) {
		spec = VFIL_readfile(NULL, av[i], NULL);
		if (spec == NULL)
			vtc_fatal(vl, "CMD include: Unable to read file '%s' "
			    "(%s)", av[i], strerror(errno));
		vtc_log(vl, 2, "Begin include '%s'", av[i]);
		parse_string(vl, priv, spec);
		vtc_log(vl, 2, "End include '%s'", av[i]);
		free(spec);
	}
}

/**********************************************************************
 * Most test-cases use only numeric IP#'s but a few requires non-demented
 * DNS services.  This is a basic sanity check for those.
 */

static int
dns_works(void)
{
	const struct suckaddr *sa;
	char abuf[VTCP_ADDRBUFSIZE];
	char pbuf[VTCP_PORTBUFSIZE];

	sa = VSS_ResolveOne(NULL, "dns-canary.varnish-cache.org", NULL,
	    AF_INET, SOCK_STREAM, 0);
	if (sa == NULL)
		return (0);
	VTCP_name(sa, abuf, sizeof abuf, pbuf, sizeof pbuf);
	VSA_free(&sa);
	if (strcmp(abuf, "192.0.2.255"))
		return (0);

	sa = VSS_ResolveOne(NULL, "dns-canary.varnish-cache.org", NULL,
	    AF_INET6, SOCK_STREAM, 0);
	if (sa == NULL)
		return (1); /* the canary is ipv4 only */
	VSA_free(&sa);
	return (0);
}

/**********************************************************************
 * Test if IPv4/IPv6 works
 */

static int
ipvx_works(const char *target)
{
	const struct suckaddr *sa;
	int fd;

	sa = VSS_ResolveOne(NULL, target, "0", 0, SOCK_STREAM, 0);
	if (sa == NULL)
		return (0);
	fd = VTCP_bind(sa, NULL);
	VSA_free(&sa);
	if (fd >= 0) {
		VTCP_close(&fd);
		return (1);
	}
	return (0);
}

/**********************************************************************/

static int
addr_no_randomize_works(void)
{
	int r = 0;

#ifdef HAVE_SYS_PERSONALITY_H
	r = personality(0xffffffff);
	r = personality(r | ADDR_NO_RANDOMIZE);
#endif
	return (r >= 0);
}

/**********************************************************************/

static int
uds_socket(void *priv, const struct sockaddr_un *uds)
{

	return (VUS_bind(uds, priv));
}
static int
abstract_uds_works(void)
{
	const char *err;
	int fd;

	fd = VUS_resolver("@vtc.feature.abstract_uds", uds_socket, NULL, &err);
	if (fd < 0)
		return (0);
	AZ(close(fd));
	return (1);
}

/* SECTION: feature feature
 *
 * Test that the required feature(s) for a test are available, and skip
 * the test otherwise; or change the interpretation of the test, as
 * documented below. feature takes any number of arguments from this list:
 *
 * 64bit
 *        The environment is 64 bits
 * ipv4
 *        127.0.0.1 works
 * ipv6
 *        [::1] works
 * dns
 *        DNS lookups are working
 * topbuild
 *        The test has been started with '-i'
 * root
 *        The test has been invoked by the root user
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
 * persistent_storage
 *        Varnish was built with the deprecated persistent storage.
 * coverage
 *        Varnish was built with code coverage enabled.
 * asan
 *        Varnish was built with the address sanitizer.
 * msan
 *        Varnish was built with the memory sanitizer.
 * tsan
 *        Varnish was built with the thread sanitizer.
 * ubsan
 *        Varnish was built with the undefined behavior sanitizer.
 * sanitizer
 *        Varnish was built with a sanitizer.
 * workspace_emulator
 *        Varnish was built with its workspace emulator.
 * abstract_uds
 *        Creation of an abstract unix domain socket succeeded.
 * disable_aslr
 *        ASLR can be disabled.
 *
 * A feature name can be prefixed with an exclamation mark (!) to skip a
 * test if the feature is present.
 *
 * Be careful with ignore_unknown_macro, because it may cause a test with a
 * misspelled macro to fail silently. You should only need it if you must
 * run a test with strings of the form "${...}".
 */

#if ENABLE_COVERAGE
static const unsigned coverage = 1;
#else
static const unsigned coverage = 0;
#endif

#if ENABLE_ASAN
static const unsigned asan = 1;
#else
static const unsigned asan = 0;
#endif

#if ENABLE_MSAN
static const unsigned msan = 1;
#else
static const unsigned msan = 0;
#endif

#if ENABLE_TSAN
static const unsigned tsan = 1;
#else
static const unsigned tsan = 0;
#endif

#if ENABLE_UBSAN
static const unsigned ubsan = 1;
#else
static const unsigned ubsan = 0;
#endif

#if ENABLE_SANITIZER
static const unsigned sanitizer = 1;
#else
static const unsigned sanitizer = 0;
#endif

#if ENABLE_WORKSPACE_EMULATOR
static const unsigned workspace_emulator = 1;
#else
static const unsigned workspace_emulator = 0;
#endif

#if WITH_PERSISTENT_STORAGE
static const unsigned with_persistent_storage = 1;
#else
static const unsigned with_persistent_storage = 0;
#endif

void v_matchproto_(cmd_f)
cmd_feature(CMD_ARGS)
{
	const char *feat;
	int r, good, skip, neg;

	(void)priv;

	if (av == NULL)
		return;

#define FEATURE(nm, tst)				\
	do {						\
		if (!strcmp(feat, nm)) {		\
			good = 1;			\
			if (tst) {			\
				skip = neg;		\
			} else {			\
				skip = !neg;		\
			}				\
		}					\
	} while (0)

	skip = 0;

	for (av++; *av != NULL; av++) {
		good = 0;
		neg = 0;
		feat = *av;

		if (feat[0] == '!') {
			neg = 1;
			feat++;
		}

		FEATURE("ipv4", ipvx_works("127.0.0.1"));
		FEATURE("ipv6", ipvx_works("[::1]"));
		FEATURE("64bit", sizeof(void*) == 8);
		FEATURE("disable_aslr", addr_no_randomize_works());
		FEATURE("dns", dns_works());
		FEATURE("topbuild", iflg);
		FEATURE("root", !geteuid());
		FEATURE("user_varnish", getpwnam("varnish") != NULL);
		FEATURE("user_vcache", getpwnam("vcache") != NULL);
		FEATURE("group_varnish", getgrnam("varnish") != NULL);
		FEATURE("persistent_storage", with_persistent_storage);
		FEATURE("coverage", coverage);
		FEATURE("asan", asan);
		FEATURE("msan", msan);
		FEATURE("tsan", tsan);
		FEATURE("ubsan", ubsan);
		FEATURE("sanitizer", sanitizer);
		FEATURE("workspace_emulator", workspace_emulator);
		FEATURE("abstract_uds", abstract_uds_works());

		if (!strcmp(feat, "cmd")) {
			good = 1;
			skip = neg;
			av++;
			if (*av == NULL)
				vtc_fatal(vl, "Missing the command-line");
			r = system(*av);
			if (WEXITSTATUS(r) != 0)
				skip = !neg;
		} else if (!strcmp(feat, "ignore_unknown_macro")) {
			ign_unknown_macro = 1;
			good = 1;
		}
		if (!good)
			vtc_fatal(vl, "FAIL test, unknown feature: %s", feat);

		if (!skip)
			continue;

		vtc_stop = 2;
		if (neg)
			vtc_log(vl, 1,
			    "SKIPPING test, conflicting feature: %s", feat);
		else
			vtc_log(vl, 1,
			    "SKIPPING test, lacking feature: %s", feat);
		return;
	}
}
