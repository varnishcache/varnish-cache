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

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtc.h"
#include "vtc_log.h"

#include "vav.h"
#include "vrnd.h"

#define		MAX_TOKENS		200

volatile sig_atomic_t	vtc_error;	/* Error encountered */
int			vtc_stop;	/* Stops current test without error */
pthread_t		vtc_thread;
int			ign_unknown_macro = 0;
static struct vtclog	*vltop;

static pthread_mutex_t	vtc_vrnd_mtx;

static void
vtc_vrnd_lock(void)
{
	PTOK(pthread_mutex_lock(&vtc_vrnd_mtx));
}

static void
vtc_vrnd_unlock(void)
{
	PTOK(pthread_mutex_unlock(&vtc_vrnd_mtx));
}

static const char *tfn;

/**********************************************************************
 * Macro facility
 */

struct macro {
	unsigned		magic;
#define MACRO_MAGIC		0x803423e3
	VTAILQ_ENTRY(macro)	list;
	char			*name;
	char			*val;
	macro_f			*func;
};

static VTAILQ_HEAD(,macro) macro_list = VTAILQ_HEAD_INITIALIZER(macro_list);

static const struct cmds global_cmds[] = {
#define CMD_GLOBAL(n) { #n, cmd_##n },
#include "cmds.h"
	{ NULL, NULL }
};

static const struct cmds top_cmds[] = {
#define CMD_TOP(n) { #n, cmd_##n },
#include "cmds.h"
	{ NULL, NULL }
};

/**********************************************************************/

static struct macro *
macro_def_int(const char *name, macro_f *func, const char *fmt, va_list ap)
{
	struct macro *m;
	char buf[2048];

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
	if (func != NULL) {
		AZ(fmt);
		m->func = func;
	} else {
		AN(fmt);
		vbprintf(buf, fmt, ap);
		REPLACE(m->val, buf);
		AN(m->val);
	}
	return (m);
}


/**********************************************************************
 * This is for defining macros before we fork the child process which
 * runs the test-case.
 */

void
extmacro_def(const char *name, macro_f *func, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)macro_def_int(name, func, fmt, ap);
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
	VTAILQ_FOREACH(m, &macro_list, list) {
		if (m->val != NULL)
			vtc_log(vltop, 4,
			    "extmacro def %s=%s", m->name, m->val);
		else
			vtc_log(vltop, 4, "extmacro def %s(...)", m->name);
	}

	PTOK(pthread_mutex_init(&macro_mtx, NULL));
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

	PTOK(pthread_mutex_lock(&macro_mtx));
	va_start(ap, fmt);
	m = macro_def_int(name, NULL, fmt, ap);
	va_end(ap);
	vtc_log(vl, 4, "macro def %s=%s", name, m->val);
	PTOK(pthread_mutex_unlock(&macro_mtx));
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

	PTOK(pthread_mutex_lock(&macro_mtx));
	VTAILQ_FOREACH(m, &macro_list, list)
		if (!strcmp(name, m->name))
			break;
	if (m != NULL) {
		if (!vtc_stop)
			vtc_log(vl, 4, "macro undef %s", name);
		CHECK_OBJ(m, MACRO_MAGIC);
		VTAILQ_REMOVE(&macro_list, m, list);
		free(m->name);
		free(m->val);
		FREE_OBJ(m);
	}
	PTOK(pthread_mutex_unlock(&macro_mtx));
}

unsigned
macro_isdef(const char *instance, const char *name)
{
	char buf1[256];
	struct macro *m;

	if (instance != NULL) {
		bprintf(buf1, "%s_%s", instance, name);
		name = buf1;
	}

	PTOK(pthread_mutex_lock(&macro_mtx));
	VTAILQ_FOREACH(m, &macro_list, list)
		if (!strcmp(name, m->name))
			break;
	PTOK(pthread_mutex_unlock(&macro_mtx));

	return (m != NULL);
}

void
macro_cat(struct vtclog *vl, struct vsb *vsb, const char *b, const char *e)
{
	struct macro *m;
	char **argv, *retval = NULL;
	const char *err = NULL;
	int argc;

	AN(b);
	if (e == NULL)
		e = strchr(b, '\0');
	AN(e);

	argv = VAV_ParseTxt(b, e, &argc, ARGV_COMMA);
	AN(argv);

	if (*argv != NULL)
		vtc_fatal(vl, "Macro ${%.*s} parsing failed: %s",
		    (int)(e - b), b, *argv);

	assert(argc >= 2);

	PTOK(pthread_mutex_lock(&macro_mtx));
	VTAILQ_FOREACH(m, &macro_list, list) {
		CHECK_OBJ_NOTNULL(m, MACRO_MAGIC);
		if (!strcmp(argv[1], m->name))
			break;
	}
	if (m != NULL) {
		if (m->func != NULL) {
			AZ(m->val);
			retval = m->func(argc, argv, &err);
			if (err == NULL)
				AN(retval);
		} else {
			AN(m->val);
			if (argc == 2)
				REPLACE(retval, m->val);
			else
				err = "macro does not take arguments";
		}
	}
	PTOK(pthread_mutex_unlock(&macro_mtx));

	VAV_Free(argv);

	if (err != NULL)
		vtc_fatal(vl, "Macro ${%.*s} failed: %s",
		    (int)(e - b), b, err);

	if (retval == NULL) {
		if (!ign_unknown_macro)
			vtc_fatal(vl, "Macro ${%.*s} not found",
			    (int)(e - b), b);
		VSB_printf(vsb, "${%.*s}", (int)(e - b), b);
		return;
	}

	VSB_cat(vsb, retval);
	free(retval);
}

