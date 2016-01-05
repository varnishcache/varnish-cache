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
 */

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "mgt/mgt_param.h"
#include "vav.h"
#include "vcli.h"
#include "vcli_common.h"
#include "vcli_priv.h"

#include "mgt_cli.h"

struct plist {
	unsigned			magic;
#define PLIST_MAGIC			0xbfc3ea16
	VTAILQ_ENTRY(plist)		list;
	struct parspec			*spec;
};

static VTAILQ_HEAD(, plist)		phead = VTAILQ_HEAD_INITIALIZER(phead);

struct params mgt_param;
static const int margin1 = 8;
static int margin2 = 0;
static const int wrap_at = 72;
static const int tab0 = 3;

/*--------------------------------------------------------------------*/

static const char OBJ_STICKY_TEXT[] =
	"\n\n"
	"NB: This parameter is evaluated only when objects are created."
	"To change it for all objects, restart or ban everything.";

static const char DELAYED_EFFECT_TEXT[] =
	"\n\n"
	"NB: This parameter may take quite some time to take (full) effect.";

static const char MUST_RESTART_TEXT[] =
	"\n\n"
	"NB: This parameter will not take any effect until the "
	"child process has been restarted.";

static const char MUST_RELOAD_TEXT[] =
	"\n\n"
	"NB: This parameter will not take any effect until the "
	"VCL programs have been reloaded.";

static const char EXPERIMENTAL_TEXT[] =
	"\n\n"
	"NB: We do not know yet if it is a good idea to change "
	"this parameter, or if the default value is even sensible.  "
	"Caution is advised, and feedback is most welcome.";

static const char WIZARD_TEXT[] =
	"\n\n"
	"NB: Do not change this parameter, unless a developer tell "
	"you to do so.";

static const char PROTECTED_TEXT[] =
	"\n\n"
	"NB: This parameter is protected and can not be changed.";

static const char ONLY_ROOT_TEXT[] =
	"\n\n"
	"NB: This parameter only works if varnishd is run as root.";

/*--------------------------------------------------------------------*/

static struct parspec *
mcf_findpar(const char *name)
{
	struct plist *pl;

	AN(name);
	VTAILQ_FOREACH(pl, &phead, list)
		if (!strcmp(pl->spec->name, name))
			return (pl->spec);
	return (NULL);
}

static void
mcf_addpar(struct parspec *ps)
{
	struct plist *pl, *pl2;
	int i;

	ALLOC_OBJ(pl, PLIST_MAGIC);
	AN(pl);
	pl->spec = ps;
	VTAILQ_FOREACH(pl2, &phead, list) {
		i = strcmp(pl2->spec->name, pl->spec->name);
		if (i == 0) {
			fprintf(stderr, "Duplicate param: %s\n", ps->name);
			exit(4);
		} else if (i > 0) {
			VTAILQ_INSERT_BEFORE(pl2, pl, list);
			return;
		}
	}
	VTAILQ_INSERT_TAIL(&phead, pl, list);
}

/*--------------------------------------------------------------------
 * Wrap the text nicely.
 * Lines are allowed to contain two TABS and we render that as a table
 * taking the width of the first column into account.
 */

static void
mcf_wrap_line(struct cli *cli, const char *b, const char *e, int tabs, int m0)
{
	int n, hadtabs = 0;
	const char *w;

	n = m0;
	VCLI_Out(cli, "%*s", n, "");

	while (b < e) {
		if (!isspace(*b)) {
			VCLI_Out(cli, "%c", *b);
			b++;
			n++;
		} else if (*b == '\t') {
			assert(tabs);
			assert(hadtabs < 2);
			do {
				VCLI_Out(cli, " ");
				n++;
			} while ((n % tabs) != (m0 + tab0) % tabs);
			b++;
			hadtabs++;
		} else {
			assert (*b == ' ');
			for (w = b + 1; w < e; w++)
				if (isspace(*w))
					break;
			if (n + (w - b) < wrap_at) {
				VCLI_Out(cli, "%.*s", (int)(w - b), b);
				n += (w - b);
				b = w;
			} else {
				assert(hadtabs == 0 || hadtabs == 2);
				VCLI_Out(cli, "\n");
				mcf_wrap_line(cli, b + 1, e, 0,
				    hadtabs ? m0 + tab0 + tabs : m0);
				return;
			}
		}
	}
	assert(b == e);
}

