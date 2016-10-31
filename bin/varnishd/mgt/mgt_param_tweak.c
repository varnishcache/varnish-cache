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
#include <unistd.h>

#include "mgt/mgt.h"

#include "mgt/mgt_param.h"
#include "vav.h"
#include "vnum.h"

/*--------------------------------------------------------------------
 * Generic handling of double typed parameters
 */

static enum tweak_r_e __match_proto__(tweak_t)
tweak_generic_double(struct vsb *vsb, volatile double *dest,
    const char *arg, const char *min, const char *max, const char *fmt)
{
	double u, minv = 0, maxv = 0;

	if (arg != NULL) {
		if (min != NULL) {
			minv = VNUM(min);
			if (isnan(minv)) {
				VSB_printf(vsb, "Illegal Min: %s\n", min);
				return (TWEFMT);
			}
		}
		if (max != NULL) {
			maxv = VNUM(max);
			if (isnan(maxv)) {
				VSB_printf(vsb, "Illegal Max: %s\n", max);
				return (TWEFMT);
			}
		}

		u = VNUM(arg);
		if (isnan(u)) {
			VSB_printf(vsb, "Not a number(%s)\n", arg);
			return (TWEFMT);
		}
		if (min != NULL && u < minv) {
			VSB_printf(vsb,
			    "Must be greater or equal to %s\n", min);
			return (TWESMALL);
		}
		if (max != NULL && u > maxv) {
			VSB_printf(vsb,
			    "Must be less than or equal to %s\n", max);
			return (TWEBIG);
		}
		*dest = u;
	} else
		VSB_printf(vsb, fmt, *dest);
	return (TWOK);
}

/*--------------------------------------------------------------------*/

enum tweak_r_e __match_proto__(tweak_t)
tweak_timeout(struct vsb *vsb, struct parspec *par,
    const char *arg)
{
	volatile double *dest;

	dest = par->priv;
	return (tweak_generic_double(vsb, dest, arg,
	    par->min, par->max, "%.3f"));
}

/*--------------------------------------------------------------------*/

enum tweak_r_e __match_proto__(tweak_t)
tweak_double(struct vsb *vsb, struct parspec *par, const char *arg)
{
	volatile double *dest;

	dest = par->priv;
	return (tweak_generic_double(vsb, dest, arg,
	    par->min, par->max, "%g"));
}

/*--------------------------------------------------------------------*/

enum tweak_r_e __match_proto__(tweak_t)
tweak_bool(struct vsb *vsb, struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

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
			VSB_printf(vsb, "use \"on\" or \"off\"\n");
			return (TWEFMT);
		}
	} else {
		VSB_printf(vsb, "%s", *dest ? "on" : "off");
	}
	return (TWOK);
}

/*--------------------------------------------------------------------*/

enum tweak_r_e
tweak_generic_uint(struct vsb *vsb, volatile unsigned *dest, const char *arg,
    const char *min, const char *max)
{
	unsigned u, minv = 0, maxv = 0;
	char *p;

	if (arg != NULL) {
		if (min != NULL) {
			p = NULL;
			minv = strtoul(min, &p, 0);
			if (*arg == '\0' || *p != '\0') {
				VSB_printf(vsb, "Illegal Min: %s\n", min);
				return (TWEFMT);
			}
		}
		if (max != NULL) {
			p = NULL;
			maxv = strtoul(max, &p, 0);
			if (*arg == '\0' || *p != '\0') {
				VSB_printf(vsb, "Illegal Max: %s\n", max);
				return (TWEFMT);
			}
		}
		p = NULL;
		if (!strcasecmp(arg, "unlimited"))
			u = UINT_MAX;
		else {
			u = strtoul(arg, &p, 0);
			if (*arg == '\0' || *p != '\0') {
				VSB_printf(vsb, "Not a number (%s)\n", arg);
				return (TWEFMT);
			}
		}
		if (min != NULL && u < minv) {
			VSB_printf(vsb, "Must be at least %s\n", min);
			return (TWESMALL);
		}
		if (max != NULL && u > maxv) {
			VSB_printf(vsb, "Must be no more than %s\n", max);
			return (TWEBIG);
		}
		*dest = u;
	} else if (*dest == UINT_MAX) {
		VSB_printf(vsb, "unlimited");
	} else {
		VSB_printf(vsb, "%u", *dest);
	}
	return (TWOK);
}

/*--------------------------------------------------------------------*/

enum tweak_r_e __match_proto__(tweak_t)
tweak_uint(struct vsb *vsb, struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	return (tweak_generic_uint(vsb, dest, arg, par->min, par->max));
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
	VSB_printf(vsb, "(bogus number)");
}

