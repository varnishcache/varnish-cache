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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mgt/mgt.h"

#include "mgt/mgt_param.h"
#include "storage/storage.h"
#include "vav.h"
#include "vnum.h"
#include "vsl_priv.h"

const char * const JSON_FMT = (const char *)&JSON_FMT;

/*--------------------------------------------------------------------
 * Generic handling of double typed parameters
 */

typedef double parse_double_f(const char *, const char **);

static double
parse_decimal(const char *p, const char **err)
{
	double v;

	v = SF_Parse_Decimal(&p, 0, err);
	if (errno == 0 && *p != '\0') {
		errno = EINVAL;
		*err = "Invalid number";
	}
	return (v);
}

static int
tweak_generic_double(struct vsb *vsb, const char *arg, const struct parspec *pp,
    parse_double_f parse, const char *fmt)
{
	volatile double u, minv = VRT_DECIMAL_MIN, maxv = VRT_DECIMAL_MAX;
	volatile double *dest = pp->priv;
	const char *err;

	if (arg != NULL && arg != JSON_FMT) {
		if (pp->min != NULL) {
			minv = parse(pp->min, &err);
			if (errno) {
				VSB_printf(vsb, "Min: %s (%s)\n", err, pp->min);
				return (-1);
			}
		}
		if (pp->max != NULL) {
			maxv = parse(pp->max, &err);
			if (errno) {
				VSB_printf(vsb, "Max: %s (%s)\n", err, pp->max);
				return (-1);
			}
		}

		u = parse(arg, &err);
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

static double
parse_duration(const char *p, const char **err)
{
	double v, r;

	v = SF_Parse_Decimal(&p, 0, err);
	if (*p == '\0')
		return (v);

	r = VNUM_duration_unit(v, p, NULL);
	if (isnan(r)) {
		errno = EINVAL;
		*err = "Invalid duration unit";
	}

	return (r);
}

int v_matchproto_(tweak_t)
tweak_timeout(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_double(vsb, arg, par, parse_duration, "%.3f"));
}

/*--------------------------------------------------------------------*/

int v_matchproto_(tweak_t)
tweak_double(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_double(vsb, arg, par, parse_decimal, "%g"));
}

/*--------------------------------------------------------------------*/

static int
parse_boolean(struct vsb *vsb, const char *arg)
{

	if (!strcasecmp(arg, "off"))
		return (0);
	if (!strcasecmp(arg, "disable"))
		return (0);
	if (!strcasecmp(arg, "no"))
		return (0);
	if (!strcasecmp(arg, "false"))
		return (0);
	if (!strcasecmp(arg, "on"))
		return (1);
	if (!strcasecmp(arg, "enable"))
		return (1);
	if (!strcasecmp(arg, "yes"))
		return (1);
	if (!strcasecmp(arg, "true"))
		return (1);

	VSB_cat(vsb, "use \"on\" or \"off\"\n");
	return (-1);
}

