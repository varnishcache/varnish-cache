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
#include <grp.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"
#include "common/params.h"

#include "mgt/mgt_param.h"
#include "waiter/waiter.h"
#include "vav.h"
#include "vcli.h"
#include "vcli_common.h"
#include "vcli_priv.h"

#include "mgt_cli.h"

struct params mgt_param;
static int nparspec;
static struct parspec const ** parspecs;
static const int margin1 = 8;
static int margin2 = 0;
static const int wrap_at = 72;
static const int tab0 = 3;

/*--------------------------------------------------------------------*/

static const char OBJ_STICKY_TEXT[] =
	"\n\nNB: This parameter is evaluated only when objects are created."
	"To change it for all objects, restart or ban everything.";

static const char DELAYED_EFFECT_TEXT[] =
	"\n\nNB: This parameter may take quite some time to take (full) effect.";

static const char MUST_RESTART_TEXT[] =
	"\n\nNB: This parameter will not take any effect until the "
	"child process has been restarted.";

static const char MUST_RELOAD_TEXT[] =
	"\n\nNB: This parameter will not take any effect until the "
	"VCL programs have been reloaded.";

static const char EXPERIMENTAL_TEXT[] =
	"\n\nNB: We do not know yet if it is a good idea to change "
	"this parameter, or if the default value is even sensible.  "
	"Caution is advised, and feedback is most welcome.";

static const char WIZARD_TEXT[] =
	"\n\nNB: Do not change this parameter, unless a developer tell "
	"you to do so.";

static const char PROTECTED_TEXT[] =
	"\n\nNB: This parameter is protected and can not be changed.";


/*--------------------------------------------------------------------*/

static const struct parspec *
mcf_findpar(const char *name, int *idx)
{
	int i;

	AN(name);
	for (i = 0; i < nparspec; i++)
		if (!strcmp(parspecs[i]->name, name)) {
			if (idx != NULL)
				*idx = i;
			return (parspecs[i]);
		}
	if (idx != NULL)
		*idx = -1;
	return (NULL);
}

/*--------------------------------------------------------------------
 * Wrap the text nicely.
 * Lines are allowed to contain to TABS and we render that as a table
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
				exit(2);
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
	int i;
	const struct parspec *pp;
	int lfmt;

	(void)priv;
	if (av[2] == NULL)
		lfmt = 0;
	else
		lfmt = 1;
	for (i = 0; i < nparspec; i++) {
		pp = parspecs[i];
		if (av[2] != NULL && !lfmt && strcmp(pp->name, av[2]))
			continue;
		if (lfmt) {
			VCLI_Out(cli, "%s\n", pp->name);
			VCLI_Out(cli, "%-*sValue is: ", margin1, " ");
		} else {
			VCLI_Out(cli, "%-*s", margin2, pp->name);
		}
		pp->func(cli, pp, NULL);
		if (pp->units != NULL && *pp->units != '\0')
			VCLI_Out(cli, " [%s]\n", pp->units);
		else
			VCLI_Out(cli, "\n");
		if (av[2] != NULL) {
			VCLI_Out(cli, "%-*sDefault is: %s\n\n",
			    margin1, "", pp->def);
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
			if (!lfmt)
				return;
			else
				VCLI_Out(cli, "\n\n");
		}
	}
	if (av[2] != NULL && !lfmt) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Unknown parameter \"%s\".", av[2]);
	}
}

/*--------------------------------------------------------------------
 * Mark parameters as protected
 */

