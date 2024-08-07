/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"

#include "vbm.h"
#include "vnum.h"
#include "vqueue.h"
#include "vre.h"
#include "vsb.h"

#include "vapi/vsl.h"

#include "vsl_api.h"

/*--------------------------------------------------------------------
 * Look up a tag
 *   0..255	tag number
 *   -1		no tag matches
 *   -2		multiple tags match
 */

int
VSL_Name2Tag(const char *name, int l)
{
	int i, n;

	if (l == -1)
		l = strlen(name);
	n = -1;
	for (i = 0; i < SLT__MAX; i++) {
		if (VSL_tags[i] != NULL &&
		    !strncasecmp(name, VSL_tags[i], l)) {
			if (strlen(VSL_tags[i]) == l) {
				/* Exact match */
				return (i);
			}
			if (n == -1)
				n = i;
			else
				n = -2;
		}
	}
	return (n);
}

int
VSL_Glob2Tags(const char *glob, int l, VSL_tagfind_f *func, void *priv)
{
	const char *p1 = NULL;
	const char *p2 = NULL;
	const char *e, *p;
	int i, l1 = 0, l2 = 0, r = 0;

	AN(glob);
	if (l >= 0)
		e = glob + l;
	else
		e = strchr(glob, '\0');
	if (glob == e)
		return (-1);		// Empty pattern cannot match

	for (p = glob; p < e; p++)
		if (*p == '*')
			break;

	if (p == e) {			// No wildcard
		i = VSL_Name2Tag(glob, l);
		if (i < 0)
			return (i);
		if (func != NULL)
			(func)(i, priv);
		return (1);
	}

	if (p != glob) {		// Prefix match
		p1 = glob;
		l1 = p - p1;
	}

	if (p != e - 1) {		// Postfix match
		p2 = p + 1;
		l2 = e - p2;
	}

	for (p++; p < e; p++)
		if (*p == '*')
			return (-3);	// More than one wildcard

	for (i = 0; i < SLT__MAX; i++) {
		p = VSL_tags[i];
		if (p == NULL)
			continue;
		e = strchr(p, '\0');
		if ((e - p) - l1 < l2)
			continue;
		if (p1 != NULL && strncasecmp(p, p1, l1))
			continue;
		if (p2 != NULL && strncasecmp(e - l2, p2, l2))
			continue;
		if (func != NULL)
			(func)(i, priv);
		r++;
	}
	if (r == 0)
		return (-1);
	return (r);
}

int
VSL_List2Tags(const char *list, int l, VSL_tagfind_f *func, void *priv)
{
	const char *p, *b, *e;
	int r, t = 0;

	p = list;
	if (l >= 0)
		e = p + l;
	else
		e = strchr(p, '\0');
	while (p < e) {
		while (p < e && *p == ',')
			p++;
		if (p == e)
			break;
		b = p;
		while (p < e && *p != ',')
			p++;
		r = VSL_Glob2Tags(b, p - b, func, priv);
		if (r < 0)
			return (r);
		t += r;
	}
	if (t == 0)
		return (-1);
	return (t);
}

const char *VSLQ_grouping[VSL_g__MAX] = {
	[VSL_g_raw]	= "raw",
	[VSL_g_vxid]	= "vxid",
	[VSL_g_request]	= "request",
	[VSL_g_session]	= "session",
};

int
VSLQ_Name2Grouping(const char *name, int l)
{
	int i, n;

	AN(name);
	if (l == -1)
		l = strlen(name);
	n = -1;
	for (i = 0; i < VSL_g__MAX; i++) {
		if (!strncasecmp(name, VSLQ_grouping[i], l)) {
			if (strlen(VSLQ_grouping[i]) == l) {
				/* Exact match */
				return (i);
			}
			if (n == -1)
				n = i;
			else
				n = -2;
		}
	}
	return (n);
}

void v_matchproto_(VSL_tagfind_f)
vsl_vbm_bitset(int bit, void *priv)
{

	vbit_set((struct vbitmap *)priv, bit);
}

void v_matchproto_(VSL_tagfind_f)
vsl_vbm_bitclr(int bit, void *priv)
{

	vbit_clr((struct vbitmap *)priv, bit);
}

static int
vsl_ix_arg(struct VSL_data *vsl, int opt, const char *arg)
{
	int i;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	vsl->flags |= F_SEEN_ixIX;

	i = VSL_List2Tags(arg, -1, opt == 'x' ? vsl_vbm_bitset : vsl_vbm_bitclr,
	    vsl->vbm_suppress);
	if (i == -1)
		return (vsl_diag(vsl, "-%c: \"%s\" matches zero tags",
		    (char)opt, arg));
	else if (i == -2)
		return (vsl_diag(vsl, "-%c: \"%s\" is ambiguous",
		    (char)opt, arg));
	else if (i == -3)
		return (vsl_diag(vsl, "-%c: Syntax error in \"%s\"",
		    (char)opt, arg));

	return (1);
}

