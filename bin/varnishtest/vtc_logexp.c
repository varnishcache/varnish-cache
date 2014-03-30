/*-
 * Copyright (c) 2008-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

/*
 * Synopsis:
 *   -v <varnish-instance>
 *   -d <0|1> (head/tail mode)
 *   -g <grouping-mode>
 *   -q <query>
 *
 * logexpect lN -v <id> [-g <grouping>] [-d 0|1] [-q query] {
 *    expect <skip> <vxid> <tag> <regex>
 * }
 *
 * skip: [uint|*]		Max number of record to skip
 * vxid: [uint|*|=]		vxid to match
 * tag:  [tagname|*|=]		Tag to match against
 * regex:			regular expression to match against (optional)
 * *:				Match anything
 * =:				Match value of last successfully matched record
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vtim.h"
#include "vqueue.h"
#include "vas.h"
#include "vre.h"

#include "vtc.h"

#define LE_ANY  (-1)
#define LE_LAST (-2)

struct logexp_test {
	unsigned			magic;
#define LOGEXP_TEST_MAGIC		0x6F62B350
	VTAILQ_ENTRY(logexp_test)	list;

	struct vsb			*str;
	int				vxid;
	int				tag;
	vre_t				*vre;
	int				skip_max;
};

struct logexp {
	unsigned			magic;
#define LOGEXP_MAGIC			0xE81D9F1B
	VTAILQ_ENTRY(logexp)		list;

	char				*name;
	struct vtclog			*vl;
	char				run;
	VTAILQ_HEAD(,logexp_test)	tests;

	struct logexp_test		*test;
	int				skip_cnt;
	int				vxid_last;
	int				tag_last;

	int				d_arg;
	int				g_arg;
	char				*query;

	struct VSM_data			*vsm;
	struct vsb			*n_arg;
	struct VSL_data			*vsl;
	struct VSLQ			*vslq;
	pthread_t			tp;
};

static VTAILQ_HEAD(, logexp)		logexps =
	VTAILQ_HEAD_INITIALIZER(logexps);

static void
logexp_delete_tests(struct logexp *le)
{
	struct logexp_test *test;

	CHECK_OBJ_NOTNULL(le, LOGEXP_MAGIC);
	while ((test = VTAILQ_FIRST(&le->tests))) {
		CHECK_OBJ_NOTNULL(test, LOGEXP_TEST_MAGIC);
		VTAILQ_REMOVE(&le->tests, test, list);
		VSB_delete(test->str);
		if (test->vre)
			VRE_free(&test->vre);
		FREE_OBJ(test);
	}
}

static void
logexp_delete(struct logexp *le)
{
	CHECK_OBJ_NOTNULL(le, LOGEXP_MAGIC);
	AZ(le->run);
	AZ(le->vsl);
	AZ(le->vslq);
	logexp_delete_tests(le);
	free(le->name);
	free(le->query);
	VSM_Delete(le->vsm);
	if (le->n_arg)
		VSB_delete(le->n_arg);
	FREE_OBJ(le);
}

static struct logexp *
logexp_new(const char *name)
{
	struct logexp *le;

	AN(name);
	ALLOC_OBJ(le, LOGEXP_MAGIC);
	AN(le);
	REPLACE(le->name, name);
	le->vl = vtc_logopen(name);
	VTAILQ_INIT(&le->tests);

	le->d_arg = 0;
	le->g_arg = VSL_g_vxid;
	le->vsm = VSM_New();
	AN(le->vsm);

	VTAILQ_INSERT_TAIL(&logexps, le, list);
	return (le);
}

static void
logexp_next(struct logexp *le)
{
	CHECK_OBJ_NOTNULL(le, LOGEXP_MAGIC);

	if (le->test) {
		CHECK_OBJ_NOTNULL(le->test, LOGEXP_TEST_MAGIC);
		le->test = VTAILQ_NEXT(le->test, list);
	} else
		le->test = VTAILQ_FIRST(&le->tests);

	CHECK_OBJ_ORNULL(le->test, LOGEXP_TEST_MAGIC);
	if (le->test)
		vtc_log(le->vl, 3, "tst| %s", VSB_data(le->test->str));
}

static int __match_proto__(VSLQ_dispatch_f)
logexp_dispatch(struct VSL_data *vsl, struct VSL_transaction * const pt[],
    void *priv)
{
	struct logexp *le;
	struct VSL_transaction *t;
	int i;
	int ok, skip;
	int vxid, tag, type, len, lvl;
	const char *legend, *data;

	(void)vsl;
	CAST_OBJ_NOTNULL(le, priv, LOGEXP_MAGIC);

	for (i = 0; (t = pt[i]); i++) {
		while (1 == VSL_Next(t->c)) {
			CHECK_OBJ_NOTNULL(le->test, LOGEXP_TEST_MAGIC);
			AN(t->c->rec.ptr);
			vxid = VSL_ID(t->c->rec.ptr);
			tag = VSL_TAG(t->c->rec.ptr);
			data = VSL_CDATA(t->c->rec.ptr);
			len = VSL_LEN(t->c->rec.ptr) - 1;

			if (tag == SLT__Batch)
				continue;

			ok = 1;
			if (le->test->vxid == LE_LAST) {
				if (le->vxid_last != vxid)
					ok = 0;
			} else if (le->test->vxid >= 0) {
				if (le->test->vxid != vxid)
					ok = 0;
			}
			if (le->test->tag == LE_LAST) {
				if (le->tag_last != tag)
					ok = 0;
			} else if (le->test->tag >= 0) {
				if (le->test->tag != tag)
					ok = 0;
			}
			if (le->test->vre &&
			    le->test->tag >= 0 &&
			    le->test->tag == tag &&
			    VRE_ERROR_NOMATCH == VRE_exec(le->test->vre, data,
				len, 0, 0, NULL, 0, NULL))
				ok = 0;

			skip = 0;
			if (!ok && (le->test->skip_max == LE_ANY ||
				le->test->skip_max > le->skip_cnt))
				skip = 1;

			if (ok) {
				lvl = 4;
				legend = "ok";
			} else if (skip) {
				lvl = 4;
				legend = "skp";
			} else {
				lvl = 0;
				legend = "err";
			}
			type = VSL_CLIENT(t->c->rec.ptr) ? 'c' :
			    VSL_BACKEND(t->c->rec.ptr) ? 'b' : '-';

			vtc_log(le->vl, lvl, "%3s| %10u %-15s %c %.*s",
			    legend, vxid, VSL_tags[tag], type, len, data);

			if (ok) {
				le->vxid_last = vxid;
				le->tag_last = tag;
				le->skip_cnt = 0;
				logexp_next(le);
				if (le->test == NULL)
					/* End of test script */
					return (1);
			}
			if (skip)
				le->skip_cnt++;
		}
	}

	return (0);
}

