/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vas.h"
#include "argv.h"
#include "shmlog.h"
#include "vre.h"
#include "vbm.h"
#include "vqueue.h"
#include "miniobj.h"
#include "varnishapi.h"

#include "vsl.h"

/*--------------------------------------------------------------------*/

static int
vsl_r_arg(struct VSL_data *vd, const char *opt)
{

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (!strcmp(opt, "-"))
		vd->r_fd = STDIN_FILENO;
	else
		vd->r_fd = open(opt, O_RDONLY);
	if (vd->r_fd < 0) {
		perror(opt);
		return (-1);
	}
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_IX_arg(struct VSL_data *vd, const char *opt, int arg)
{
	vre_t **rp;
	const char *error;
	int erroroffset;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (arg == 'I')
		rp = &vd->regincl;
	else
		rp = &vd->regexcl;
	if (*rp != NULL) {
		fprintf(stderr, "Option %c can only be given once", arg);
		return (-1);
	}
	*rp = VRE_compile(opt, vd->regflags, &error, &erroroffset);
	if (*rp == NULL) {
		fprintf(stderr, "Illegal regex: %s\n", error);
		return (-1);
	}
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_ix_arg(struct VSL_data *vd, const char *opt, int arg)
{
	int i, j, l;
	const char *b, *e, *p, *q;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	/* If first option is 'i', set all bits for supression */
	if (arg == 'i' && !(vd->flags & F_SEEN_IX))
		for (i = 0; i < 256; i++)
			vbit_set(vd->vbm_supress, i);
	vd->flags |= F_SEEN_IX;

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
		for (i = 0; i < 256; i++) {
			if (VSL_tags[i] == NULL)
				continue;
			p = VSL_tags[i];
			q = b;
			for (j = 0; j < l; j++)
				if (tolower(*q++) != tolower(*p++))
					break;
			if (j != l || *p != '\0')
				continue;

			if (arg == 'x')
				vbit_set(vd->vbm_supress, i);
			else
				vbit_clr(vd->vbm_supress, i);
			break;
		}
		if (i == 256) {
			fprintf(stderr,
			    "Could not match \"%*.*s\" to any tag\n", l, l, b);
			return (-1);
		}
	}
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_s_arg(struct VSL_data *vd, const char *opt)
{
	char *end;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (*opt == '\0') {
		fprintf(stderr, "number required for -s\n");
		return (-1);
	}
	vd->skip = strtoul(opt, &end, 10);
	if (*end != '\0') {
		fprintf(stderr, "invalid number for -s\n");
		return (-1);
	}
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_k_arg(struct VSL_data *vd, const char *opt)
{
	char *end;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (*opt == '\0') {
		fprintf(stderr, "number required for -k\n");
		return (-1);
	}
	vd->keep = strtoul(opt, &end, 10);
	if (*end != '\0') {
		fprintf(stderr, "invalid number for -k\n");
		return (-1);
	}
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_n_arg(struct VSL_data *vd, const char *opt)
{

	REPLACE(vd->n_opt, opt);
	AN(vd->n_opt);
	if (vin_n_arg(vd->n_opt, NULL, NULL, &vd->fname)) {
		fprintf(stderr, "Invalid instance name: %s\n",
		    strerror(errno));
		return (-1);
	}
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSL_Log_Arg(struct VSL_data *vd, int arg, const char *opt)
{

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	switch (arg) {
	case 'b': vd->b_opt = !vd->b_opt; return (1);
	case 'c': vd->c_opt = !vd->c_opt; return (1);
	case 'd':
		vd->d_opt = !vd->d_opt;
		vd->flags |= F_NON_BLOCKING;
		return (1);
	case 'i': case 'x': return (vsl_ix_arg(vd, opt, arg));
	case 'k': return (vsl_k_arg(vd, opt));
	case 'n': return (vsl_n_arg(vd, opt));
	case 'r': return (vsl_r_arg(vd, opt));
	case 's': return (vsl_s_arg(vd, opt));
	case 'I': case 'X': return (vsl_IX_arg(vd, opt, arg));
	case 'C': vd->regflags = VRE_CASELESS; return (1);
	case 'L':
		vd->L_opt = strtoul(opt, NULL, 0);
		if (vd->L_opt < 1024 || vd->L_opt > 65000) {
			fprintf(stderr, "%s\n", VIN_L_MSG);
			exit (1);
		}
		free(vd->n_opt);
		vd->n_opt = vin_L_arg(vd->L_opt);
		assert(vd->n_opt != NULL);
		return (1);
	default:
		return (0);
	}
}

/*--------------------------------------------------------------------*/

static int
vsl_sf_arg(struct VSL_data *vd, const char *opt)
{
	struct vsl_sf *sf;
	char **av, *q, *p;
	int i;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);

	if (VTAILQ_EMPTY(&vd->sf_list)) {
		if (*opt == '^')
			vd->sf_init = 1;
	}

	av = ParseArgv(opt, ARGV_COMMA);
	AN(av);
	if (av[0] != NULL) {
		fprintf(stderr, "Parse error: %s", av[0]);
		exit (1);
	}
	for (i = 1; av[i] != NULL; i++) {
		ALLOC_OBJ(sf, VSL_SF_MAGIC);
		AN(sf);
		VTAILQ_INSERT_TAIL(&vd->sf_list, sf, next);

		p = av[i];
		if (*p == '^') {
			sf->flags |= VSL_SF_EXCL;
			p++;
		}

		q = strchr(p, '.');
		if (q != NULL) {
			*q++ = '\0';
			if (*p != '\0')
				REPLACE(sf->class, p);
			p = q;
			if (*p != '\0') {
				q = strchr(p, '.');
				if (q != NULL) {
					*q++ = '\0';
					if (*p != '\0')
						REPLACE(sf->ident, p);
					p = q;
				}
			}
		}
		if (*p != '\0') {
			REPLACE(sf->name, p);
		}

		/* Check for wildcards */
		if (sf->class != NULL) {
			q = strchr(sf->class, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSL_SF_CL_WC;
			}
		}
		if (sf->ident != NULL) {
			q = strchr(sf->ident, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSL_SF_ID_WC;
			}
		}
		if (sf->name != NULL) {
			q = strchr(sf->name, '*');
			if (q != NULL && q[1] == '\0') {
				*q = '\0';
				sf->flags |= VSL_SF_NM_WC;
			}
		}
	}
	FreeArgv(av);
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSL_Stat_Arg(struct VSL_data *vd, int arg, const char *opt)
{

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	switch (arg) {
	case 'f': return (vsl_sf_arg(vd, opt));
	case 'n': return (vsl_n_arg(vd, opt));
	default:
		return (0);
	}
}
