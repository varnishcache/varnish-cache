/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "mgt/mgt_param.h"
#include "vav.h"
#include "vcli_serve.h"

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

static const char TYPE_TIMEOUT_TEXT[] =
	"\n\n"
	"NB: This parameter can be disabled with the value \"never\".";

static const char OBJ_STICKY_TEXT[] =
	"\n\n"
	"NB: This parameter is evaluated only when objects are created. "
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
	"this parameter, or if the default value is even sensible. "
	"Caution is advised, and feedback is most welcome.";

static const char WIZARD_TEXT[] =
	"\n\n"
	"NB: Do not change this parameter, unless a developer tell "
	"you to do so.";

static const char PROTECTED_TEXT[] =
	"\n\n"
	"NB: This parameter is protected and cannot be changed.";

static const char ONLY_ROOT_TEXT[] =
	"\n\n"
	"NB: This parameter only works if varnishd is run as root.";

static const char NOT_IMPLEMENTED_TEXT[] =
	"\n\n"
	"NB: This parameter depends on a feature which is not available"
	" on this platform.";

static const char PLATFORM_DEPENDENT_TEXT[] =
	"\n\n"
	"NB: This parameter depends on a feature which is not available"
	" on all platforms.";

static const char BUILD_OPTIONS_TEXT[] =
	"\n\n"
	"NB: The actual default value for this parameter depends on the"
	" Varnish build environment and options.";

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

static void v_matchproto_(cli_func_t)
mcf_param_show(struct cli *cli, const char * const *av, void *priv)
{
	struct plist *pl;
	const struct parspec *pp, *pa;
	int n, lfmt = 0, chg = 0;
	struct vsb *vsb;
	const char *show = NULL;

	(void)priv;

	for (n = 2; av[n] != NULL; n++) {
		if (strcmp(av[n], "-l") == 0) {
			lfmt = 1;
			continue;
		}
		if (strcmp(av[n], "changed") == 0) {
			chg = 1;
			continue;
		}
		if (show != NULL) {
			VCLI_SetResult(cli, CLIS_TOOMANY);
			VCLI_Out(cli, "Too many parameters");
			return;
		}
		show = av[n];
		lfmt = 1;
	}

	vsb = VSB_new_auto();
	AN(vsb);

	n = 0;
	VTAILQ_FOREACH(pl, &phead, list) {
		pp = pl->spec;
		if (lfmt && show != NULL && strcmp(pp->name, show))
			continue;
		if (pp->func == tweak_alias &&
		    (show == NULL || strcmp(pp->name, show)))
			continue;
		n++;

		VSB_clear(vsb);
		if (pp->func(vsb, pp, NULL))
			VCLI_SetResult(cli, CLIS_PARAM);
		AZ(VSB_finish(vsb));
		if (chg && pp->def != NULL && !strcmp(pp->def, VSB_data(vsb)))
			continue;

		if (pp->flags & NOT_IMPLEMENTED) {
			if (lfmt) {
				VCLI_Out(cli, "%s\n", pp->name);
				VCLI_Out(cli, "%-*sNot available", margin1, " ");
			} else {
				VCLI_Out(cli, "%-*s-", margin2, pp->name);
			}
		} else {
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
		}
		VCLI_Out(cli, "\n");

		if (lfmt && pp->func == tweak_alias) {
			pa = TRUST_ME(pp->priv);
			VCLI_Out(cli, "%-*sAlias of: %s\n",
			    margin1, " ", pa->name);
		}
		if (lfmt && pp->flags & NOT_IMPLEMENTED) {
			VCLI_Out(cli, "\n");
			mcf_wrap(cli, NOT_IMPLEMENTED_TEXT);
			VCLI_Out(cli, "\n\n");
		} else if (lfmt) {
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
			if (pp->func == tweak_timeout)
				mcf_wrap(cli, TYPE_TIMEOUT_TEXT);
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
			if (pp->flags & BUILD_OPTIONS)
				mcf_wrap(cli, BUILD_OPTIONS_TEXT);
			VCLI_Out(cli, "\n\n");
		}
	}
	if (show != NULL && n == 0) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Unknown parameter \"%s\".", show);
	}
	VSB_destroy(&vsb);
}

