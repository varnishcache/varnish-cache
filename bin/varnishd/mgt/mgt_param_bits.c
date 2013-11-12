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

#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "common/params.h"
#include "mgt/mgt.h"
#include "mgt/mgt_param.h"

#include "vav.h"

#include "vapi/vsl_int.h"

/*--------------------------------------------------------------------
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

/*--------------------------------------------------------------------
 */

static int
bit_tweak(struct vsb *vsb, uint8_t *p, unsigned l, const char *arg,
    const char * const *tags, const char *desc, const char *sign)
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
		if (s[0] == *sign)
			(void)bit(p, j, BSET);
		else
			(void)bit(p, j, BCLR);
	}
	VAV_Free(av);
	return (0);
}


/*--------------------------------------------------------------------
 * The vsl_mask parameter
 */

static const char * const VSL_tags[256] = {
#  define SLTM(foo,flags,sdesc,ldesc) [SLT_##foo] = #foo,
#  include "tbl/vsl_tags.h"
#  undef SLTM
	NULL
};

static int
tweak_vsl_mask(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	unsigned j;
	const char *s;
	(void)par;

	if (arg != NULL) {
		if (!strcmp(arg, "default")) {
			memset(mgt_param.vsl_mask,
			    0, sizeof mgt_param.vsl_mask);
			(void)bit(mgt_param.vsl_mask, SLT_VCL_trace, BSET);
			(void)bit(mgt_param.vsl_mask, SLT_WorkThread, BSET);
			(void)bit(mgt_param.vsl_mask, SLT_Hash, BSET);
		} else {
			return (bit_tweak(vsb, mgt_param.vsl_mask,
			    SLT__Reserved, arg, VSL_tags,
			    "VSL tag", "-"));
		}
	} else {
		s = "";
		for (j = 0; j < (unsigned)SLT__Reserved; j++) {
			if (bit(mgt_param.vsl_mask, j, BTST)) {
				VSB_printf(vsb, "%s-%s", s, VSL_tags[j]);
				s = ",";
			}
		}
		if (*s == '\0')
			VSB_printf(vsb, "(all enabled)");
	}
	return (0);
}

/*--------------------------------------------------------------------
 * The debug parameter
 */

static const char * const debug_tags[] = {
#  define DEBUG_BIT(U, l, d) [DBG_##U] = #l,
#  include "tbl/debug_bits.h"
#  undef DEBUG_BIT
       NULL
};

static int
tweak_debug(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	const char *s;
	unsigned j;
	(void)par;

	if (arg != NULL) {
		if (!strcmp(arg, "none")) {
			memset(mgt_param.debug_bits,
			    0, sizeof mgt_param.debug_bits);
		} else {
			return (bit_tweak(vsb, mgt_param.debug_bits,
			    DBG_Reserved, arg, debug_tags, "debug bit", "+"));
		}
	} else {
		s = "";
		for (j = 0; j < (unsigned)DBG_Reserved; j++) {
			if (bit(mgt_param.debug_bits, j, BTST)) {
				VSB_printf(vsb, "%s+%s", s, debug_tags[j]);
				s = ",";
			}
		}
		if (*s == '\0')
			VSB_printf(vsb, "none");
	}
	return (0);
}

/*--------------------------------------------------------------------
 * The feature parameter
 */

static const char * const feature_tags[] = {
#  define FEATURE_BIT(U, l, d, ld) [FEATURE_##U] = #l,
#  include "tbl/feature_bits.h"
#  undef FEATURE_BIT
       NULL
};

static int
tweak_feature(struct vsb *vsb, const struct parspec *par, const char *arg)
{
	const char *s;
	unsigned j;
	(void)par;

	if (arg != NULL) {
		if (!strcmp(arg, "none")) {
			memset(mgt_param.feature_bits,
			    0, sizeof mgt_param.feature_bits);
		} else {
			return (bit_tweak(vsb, mgt_param.feature_bits,
			    FEATURE_Reserved, arg, feature_tags,
			    "feature bit", "+"));
		}
	} else {
		s = "";
		for (j = 0; j < (unsigned)FEATURE_Reserved; j++) {
			if (bit(mgt_param.feature_bits, j, BTST)) {
				VSB_printf(vsb, "%s+%s", s, feature_tags[j]);
				s = ",";
			}
		}
		if (*s == '\0')
			VSB_printf(vsb, "none");
	}
	return (0);
}

/*--------------------------------------------------------------------
 * The parameter table itself
 */

struct parspec VSL_parspec[] = {
	{ "vsl_mask", tweak_vsl_mask, NULL, NULL, NULL,
		"Mask individual VSL messages from being logged.\n"
		"\tdefault\tSet default value\n"
		"\nUse +/- prefixe in front of VSL tag name, to mask/unmask "
		"individual VSL messages.",
		0, "default", "" },
	{ "debug", tweak_debug, NULL, NULL, NULL,
		"Enable/Disable various kinds of debugging.\n"
		"\tnone\tDisable all debugging\n\n"
		"Use +/- prefix to set/reset individual bits:"
#define DEBUG_BIT(U, l, d) "\n\t" #l "\t" d
#include "tbl/debug_bits.h"
#undef DEBUG_BIT
		, 0, "none", "" },
	{ "feature", tweak_feature, NULL, NULL, NULL,
		"Enable/Disable various minor features.\n"
		"\tnone\tDisable all features.\n\n"
		"Use +/- prefix to enable/disable individual feature:"
#define FEATURE_BIT(U, l, d, ld) "\n\t" #l "\t" d
#include "tbl/feature_bits.h"
#undef FEATURE_BIT
		, 0, "none", "" },
	{ NULL, NULL, NULL }
};
