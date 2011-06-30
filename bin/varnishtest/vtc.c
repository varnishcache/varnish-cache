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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "libvarnish.h"
#include "vev.h"
#include "vsb.h"
#include "vqueue.h"
#include "miniobj.h"

#include "vtc.h"

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

#define		MAX_TOKENS		200

static char		*vtc_desc;
volatile sig_atomic_t	vtc_error;	/* Error encountered */
int			vtc_stop;	/* Stops current test without error */
pthread_t		vtc_thread;
static struct vtclog	*vltop;
int			in_tree = 0;	/* Are we running in-tree */

/**********************************************************************
 * Macro facility
 */

struct macro {
	VTAILQ_ENTRY(macro)	list;
	char			*name;
	char			*val;
};

static VTAILQ_HEAD(,macro) macro_list = VTAILQ_HEAD_INITIALIZER(macro_list);

static pthread_mutex_t		macro_mtx;

static void
init_macro(void)
{
	AZ(pthread_mutex_init(&macro_mtx, NULL));
}

void
macro_def(struct vtclog *vl, const char *instance, const char *name,
    const char *fmt, ...)
{
	char buf1[256];
	char buf2[256];
	struct macro *m;
	va_list ap;

	if (instance != NULL) {
		bprintf(buf1, "%s_%s", instance, name);
		name = buf1;
	}

	AZ(pthread_mutex_lock(&macro_mtx));
	VTAILQ_FOREACH(m, &macro_list, list)
		if (!strcmp(name, m->name))
			break;
	if (m == NULL && fmt != NULL) {
		m = calloc(sizeof *m, 1);
		AN(m);
		REPLACE(m->name, name);
		VTAILQ_INSERT_TAIL(&macro_list, m, list);
	}
	if (fmt != NULL) {
		AN(m);
		va_start(ap, fmt);
		free(m->val);
		m->val = NULL;
		vbprintf(buf2, fmt, ap);
		va_end(ap);
		m->val = strdup(buf2);
		AN(m->val);
		vtc_log(vl, 4, "macro def %s=%s", name, m->val);
	} else if (m != NULL) {
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
		double t = TIM_real();
		retval = malloc(64);
		AN(retval);
		TIM_format(t, retval);
		return (retval);
	}

	AZ(pthread_mutex_lock(&macro_mtx));
	VTAILQ_FOREACH(m, &macro_list, list)
		if (!memcmp(b, m->name, l) && m->name[l] == '\0')
			break;
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
			VSB_delete(vsb);
			vtc_log(vl, 0, "Macro ${%s} not found", p);
			return (NULL);
		}
		VSB_printf(vsb, "%s", m);
		text = q + 1;
	}
	AZ(VSB_finish(vsb));
	return (vsb);
}

/* extmacro is a list of macro's that are defined from the
   command line and are applied to the macro list of each test
   instance. No locking is required as they are set before any tests
   are started.
*/

struct extmacro {
	VTAILQ_ENTRY(extmacro)	list;
	char			*name;
	char			*val;
};

static VTAILQ_HEAD(, extmacro) extmacro_list =
	VTAILQ_HEAD_INITIALIZER(extmacro_list);

void
extmacro_def(const char *name, const char *fmt, ...)
{
	char buf[256];
	struct extmacro *m;
	va_list ap;

	VTAILQ_FOREACH(m, &extmacro_list, list)
		if (!strcmp(name, m->name))
			break;
	if (m == NULL && fmt != NULL) {
		m = calloc(sizeof *m, 1);
		AN(m);
		REPLACE(m->name, name);
		VTAILQ_INSERT_TAIL(&extmacro_list, m, list);
	}
	if (fmt != NULL) {
		AN(m);
		va_start(ap, fmt);
		free(m->val);
		vbprintf(buf, fmt, ap);
		va_end(ap);
		m->val = strdup(buf);
		AN(m->val);
	} else if (m != NULL) {
		VTAILQ_REMOVE(&extmacro_list, m, list);
		free(m->name);
		free(m->val);
		free(m);
	}
}