struct vsb *
macro_expandf(struct vtclog *vl, const char *fmt, ...)
{
	va_list ap;
	struct vsb *vsb1, *vsb2;

	vsb1 = VSB_new_auto();
	AN(vsb1);
	va_start(ap, fmt);
	VSB_vprintf(vsb1, fmt, ap);
	va_end(ap);
	AZ(VSB_finish(vsb1));
	vsb2 = macro_expand(vl, VSB_data(vsb1));
	VSB_destroy(&vsb1);
	return (vsb2);
}

struct vsb *
macro_expand(struct vtclog *vl, const char *text)
{
	struct vsb *vsb;
	const char *p, *q;

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
		macro_cat(vl, vsb, p, q);
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
parse_string(struct vtclog *vl, void *priv, const char *spec)
{
	char *token_s[MAX_TOKENS], *token_e[MAX_TOKENS];
	struct vsb *token_exp;
	char *e, *p, *q, *f, *buf;
	int nest_brace;
	int tn;
	unsigned n, m;
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
		memset(token_s, 0, sizeof token_s);
		memset(token_e, 0, sizeof token_e);
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
			AN(token_e[tn]);	/*lint !e771 */
			*token_e[tn] = '\0';	/*lint !e771 */
			if (NULL != strstr(token_s[tn], "${")) {
				token_exp = macro_expand(vl, token_s[tn]);
				if (vtc_error)
					return;
				token_s[tn] = VSB_data(token_exp);
				token_e[tn] = strchr(token_s[tn], '\0');
			}
		}


/* SECTION: loop loop
 *
 * loop NUMBER STRING
 *         Process STRING as a specification, NUMBER times.
 *
 * This works inside all specification strings
 */

		if (!strcmp(token_s[0], "loop")) {
			n = strtoul(token_s[1], NULL, 0);
			for (m = 0; m < n; m++) {
				vtc_log(vl, 4, "Loop #%u", m);
				parse_string(vl, priv, token_s[2]);
			}
			continue;
		}

		AN(vl->cmds);
		for (cp = vl->cmds; cp->name != NULL; cp++)
			if (!strcmp(token_s[0], cp->name))
				break;

		if (cp->name == NULL) {
			for (cp = global_cmds; cp->name != NULL; cp++)
				if (!strcmp(token_s[0], cp->name))
					break;
		}

		if (cp->name == NULL)
			vtc_fatal(vl, "Unknown command: \"%s\"", token_s[0]);

		assert(cp->cmd != NULL);
		cp->cmd(token_s, priv, vl);
	}
}

/**********************************************************************
 * Reset commands (between tests)
 */

static void
reset_cmds(const struct cmds *cmd)
{

	for (; cmd->name != NULL; cmd++)
		cmd->cmd(NULL, NULL, NULL);
}

/**********************************************************************
 * Execute a file
 */

int
fail_out(void)
{
	unsigned old_err;
	static int once = 0;

	if (once++) {
		vtc_log(vltop, 1, "failure during reset");
		return (vtc_error);
	}
	old_err = vtc_error;
	if (!vtc_stop)
		vtc_stop = 1;
	vtc_log(vltop, 1, "RESETTING after %s", tfn);
	reset_cmds(global_cmds);
	reset_cmds(top_cmds);
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
	struct vsb *vsb;
	const char *p;

	AN(tmpdir);

	(void)signal(SIGPIPE, SIG_IGN);

	PTOK(pthread_mutex_init(&vtc_vrnd_mtx, NULL));
	VRND_Lock = vtc_vrnd_lock;
	VRND_Unlock = vtc_vrnd_unlock;
	VRND_SeedAll();

	tfn = fn;
	vtc_loginit(logbuf, loglen);
	vltop = vtc_logopen("top");
	AN(vltop);
	vtc_log_set_cmd(vltop, top_cmds);

	vtc_log(vltop, 1, "TEST %s starting", fn);

	init_macro();
	init_server();
	init_syslog();
	init_tunnel();

	vsb = VSB_new_auto();
	AN(vsb);
	if (*fn != '/')
		macro_cat(vltop, vsb, "pwd", NULL);
	p = strrchr(fn, '/');
	if (p != NULL) {
		VSB_putc(vsb, '/');
		VSB_bcat(vsb, fn, p - fn);
	}
	if (VSB_len(vsb) == 0)
		VSB_putc(vsb, '/');
	AZ(VSB_finish(vsb));
	macro_def(vltop, NULL, "testdir", "%s", VSB_data(vsb));
	VSB_destroy(&vsb);

	/* Move into our tmpdir */
	AZ(chdir(tmpdir));
	macro_def(vltop, NULL, "tmpdir", "%s", tmpdir);
	p = strrchr(tmpdir, '/');
	AN(p);
	p++;
	AN(*p);
	macro_def(vltop, NULL, "vtcid", "%s", p);

	/* Drop file to tell what was going on here */
	f = fopen("INFO", "w");
	AN(f);
	fprintf(f, "Test case: %s\n", fn);
	AZ(fclose(f));

	vtc_stop = 0;

	vtc_thread = pthread_self();
	parse_string(vltop, NULL, script);
	return (fail_out());
}
