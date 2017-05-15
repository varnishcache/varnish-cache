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

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtc.h"

#include "vav.h"
#include "vnum.h"
#include "vre.h"
#include "vtim.h"

#ifdef HAVE_SYS_PERSONALITY_H
#  include <sys/personality.h>
#endif

#define		MAX_TOKENS		200

volatile sig_atomic_t	vtc_error;	/* Error encountered */
int			vtc_stop;	/* Stops current test without error */
pthread_t		vtc_thread;
static struct vtclog	*vltop;

/**********************************************************************
 * Macro facility
 */

struct macro {
	unsigned		magic;
#define MACRO_MAGIC		0x803423e3
	VTAILQ_ENTRY(macro)	list;
	char			*name;
	char			*val;
};

static VTAILQ_HEAD(,macro) macro_list = VTAILQ_HEAD_INITIALIZER(macro_list);

/**********************************************************************/

static struct macro *
macro_def_int(const char *name, const char *fmt, va_list ap)
{
	struct macro *m;
	char buf[256];

	VTAILQ_FOREACH(m, &macro_list, list)
		if (!strcmp(name, m->name))
			break;
	if (m == NULL) {
		ALLOC_OBJ(m, MACRO_MAGIC);
		AN(m);
		REPLACE(m->name, name);
		AN(m->name);
		VTAILQ_INSERT_TAIL(&macro_list, m, list);
	}
	AN(m);
	vbprintf(buf, fmt, ap);
	REPLACE(m->val, buf);
	AN(m->val);
	return (m);
}


/**********************************************************************
 * This is for defining macros before we fork the child process which
 * runs the test-case.
 */

void
extmacro_def(const char *name, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)macro_def_int(name, fmt, ap);
	va_end(ap);
}

/**********************************************************************
 * Below this point is run inside the testing child-process.
 */

static pthread_mutex_t		macro_mtx;

static void
init_macro(void)
{
	struct macro *m;

	/* Dump the extmacros for completeness */
	VTAILQ_FOREACH(m, &macro_list, list)
		vtc_log(vltop, 4, "extmacro def %s=%s", m->name, m->val);

	AZ(pthread_mutex_init(&macro_mtx, NULL));
}

void
macro_def(struct vtclog *vl, const char *instance, const char *name,
    const char *fmt, ...)
{
	char buf1[256];
	struct macro *m;
	va_list ap;

	AN(fmt);

	if (instance != NULL) {
		bprintf(buf1, "%s_%s", instance, name);
		name = buf1;
	}

	AZ(pthread_mutex_lock(&macro_mtx));
	va_start(ap, fmt);
	m = macro_def_int(name, fmt, ap);
	va_end(ap);
	vtc_log(vl, 4, "macro def %s=%s", name, m->val);
	AZ(pthread_mutex_unlock(&macro_mtx));
}

void
macro_undef(struct vtclog *vl, const char *instance, const char *name)
{
	char buf1[256];
	struct macro *m;

	if (instance != NULL) {
		bprintf(buf1, "%s_%s", instance, name);
		name = buf1;
	}

	AZ(pthread_mutex_lock(&macro_mtx));
	VTAILQ_FOREACH(m, &macro_list, list)
		if (!strcmp(name, m->name))
			break;
	if (m != NULL) {
		vtc_log(vl, 4, "macro undef %s", name);
		VTAILQ_REMOVE(&macro_list, m, list);
		free(m->name);
		free(m->val);
		free(m);
	}
	AZ(pthread_mutex_unlock(&macro_mtx));
}