static void
mcf_wrap(struct cli *cli, const char *text)
{
	const char *p, *q, *r;
	int tw = 0;

	if (strchr(text, '\t') != NULL) {
		for (p = text; *p != '\0'; ) {
			q = strstr(p, "\n\t");
			if (q == NULL)
				break;
			q += 2;
			r = strchr(q, '\t');
			if (r == NULL) {
				fprintf(stderr,
				    "LINE with just one TAB: <%s>\n", text);
				exit(4);
			}
			if (r - q > tw)
				tw = r - q;
			p = q;
		}
		tw += 2;
		if (tw < 20)
			tw = 20;
	}

	for (p = text; *p != '\0'; ) {
		if (*p == '\n') {
			VCLI_Out(cli, "\n");
			p++;
			continue;
		}
		q = strchr(p, '\n');
		if (q == NULL)
			q = strchr(p, '\0');
		mcf_wrap_line(cli, p, q, tw, margin1);
		p = q;
	}
}

/*--------------------------------------------------------------------*/

void
mcf_param_show(struct cli *cli, const char * const *av, void *priv)
{
	int n;
	struct plist *pl;
	const struct parspec *pp;
	int lfmt = 0, chg = 0;
	struct vsb *vsb;

	vsb = VSB_new_auto();
	(void)priv;

	if (av[2] != NULL && !strcmp(av[2], "changed"))
		chg = 1;
	else if (av[2] != NULL)
		lfmt = 1;

	n = 0;
	VTAILQ_FOREACH(pl, &phead, list) {
		pp = pl->spec;
		if (lfmt && strcmp(pp->name, av[2]) && strcmp("-l", av[2]))
			continue;
		n++;

		VSB_clear(vsb);
		if (pp->func(vsb, pp, NULL))
			VCLI_SetResult(cli, CLIS_PARAM);
		AZ(VSB_finish(vsb));
		if (chg && pp->def != NULL && !strcmp(pp->def, VSB_data(vsb)))
			continue;

		if (lfmt) {
			VCLI_Out(cli, "%s\n", pp->name);
			VCLI_Out(cli, "%-*sValue is: ", margin1, " ");
		} else {
			VCLI_Out(cli, "%-*s", margin2, pp->name);
		}
		VCLI_Out(cli, "%s", VSB_data(vsb));
		if (pp->units != NULL && *pp->units != '\0')
			VCLI_Out(cli, " [%s]", pp->units);
		if (pp->def != NULL && !strcmp(pp->def, VSB_data(vsb)))
			VCLI_Out(cli, " (default)");
		VCLI_Out(cli, "\n");
		if (lfmt) {
			if (pp->def != NULL && strcmp(pp->def, VSB_data(vsb)))
				VCLI_Out(cli, "%-*sDefault is: %s\n",
				    margin1, "", pp->def);
			if (pp->min != NULL)
				VCLI_Out(cli, "%-*sMinimum is: %s\n",
				    margin1, "", pp->min);
			if (pp->max != NULL)
				VCLI_Out(cli, "%-*sMaximum is: %s\n",
				    margin1, "", pp->max);
			VCLI_Out(cli, "\n");
			mcf_wrap(cli, pp->descr);
			if (pp->flags & OBJ_STICKY)
				mcf_wrap(cli, OBJ_STICKY_TEXT);
			if (pp->flags & DELAYED_EFFECT)
				mcf_wrap(cli, DELAYED_EFFECT_TEXT);
			if (pp->flags & EXPERIMENTAL)
				mcf_wrap(cli, EXPERIMENTAL_TEXT);
			if (pp->flags & MUST_RELOAD)
				mcf_wrap(cli, MUST_RELOAD_TEXT);
			if (pp->flags & MUST_RESTART)
				mcf_wrap(cli, MUST_RESTART_TEXT);
			if (pp->flags & WIZARD)
				mcf_wrap(cli, WIZARD_TEXT);
			if (pp->flags & PROTECTED)
				mcf_wrap(cli, PROTECTED_TEXT);
			if (pp->flags & ONLY_ROOT)
				mcf_wrap(cli, ONLY_ROOT_TEXT);
			VCLI_Out(cli, "\n\n");
		}
	}
	if (av[2] != NULL && lfmt && strcmp(av[2], "-l") && n == 0) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Unknown parameter \"%s\".", av[2]);
	}
	VSB_delete(vsb);
}