static void *
logexp_thread(void *priv)
{
	struct logexp *le;
	int i;

	CAST_OBJ_NOTNULL(le, priv, LOGEXP_MAGIC);
	AN(le->run);
	AN(le->vsm);
	AN(le->vslq);

	i = 0;
	AZ(le->test);
	vtc_log(le->vl, 4, "beg|");
	if (le->query != NULL)
		vtc_log(le->vl, 4, "qry| %s", le->query);
	logexp_next(le);
	while (le->test) {
		i = VSLQ_Dispatch(le->vslq, logexp_dispatch, le);
		if (i < 0)
			vtc_log(le->vl, 0, "dispatch: %d", i);
		if (i == 0 && le->test)
			VTIM_sleep(0.01);
	}
	vtc_log(le->vl, 4, "end|");

	return (NULL);
}

static void
logexp_close(struct logexp *le)
{

	CHECK_OBJ_NOTNULL(le, LOGEXP_MAGIC);
	AN(le->vsm);
	if (le->vslq)
		VSLQ_Delete(&le->vslq);
	AZ(le->vslq);
	if (le->vsl) {
		VSL_Delete(le->vsl);
		le->vsl = NULL;
	}
	VSM_Close(le->vsm);
}

static void
logexp_start(struct logexp *le)
{
	struct VSL_cursor *c;

	CHECK_OBJ_NOTNULL(le, LOGEXP_MAGIC);
	AZ(le->vsl);
	AZ(le->vslq);

	if (le->n_arg == NULL) {
		vtc_log(le->vl, 0, "-v argument not given");
		return;
	}
	if (VSM_n_Arg(le->vsm, VSB_data(le->n_arg)) <= 0) {
		vtc_log(le->vl, 0, "-v argument error: %s",
		    VSM_Error(le->vsm));
		return;
	}
	if (VSM_Open(le->vsm)) {
		vtc_log(le->vl, 0, "VSM_Open: %s", VSM_Error(le->vsm));
		return;
	}
	le->vsl = VSL_New();
	AN(le->vsl);
	c = VSL_CursorVSM(le->vsl, le->vsm, !le->d_arg);
	if (c == NULL) {
		vtc_log(le->vl, 0, "VSL_CursorVSM: %s", VSL_Error(le->vsl));
		logexp_close(le);
		return;
	}
	le->vslq = VSLQ_New(le->vsl, &c, le->g_arg, le->query);
	if (le->vslq == NULL) {
		VSL_DeleteCursor(c);
		vtc_log(le->vl, 0, "VSLQ_New: %s", VSL_Error(le->vsl));
		AZ(le->vslq);
		logexp_close(le);
		return;
	}
	AZ(c);

	le->test = NULL;
	le->skip_cnt = 0;
	le->vxid_last = le->tag_last = -1;
	le->run = 1;
	AZ(pthread_create(&le->tp, NULL, logexp_thread, le));
}