static char *
macro_get(const char *b, const char *e)
{
	struct macro *m;
	int l;
	char *retval = NULL;

	l = e - b;

	if (l == 4 && !memcmp(b, "date", l)) {
		double t = VTIM_real();
		retval = malloc(64);
		AN(retval);
		VTIM_format(t, retval);
		return (retval);
	}

	AZ(pthread_mutex_lock(&macro_mtx));
	VTAILQ_FOREACH(m, &macro_list, list) {
		CHECK_OBJ_NOTNULL(m, MACRO_MAGIC);
		if (!strncmp(b, m->name, l) && m->name[l] == '\0')
			break;
	}
	if (m != NULL)
		retval = strdup(m->val);
	AZ(pthread_mutex_unlock(&macro_mtx));
	return (retval);
}

struct vsb *
macro_expand(struct vtclog *vl, const char *text)
{
	struct vsb *vsb;
	const char *p, *q;
	char *m;

	vsb = VSB_new_auto();
	AN(vsb);
	while (*text != '\0') {
		p = strstr(text, "${");
		if (p == NULL) {
			VSB_cat(vsb, text);
			break;
		}
		VSB_bcat(vsb, text, p - text);
		q = strchr(p, '}');
		if (q == NULL) {
			VSB_cat(vsb, text);
			break;
		}
		assert(p[0] == '$');
		assert(p[1] == '{');
		assert(q[0] == '}');
		p += 2;
		m = macro_get(p, q);
		if (m == NULL) {
			VSB_destroy(&vsb);
			vtc_fatal(vl, "Macro ${%.*s} not found", (int)(q - p),
			    p);
			NEEDLESS(return (NULL));
		}
		VSB_printf(vsb, "%s", m);
		free(m);
		text = q + 1;
	}
	AZ(VSB_finish(vsb));
	return (vsb);
}

/**********************************************************************
 * Parse a string
 *
 * We make a copy of the string and deliberately leak it, so that all
 * the cmd functions we call don't have to strdup(3) all over the place.
 *
 * Static checkers like Coverity may bitch about this, but we don't care.
 */

void
parse_string(const char *spec, const struct cmds *cmd, void *priv,
    struct vtclog *vl)
{
	char *token_s[MAX_TOKENS], *token_e[MAX_TOKENS];
	struct vsb *token_exp[MAX_TOKENS];
	char *e, *p, *q, *f, *buf;
	int nest_brace;
	int tn;
	const struct cmds *cp;

	AN(spec);
	buf = strdup(spec);
	AN(buf);
	e = strchr(buf, '\0');
	AN(e);
	for (p = buf; p < e; p++) {
		if (vtc_error || vtc_stop)
			break;
		/* Start of line */
		if (isspace(*p))
			continue;
		if (*p == '\n')
			continue;

		if (*p == '#') {
			for (; *p != '\0' && *p != '\n'; p++)
				;
			if (*p == '\0')
				break;
			continue;
		}

		q = strchr(p, '\n');
		if (q == NULL)
			q = strchr(p, '\0');
		if (q - p > 60)
			vtc_log(vl, 2, "=== %.60s...", p);
		else
			vtc_log(vl, 2, "=== %.*s", (int)(q - p), p);

		/* First content on line, collect tokens */
		tn = 0;
		f = p;
		while (p < e) {
			assert(tn < MAX_TOKENS);
			assert(p < e);
			if (*p == '\n') { /* End on NL */
				break;
			}
			if (isspace(*p)) { /* Inter-token whitespace */
				p++;
				continue;
			}
			if (*p == '\\' && p[1] == '\n') { /* line-cont */
				p += 2;
				continue;
			}
			if (*p == '"') { /* quotes */
				token_s[tn] = ++p;
				q = p;
				for (; *p != '\0'; p++) {
					assert(p < e);
					if (*p == '"')
						break;
					if (*p == '\\') {
						p += VAV_BackSlash(p, q) - 1;
						q++;
					} else {
						if (*p == '\n')
							vtc_fatal(vl,
				"Unterminated quoted string in line: %*.*s",
				(int)(p - f), (int)(p - f), f);
						assert(*p != '\n');
						*q++ = *p;
					}
				}
				token_e[tn++] = q;
				p++;
			} else if (*p == '{') { /* Braces */
				nest_brace = 0;
				token_s[tn] = p + 1;
				for (; p < e; p++) {
					if (*p == '{')
						nest_brace++;
					else if (*p == '}') {
						if (--nest_brace == 0)
							break;
					}
				}
				assert(*p == '}');
				token_e[tn++] = p++;
			} else { /* other tokens */
				token_s[tn] = p;
				for (; p < e && !isspace(*p); p++)
					continue;
				token_e[tn++] = p;
			}
		}

		assert(p <= e);
		assert(tn < MAX_TOKENS);
		token_s[tn] = NULL;
		for (tn = 0; token_s[tn] != NULL; tn++) {
			token_exp[tn] = NULL;
			AN(token_e[tn]);	/*lint !e771 */
			*token_e[tn] = '\0';	/*lint !e771 */
			if (NULL != strstr(token_s[tn], "${")) {
				token_exp[tn] = macro_expand(vl, token_s[tn]);
				if (vtc_error)
					return;
				token_s[tn] = VSB_data(token_exp[tn]);
				token_e[tn] = strchr(token_s[tn], '\0');
			}
		}

		for (cp = cmd; cp->name != NULL; cp++)
			if (!strcmp(token_s[0], cp->name))
				break;

		if (cp->name == NULL)
			vtc_fatal(vl, "Unknown command: \"%s\"", token_s[0]);

		assert(cp->cmd != NULL);
		cp->cmd(token_s, priv, cmd, vl);
	}
}