/*--------------------------------------------------------------------
 * Mark parameters as protected
 */

void
MCF_ParamProtect(struct cli *cli, const char *args)
{
	char **av;
	struct parspec *pp;
	int i;

	av = VAV_Parse(args, NULL, ARGV_COMMA);
	if (av[0] != NULL) {
		VCLI_Out(cli, "Parse error: %s", av[0]);
		VCLI_SetResult(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	for (i = 1; av[i] != NULL; i++) {
		pp = mcf_findpar(av[i]);
		if (pp == NULL) {
			VCLI_Out(cli, "Unknown parameter %s", av[i]);
			VCLI_SetResult(cli, CLIS_PARAM);
			VAV_Free(av);
			return;
		}
		pp->flags |= PROTECTED;
	}
	VAV_Free(av);
}

/*--------------------------------------------------------------------*/

void
MCF_ParamSet(struct cli *cli, const char *param, const char *val)
{
	const struct parspec *pp;

	pp = mcf_findpar(param);
	if (pp == NULL) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Unknown parameter \"%s\".", param);
		return;
	}
	if (pp->flags & PROTECTED) {
		VCLI_SetResult(cli, CLIS_AUTH);
		VCLI_Out(cli, "parameter \"%s\" is protected.", param);
		return;
	}
	if (pp->func(cli->sb, pp, val))
		VCLI_SetResult(cli, CLIS_PARAM);

	if (cli->result == CLIS_OK && heritage.param != NULL)
		*heritage.param = mgt_param;

	if (cli->result != CLIS_OK) {
		VCLI_Out(cli, "\n(attempting to set param '%s' to '%s')",
		    pp->name, val);
	} else if (child_pid >= 0 && pp->flags & MUST_RESTART) {
		VCLI_Out(cli,
		    "\nChange will take effect when child is restarted");
	} else if (pp->flags & MUST_RELOAD) {
		VCLI_Out(cli,
		    "\nChange will take effect when VCL script is reloaded");
	}
}


/*--------------------------------------------------------------------*/

void
mcf_param_set(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	MCF_ParamSet(cli, av[2], av[3]);
}

/*--------------------------------------------------------------------
 * Add a group of parameters to the global set and sort by name.
 */

void
MCF_AddParams(struct parspec *ps)
{
	struct parspec *pp;
	const char *s;

	for (pp = ps; pp->name != NULL; pp++) {
		AN(pp->func);
		s = strchr(pp->descr, '\0');
		if (isspace(s[-1])) {
			fprintf(stderr,
			    "Param->descr has trailing space: %s\n", pp->name);
			exit(4);
		}
		mcf_addpar(pp);
		if (strlen(pp->name) + 1 > margin2)
			margin2 = strlen(pp->name) + 1;
	}
}

/*--------------------------------------------------------------------
 * Wash a min/max/default value
 */

static void
mcf_wash_param(struct cli *cli, const struct parspec *pp, const char **val,
    const char *name, struct vsb *vsb)
{
	int err;

	AN(*val);
	VSB_clear(vsb);
	VSB_printf(vsb, "FAILED to set %s for param %s: %s\n",
	    name, pp->name, *val);
	err = pp->func(vsb, pp, *val);
	AZ(VSB_finish(vsb));
	if (err) {
		VCLI_Out(cli, "%s\n", VSB_data(vsb));
		VCLI_SetResult(cli, CLIS_CANT);
		return;
	}
	VSB_clear(vsb);
	err = pp->func(vsb, pp, NULL);
	AZ(err);
	AZ(VSB_finish(vsb));
	if (strcmp(*val, VSB_data(vsb))) {
		*val = strdup(VSB_data(vsb));
		AN(*val);
	}
}

/*--------------------------------------------------------------------
 * Wash the min/max/default values, and leave the default set.
 */

