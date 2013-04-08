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

#include "miniobj.h"
#include "vas.h"
#include "vdef.h"

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
	for (i = 0; i < 256; i++) {
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


/*--------------------------------------------------------------------*/

static int
vsl_r_arg(struct VSM_data *vd, const char *opt)
{
	struct vsl *vsl = vsl_Setup(vd);

	if (vsl->r_fd > STDIN_FILENO)
		(void)close(vsl->r_fd);
	if (!strcmp(opt, "-"))
		vsl->r_fd = STDIN_FILENO;
	else
		vsl->r_fd = open(opt, O_RDONLY);
	if (vsl->r_fd < 0)
		return (vsm_diag(vd,
		    "Could not open %s: %s", opt, strerror(errno)));
	if (vsl->rbuflen == 0) {
		vsl->rbuflen = BUFSIZ;
		vsl->rbuf = malloc(vsl->rbuflen);
		AN(vsl->rbuf);
	}
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_IX_arg(struct VSM_data *vd, const char *opt, int arg)
{
	struct vsl *vsl = vsl_Setup(vd);
	vre_t **rp;
	const char *error;
	int erroroffset;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (arg == 'I')
		rp = &vsl->regincl;
	else
		rp = &vsl->regexcl;
	if (*rp != NULL)
		return (vsm_diag(vd, "Option %c can only be given once", arg));
	*rp = VRE_compile(opt, vsl->regflags, &error, &erroroffset);
	if (*rp == NULL)
		return (vsm_diag(vd, "Illegal regex: %s\n", error));
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_ix_arg(struct VSM_data *vd, const char *opt, int arg)
{
	struct vsl *vsl = vsl_Setup(vd);
	int i, l;
	const char *b, *e;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	/* If first option is 'i', set all bits for supression */
	if (arg == 'i' && !(vsl->flags & F_SEEN_IX))
		for (i = 0; i < 256; i++)
			vbit_set(vsl->vbm_supress, i);
	vsl->flags |= F_SEEN_IX;

	for (b = opt; *b; b = e) {
		while (isspace(*b))
			b++;
		e = strchr(b, ',');
		if (e == NULL)
			e = strchr(b, '\0');
		l = e - b;
		if (*e == ',')
			e++;
		while (isspace(b[l - 1]))
			l--;
		i = VSL_Name2Tag(b, l);
		if (i >= 0) {
			if (arg == 'x')
				vbit_set(vsl->vbm_supress, i);
			else
				vbit_clr(vsl->vbm_supress, i);
		} else if (i == -2) {
			return (vsm_diag(vd,
			    "\"%*.*s\" matches multiple tags\n", l, l, b));
		} else {
			return (vsm_diag(vd,
			    "Could not match \"%*.*s\" to any tag\n", l, l, b));
		}
	}
	return (1);
}

/*--------------------------------------------------------------------*/


static int
vsl_m_arg(struct VSM_data *vd, const char *opt)
{
	struct vsl *vsl = vsl_Setup(vd);
	struct vsl_re_match *m;
	const char *error;
	char *o, *regex;
	int erroroffset;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (!strchr(opt, ':'))
		return (vsm_diag(vd,
		    "No : found in -m option %s\n", opt));

	o = strdup(opt);
	AN(o);
	regex = strchr(o, ':');
	*regex = '\0';
	regex++;

	ALLOC_OBJ(m, VSL_RE_MATCH_MAGIC);
	AN(m);
	m->tag = VSL_Name2Tag(o, -1);
	if (m->tag < 0) {
		(void)vsm_diag(vd, "Illegal tag %s specified\n", o);
		free(o);
		FREE_OBJ(m);
		return (-1);
	}
	/* Get tag, regex */
	m->re = VRE_compile(regex, vsl->regflags, &error, &erroroffset);
	if (m->re == NULL) {
		(void)vsm_diag(vd, "Illegal regex: %s\n", error);
		free(o);
		FREE_OBJ(m);
		return (-1);
	}
	vsl->num_matchers++;
	VTAILQ_INSERT_TAIL(&vsl->matchers, m, next);
	free(o);
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_s_arg(struct VSM_data *vd, const char *opt)
{
	struct vsl *vsl = vsl_Setup(vd);
	char *end;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (*opt == '\0')
		return (vsm_diag(vd, "number required for -s\n"));
	vsl->skip = strtoul(opt, &end, 10);
	if (*end != '\0')
		return (vsm_diag(vd, "invalid number for -k\n"));
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_k_arg(struct VSM_data *vd, const char *opt)
{
	struct vsl *vsl = vsl_Setup(vd);
	char *end;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (*opt == '\0')
		return (vsm_diag(vd, "number required for -k\n"));
	vsl->keep = strtoul(opt, &end, 10);
	if (*end != '\0')
		return (vsm_diag(vd, "invalid number for -k\n"));
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSL_Arg(struct VSM_data *vd, int arg, const char *opt)
{
	struct vsl *vsl = vsl_Setup(vd);

	switch (arg) {
	case 'b': vsl->b_opt = !vsl->b_opt; return (1);
	case 'c': vsl->c_opt = !vsl->c_opt; return (1);
	case 'd':
		vsl->d_opt = !vsl->d_opt;
		return (1);
	case 'i': case 'x': return (vsl_ix_arg(vd, opt, arg));
	case 'k': return (vsl_k_arg(vd, opt));
	case 'n': return (VSM_n_Arg(vd, opt));
	case 'r': return (vsl_r_arg(vd, opt));
	case 's': return (vsl_s_arg(vd, opt));
	case 'I': case 'X': return (vsl_IX_arg(vd, opt, arg));
	case 'm': return (vsl_m_arg(vd, opt));
	case 'C': vsl->regflags = VRE_CASELESS; return (1);
	default:
		return (0);
	}
}
