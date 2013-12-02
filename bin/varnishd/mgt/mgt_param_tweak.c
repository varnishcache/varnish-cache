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

#include <grp.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"
#include "common/params.h"

#include "mgt/mgt_param.h"
#include "waiter/waiter.h"
#include "vav.h"
#include "vnum.h"
#include "vss.h"

/*--------------------------------------------------------------------
 * Generic handling of double typed parameters
 */

static int
tweak_generic_double(struct vsb *vsb, volatile double *dest,
    const char *arg, const char *min, const char *max, const char *fmt)
{
	double u, minv = 0, maxv = 0;
	char *p;

	if (arg != NULL) {
		if (min != NULL) {
			p = NULL;
			minv = strtod(min, &p);
			if (*arg == '\0' || *p != '\0') {
				VSB_printf(vsb, "Illegal Min: %s\n", min);
				return (-1);
			}
		}
		if (max != NULL) {
			p = NULL;
			maxv = strtod(max, &p);
			if (*arg == '\0' || *p != '\0') {
				VSB_printf(vsb, "Illegal Max: %s\n", max);
				return (-1);
			}
		}

		p = NULL;
		u = strtod(arg, &p);
		if (*arg == '\0' || *p != '\0') {
			VSB_printf(vsb, "Not a number(%s)\n", arg);
			return (-1);
		}
		if (min != NULL && u < minv) {
			VSB_printf(vsb,
			    "Must be greater or equal to %s\n", min);
			return (-1);
		}
		if (max != NULL && u > maxv) {
			VSB_printf(vsb,
			    "Must be less than or equal to %s\n", max);
			return (-1);
		}
		*dest = u;
	} else
		VSB_printf(vsb, fmt, *dest);
	return (0);
}

/*--------------------------------------------------------------------*/

int
tweak_timeout(struct vsb *vsb, const struct parspec *par,
    const char *arg)
{
	volatile double *dest;

	dest = par->priv;
	return (tweak_generic_double(vsb, dest, arg,
	    par->min, par->max, "%.3f"));
}

/*--------------------------------------------------------------------*/

int
tweak_double(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile double *dest;

	dest = par->priv;
	return (tweak_generic_double(vsb, dest, arg,
	    par->min, par->max, "%g"));
}

/*--------------------------------------------------------------------*/