static int
vsl_IX_arg(struct VSL_data *vsl, int opt, const char *arg)
{
	int i, l, off, err;
	const char *b, *e;
	vre_t *vre;
	struct vsb vsb[1];
	struct vslf *vslf;
	struct vbitmap *tags = NULL;
	char errbuf[VRE_ERROR_LEN];


	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	AN(arg);
	vsl->flags |= F_SEEN_ixIX;

	b = arg;
	e = strchr(b, ':');
	if (e) {
		tags = vbit_new(SLT__MAX);
		AN(tags);
		l = e - b;
		i = VSL_List2Tags(b, l, vsl_vbm_bitset, tags);
		if (i < 0)
			vbit_destroy(tags);
		if (i == -1)
			return (vsl_diag(vsl,
			    "-%c: \"%*.*s\" matches zero tags",
			    (char)opt, l, l, b));
		else if (i == -2)
			return (vsl_diag(vsl,
			    "-%c: \"%*.*s\" is ambiguous",
			    (char)opt, l, l, b));
		else if (i <= -3)
			return (vsl_diag(vsl,
			    "-%c: Syntax error in \"%*.*s\"",
			    (char)opt, l, l, b));
		b = e + 1;
	}

	vre = VRE_compile(b, vsl->C_opt ? VRE_CASELESS : 0, &err, &off, 1);
	if (vre == NULL) {
		if (tags)
			vbit_destroy(tags);
		AN(VSB_init(vsb, errbuf, sizeof errbuf));
		AZ(VRE_error(vsb, err));
		AZ(VSB_finish(vsb));
		VSB_fini(vsb);
		return (vsl_diag(vsl, "-%c: Regex error at position %d (%s)",
		    (char)opt, off, errbuf));
	}

	ALLOC_OBJ(vslf, VSLF_MAGIC);
	AN(vslf);
	vslf->tags = tags;
	vslf->vre = vre;

	if (opt == 'I')
		VTAILQ_INSERT_TAIL(&vsl->vslf_select, vslf, list);
	else {
		assert(opt == 'X');
		VTAILQ_INSERT_TAIL(&vsl->vslf_suppress, vslf, list);
	}

	return (1);
}

static int
vsl_R_arg(struct VSL_data *vsl, const char *arg)
{
	char buf[32] = "";
	char *p;
	long l;

	AN(arg);
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	errno = 0;
	l = strtol(arg, &p, 0);
	if ((l == LONG_MIN || l == LONG_MAX) && errno == ERANGE)
		return (vsl_diag(vsl, "-R: Range error"));
	if (l <= 0 || l > INT_MAX)
		return (vsl_diag(vsl, "-R: Range error"));
	vsl->R_opt_l = l;
	assert(p != arg);
	AN(p);
	if (*p == '\0') {
		vsl->R_opt_p = 1.0;
		return (1);
	}
	if (*p != '/' || p[1] == '\0')
		return (vsl_diag(vsl, "-R: Syntax error"));
	p++;
	if (strlen(p) > sizeof(buf) - 2)
		return (vsl_diag(vsl, "-R: Syntax error"));
	if (!isdigit(*p))
		strcat(buf, "1");
	strcat(buf, p);
	vsl->R_opt_p = VNUM_duration(buf);
	if (isnan(vsl->R_opt_p) || vsl->R_opt_p <= 0.0)
		return (vsl_diag(vsl, "-R: Syntax error: Invalid duration"));
	return (1);
}

int
VSL_Arg(struct VSL_data *vsl, int opt, const char *arg)
{
	int i;
	char *p;
	double d;
	long l;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	/* If first option is 'i', set all bits for suppression */
	if ((opt == 'i' || opt == 'I') && !(vsl->flags & F_SEEN_ixIX))
		for (i = 0; i < SLT__MAX; i++)
			vbit_set(vsl->vbm_suppress, i);

	switch (opt) {
	case 'b': vsl->b_opt = 1; return (1);
	case 'c': vsl->c_opt = 1; return (1);
	case 'C':
		/* Caseless regular expressions */
		vsl->C_opt = 1;
		return (1);
	case 'E':
		vsl->E_opt = 1;
		vsl->c_opt = 1;
		return (1);
	case 'i': case 'x': return (vsl_ix_arg(vsl, opt, arg));
	case 'I': case 'X': return (vsl_IX_arg(vsl, opt, arg));
	case 'L':
		AN(arg);
		l = strtol(arg, &p, 0);
		while (isspace(*p))
			p++;
		if (*p != '\0')
			return (vsl_diag(vsl, "-L: Syntax error"));
		if (l <= 0 || l > INT_MAX)
			return (vsl_diag(vsl, "-L: Range error"));
		vsl->L_opt = (int)l;
		return (1);
	case 'R':
		return (vsl_R_arg(vsl, arg));
	case 'T':
		AN(arg);
		d = VNUM(arg);
		if (isnan(d))
			return (vsl_diag(vsl, "-T: Syntax error"));
		if (d < 0.)
			return (vsl_diag(vsl, "-T: Range error"));
		vsl->T_opt = d;
		return (1);
	case 'v': vsl->v_opt = 1; return (1);
	default:
		return (0);
	}
}