static inline void
mcf_json_key_valstr(struct cli *cli, const char *key, const char *val)
{
	VCLI_Out(cli, "\"%s\": ", key);
	VCLI_JSON_str(cli, val);
	VCLI_Out(cli, ",\n");
}

static void v_matchproto_(cli_func_t)
mcf_param_show_json(struct cli *cli, const char * const *av, void *priv)
{
	int n, comma = 0, chg = 0;
	struct plist *pl;
	const struct parspec *pp, *pa;
	struct vsb *vsb, *def;
	const char *show = NULL, *sep;

	(void)priv;

	for (int i = 2; av[i] != NULL; i++) {
		if (strcmp(av[i], "-l") == 0) {
			VCLI_SetResult(cli, CLIS_PARAM);
			VCLI_Out(cli, "-l not permitted with param.show -j");
			return;
		}
		if (strcmp(av[i], "changed") == 0) {
			chg = 1;
			continue;
		}
		if (strcmp(av[i], "-j") == 0)
			continue;
		if (show != NULL) {
			VCLI_SetResult(cli, CLIS_TOOMANY);
			VCLI_Out(cli, "Too many parameters");
			return;
		}
		show = av[i];
	}

	vsb = VSB_new_auto();
	AN(vsb);
	def = VSB_new_auto();
	AN(def);

	n = 0;
	VCLI_JSON_begin(cli, 2, av);
	VCLI_Out(cli, ",\n");
	VTAILQ_FOREACH(pl, &phead, list) {
		pp = pl->spec;
		if (show != NULL && strcmp(pp->name, show) != 0)
			continue;
		if (pp->func == tweak_alias &&
		    (show == NULL || strcmp(pp->name, show)))
			continue;
		n++;

		VSB_clear(vsb);
		if (pp->func(vsb, pp, JSON_FMT))
			VCLI_SetResult(cli, CLIS_PARAM);
		AZ(VSB_finish(vsb));
		VSB_clear(def);
		if (pp->func(def, pp, NULL))
			VCLI_SetResult(cli, CLIS_PARAM);
		AZ(VSB_finish(def));
		if (chg && pp->def != NULL && !strcmp(pp->def, VSB_data(def)))
			continue;

		VCLI_Out(cli, "%s", comma ? ",\n" : "");
		comma++;
		VCLI_Out(cli, "{\n");
		VSB_indent(cli->sb, 2);
		mcf_json_key_valstr(cli, "name", pp->name);
		if (pp->func == tweak_alias) {
			pa = TRUST_ME(pp->priv);
			mcf_json_key_valstr(cli, "alias", pa->name);
		}
		if (pp->flags & NOT_IMPLEMENTED) {
			VCLI_Out(cli, "\"implemented\": false\n");
			VSB_indent(cli->sb, -2);
			VCLI_Out(cli, "}");
			continue;
		}
		VCLI_Out(cli, "\"implemented\": true,\n");
		VCLI_Out(cli, "\"value\": %s,\n", VSB_data(vsb));
		if (pp->units != NULL && *pp->units != '\0')
			mcf_json_key_valstr(cli, "units", pp->units);

		if (pp->def != NULL)
			mcf_json_key_valstr(cli, "default", pp->def);
		if (pp->min != NULL)
			mcf_json_key_valstr(cli, "minimum", pp->min);
		if (pp->max != NULL)
			mcf_json_key_valstr(cli, "maximum", pp->max);
		mcf_json_key_valstr(cli, "description", pp->descr);

		VCLI_Out(cli, "\"flags\": [");
		VSB_indent(cli->sb, 2);
		sep = "";

#define flag_out(flag, string) do {					\
			if (pp->flags & flag) {				\
				VCLI_Out(cli, "%s\n", sep);		\
				VCLI_Out(cli, "\"%s\"", #string);	\
				sep = ",";				\
			}						\
		} while(0)

		flag_out(OBJ_STICKY, obj_sticky);
		flag_out(DELAYED_EFFECT, delayed_effect);
		flag_out(EXPERIMENTAL, experimental);
		flag_out(MUST_RELOAD, must_reload);
		flag_out(MUST_RESTART, must_restart);
		flag_out(WIZARD, wizard);
		flag_out(PROTECTED, protected);
		flag_out(ONLY_ROOT, only_root);
		flag_out(BUILD_OPTIONS, build_options);

#undef flag_out

		if (pp->flags)
			VCLI_Out(cli, "\n");
		VSB_indent(cli->sb, -2);
		VCLI_Out(cli, "]\n");
		VSB_indent(cli->sb, -2);
		VCLI_Out(cli, "}");
	}
	VCLI_JSON_end(cli);
	if (show != NULL && n == 0) {
		VSB_clear(cli->sb);
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Unknown parameter \"%s\".", show);
	}
	VSB_destroy(&vsb);
	VSB_destroy(&def);
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
	if (pp->flags & NOT_IMPLEMENTED) {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli,
		    "parameter \"%s\" is not available on this platform.",
		    param
		);
		return;
	}
	if (pp->flags & PROTECTED) {
		VCLI_SetResult(cli, CLIS_AUTH);
		VCLI_Out(cli, "parameter \"%s\" is protected.", param);
		return;
	}
	if (!val)
		val = pp->def;
	if (pp->func(cli->sb, pp, val))
		VCLI_SetResult(cli, CLIS_PARAM);

	if (cli->result == CLIS_OK && heritage.param != NULL)
		*heritage.param = mgt_param;

	if (cli->result != CLIS_OK) {
		VCLI_Out(cli, "\n(attempting to set param '%s' to '%s')",
		    pp->name, val);
	} else if (MCH_Running() && pp->flags & MUST_RESTART) {
		VCLI_Out(cli,
		    "\nChange will take effect when child is restarted");
	} else if (pp->flags & MUST_RELOAD) {
		VCLI_Out(cli,
		    "\nChange will take effect when VCL script is reloaded");
	}
}