static void
logexp_wait(struct logexp *le)
{
	void *res;

	CHECK_OBJ_NOTNULL(le, LOGEXP_MAGIC);
	vtc_log(le->vl, 2, "Waiting for logexp");
	AZ(pthread_join(le->tp, &res));
	logexp_close(le);
	if (res != NULL && !vtc_stop)
		vtc_log(le->vl, 0, "logexp returned \"%p\"", (char *)res);
	le->run = 0;
}

static void
cmd_logexp_expect(CMD_ARGS)
{
	struct logexp *le;
	int skip_max;
	int vxid;
	int tag;
	vre_t *vre;
	const char *err;
	int pos;
	struct logexp_test *test;
	char *end;

	(void)cmd;
	CAST_OBJ_NOTNULL(le, priv, LOGEXP_MAGIC);
	if (av[1] == NULL || av[2] == NULL || av[3] == NULL) {
		vtc_log(vl, 0, "Syntax error");
		return;
	}

	if (!strcmp(av[1], "*"))
		skip_max = LE_ANY;
	else {
		skip_max = (int)strtol(av[1], &end, 10);
		if (*end != '\0' || skip_max < 0) {
			vtc_log(vl, 0, "Not a positive integer: '%s'", av[1]);
			return;
		}
	}
	if (!strcmp(av[2], "*"))
		vxid = LE_ANY;
	else if (!strcmp(av[2], "="))
		vxid = LE_LAST;
	else {
		vxid = (int)strtol(av[2], &end, 10);
		if (*end != '\0' || vxid < 0) {
			vtc_log(vl, 0, "Not a positive integer: '%s'", av[2]);
			return;
		}
	}
	if (!strcmp(av[3], "*"))
		tag = LE_ANY;
	else if (!strcmp(av[3], "="))
		tag = LE_LAST;
	else {
		tag = VSL_Name2Tag(av[3], strlen(av[3]));
		if (tag < 0) {
			vtc_log(vl, 0, "Unknown tag name: '%s'", av[3]);
			return;
		}
	}
	vre = NULL;
	if (av[4]) {
		vre = VRE_compile(av[4], 0, &err, &pos);
		if (vre == NULL) {
			vtc_log(vl, 0, "Regex error (%s): '%s' pos %d",
			    err, av[4], pos);
			return;
		}
	}

	ALLOC_OBJ(test, LOGEXP_TEST_MAGIC);
	AN(test);
	test->str = VSB_new_auto();
	AN(test->str);
	AZ(VSB_printf(test->str, "%s %s %s %s ", av[0], av[1], av[2], av[3]));
	if (av[4])
		VSB_quote(test->str, av[4], -1, 0);
	AZ(VSB_finish(test->str));
	test->skip_max = skip_max;
	test->vxid = vxid;
	test->tag = tag;
	test->vre = vre;
	VTAILQ_INSERT_TAIL(&le->tests, test, list);
	vtc_log(vl, 4, "%s", VSB_data(test->str));
}