int
tweak_bool(struct vsb *vsb, const struct parspec *par, const char *arg)
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
			return (-1);
		}
	} else {
		VSB_printf(vsb, "%s", *dest ? "on" : "off");
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int
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
			VSB_printf(vsb, "Must be at least %s\n", min);
			return (-1);
		}
		if (max != NULL && u > maxv) {
			VSB_printf(vsb, "Must be no more than %s\n", max);
			return (-1);
		}
		*dest = u;
	} else if (*dest == UINT_MAX) {
		VSB_printf(vsb, "unlimited");
	} else {
		VSB_printf(vsb, "%u", *dest);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int
tweak_uint(struct vsb *vsb, const struct parspec *par, const char *arg)
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

	if (t & 0xff) {
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

static int
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
			VSB_printf(vsb, "Could not convert to bytes.\n");
			VSB_printf(vsb, "%s\n", p);
			VSB_printf(vsb,
			    "  Try something like '80k' or '120M'\n");
			return (-1);
		}
		if ((uintmax_t)((ssize_t)r) != r) {
			fmt_bytes(vsb, r);
			VSB_printf(vsb,
			    " is too large for this architecture.\n");
			return (-1);
		}
		if (max != NULL && r > rmax) {
			VSB_printf(vsb, "Must be no more than %s\n", max);
			VSB_printf(vsb, "\n");
			return (-1);
		}
		if (min != NULL && r < rmin) {
			VSB_printf(vsb, "Must be at least %s\n", min);
			return (-1);
		}
		*dest = r;
	} else {
		fmt_bytes(vsb, *dest);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int
tweak_bytes(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile ssize_t *dest;

	dest = par->priv;
	return (tweak_generic_bytes(vsb, dest, arg, par->min, par->max));
}

/*--------------------------------------------------------------------*/

int
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
 * XXX: slightly magic.  We want to initialize to "nobody" (XXX: shouldn't
 * XXX: that be something autocrap found for us ?) but we don't want to
 * XXX: fail initialization if that user doesn't exists, even though we
 * XXX: do want to fail it, in subsequent sets.
 * XXX: The magic init string is a hack for this.
 */

int
tweak_user(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	struct passwd *pw;

	(void)par;
	if (arg != NULL) {
		if (*arg != '\0') {
			pw = getpwnam(arg);
			if (pw == NULL) {
				VSB_printf(vsb, "Unknown user");
				return(-1);
			}
			REPLACE(mgt_param.user, pw->pw_name);
			mgt_param.uid = pw->pw_uid;
		} else {
			mgt_param.uid = getuid();
		}
	} else if (mgt_param.user) {
		VSB_printf(vsb, "%s (%d)", mgt_param.user, (int)mgt_param.uid);
	} else {
		VSB_printf(vsb, "UID %d", (int)mgt_param.uid);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * XXX: see comment for tweak_user, same thing here.
 */

int
tweak_group(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if (*arg != '\0') {
			gr = getgrnam(arg);
			if (gr == NULL) {
				VSB_printf(vsb, "Unknown group");
				return(-1);
			}
			REPLACE(mgt_param.group, gr->gr_name);
			mgt_param.gid = gr->gr_gid;
		} else {
			mgt_param.gid = getgid();
		}
	} else if (mgt_param.group) {
		VSB_printf(vsb, "%s (%d)", mgt_param.group, (int)mgt_param.gid);
	} else {
		VSB_printf(vsb, "GID %d", (int)mgt_param.gid);
	}
	return (0);
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

int
tweak_listen_address(struct vsb *vsb, const struct parspec *par,
    const char *arg)
{
	char **av;
	int i, retval = 0;
	struct listen_sock		*ls;
	struct listen_sock_head		lsh;

	(void)par;
	if (arg == NULL) {
		VSB_quote(vsb, mgt_param.listen_address, -1, 0);
		return (0);
	}

	av = VAV_Parse(arg, NULL, ARGV_COMMA);
	if (av == NULL) {
		VSB_printf(vsb, "Parse error: out of memory");
		return(-1);
	}
	if (av[0] != NULL) {
		VSB_printf(vsb, "Parse error: %s", av[0]);
		VAV_Free(av);
		return(-1);
	}
	if (av[1] == NULL) {
		VSB_printf(vsb, "Empty listen address");
		VAV_Free(av);
		return(-1);
	}
	VTAILQ_INIT(&lsh);
	for (i = 1; av[i] != NULL; i++) {
		struct vss_addr **ta;
		int j, n;

		n = VSS_resolve(av[i], "http", &ta);
		if (n == 0) {
			VSB_printf(vsb, "Invalid listen address ");
			VSB_quote(vsb, av[i], -1, 0);
			retval = -1;
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
	if (retval) {
		clean_listen_sock_head(&lsh);
		return (-1);
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
	return (0);
}

/*--------------------------------------------------------------------*/

int
tweak_string(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	char **p = TRUST_ME(par->priv);

	AN(p);
	/* XXX should have tweak_generic_string */
	if (arg == NULL) {
		VSB_quote(vsb, *p, -1, 0);
	} else {
		REPLACE(*p, arg);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int
tweak_waiter(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	/* XXX should have tweak_generic_string */
	(void)par;
	return (WAIT_tweak_waiter(vsb, arg));
}

/*--------------------------------------------------------------------*/

int
tweak_poolparam(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	volatile struct poolparam *pp, px;
	char **av;
	int retval = 0;

	pp = par->priv;
	if (arg == NULL) {
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
				VSB_printf(vsb,
				    "Three fields required:"
				    " min_pool, max_pool and max_age\n");
				retval = -1;
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
				retval = -1;
				break;
			}
			*pp = px;
		} while(0);
		VAV_Free(av);
	}
	return (retval);
}