/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
mcf_param_set(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	MCF_ParamSet(cli, av[2], av[3]);
}

static void v_matchproto_(cli_func_t)
mcf_param_set_json(struct cli *cli, const char * const *av, void *priv)
{
	const char *const avs[] = { av[0], av[1], av[2], av[3], NULL };

	MCF_ParamSet(cli, av[3], av[4]);
	if (cli->result == CLIS_OK)
		mcf_param_show_json(cli, avs, priv);
}

/*--------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
mcf_param_reset(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	MCF_ParamSet(cli, av[2], NULL);
}

/*--------------------------------------------------------------------
 * Initialize parameters and sort by name.
 */

static struct parspec mgt_parspec[] = {
#define PARAM_ALL
#define PARAM_PRE {
#define PARAM(typ, fld, nm, ...) #nm, __VA_ARGS__
#define PARAM_POST },
#include "tbl/params.h"
	{ NULL, NULL, NULL }
};

static void
mcf_init_params(void)
{
	struct parspec *pp;
	const char *s;

	for (pp = mgt_parspec; pp->name != NULL; pp++) {
		AN(pp->func);
		s = strchr(pp->descr, '\0');
		if (isspace(s[-1])) {
			fprintf(stderr,
			    "Param->descr has trailing space: %s\n", pp->name);
			exit(4);
		}
		mcf_addpar(pp);
		margin2 = vmax_t(int, margin2, strlen(pp->name) + 1);
	}
}

/*--------------------------------------------------------------------
 * Wash a min/max/default value
 */

