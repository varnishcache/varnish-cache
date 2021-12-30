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
 *
 * Functions for tweaking parameters
 *
 */

#include "config.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mgt/mgt.h"

#include "mgt/mgt_param.h"
#include "storage/storage.h"
#include "vav.h"
#include "vnum.h"

const char * const JSON_FMT = (const char *)&JSON_FMT;

/*--------------------------------------------------------------------
 * Generic handling of double typed parameters
 */

static int
tweak_generic_double(struct vsb *vsb, const char *arg, const struct parspec *pp,
    const char *fmt)
{
	volatile double u, minv = VRT_DECIMAL_MIN, maxv = VRT_DECIMAL_MAX;
	volatile double *dest = pp->priv;
	const char *p, *err;

	if (arg != NULL && arg != JSON_FMT) {
		if (pp->min != NULL) {
			p = pp->min;
			minv = SF_Parse_Decimal(&p, 0, &err);
			if (errno) {
				VSB_printf(vsb, "Min: %s (%s)\n", err, pp->min);
				return (-1);
			}
		}
		if (pp->max != NULL) {
			p = pp->max;
			maxv = SF_Parse_Decimal(&p, 0, &err);
			if (errno) {
				VSB_printf(vsb, "Max: %s (%s)\n", err, pp->max);
				return (-1);
			}
		}

		p = arg;
		u = SF_Parse_Decimal(&p, 0, &err);
		if (errno) {
			VSB_printf(vsb, "%s (%s)\n", err, arg);
			return (-1);
		}
		if (u < minv) {
			VSB_printf(vsb,
			    "Must be greater or equal to %s\n", pp->min);
			return (-1);
		}
		if (u > maxv) {
			VSB_printf(vsb,
			    "Must be less than or equal to %s\n", pp->max);
			return (-1);
		}
		*dest = u;
	} else {
		VSB_printf(vsb, fmt, *dest);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_timeout(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_double(vsb, arg, par, "%.3f"));
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_double(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_double(vsb, arg, par, "%g"));
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_boolean(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	if (arg != NULL && arg != JSON_FMT) {
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
			VSB_cat(vsb, "use \"on\" or \"off\"\n");
			return (-1);
		}
	} else if (arg == JSON_FMT) {
		VSB_printf(vsb, "%s", *dest ? "true" : "false");
	} else {
		VSB_printf(vsb, "%s", *dest ? "on" : "off");
	}
	return (0);
}

/*--------------------------------------------------------------------*/

static int
tweak_generic_uint(struct vsb *vsb, volatile unsigned *dest, const char *arg,
    const char *min, const char *max,
    const char *min_reason, const char *max_reason)
{
	unsigned u, minv = 0, maxv = 0;
	char *p;

	if (arg != NULL && arg != JSON_FMT) {
		if (min != NULL) {
			p = NULL;
			minv = strtoul(min, &p, 0);
			if (*arg == '\0' || *p != '\0') {
				VSB_printf(vsb, "Illegal Min: %s\n", min);
				return (-1);
			}
		}
		if (max != NULL) {
			p = NULL;
			maxv = strtoul(max, &p, 0);
			if (*arg == '\0' || *p != '\0') {
				VSB_printf(vsb, "Illegal Max: %s\n", max);
				return (-1);
			}
		}
		p = NULL;
		if (!strcasecmp(arg, "unlimited"))
			u = UINT_MAX;
		else {
			u = strtoul(arg, &p, 0);
			if (*arg == '\0' || *p != '\0') {
				VSB_printf(vsb, "Not a number (%s)\n", arg);
				return (-1);
			}
		}
		if (min != NULL && u < minv) {
			VSB_printf(vsb, "Must be at least %s", min);
			if (min_reason != NULL)
				VSB_printf(vsb, " (%s)", min_reason);
			VSB_putc(vsb, '\n');
			return (-1);
		}
		if (max != NULL && u > maxv) {
			VSB_printf(vsb, "Must be no more than %s", max);
			if (max_reason != NULL)
				VSB_printf(vsb, " (%s)", max_reason);
			VSB_putc(vsb, '\n');
			return (-1);
		}
		*dest = u;
	} else if (*dest == UINT_MAX && arg != JSON_FMT) {
		VSB_cat(vsb, "unlimited");
	} else {
		VSB_printf(vsb, "%u", *dest);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_uint(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	return (tweak_generic_uint(vsb, dest, arg, par->min, par->max,
	    par->dyn_min_reason, par->dyn_max_reason));
}

/*--------------------------------------------------------------------*/

static void
fmt_bytes(struct vsb *vsb, uintmax_t t)
{
	const char *p;

	if (t == 0 || t & 0xff) {
		VSB_printf(vsb, "%jub", t);
		return;
	}
	for (p = "kMGTPEZY"; *p; p++) {
		if (t & 0x300) {
			VSB_printf(vsb, "%.2f%c", t / 1024.0, *p);
			return;
		}
		t /= 1024;
		if (t & 0x0ff) {
			VSB_printf(vsb, "%ju%c", t, *p);
			return;
		}
	}
	VSB_cat(vsb, "(bogus number)");
}

static int
tweak_generic_bytes(struct vsb *vsb, volatile ssize_t *dest, const char *arg,
    const char *min, const char *max)
{
	uintmax_t r, rmin = 0, rmax = 0;
	const char *p;

	if (arg != NULL && arg != JSON_FMT) {
		if (min != NULL) {
			p = VNUM_2bytes(min, &rmin, 0);
			if (p != NULL) {
				VSB_printf(vsb, "Invalid min-val: %s\n", min);
				return (-1);
			}
		}
		if (max != NULL) {
			p = VNUM_2bytes(max, &rmax, 0);
			if (p != NULL) {
				VSB_printf(vsb, "Invalid max-val: %s\n", max);
				return (-1);
			}
		}
		p = VNUM_2bytes(arg, &r, 0);
		if (p != NULL) {
			VSB_cat(vsb, "Could not convert to bytes.\n");
			VSB_printf(vsb, "%s\n", p);
			VSB_cat(vsb, "  Try something like '80k' or '120M'\n");
			return (-1);
		}
		if ((uintmax_t)((ssize_t)r) != r) {
			fmt_bytes(vsb, r);
			VSB_cat(vsb, " is too large for this architecture.\n");
			return (-1);
		}
		if (max != NULL && r > rmax) {
			VSB_printf(vsb, "Must be no more than %s\n", max);
			VSB_cat(vsb, "\n");
			return (-1);
		}
		if (min != NULL && r < rmin) {
			VSB_printf(vsb, "Must be at least %s\n", min);
			return (-1);
		}
		*dest = r;
	} else if (arg == JSON_FMT) {
		VSB_printf(vsb, "%zd", *dest);
	} else {
		fmt_bytes(vsb, *dest);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_bytes(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile ssize_t *dest;

	dest = par->priv;
	return (tweak_generic_bytes(vsb, dest, arg, par->min, par->max));
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_bytes_u(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile unsigned *d1;
	volatile ssize_t dest;

	d1 = par->priv;
	dest = *d1;
	if (tweak_generic_bytes(vsb, &dest, arg, par->min, par->max))
		return (-1);
	*d1 = dest;
	return (0);
}

/*--------------------------------------------------------------------
 * vsl_buffer and vsl_reclen have dependencies.
 */

int v_matchproto_(tweak_t)
tweak_vsl_buffer(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile unsigned *d1;
	volatile ssize_t dest;

	d1 = par->priv;
	dest = *d1;
	if (tweak_generic_bytes(vsb, &dest, arg, par->min, par->max))
		return (-1);
	*d1 = dest;
	MCF_ParamConf(MCF_MAXIMUM, "vsl_reclen", "%u", *d1 - 12);
	return (0);
}

int v_matchproto_(tweak_t)
tweak_vsl_reclen(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile unsigned *d1;
	volatile ssize_t dest;

	d1 = par->priv;
	dest = *d1;
	if (tweak_generic_bytes(vsb, &dest, arg, par->min, par->max))
		return (-1);
	*d1 = dest;
	MCF_ParamConf(MCF_MINIMUM, "vsl_buffer", "%u", *d1 + 12);
	return (0);
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_string(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	char **p = TRUST_ME(par->priv);

	AN(p);
	if (arg == NULL) {
		VSB_quote(vsb, *p, -1, 0);
	} else if (arg == JSON_FMT) {
		VSB_putc(vsb, '"');
		VSB_quote(vsb, *p, -1, VSB_QUOTE_JSON);
		VSB_putc(vsb, '"');
	} else {
		REPLACE(*p, arg);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_poolparam(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile struct poolparam *pp, px;
	struct parspec pt;
	char **av;
	int retval = 0;

	pp = par->priv;
	if (arg == JSON_FMT) {
		VSB_cat(vsb, "{\n");
		VSB_indent(vsb, 8);
		VSB_printf(vsb, "\"min_pool\": %u,\n", pp->min_pool);
		VSB_printf(vsb, "\"max_pool\": %u,\n", pp->max_pool);
		VSB_printf(vsb, "\"max_age\": %g\n", pp->max_age);
		VSB_indent(vsb, -4);
		VSB_cat(vsb, "}");
	} else if (arg == NULL) {
		VSB_printf(vsb, "%u,%u,%g",
		    pp->min_pool, pp->max_pool, pp->max_age);
	} else {
		av = VAV_Parse(arg, NULL, ARGV_COMMA);
		do {
			if (av[0] != NULL) {
				VSB_printf(vsb, "Parse error: %s", av[0]);
				retval = -1;
				break;
			}
			if (av[1] == NULL || av[2] == NULL || av[3] == NULL) {
				VSB_cat(vsb,
				    "Three fields required:"
				    " min_pool, max_pool and max_age\n");
				retval = -1;
				break;
			}
			px = *pp;
			retval = tweak_generic_uint(vsb, &px.min_pool, av[1],
			    par->min, par->max, par->dyn_min_reason,
			    par->dyn_max_reason);
			if (retval)
				break;
			retval = tweak_generic_uint(vsb, &px.max_pool, av[2],
			    par->min, par->max, par->dyn_min_reason,
			    par->dyn_max_reason);
			if (retval)
				break;
			pt.priv = &px.max_age;
			pt.min = "0";
			pt.max = "1000000";
			retval = tweak_generic_double(vsb, av[3], &pt, "%.0f");
			if (retval)
				break;
			if (px.min_pool > px.max_pool) {
				VSB_cat(vsb,
				    "min_pool cannot be larger"
				    " than max_pool\n");
				retval = -1;
				break;
			}
			*pp = px;
		} while (0);
		VAV_Free(av);
	}
	return (retval);
}

/*--------------------------------------------------------------------
 * Thread pool tweaks.
 *
 * The min/max values automatically update the opposites appropriate
 * limit, so they don't end up crossing.
 */

int v_matchproto_(tweak_t)
tweak_thread_pool_min(struct vsb *vsb, const struct parspec *par,
    const char *arg)
{
	if (tweak_uint(vsb, par, arg))
		return (-1);

	MCF_ParamConf(MCF_MINIMUM, "thread_pool_max",
	    "%u", mgt_param.wthread_min);
	MCF_ParamConf(MCF_MAXIMUM, "thread_pool_reserve",
	    "%u", mgt_param.wthread_min * 950 / 1000);
	return (0);
}

int v_matchproto_(tweak_t)
tweak_thread_pool_max(struct vsb *vsb, const struct parspec *par,
    const char *arg)
{

	if (tweak_uint(vsb, par, arg))
		return (-1);

	MCF_ParamConf(MCF_MAXIMUM, "thread_pool_min",
	    "%u", mgt_param.wthread_max);
	return (0);
}

/*--------------------------------------------------------------------
 * Tweak storage
 */

int v_matchproto_(tweak_t)
tweak_storage(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	struct stevedore *stv;

	/* XXX: If we want to remove the MUST_RESTART flag from the
	 * h2_rxbuf_storage parameter, we could have a mechanism here
	 * that when the child is running calls out through CLI to change
	 * the stevedore being used. */

	if (arg == NULL || arg == JSON_FMT)
		return (tweak_string(vsb, par, arg));

	if (!strcmp(arg, "Transient")) {
		/* Always allow setting to the special name
		 * "Transient". There will always be a stevedore with this
		 * name, but it may not have been configured at the time
		 * this is called. */
	} else {
		/* Only allow setting the value to a known configured
		 * stevedore */
		STV_Foreach(stv) {
			CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
			if (!strcmp(stv->ident, arg))
				break;
		}
		if (stv == NULL) {
			VSB_printf(vsb, "unknown storage backend '%s'", arg);
			return (-1);
		}
	}
	return (tweak_string(vsb, par, arg));
}

/*--------------------------------------------------------------------
 * Tweak alias
 */

int v_matchproto_(tweak_t)
tweak_alias(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	par = TRUST_ME(par->priv);
	return (par->func(vsb, par, arg));
}