void
MCF_ParamProtect(struct cli *cli, const char *args)
{
	char **av;
	struct parspec *pp;
	int i, j;

	av = VAV_Parse(args, NULL, ARGV_COMMA);
	if (av[0] != NULL) {
		VCLI_Out(cli, "Parse error: %s", av[0]);
		VCLI_SetResult(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	for (i = 1; av[i] != NULL; i++) {
		if (mcf_findpar(av[i], &j) == NULL) {
			VCLI_Out(cli, "Unknown parameter %s", av[i]);
			VCLI_SetResult(cli, CLIS_PARAM);
			VAV_Free(av);
			return;
		}
		pp = calloc(sizeof *pp, 1L);
		AN(pp);
		memcpy(pp, parspecs[j], sizeof *pp);
		pp->flags |= PROTECTED;
		parspecs[j] = pp;
	}
	VAV_Free(av);
}

/*--------------------------------------------------------------------*/

void
MCF_ParamSet(struct cli *cli, const char *param, const char *val)
{
	const struct parspec *pp;

	pp = mcf_findpar(param, NULL);
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
	pp->func(cli, pp, val);

	if (cli->result == CLIS_OK && heritage.param != NULL)
		*heritage.param = mgt_param;

	if (cli->result != CLIS_OK) {
		VCLI_Out(cli, "(attempting to set param %s to %s)\n",
		    pp->name, val);
	} else if (child_pid >= 0 && pp->flags & MUST_RESTART) {
		VCLI_Out(cli, "Change will take effect"
		    " when child is restarted");
	} else if (pp->flags & MUST_RELOAD) {
		VCLI_Out(cli, "Change will take effect"
		    " when VCL script is reloaded");
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

static int
mcf_parspec_cmp(const void *a, const void *b)
{
	struct parspec * const * pa = a;
	struct parspec * const * pb = b;
	return (strcmp((*pa)->name, (*pb)->name));
}

static void
MCF_AddParams(const struct parspec *ps)
{
	const struct parspec *pp;
	const char *s;
	int n;

	n = 0;
	for (pp = ps; pp->name != NULL; pp++) {
		AN(pp->func);
		s = strchr(pp->descr, '\0');
		if (isspace(s[-1])) {
			fprintf(stderr,
			    "Param->descr has trailing space: %s\n", pp->name);
			exit(2);
		}
		if (mcf_findpar(pp->name, NULL) != NULL) {
			fprintf(stderr, "Duplicate param: %s\n", pp->name);
			exit(2);
		}
		if (strlen(pp->name) + 1 > margin2)
			margin2 = strlen(pp->name) + 1;
		n++;
	}
	parspecs = realloc(parspecs, (1L + nparspec + n) * sizeof *parspecs);
	XXXAN(parspecs);
	for (pp = ps; pp->name != NULL; pp++)
		parspecs[nparspec++] = pp;
	parspecs[nparspec] = NULL;
	qsort (parspecs, nparspec, sizeof parspecs[0], mcf_parspec_cmp);
}

/*--------------------------------------------------------------------
 * Set defaults for all parameters
 */

static void
MCF_SetDefaults(struct cli *cli)
{
	const struct parspec *pp;
	int i;

	for (i = 0; i < nparspec; i++) {
		pp = parspecs[i];
		if (cli != NULL)
			VCLI_Out(cli,
			    "Set Default for %s = %s\n", pp->name, pp->def);
		pp->func(cli, pp, pp->def);
		if (cli != NULL && cli->result != CLIS_OK)
			return;
	}
}

/*--------------------------------------------------------------------*/

void
MCF_CollectParams(void)
{

	MCF_AddParams(mgt_parspec);
	MCF_AddParams(WRK_parspec);
	MCF_AddParams(VSL_parspec);

	/* If we have nobody/nogroup, use them as defaults */
	if (getpwnam("nobody") != NULL)
		MCF_SetDefault("user", "nobody");
	if (getgrnam("nogroup") != NULL)
		MCF_SetDefault("group", "nogroup");
}

/*--------------------------------------------------------------------*/

void
MCF_InitParams(struct cli *cli)
{

	/* XXX: We do this twice, to get past any interdependencies */
	MCF_SetDefaults(NULL);
	MCF_SetDefaults(cli);
}

/*--------------------------------------------------------------------*/

void
MCF_SetDefault(const char *param, const char *def)
{
	struct parspec *pn;
	int i;

	for (i = 0; i < nparspec; i++)
		if (!strcmp(parspecs[i]->name, param))
			break;
	assert(i < nparspec);
	pn = malloc(sizeof *pn);
	AN(pn);
	*pn  = *(parspecs[i]);
	pn->def = def;
	parspecs[i] = pn;
}

/*--------------------------------------------------------------------*/

void
MCF_DumpRstParam(void)
{
	const struct parspec *pp;
	const char *p, *q, *t1, *t2;
	int i, j;

	printf("\n.. The following is the autogenerated "
	    "output from varnishd -x dumprstparam\n\n");
	for (i = 0; i < nparspec; i++) {
		pp = parspecs[i];
		printf(".. _ref_param_%s:\n\n", pp->name);
		printf("%s\n", pp->name);
		for (j = 0; j < strlen(pp->name); j++)
			printf("~");
		printf("\n");
		if (pp->units != NULL && *pp->units != '\0')
			printf("\t* Units: %s\n", pp->units);
		printf("\t* Default: %s\n", pp->def);
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