static void
mcf_dyn_vsb(enum mcf_which_e which, struct parspec *pp, struct vsb *vsb)
{

	switch (which) {
	case MCF_DEFAULT:
		REPLACE(pp->dyn_def, VSB_data(vsb));
		pp->def = pp->dyn_def;
		break;
	case MCF_MINIMUM:
		REPLACE(pp->dyn_min, VSB_data(vsb));
		pp->min = pp->dyn_min;
		break;
	case MCF_MAXIMUM:
		REPLACE(pp->dyn_max, VSB_data(vsb));
		pp->max = pp->dyn_max;
		break;
	default:
		WRONG("bad 'which'");
	}
}

static void
mcf_wash_param(struct cli *cli, struct parspec *pp, enum mcf_which_e which,
    const char *name, struct vsb *vsb)
{
	const char *val;
	int err;

	switch (which) {
	case MCF_DEFAULT: val = pp->def; break;
	case MCF_MINIMUM: val = pp->min; break;
	case MCF_MAXIMUM: val = pp->max; break;
	default:
		WRONG("bad 'which'");
	}
	AN(val);

	if (pp->func == tweak_alias) {
		assert(which == MCF_DEFAULT);
		pp->priv = mcf_findpar(pp->def);
		pp->def = NULL;
		return;
	}

	VSB_clear(vsb);
	VSB_printf(vsb, "FAILED to set %s for param %s: %s\n",
	    name, pp->name, val);
	err = pp->func(vsb, pp, val);
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
	if (strcmp(val, VSB_data(vsb)))
		mcf_dyn_vsb(which, pp, vsb);
}

/*--------------------------------------------------------------------*/

static struct cli_proto cli_params[] = {
	{ CLICMD_PARAM_SHOW,	"", mcf_param_show, mcf_param_show_json },
	{ CLICMD_PARAM_SET,	"", mcf_param_set, mcf_param_set_json },
	{ CLICMD_PARAM_RESET,	"", mcf_param_reset, mcf_param_set_json },
	{ NULL }
};

/*--------------------------------------------------------------------
 * Configure the parameters
 */

void
MCF_InitParams(struct cli *cli)
{
	struct plist *pl;
	struct parspec *pp;
	struct vsb *vsb;
	ssize_t def, low;

	mcf_init_params();
	MCF_TcpParams();

	def = 80 * 1024;

	if (sizeof(void *) < 8) {		/*lint !e506 !e774  */
		/*
		 * Adjust default parameters for 32 bit systems to conserve
		 * VM space.
		 *
		 * Reflect changes in doc/sphinx/reference/varnishd.rst !
		 */
		MCF_ParamConf(MCF_DEFAULT, "workspace_client", "24k");
		MCF_ParamConf(MCF_DEFAULT, "workspace_backend", "20k");
		MCF_ParamConf(MCF_DEFAULT, "http_resp_size", "8k");
		MCF_ParamConf(MCF_DEFAULT, "http_req_size", "12k");
		MCF_ParamConf(MCF_DEFAULT, "gzip_buffer", "4k");
		MCF_ParamConf(MCF_DEFAULT, "vsl_buffer", "4k");
		MCF_ParamConf(MCF_MAXIMUM, "vsl_space", "1G");
		def = 64 * 1024;
	}

	low = sysconf(_SC_THREAD_STACK_MIN);
	MCF_ParamConf(MCF_MINIMUM, "thread_pool_stack", "%jdb", (intmax_t)low);

#if defined(ENABLE_SANITIZER) || defined(ENABLE_COVERAGE)
	def = 192 * 1024;
#endif

	if (def < low)
		def = low;
	MCF_ParamConf(MCF_DEFAULT, "thread_pool_stack", "%jdb", (intmax_t)def);

#if !defined(MAX_THREAD_POOLS)
#  define MAX_THREAD_POOLS 32
#endif

	MCF_ParamConf(MCF_MAXIMUM, "thread_pools", "%d", MAX_THREAD_POOLS);

#if !defined(HAVE_ACCEPT_FILTERS) || defined(__linux)
	MCF_ParamConf(MCF_DEFAULT, "accept_filter", "off");
#endif

	VCLS_AddFunc(mgt_cls, MCF_AUTH, cli_params);

	vsb = VSB_new_auto();
	AN(vsb);

	VTAILQ_FOREACH(pl, &phead, list) {
		pp = pl->spec;

		if (pp->flags & NOT_IMPLEMENTED)
			continue;
		if (pp->min != NULL)
			mcf_wash_param(cli, pp, MCF_MINIMUM, "minimum", vsb);
		if (pp->max != NULL)
			mcf_wash_param(cli, pp, MCF_MAXIMUM, "maximum", vsb);
		AN(pp->def);
		mcf_wash_param(cli, pp, MCF_DEFAULT, "default", vsb);
	}
	VSB_destroy(&vsb);

	AN(mgt_cc_cmd);
	REPLACE(mgt_cc_cmd_def, mgt_cc_cmd);
}