/**********************************************************************
 * Reset commands (between tests)
 */

static void
reset_cmds(const struct cmds *cmd)
{

	for (; cmd->name != NULL; cmd++)
		cmd->cmd(NULL, NULL, NULL, NULL);
}

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

static void
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

static void
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
static void
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
 * Test that the required feature(s) for a test are available, and skip the test
 * otherwise. feature takes any number of arguments from this list:
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
 */

static void
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

/**********************************************************************
 * Execute a file
 */

static const struct cmds cmds[] = {
#define CMD(n) { #n, cmd_##n },
	CMD(server)
	CMD(client)
	CMD(varnish)
	CMD(delay)
	CMD(varnishtest)
	CMD(shell)
	CMD(err_shell)
	CMD(barrier)
	CMD(feature)
	CMD(logexpect)
	CMD(process)
	CMD(setenv)
#undef CMD
	{ NULL, NULL }
};

static const char *tfn;

int
fail_out(void)
{
	unsigned old_err;

	old_err = vtc_error;
	if (!vtc_stop)
		vtc_stop = 1;
	vtc_log(vltop, 1, "RESETTING after %s", tfn);
	reset_cmds(cmds);
	vtc_error |= old_err;

	if (vtc_error)
		vtc_log(vltop, 1, "TEST %s FAILED", tfn);
	else
		vtc_log(vltop, 1, "TEST %s completed", tfn);

	if (vtc_stop > 1)
		return (1);
	return (vtc_error);
}

int
exec_file(const char *fn, const char *script, const char *tmpdir,
    char *logbuf, unsigned loglen)
{
	FILE *f;

	(void)signal(SIGPIPE, SIG_IGN);

	tfn = fn;
	vtc_loginit(logbuf, loglen);
	vltop = vtc_logopen("top");
	AN(vltop);

	init_macro();
	init_server();

	/* Move into our tmpdir */
	AZ(chdir(tmpdir));
	macro_def(vltop, NULL, "tmpdir", "%s", tmpdir);

	/* Drop file to tell what was going on here */
	f = fopen("INFO", "w");
	AN(f);
	fprintf(f, "Test case: %s\n", fn);
	AZ(fclose(f));

	vtc_stop = 0;
	vtc_log(vltop, 1, "TEST %s starting", fn);

	vtc_thread = pthread_self();
	parse_string(script, cmds, NULL, vltop);
	return(fail_out());
}