const char *
extmacro_get(const char *name)
{
	struct extmacro *m;

	VTAILQ_FOREACH(m, &extmacro_list, list)
		if (!strcmp(name, m->name))
			return (m->val);

	return (NULL);
}

/**********************************************************************
 * Execute a file
 */

void
parse_string(char *buf, const struct cmds *cmd, void *priv, struct vtclog *vl)
{
	char *token_s[MAX_TOKENS], *token_e[MAX_TOKENS];
	struct vsb *token_exp[MAX_TOKENS];
	char *p, *q, *f;
	int nest_brace;
	int tn;
	const struct cmds *cp;

	assert(buf != NULL);
	for (p = buf; *p != '\0'; p++) {
		if (vtc_error || vtc_stop)
			break;
		/* Start of line */
		if (isspace(*p))
			continue;
		if (*p == '#') {
			for (; *p != '\0' && *p != '\n'; p++)
				;
			if (*p == '\0')
				break;
			continue;
		}

		/* First content on line, collect tokens */
		tn = 0;
		f = p;
		while (*p != '\0') {
			assert(tn < MAX_TOKENS);
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
					if (*p == '"')
						break;
					if (*p == '\\') {
						p += VAV_BackSlash(p, q) - 1;
						q++;
					} else {
						if (*p == '\n')
							vtc_log(vl, 0,
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
				for (; *p != '\0'; p++) {
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
				for (; *p != '\0' && !isspace(*p); p++)
					;
				token_e[tn++] = p;
			}
		}
		assert(tn < MAX_TOKENS);
		token_s[tn] = NULL;
		for (tn = 0; token_s[tn] != NULL; tn++) {
			token_exp[tn] = NULL;
			AN(token_e[tn]);	/*lint !e771 */
			*token_e[tn] = '\0';	/*lint !e771 */
			if (NULL == strstr(token_s[tn], "${"))
				continue;
			token_exp[tn] = macro_expand(vl, token_s[tn]);
			if (vtc_error)
				return;
			token_s[tn] = VSB_data(token_exp[tn]);
			token_e[tn] = strchr(token_s[tn], '\0');
		}

		for (cp = cmd; cp->name != NULL; cp++)
			if (!strcmp(token_s[0], cp->name))
				break;
		if (cp->name == NULL) {
			vtc_log(vl, 0, "Unknown command: \"%s\"", token_s[0]);
			return;
		}
		vtc_log(vl, 3, "%s", token_s[0]);

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

/**********************************************************************
 * Output test description
 */

static void
cmd_varnishtest(CMD_ARGS)
{

	(void)priv;
	(void)cmd;
	(void)vl;

	if (av == NULL)
		return;
	assert(!strcmp(av[0], "varnishtest"));

	vtc_log(vl, 1, "TEST %s", av[1]);
	AZ(av[2]);
	vtc_desc = strdup(av[1]);
}

/**********************************************************************
 * Shell command execution
 */

static void
cmd_shell(CMD_ARGS)
{
	(void)priv;
	(void)cmd;
	int r;

	if (av == NULL)
		return;
	AN(av[1]);
	AZ(av[2]);
	vtc_dump(vl, 4, "shell", av[1], -1);
	r = system(av[1]);
	assert(WEXITSTATUS(r) == 0);
}

/**********************************************************************
 * Dump command arguments
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
	f = strtod(av[1], NULL);
	vtc_log(vl, 3, "delaying %g second(s)", f);
	TIM_sleep(f);
}

/**********************************************************************
 * Check random generator
 */

#define NRNDEXPECT	12
static const unsigned long random_expect[NRNDEXPECT] = {
	1804289383,	846930886,	1681692777,	1714636915,
	1957747793,	424238335,	719885386,	1649760492,
	 596516649,	1189641421,	1025202362,	1350490027
};

#define RND_NEXT_1K	0x3bdcbe30

static void
cmd_random(CMD_ARGS)
{
	uint32_t l;
	int i;

	(void)cmd;
	(void)priv;
	if (av == NULL)
		return;
	srandom(1);
	for (i = 0; i < NRNDEXPECT; i++) {
		l = random();
		if (l == random_expect[i])
			continue;
		vtc_log(vl, 4, "random[%d] = 0x%x (expect 0x%x)",
		    i, l, random_expect[i]);
		vtc_log(vl, 1, "SKIPPING test: unknown srandom(1) sequence.");
		vtc_stop = 1;
		break;
	}
	l = 0;
	for (i = 0; i < 1000; i++)
		l += random();
	if (l != RND_NEXT_1K) {
		vtc_log(vl, 4, "sum(random[%d...%d]) = 0x%x (expect 0x%x)",
		    NRNDEXPECT, NRNDEXPECT + 1000,
		    l, RND_NEXT_1K);
		vtc_log(vl, 1, "SKIPPING test: unknown srandom(1) sequence.");
		vtc_stop = 1;
	}
}

/**********************************************************************
 * Check features.
 */

static void
cmd_feature(CMD_ARGS)
{
	int i;

	(void)priv;
	(void)cmd;
	if (av == NULL)
		return;

	for (i = 1; av[i] != NULL; i++) {
#ifdef SO_RCVTIMEO_WORKS
		if (!strcmp(av[i], "SO_RCVTIMEO_WORKS"))
			continue;
#endif
		if (sizeof(void*) == 8 && !strcmp(av[i], "64bit"))
			continue;

		vtc_log(vl, 1, "SKIPPING test, missing feature %s", av[i]);
		vtc_stop = 1;
		return;
	}
}

/**********************************************************************
 * Execute a file
 */

static const struct cmds cmds[] = {
	{ "server",	cmd_server },
	{ "client",	cmd_client },
	{ "varnish",	cmd_varnish },
	{ "delay",	cmd_delay },
	{ "varnishtest",cmd_varnishtest },
	{ "shell",	cmd_shell },
	{ "sema",	cmd_sema },
	{ "random",	cmd_random },
	{ "feature",	cmd_feature },
	{ NULL,		NULL }
};

int
exec_file(const char *fn, const char *script, const char *tmpdir,
    char *logbuf, unsigned loglen)
{
	unsigned old_err;
	char *cwd, *p;
	FILE *f;
	struct extmacro *m;

	vtc_loginit(logbuf, loglen);
	vltop = vtc_logopen("top");
	AN(vltop);

	init_macro();
	init_sema();

	/* Apply extmacro definitions */
	VTAILQ_FOREACH(m, &extmacro_list, list)
		macro_def(vltop, NULL, m->name, m->val);

	/* Other macro definitions */
	cwd = getcwd(NULL, PATH_MAX);
	macro_def(vltop, NULL, "pwd", cwd);
	macro_def(vltop, NULL, "topbuild", "%s/%s", cwd, TOP_BUILDDIR);
	macro_def(vltop, NULL, "bad_ip", "10.255.255.255");

	/* Move into our tmpdir */
	AZ(chdir(tmpdir));
	macro_def(vltop, NULL, "tmpdir", tmpdir);

	/* Drop file to tell what was going on here */
	f = fopen("INFO", "w");
	AN(f);
	fprintf(f, "Test case: %s\n", fn);
	AZ(fclose(f));

	vtc_stop = 0;
	vtc_desc = NULL;
	vtc_log(vltop, 1, "TEST %s starting", fn);

	p = strdup(script);
	AN(p);

	vtc_thread = pthread_self();
	parse_string(p, cmds, NULL, vltop);
	old_err = vtc_error;
	vtc_stop = 1;
	vtc_log(vltop, 1, "RESETTING after %s", fn);
	reset_cmds(cmds);
	vtc_error = old_err;

	if (vtc_error)
		vtc_log(vltop, 1, "TEST %s FAILED", fn);
	else
		vtc_log(vltop, 1, "TEST %s completed", fn);

	free(vtc_desc);
	return (vtc_error);
}