/*--------------------------------------------------------------------*/

void
MCF_ParamConf(enum mcf_which_e which, const char * const param,
    const char *fmt, ...)
{
	struct parspec *pp;
	struct vsb *vsb;
	va_list ap;

	pp = mcf_findpar(param);
	AN(pp);
	vsb = VSB_new_auto();
	AN(vsb);
	va_start(ap, fmt);
	VSB_vprintf(vsb, fmt, ap);
	va_end(ap);
	AZ(VSB_finish(vsb));
	mcf_dyn_vsb(which, pp, vsb);
	VSB_destroy(&vsb);
}

/*--------------------------------------------------------------------*/

void
MCF_DumpRstParam(void)
{
	struct plist *pl;
	const struct parspec *pp;
	const char *p, *q, *t1, *t2;
	unsigned flags;
	size_t z;

	printf("\n.. The following is the autogenerated "
	    "output from varnishd -x parameter\n\n");
	VTAILQ_FOREACH(pl, &phead, list) {
		pp = pl->spec;
		if (!strcmp("deprecated_dummy", pp->name))
		    continue;
		printf(".. _ref_param_%s:\n\n", pp->name);
		printf("%s\n", pp->name);
		for (z = 0; z < strlen(pp->name); z++)
			printf("~");
		printf("\n");

		if (pp->flags && pp->flags & PLATFORM_DEPENDENT)
			printf("\n%s\n\n", PLATFORM_DEPENDENT_TEXT);

		if (pp->flags && pp->flags & BUILD_OPTIONS)
			printf("\n%s\n\n", BUILD_OPTIONS_TEXT);

		if (pp->units != NULL && *pp->units != '\0')
			printf("\t* Units: %s\n", pp->units);
#define MCF_DYN_REASON(lbl, nm)					\
		if (pp->dyn_ ## nm ## _reason != NULL)		\
			printf("\t* " #lbl ": %s\n",		\
			    pp->dyn_ ## nm ## _reason);		\
		else if (pp->nm != NULL)			\
			printf("\t* " #lbl ": %s\n", pp->nm);
		MCF_DYN_REASON(Default, def);
		MCF_DYN_REASON(Minimum, min);
		MCF_DYN_REASON(Maximum, max);
#undef MCF_DYN_REASON
		flags = pp->flags & ~DOCS_FLAGS;
		if (pp->func == tweak_timeout)
			flags |= TYPE_TIMEOUT;
		if (flags) {
			printf("\t* Flags: ");
			q = "";

			if (flags & TYPE_TIMEOUT) {
				printf("%stimeout", q);
				q = ", ";
			}
			if (flags & DELAYED_EFFECT) {
				printf("%sdelayed", q);
				q = ", ";
			}
			if (flags & MUST_RESTART) {
				printf("%smust_restart", q);
				q = ", ";
			}
			if (flags & MUST_RELOAD) {
				printf("%smust_reload", q);
				q = ", ";
			}
			if (flags & EXPERIMENTAL) {
				printf("%sexperimental", q);
				q = ", ";
			}
			if (flags & WIZARD) {
				printf("%swizard", q);
				q = ", ";
			}
			if (flags & ONLY_ROOT) {
				printf("%sonly_root", q);
				q = ", ";
			}
			if (flags & OBJ_STICKY) {
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
				AN(t2);
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
