/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "miniobj.h"
#include "vas.h"

#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vbm.h"
#include "vre.h"
#include "vsl_api.h"
#include "vsm_api.h"

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
	int i, r, l2;
	int pre = 0;
	int post = 0;
	char buf[64];

	AN(glob);
	if (l < 0)
		l = strlen(glob);
	if (l == 0 || l > sizeof buf - 1)
		return (-1);
	if (strchr(glob, '*') != NULL) {
		if (glob[0] == '*') {
			/* Prefix wildcard */
			pre = 1;
			glob++;
			l--;
		}
		if (l > 0 && glob[l - 1] == '*') {
			/* Postfix wildcard */
			post = 1;
			l--;
		}
	}
	if (pre && post)
		/* Support only post or prefix wildcards */
		return (-3);
	memcpy(buf, glob, l);
	buf[l] = '\0';
	if (strchr(buf, '*') != NULL)
		/* No multiple wildcards */
		return (-3);
	if (pre == 0 && post == 0) {
		/* No wildcards, use VSL_Name2Tag */
		i = VSL_Name2Tag(buf, l);
		if (i < 0)
			return (i);
		if (func != NULL)
			(func)(i, priv);
		return (1);
	}

	r = 0;
	for (i = 0; i < SLT__MAX; i++) {
		if (VSL_tags[i] == NULL)
			continue;
		l2 = strlen(VSL_tags[i]);
		if (l2 < l)
			continue;
		if (pre) {
			/* Prefix wildcard match */
			if (strcasecmp(buf, VSL_tags[i] + l2 - l))
				continue;
		} else {
			/* Postfix wildcard match */
			if (strncasecmp(buf, VSL_tags[i], l))
				continue;
		}
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
	const char *p, *q, *e;
	int r, t;

	if (l < 0)
		l = strlen(list);
	p = list;
	e = p + l;
	t = 0;
	while (p < e) {
		while (p < e && *p == ',')
			p++;
		if (p == e)
			break;
		q = p;
		while (q < e && *q != ',')
			q++;
		r = VSL_Glob2Tags(p, q - p, func, priv);
		if (r < 0)
			return (r);
		t += r;
		p = q;
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

void __match_proto__(VSL_tagfind_f)
vsl_vbm_bitset(int bit, void *priv)
{

	vbit_set((struct vbitmap *)priv, bit);
}

void __match_proto__(VSL_tagfind_f)
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
	    vsl->vbm_supress);
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
	int i, l, off;
	const char *b, *e, *err;
	vre_t *vre;
	struct vslf *vslf;
	struct vbitmap *tags = NULL;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	vsl->flags |= F_SEEN_ixIX;

	b = arg;
	e = strchr(b, ':');
	if (e) {
		tags = vbit_init(SLT__MAX);
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

	vre = VRE_compile(b, vsl->C_opt ? VRE_CASELESS : 0, &err, &off);
	if (vre == NULL) {
		if (tags)
			vbit_destroy(tags);
		return (vsl_diag(vsl, "-%c: Regex error at position %d (%s)\n",
			(char)opt, off, err));
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

int
VSL_Arg(struct VSL_data *vsl, int opt, const char *arg)
{
	int i;
	char *p;
	double d;
	long l;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	/* If first option is 'i', set all bits for supression */
	if ((opt == 'i' || opt == 'I') && !(vsl->flags & F_SEEN_ixIX))
		for (i = 0; i < SLT__MAX; i++)
			vbit_set(vsl->vbm_supress, i);

	switch (opt) {
	case 'b': vsl->b_opt = 1; return (1);
	case 'c': vsl->c_opt = 1; return (1);
	case 'C':
		/* Caseless regular expressions */
		vsl->C_opt = 1;
		return (1);
	case 'i': case 'x': return (vsl_ix_arg(vsl, opt, arg));
	case 'I': case 'X': return (vsl_IX_arg(vsl, opt, arg));
	case 'L':
		l = strtol(arg, &p, 0);
		while (isspace(*p))
			p++;
		if (*p != '\0')
			return (vsl_diag(vsl, "-L: Syntax error"));
		if (l < 0 || l > INT_MAX)
			return (vsl_diag(vsl, "-L: Range error"));
		vsl->L_opt = (int)l;
		return (1);
	case 'T':
		d = strtod(arg, &p);
		while (isspace(*p))
			p++;
		if (*p != '\0')
			return (vsl_diag(vsl, "-P: Syntax error"));
		if (d < 0.)
			return (vsl_diag(vsl, "-L: Range error"));
		vsl->T_opt = d;
		return (1);
	case 'v': vsl->v_opt = 1; return (1);
	default:
		return (0);
	}
}