void
MCF_InitParams(struct cli *cli)
{
	struct plist *pl;
	struct parspec *pp;
	struct vsb *vsb;

	vsb = VSB_new_auto();
	AN(vsb);
	VTAILQ_FOREACH(pl, &phead, list) {
		pp = pl->spec;

		if (pp->min != NULL)
			mcf_wash_param(cli, pp, &pp->min, "minimum", vsb);
		if (pp->max != NULL)
			mcf_wash_param(cli, pp, &pp->max, "maximum", vsb);
		AN(pp->def);
		mcf_wash_param(cli, pp, &pp->def, "default", vsb);
	}
	VSB_delete(vsb);
}

/*--------------------------------------------------------------------*/

void
MCF_CollectParams(void)
{

	MCF_AddParams(mgt_parspec);
	MCF_AddParams(WRK_parspec);
	MCF_AddParams(VSL_parspec);
}

/*--------------------------------------------------------------------*/

void
MCF_SetDefault(const char *param, const char *new_def)
{
	struct parspec *pp;

	pp = mcf_findpar(param);
	AN(pp);
	pp->def = new_def;
	AN(pp->def);
}

void
MCF_SetMinimum(const char *param, const char *new_min)
{
	struct parspec *pp;

	AN(new_min);
	pp = mcf_findpar(param);
	AN(pp);
	pp->min = new_min;
	AN(pp->min);
}

void
MCF_SetMaximum(const char *param, const char *new_max)
{
	struct parspec *pp;

	AN(new_max);
	pp = mcf_findpar(param);
	AN(pp);
	pp->max = new_max;
	AN(pp->max);
}

/*--------------------------------------------------------------------*/

void
MCF_DumpRstParam(void)
{
	struct plist *pl;
	const struct parspec *pp;
	const char *p, *q, *t1, *t2;
	int j;

	printf("\n.. The following is the autogenerated "
	    "output from varnishd -x dumprstparam\n\n");
	VTAILQ_FOREACH(pl, &phead, list) {
		pp = pl->spec;
		printf(".. _ref_param_%s:\n\n", pp->name);
		printf("%s\n", pp->name);
		for (j = 0; j < strlen(pp->name); j++)
			printf("~");
		printf("\n");
		if (pp->units != NULL && *pp->units != '\0')
			printf("\t* Units: %s\n", pp->units);
		printf("\t* Default: %s\n", pp->def);
		if (pp->min != NULL)
			printf("\t* Minimum: %s\n", pp->min);
		if (pp->max != NULL)
			printf("\t* Maximum: %s\n", pp->max);
		/*
		 * XXX: we should mark the params with one/two flags
		 * XXX: that say if ->min/->max are valid, so we
		 * XXX: can emit those also in help texts.
		 */
		if (pp->flags) {
			printf("\t* Flags: ");
			q = "";
			if (pp->flags & DELAYED_EFFECT) {
				printf("%sdelayed", q);
				q = ", ";
			}
			if (pp->flags & MUST_RESTART) {
				printf("%smust_restart", q);
				q = ", ";
			}
			if (pp->flags & MUST_RELOAD) {
				printf("%smust_reload", q);
				q = ", ";
			}
			if (pp->flags & EXPERIMENTAL) {
				printf("%sexperimental", q);
				q = ", ";
			}
			if (pp->flags & WIZARD) {
				printf("%swizard", q);
				q = ", ";
			}
			if (pp->flags & ONLY_ROOT) {
				printf("%sonly_root", q);
				q = ", ";
			}
			if (pp->flags & OBJ_STICKY) {
				printf("%sobj_sticky", q);
				q = ", ";
			}
			printf("\n");
		}
		printf("\n");
		p = pp->descr;
		while (*p != '\0') {
			q = strchr(p, '\n');
			if (q == NULL)
				q = strchr(p, '\0');
			t1 = strchr(p, '\t');
			if (t1 != NULL && t1 < q) {
				t2 = strchr(t1 + 1, '\t');
				printf("\n\t*");
				(void)fwrite(t1 + 1, (t2 - 1) - t1, 1, stdout);
				printf("*\n\t\t");
				p = t2 + 1;
			}
			(void)fwrite(p, q - p, 1, stdout);
			p = q;
			if (*p == '\n') {
				printf("\n");
				p++;
			}
			continue;
		}
		printf("\n\n");
	}
}
