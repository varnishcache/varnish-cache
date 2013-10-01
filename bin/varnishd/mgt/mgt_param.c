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
#include "vnum.h"
#include "vss.h"

#include "mgt_cli.h"

struct params mgt_param;
static int nparspec;
static struct parspec const ** parspecs;
static int margin;

/*--------------------------------------------------------------------*/

static const struct parspec *
mcf_findpar(const char *name)
{
	int i;

	for (i = 0; i < nparspec; i++)
		if (!strcmp(parspecs[i]->name, name))
			return (parspecs[i]);
	return (NULL);
}

/*--------------------------------------------------------------------*/

static void
tweak_generic_timeout(struct cli *cli, volatile unsigned *dst, const char *arg)
{
	unsigned u;

	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u == 0) {
			VCLI_Out(cli, "Timeout must be greater than zero\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		*dst = u;
	} else
		VCLI_Out(cli, "%u", *dst);
}

/*--------------------------------------------------------------------*/

void
tweak_timeout(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	tweak_generic_timeout(cli, dest, arg);
}

/*--------------------------------------------------------------------*/

static int
tweak_generic_timeout_double(struct cli *cli, volatile double *dest,
    const char *arg, double min, double max)
{
	double u;
	char *p;

	if (arg != NULL) {
		p = NULL;
		u = strtod(arg, &p);
		if (*arg == '\0' || *p != '\0') {
			VCLI_Out(cli, "Not a number(%s)\n", arg);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		if (u < min) {
			VCLI_Out(cli,
			    "Timeout must be greater or equal to %.g\n", min);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		if (u > max) {
			VCLI_Out(cli,
			    "Timeout must be less than or equal to %.g\n", max);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		*dest = u;
	} else
		VCLI_Out(cli, "%.6f", *dest);
	return (0);
}

void
tweak_timeout_double(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	volatile double *dest;

	dest = par->priv;
	(void)tweak_generic_timeout_double(cli, dest, arg, par->min, par->max);
}

/*--------------------------------------------------------------------*/

void
tweak_generic_double(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	volatile double *dest;
	char *p;
	double u;

	dest = par->priv;
	if (arg != NULL) {
		p = NULL;
		u = strtod(arg, &p);
		if (*p != '\0') {
			VCLI_Out(cli,
			    "Not a number (%s)\n", arg);
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if (u < par->min) {
			VCLI_Out(cli,
			    "Must be greater or equal to %.g\n",
				 par->min);
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if (u > par->max) {
			VCLI_Out(cli,
			    "Must be less than or equal to %.g\n",
				 par->max);
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		*dest = u;
	} else
		VCLI_Out(cli, "%f", *dest);
}

/*--------------------------------------------------------------------*/

void
tweak_bool(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;
	int mode = 0;

	if (!strcmp(par->def, "off") || !strcmp(par->def, "on"))
		mode = 1;

	dest = par->priv;
	if (arg != NULL) {
		if (!strcasecmp(arg, "off"))
			*dest = 0;
		else if (!strcasecmp(arg, "disable"))
			*dest = 0;
		else if (!strcasecmp(arg, "no"))
			*dest = 0;
		else if (!strcasecmp(arg, "false"))
			*dest = 0;
		else if (!strcasecmp(arg, "on"))
			*dest = 1;
		else if (!strcasecmp(arg, "enable"))
			*dest = 1;
		else if (!strcasecmp(arg, "yes"))
			*dest = 1;
		else if (!strcasecmp(arg, "true"))
			*dest = 1;
		else {
			VCLI_Out(cli,
			    mode ?
				"use \"on\" or \"off\"\n" :
				"use \"true\" or \"false\"\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
	} else if (mode) {
		VCLI_Out(cli, *dest ? "on" : "off");
	} else {
		VCLI_Out(cli, *dest ? "true" : "false");
	}
}

/*--------------------------------------------------------------------*/

int
tweak_generic_uint(struct cli *cli, volatile unsigned *dest, const char *arg,
    unsigned min, unsigned max)
{
	unsigned u;
	char *p;

	if (arg != NULL) {
		p = NULL;
		if (!strcasecmp(arg, "unlimited"))
			u = UINT_MAX;
		else {
			u = strtoul(arg, &p, 0);
			if (*arg == '\0' || *p != '\0') {
				VCLI_Out(cli, "Not a number (%s)\n", arg);
				VCLI_SetResult(cli, CLIS_PARAM);
				return (-1);
			}
		}
		if (u < min) {
			VCLI_Out(cli, "Must be at least %u\n", min);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		if (u > max) {
			VCLI_Out(cli, "Must be no more than %u\n", max);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		*dest = u;
	} else if (*dest == UINT_MAX) {
		VCLI_Out(cli, "unlimited");
	} else {
		VCLI_Out(cli, "%u", *dest);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

void
tweak_uint(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	(void)tweak_generic_uint(cli, dest, arg,
	    (uint)par->min, (uint)par->max);
}

/*--------------------------------------------------------------------*/

static void
fmt_bytes(struct cli *cli, uintmax_t t)
{
	const char *p;

	if (t & 0xff) {
		VCLI_Out(cli, "%jub", t);
		return;
	}
	for (p = "kMGTPEZY"; *p; p++) {
		if (t & 0x300) {
			VCLI_Out(cli, "%.2f%c", t / 1024.0, *p);
			return;
		}
		t /= 1024;
		if (t & 0x0ff) {
			VCLI_Out(cli, "%ju%c", t, *p);
			return;
		}
	}
	VCLI_Out(cli, "(bogus number)");
}

static void
tweak_generic_bytes(struct cli *cli, volatile ssize_t *dest, const char *arg,
    double min, double max)
{
	uintmax_t r;
	const char *p;

	if (arg != NULL) {
		p = VNUM_2bytes(arg, &r, 0);
		if (p != NULL) {
			VCLI_Out(cli, "Could not convert to bytes.\n");
			VCLI_Out(cli, "%s\n", p);
			VCLI_Out(cli,
			    "  Try something like '80k' or '120M'\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if ((uintmax_t)((ssize_t)r) != r) {
			fmt_bytes(cli, r);
			VCLI_Out(cli, " is too large for this architecture.\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if (max != 0. && r > max) {
			VCLI_Out(cli, "Must be no more than ");
			fmt_bytes(cli, (uintmax_t)max);
			VCLI_Out(cli, "\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if (r < min) {
			VCLI_Out(cli, "Must be at least ");
			fmt_bytes(cli, (uintmax_t)min);
			VCLI_Out(cli, "\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		*dest = r;
	} else {
		fmt_bytes(cli, *dest);
	}
}

/*--------------------------------------------------------------------*/

void
tweak_bytes(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile ssize_t *dest;

	assert(par->min >= 0);
	dest = par->priv;
	tweak_generic_bytes(cli, dest, arg, par->min, par->max);
}


/*--------------------------------------------------------------------*/

void
tweak_bytes_u(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *d1;
	volatile ssize_t dest;

	assert(par->max <= UINT_MAX);
	assert(par->min >= 0);
	d1 = par->priv;
	dest = *d1;
	tweak_generic_bytes(cli, &dest, arg, par->min, par->max);
	*d1 = dest;
}

/*--------------------------------------------------------------------
 * XXX: slightly magic.  We want to initialize to "nobody" (XXX: shouldn't
 * XXX: that be something autocrap found for us ?) but we don't want to
 * XXX: fail initialization if that user doesn't exists, even though we
 * XXX: do want to fail it, in subsequent sets.
 * XXX: The magic init string is a hack for this.
 */

void
tweak_user(struct cli *cli, const struct parspec *par, const char *arg)
{
	struct passwd *pw;

	(void)par;
	if (arg != NULL) {
		if (*arg != '\0') {
			pw = getpwnam(arg);
			if (pw == NULL) {
				VCLI_Out(cli, "Unknown user");
				VCLI_SetResult(cli, CLIS_PARAM);
				return;
			}
			REPLACE(mgt_param.user, pw->pw_name);
			mgt_param.uid = pw->pw_uid;
		} else {
			mgt_param.uid = getuid();
		}
	} else if (mgt_param.user) {
		VCLI_Out(cli, "%s (%d)", mgt_param.user, (int)mgt_param.uid);
	} else {
		VCLI_Out(cli, "UID %d", (int)mgt_param.uid);
	}
}

/*--------------------------------------------------------------------
 * XXX: see comment for tweak_user, same thing here.
 */

void
tweak_group(struct cli *cli, const struct parspec *par, const char *arg)
{
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if (*arg != '\0') {
			gr = getgrnam(arg);
			if (gr == NULL) {
				VCLI_Out(cli, "Unknown group");
				VCLI_SetResult(cli, CLIS_PARAM);
				return;
			}
			REPLACE(mgt_param.group, gr->gr_name);
			mgt_param.gid = gr->gr_gid;
		} else {
			mgt_param.gid = getgid();
		}
	} else if (mgt_param.group) {
		VCLI_Out(cli, "%s (%d)", mgt_param.group, (int)mgt_param.gid);
	} else {
		VCLI_Out(cli, "GID %d", (int)mgt_param.gid);
	}
}

/*--------------------------------------------------------------------*/

static void
clean_listen_sock_head(struct listen_sock_head *lsh)
{
	struct listen_sock *ls, *ls2;

	VTAILQ_FOREACH_SAFE(ls, lsh, list, ls2) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
		VTAILQ_REMOVE(lsh, ls, list);
		free(ls->name);
		free(ls->addr);
		FREE_OBJ(ls);
	}
}

void
tweak_listen_address(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	char **av;
	int i;
	struct listen_sock		*ls;
	struct listen_sock_head		lsh;

	(void)par;
	if (arg == NULL) {
		VCLI_Quote(cli, mgt_param.listen_address);
		return;
	}

	av = VAV_Parse(arg, NULL, ARGV_COMMA);
	if (av == NULL) {
		VCLI_Out(cli, "Parse error: out of memory");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	if (av[0] != NULL) {
		VCLI_Out(cli, "Parse error: %s", av[0]);
		VCLI_SetResult(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	if (av[1] == NULL) {
		VCLI_Out(cli, "Empty listen address");
		VCLI_SetResult(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	VTAILQ_INIT(&lsh);
	for (i = 1; av[i] != NULL; i++) {
		struct vss_addr **ta;
		int j, n;

		n = VSS_resolve(av[i], "http", &ta);
		if (n == 0) {
			VCLI_Out(cli, "Invalid listen address ");
			VCLI_Quote(cli, av[i]);
			VCLI_SetResult(cli, CLIS_PARAM);
			break;
		}
		for (j = 0; j < n; ++j) {
			ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
			AN(ls);
			ls->sock = -1;
			ls->addr = ta[j];
			ls->name = strdup(av[i]);
			AN(ls->name);
			VTAILQ_INSERT_TAIL(&lsh, ls, list);
		}
		free(ta);
	}
	VAV_Free(av);
	if (cli != NULL && cli->result != CLIS_OK) {
		clean_listen_sock_head(&lsh);
		return;
	}

	REPLACE(mgt_param.listen_address, arg);

	clean_listen_sock_head(&heritage.socks);
	heritage.nsocks = 0;

	while (!VTAILQ_EMPTY(&lsh)) {
		ls = VTAILQ_FIRST(&lsh);
		VTAILQ_REMOVE(&lsh, ls, list);
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
		VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
		heritage.nsocks++;
	}
}

/*--------------------------------------------------------------------*/

void
tweak_string(struct cli *cli, const struct parspec *par, const char *arg)
{
	char **p = TRUST_ME(par->priv);

	AN(p);
	/* XXX should have tweak_generic_string */
	if (arg == NULL) {
		VCLI_Quote(cli, *p);
	} else {
		REPLACE(*p, arg);
	}
}

/*--------------------------------------------------------------------*/

void
tweak_waiter(struct cli *cli, const struct parspec *par, const char *arg)
{

	/* XXX should have tweak_generic_string */
	(void)par;
	WAIT_tweak_waiter(cli, arg);
}

/*--------------------------------------------------------------------*/

void
tweak_poolparam(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile struct poolparam *pp, px;
	char **av;

	pp = par->priv;
	if (arg == NULL) {
		VCLI_Out(cli, "%u,%u,%g",
		    pp->min_pool, pp->max_pool, pp->max_age);
	} else {
		av = VAV_Parse(arg, NULL, ARGV_COMMA);
		do {
			if (av[0] != NULL) {
				VCLI_Out(cli, "Parse error: %s", av[0]);
				VCLI_SetResult(cli, CLIS_PARAM);
				break;
			}
			if (av[1] == NULL || av[2] == NULL || av[3] == NULL) {
				VCLI_Out(cli,
				    "Three fields required:"
				    " min_pool, max_pool and max_age\n");
				VCLI_SetResult(cli, CLIS_PARAM);
				break;
			}
			px = *pp;
			if (tweak_generic_uint(cli, &px.min_pool, av[1],
			    (uint)par->min, (uint)par->max))
				break;
			if (tweak_generic_uint(cli, &px.max_pool, av[2],
			    (uint)par->min, (uint)par->max))
				break;
			if (tweak_generic_timeout_double(cli, &px.max_age,
			    av[3], 0, 1e6))
				break;
			if (px.min_pool > px.max_pool) {
				VCLI_Out(cli,
				    "min_pool cannot be larger"
				    " than max_pool\n");
				VCLI_SetResult(cli, CLIS_PARAM);
				break;
			}
			*pp = px;
		} while(0);
		VAV_Free(av);
	}
}

/*--------------------------------------------------------------------*/

/*
 * Make sure to end all lines with either a space or newline of the
 * formatting will go haywire.
 */

#define DELAYED_EFFECT_TEXT \
	"\nNB: This parameter may take quite some time to take (full) effect."

#define MUST_RESTART_TEXT \
	"\nNB: This parameter will not take any effect until the " \
	"child process has been restarted."

#define MUST_RELOAD_TEXT \
	"\nNB: This parameter will not take any effect until the " \
	"VCL programs have been reloaded."

#define EXPERIMENTAL_TEXT \
	"\nNB: We do not know yet if it is a good idea to change " \
	"this parameter, or if the default value is even sensible.  " \
	"Caution is advised, and feedback is most welcome."

#define WIZARD_TEXT \
	"\nNB: Do not change this parameter, unless a developer tell " \
	"you to do so."

#define PROTECTED_TEXT \
	"\nNB: This parameter is protected and can not be changed."

/*--------------------------------------------------------------------*/

#define WIDTH 76

static void
mcf_wrap(struct cli *cli, const char *text)
{
	const char *p, *q;

	/* Format text to COLUMNS width */
	for (p = text; *p != '\0'; ) {
		q = strchr(p, '\n');
		if (q == NULL)
			q = strchr(p, '\0');
		if (q > p + WIDTH - margin) {
			q = p + WIDTH - margin;
			while (q > p && *q != ' ')
				q--;
			AN(q);
		}
		VCLI_Out(cli, "%*s %.*s\n", margin, "", (int)(q - p), p);
		p = q;
		if (*p == ' ' || *p == '\n')
			p++;
	}
}

void
mcf_param_show(struct cli *cli, const char * const *av, void *priv)
{
	int i;
	const struct parspec *pp;
	int lfmt;

	(void)priv;
	if (av[2] == NULL || strcmp(av[2], "-l"))
		lfmt = 0;
	else
		lfmt = 1;
	for (i = 0; i < nparspec; i++) {
		pp = parspecs[i];
		if (av[2] != NULL && !lfmt && strcmp(pp->name, av[2]))
			continue;
		VCLI_Out(cli, "%-*s ", margin, pp->name);
		if (pp->func == NULL) {
			VCLI_Out(cli, "Not implemented.\n");
			if (av[2] != NULL && !lfmt)
				return;
			else
				continue;
		}
		pp->func(cli, pp, NULL);
		if (pp->units != NULL && *pp->units != '\0')
			VCLI_Out(cli, " [%s]\n", pp->units);
		else
			VCLI_Out(cli, "\n");
		if (av[2] != NULL) {
			VCLI_Out(cli, "%-*s Default is %s\n",
			    margin, "", pp->def);
			mcf_wrap(cli, pp->descr);
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
				VCLI_Out(cli, "\n");
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
		for (j = 0; j < nparspec; j++)
			if (!strcmp(parspecs[j]->name, av[i]))
				break;
		if (j == nparspec) {
			VCLI_Out(cli, "Unknown parameter %s", av[i]);
			VCLI_SetResult(cli, CLIS_PARAM);
			VAV_Free(av);
			return;
		}
		pp = calloc(sizeof *pp, 1L);
		XXXAN(pp);
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
parspec_cmp(const void *a, const void *b)
{
	struct parspec * const * pa = a;
	struct parspec * const * pb = b;
	return (strcmp((*pa)->name, (*pb)->name));
}

static void
MCF_AddParams(const struct parspec *ps)
{
	const struct parspec *pp;
	int n;

	n = 0;
	for (pp = ps; pp->name != NULL; pp++) {
		if (mcf_findpar(pp->name) != NULL)
			fprintf(stderr, "Duplicate param: %s\n", pp->name);
		if (strlen(pp->name) + 1 > margin)
			margin = strlen(pp->name) + 1;
		n++;
	}
	parspecs = realloc(parspecs, (1L + nparspec + n) * sizeof *parspecs);
	XXXAN(parspecs);
	for (pp = ps; pp->name != NULL; pp++)
		parspecs[nparspec++] = pp;
	parspecs[nparspec] = NULL;
	qsort (parspecs, nparspec, sizeof parspecs[0], parspec_cmp);
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
	const char *p, *q;
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
			printf("\t- Units: %s\n", pp->units);
		printf("\t- Default: %s\n", pp->def);
		/*
		 * XXX: we should mark the params with one/two flags
		 * XXX: that say if ->min/->max are valid, so we
		 * XXX: can emit those also in help texts.
		 */
		if (pp->flags) {
			printf("\t- Flags: ");
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
		printf("\n\t");
		for (p = pp->descr; *p; p++) {
			if (*p == '\n' && p[1] =='\0')
				break;
			if (*p == '\n' && p[1] =='\n') {
				printf("\n\n\t");
				p++;
			} else if (*p == '\n') {
				printf("\n\t");
			} else if (*p == ':' && p[1] == '\n') {
				/*
				 * Start of definition list,
				 * use RSTs code mode for this
				 */
				printf("::\n");
			} else {
				printf("%c", *p);
			}
		}
		printf("\n\n");
	}
}