static enum tweak_r_e __match_proto__(tweak_t)
tweak_generic_bytes(struct vsb *vsb, volatile ssize_t *dest, const char *arg,
    const char *min, const char *max)
{
	uintmax_t r, rmin = 0, rmax = 0;
	const char *p;

	if (arg != NULL) {
		if (min != NULL) {
			p = VNUM_2bytes(min, &rmin, 0);
			if (p != NULL) {
				VSB_printf(vsb, "Invalid min-val: %s\n", min);
				return (TWEFMT);
			}
		}
		if (max != NULL) {
			p = VNUM_2bytes(max, &rmax, 0);
			if (p != NULL) {
				VSB_printf(vsb, "Invalid max-val: %s\n", max);
				return (TWEFMT);
			}
		}
		p = VNUM_2bytes(arg, &r, 0);
		if (p != NULL) {
			VSB_printf(vsb, "Could not convert to bytes.\n");
			VSB_printf(vsb, "%s\n", p);
			VSB_printf(vsb,
			    "  Try something like '80k' or '120M'\n");
			return (TWEFMT);
		}
		if ((uintmax_t)((ssize_t)r) != r) {
			fmt_bytes(vsb, r);
			VSB_printf(vsb,
			    " is too large for this architecture.\n");
			return (TWEFMT);
		}
		if (max != NULL && r > rmax) {
			VSB_printf(vsb, "Must be no more than %s\n", max);
			VSB_printf(vsb, "\n");
			return (TWEBIG);
		}
		if (min != NULL && r < rmin) {
			VSB_printf(vsb, "Must be at least %s\n", min);
			return (TWESMALL);
		}
		*dest = r;
	} else {
		fmt_bytes(vsb, *dest);
	}
	return (TWOK);
}

/*--------------------------------------------------------------------*/

enum tweak_r_e __match_proto__(tweak_t)
tweak_bytes(struct vsb *vsb, struct parspec *par, const char *arg)
{
	volatile ssize_t *dest;

	dest = par->priv;
	return (tweak_generic_bytes(vsb, dest, arg, par->min, par->max));
}

/*--------------------------------------------------------------------*/

enum tweak_r_e __match_proto__(tweak_t)
tweak_bytes_u(struct vsb *vsb, struct parspec *par, const char *arg)
{
	volatile unsigned *d1;
	volatile ssize_t dest;
	enum tweak_r_e r;

	d1 = par->priv;
	dest = *d1;
	r = tweak_generic_bytes(vsb, &dest, arg, par->min, par->max);
	if (r == TWOK)
		*d1 = dest;
	return (r);
}

/*--------------------------------------------------------------------
 * vsl_buffer and vsl_reclen have dependencies.
 */

enum tweak_r_e __match_proto__(tweak_t)
tweak_vsl_buffer(struct vsb *vsb, struct parspec *par, const char *arg)
{
	volatile unsigned *d1;
	volatile ssize_t dest;
	enum tweak_r_e r;

	par->flags |= _LIMITING;

	d1 = par->priv;
	dest = *d1;
	r = tweak_generic_bytes(vsb, &dest, arg, par->min, par->max);
	if (r == TWOK) {
		*d1 = dest;
		MCF_ParamConf(MCF_MAXIMUM, "vsl_reclen", "%ub", *d1 - 12);
		MCF_ParamConf(MCF_MAXIMUM, "shm_reclen", "%ub", *d1 - 12);
	}

	par->flags &= ~_LIMITING;

	return (r);
}

enum tweak_r_e __match_proto__(tweak_t)
tweak_vsl_reclen(struct vsb *vsb, struct parspec *par, const char *arg)
{
	volatile unsigned *d1;
	volatile ssize_t dest;
	enum tweak_r_e r;

	par->flags |= _LIMITING;

	d1 = par->priv;
	dest = *d1;
	r = tweak_generic_bytes(vsb, &dest, arg, par->min, par->max);
	if (r == TWOK) {
		*d1 = dest;
		MCF_ParamConf(MCF_MINIMUM, "vsl_buffer", "%ub", *d1 + 12);
	}

	par->flags &= ~_LIMITING;

	return (r);
}

/*--------------------------------------------------------------------*/

enum tweak_r_e __match_proto__(tweak_t)
tweak_string(struct vsb *vsb, struct parspec *par, const char *arg)
{
	char **p = TRUST_ME(par->priv);

	AN(p);
	/* XXX should have tweak_generic_string */
	if (arg == NULL) {
		VSB_quote(vsb, *p, -1, 0);
	} else {
		REPLACE(*p, arg);
	}
	return (TWOK);
}

/*--------------------------------------------------------------------*/

enum tweak_r_e __match_proto__(tweak_t)
tweak_poolparam(struct vsb *vsb, struct parspec *par, const char *arg)
{
	volatile struct poolparam *pp, px;
	char **av;
	enum tweak_r_e retval = TWOK;

	pp = par->priv;
	if (arg == NULL) {
		VSB_printf(vsb, "%u,%u,%g",
		    pp->min_pool, pp->max_pool, pp->max_age);
	} else {
		av = VAV_Parse(arg, NULL, ARGV_COMMA);
		do {
			if (av[0] != NULL) {
				VSB_printf(vsb, "Parse error: %s", av[0]);
				retval = TWEFMT;
				break;
			}
			if (av[1] == NULL || av[2] == NULL || av[3] == NULL) {
				VSB_printf(vsb,
				    "Three fields required:"
				    " min_pool, max_pool and max_age\n");
				retval = TWEFMT;
				break;
			}
			px = *pp;
			retval = tweak_generic_uint(vsb, &px.min_pool, av[1],
			    par->min, par->max);
			if (retval)
				break;
			retval = tweak_generic_uint(vsb, &px.max_pool, av[2],
			    par->min, par->max);
			if (retval)
				break;
			retval = tweak_generic_double(vsb,
			    &px.max_age, av[3], "0", "1e6", "%.0f");
			if (retval)
				break;
			if (px.min_pool > px.max_pool) {
				VSB_printf(vsb,
				    "min_pool cannot be larger"
				    " than max_pool\n");
				retval = TWEFMT;
				break;
			}
			*pp = px;
		} while(0);
		VAV_Free(av);
	}
	return (retval);
}