int v_matchproto_(tweak_t)
tweak_boolean(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;
	int val;

	dest = par->priv;
	if (arg != NULL && arg != JSON_FMT) {
		val = parse_boolean(vsb, arg);
		if (val < 0)
			return (-1);
		*dest = val;
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
			retval = tweak_generic_double(vsb, av[3], &pt,
			    parse_decimal, "%.0f");
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
	const struct parspec *orig;
	struct parspec alias[1];

	orig = TRUST_ME(par->priv);
	AN(orig);
	memcpy(alias, orig, sizeof *orig);
	alias->name = par->name;
	alias->priv = TRUST_ME(orig);
	return (alias->func(vsb, alias, arg));
}

/*--------------------------------------------------------------------
 * Tweak bits
 */

enum bit_do {BSET, BCLR, BTST};

static int
bit(uint8_t *p, unsigned no, enum bit_do act)
{
	uint8_t b;

	p += (no >> 3);
	b = (0x80 >> (no & 7));
	if (act == BSET)
		*p |= b;
	else if (act == BCLR)
		*p &= ~b;
	return (*p & b);
}

static inline void
bit_clear(uint8_t *p, unsigned l)
{

	memset(p, 0, ((size_t)l + 7) >> 3);
}

/*--------------------------------------------------------------------
 */

static int
bit_tweak(struct vsb *vsb, uint8_t *p, unsigned l, const char *arg,
    const char * const *tags, const char *desc, char sign)
{
	int i, n;
	unsigned j;
	char **av;
	const char *s;

	av = VAV_Parse(arg, &n, ARGV_COMMA);
	if (av[0] != NULL) {
		VSB_printf(vsb, "Cannot parse: %s\n", av[0]);
		VAV_Free(av);
		return (-1);
	}
	for (i = 1; av[i] != NULL; i++) {
		s = av[i];
		if (sign == '+' && !strcmp(s, "none")) {
			bit_clear(p, l);
			continue;
		}
		if (sign == '-' && !strcmp(s, "all")) {
			bit_clear(p, l);
			continue;
		}
		if (*s != '-' && *s != '+') {
			VSB_printf(vsb, "Missing '+' or '-' (%s)\n", s);
			VAV_Free(av);
			return (-1);
		}
		for (j = 0; j < l; j++) {
			if (tags[j] != NULL && !strcasecmp(s + 1, tags[j]))
				break;
		}
		if (tags[j] == NULL) {
			VSB_printf(vsb, "Unknown %s (%s)\n", desc, s);
			VAV_Free(av);
			return (-1);
		}
		assert(j < l);
		if (s[0] == sign)
			(void)bit(p, j, BSET);
		else
			(void)bit(p, j, BCLR);
	}
	VAV_Free(av);
	return (0);
}


/*--------------------------------------------------------------------
 */

static int
tweak_generic_bits(struct vsb *vsb, const struct parspec *par, const char *arg,
    uint8_t *p, unsigned l, const char * const *tags, const char *desc,
    char sign)
{
	unsigned j;

	if (arg != NULL && !strcmp(arg, "default")) {
		/* XXX: deprecated in favor of param.reset */
		return (tweak_generic_bits(vsb, par, par->def, p, l, tags,
		    desc, sign));
	}

	if (arg != NULL && arg != JSON_FMT)
		return (bit_tweak(vsb, p, l, arg, tags, desc, sign));

	if (arg == JSON_FMT)
		VSB_putc(vsb, '"');
	VSB_cat(vsb, sign == '+' ? "none" : "all");
	for (j = 0; j < l; j++) {
		if (bit(p, j, BTST))
			VSB_printf(vsb, ",%c%s", sign, tags[j]);
	}
	if (arg == JSON_FMT)
		VSB_putc(vsb, '"');
	return (0);
}

/*--------------------------------------------------------------------
 * The vsl_mask parameter
 */

static const char * const VSL_tags[256] = {
#  define SLTM(foo,flags,sdesc,ldesc) [SLT_##foo] = #foo,
#  include "tbl/vsl_tags.h"
};

int v_matchproto_(tweak_t)
tweak_vsl_mask(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_bits(vsb, par, arg, mgt_param.vsl_mask,
	    SLT__Reserved, VSL_tags, "VSL tag", '-'));
}

/*--------------------------------------------------------------------
 * The debug parameter
 */

static const char * const debug_tags[] = {
#  define DEBUG_BIT(U, l, d) [DBG_##U] = #l,
#  include "tbl/debug_bits.h"
       NULL
};

int v_matchproto_(tweak_t)
tweak_debug(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_bits(vsb, par, arg, mgt_param.debug_bits,
	    DBG_Reserved, debug_tags, "debug bit", '+'));
}

/*--------------------------------------------------------------------
 * The experimental parameter
 */

static const char * const experimental_tags[] = {
#  define EXPERIMENTAL_BIT(U, l, d) [EXPERIMENT_##U] = #l,
#  include "tbl/experimental_bits.h"
       NULL
};

int v_matchproto_(tweak_t)
tweak_experimental(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_bits(vsb, par, arg, mgt_param.experimental_bits,
	    EXPERIMENT_Reserved, experimental_tags, "experimental bit", '+'));
}

/*--------------------------------------------------------------------
 * The feature parameter
 */

static const char * const feature_tags[] = {
#  define FEATURE_BIT(U, l, d) [FEATURE_##U] = #l,
#  include "tbl/feature_bits.h"
       NULL
};

int v_matchproto_(tweak_t)
tweak_feature(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_bits(vsb, par, arg, mgt_param.feature_bits,
	    FEATURE_Reserved, feature_tags, "feature bit", '+'));
}

/*--------------------------------------------------------------------
 * The vcc_feature parameter
 */

static const char * const vcc_feature_tags[] = {
#  define VCC_FEATURE_BIT(U, l, d) [VCC_FEATURE_##U] = #l,
#  include "tbl/vcc_feature_bits.h"
       NULL
};

int v_matchproto_(tweak_t)
tweak_vcc_feature(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	const struct parspec *orig;
	char buf[32];
	int val;

	if (arg != NULL && arg != JSON_FMT &&
	    strcmp(par->name, "vcc_feature")) {
		orig = TRUST_ME(par->priv);
		val = parse_boolean(vsb, arg);
		if (val < 0)
			return (-1);
		bprintf(buf, "%c%s", val ? '+' : '-',
		    par->name + strlen("vcc_"));
		return (tweak_vcc_feature(vsb, orig, buf));
	}
	return (tweak_generic_bits(vsb, par, arg, mgt_param.vcc_feature_bits,
	    VCC_FEATURE_Reserved, vcc_feature_tags, "vcc_feature bit", '+'));
}