static const struct cmds logexp_cmds[] = {
	{ "expect",		cmd_logexp_expect },
	{ NULL,			NULL },
};

static void
logexp_spec(struct logexp *le, const char *spec)
{
	char *s;

	CHECK_OBJ_NOTNULL(le, LOGEXP_MAGIC);

	logexp_delete_tests(le);

	s = strdup(spec);
	AN(s);
	parse_string(s, logexp_cmds, le, le->vl);
	free(s);
}

void
cmd_logexp(CMD_ARGS)
{
	struct logexp *le, *le2;
	const char tmpdir[] = "${tmpdir}";
	struct vsb *vsb;

	(void)priv;
	(void)cmd;
	(void)vl;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(le, &logexps, list, le2) {
			CHECK_OBJ_NOTNULL(le, LOGEXP_MAGIC);
			VTAILQ_REMOVE(&logexps, le, list);
			if (le->run) {
				(void)pthread_cancel(le->tp);
				logexp_wait(le);
			}
			logexp_delete(le);
		}
		return;
	}

	assert(!strcmp(av[0], "logexpect"));
	av++;

	VTAILQ_FOREACH(le, &logexps, list) {
		if (!strcmp(le->name, av[0]))
			break;
	}
	if (le == NULL)
		le = logexp_new(av[0]);
	av++;

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;
		if (!strcmp(*av, "-wait")) {
			if (!le->run) {
				vtc_log(le->vl, 0, "logexp not -started '%s'",
					*av);
				return;
			}
			logexp_wait(le);
			continue;
		}

		/*
		 * We do an implict -wait if people muck about with a
		 * running logexp.
		 */
		if (le->run)
			logexp_wait(le);
		AZ(le->run);

		if (!strcmp(*av, "-v")) {
			if (av[1] == NULL) {
				vtc_log(le->vl, 0, "Missing -v argument");
				return;
			}
			if (le->n_arg != NULL) {
				VSB_delete(le->n_arg);
				le->n_arg = NULL;
			}
			vsb = VSB_new_auto();
			AN(vsb);
			AZ(VSB_printf(vsb, "%s/%s", tmpdir, av[1]));
			AZ(VSB_finish(vsb));
			le->n_arg = macro_expand(le->vl, VSB_data(vsb));
			VSB_delete(vsb);
			if (le->n_arg == NULL)
				return;
			av++;
			continue;
		}
		if (!strcmp(*av, "-d")) {
			if (av[1] == NULL) {
				vtc_log(le->vl, 0, "Missing -d argument");
				return;
			}
			le->d_arg = atoi(av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-g")) {
			if (av[1] == NULL) {
				vtc_log(le->vl, 0, "Missing -g argument");
				return;
			}
			le->g_arg = VSLQ_Name2Grouping(av[1], strlen(av[1]));
			if (le->g_arg < 0) {
				vtc_log(le->vl, 0, "Unknown grouping '%s'",
				    av[1]);
				return;
			}
			av++;
			continue;
		}
		if (!strcmp(*av, "-q")) {
			if (av[1] == NULL) {
				vtc_log(le->vl, 0, "Missing -q argument");
				return;
			}
			REPLACE(le->query, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-start")) {
			logexp_start(le);
			continue;
		}
		if (!strcmp(*av, "-run")) {
			logexp_start(le);
			logexp_wait(le);
			continue;
		}
		if (**av == '-') {
			vtc_log(le->vl, 0, "Unknown logexp argument: %s", *av);
			return;
		}
		logexp_spec(le, *av);
	}
}
