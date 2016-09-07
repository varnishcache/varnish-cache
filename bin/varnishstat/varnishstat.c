/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dag-Erling Smørgrav <des@des.no>
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
 * Statistics output program
 */

#include "config.h"

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>

#include "vnum.h"
#include "vtim.h"

#include "varnishstat.h"


/*--------------------------------------------------------------------*/

static int
do_xml_cb(void *priv, const struct VSC_point * const pt)
{
	uint64_t val;
	const struct VSC_section *sec;

	(void)priv;
	if (pt == NULL)
		return (0);
	AZ(strcmp(pt->desc->ctype, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;
	sec = pt->section;

	printf("\t<stat>\n");
	if (strcmp(sec->fantom->type, ""))
		printf("\t\t<type>%s</type>\n", sec->fantom->type);
	if (strcmp(sec->fantom->ident, ""))
		printf("\t\t<ident>%s</ident>\n", sec->fantom->ident);
	printf("\t\t<name>%s</name>\n", pt->desc->name);
	printf("\t\t<value>%ju</value>\n", (uintmax_t)val);
	printf("\t\t<flag>%c</flag>\n", pt->desc->semantics);
	printf("\t\t<format>%c</format>\n", pt->desc->format);
	printf("\t\t<description>%s</description>\n", pt->desc->sdesc);
	printf("\t</stat>\n");
	return (0);
}

static void
do_xml(struct VSM_data *vd)
{
	char time_stamp[20];
	time_t now;

	printf("<?xml version=\"1.0\"?>\n");
	now = time(NULL);
	(void)strftime(time_stamp, 20, "%Y-%m-%dT%H:%M:%S", localtime(&now));
	printf("<varnishstat timestamp=\"%s\">\n", time_stamp);
	(void)VSC_Iter(vd, NULL, do_xml_cb, NULL);
	printf("</varnishstat>\n");
}


/*--------------------------------------------------------------------*/

static int
do_json_cb(void *priv, const struct VSC_point * const pt)
{
	uint64_t val;
	int *jp;
	const struct VSC_section *sec;

	if (pt == NULL)
		return (0);

	jp = priv;
	AZ(strcmp(pt->desc->ctype, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;
	sec = pt->section;

	if (*jp)
		*jp = 0;
	else
		printf(",\n");

	printf("  \"");
	/* build the JSON key name.  */
	if (sec->fantom->type[0])
		printf("%s.", sec->fantom->type);
	if (sec->fantom->ident[0])
		printf("%s.", sec->fantom->ident);
	printf("%s\": {\n", pt->desc->name);
	printf("    \"description\": \"%s\",\n", pt->desc->sdesc);

	if (strcmp(sec->fantom->type, ""))
		printf("    \"type\": \"%s\", ", sec->fantom->type);
	if (strcmp(sec->fantom->ident, ""))
		printf("\"ident\": \"%s\", ", sec->fantom->ident);
	printf("\"flag\": \"%c\", ", pt->desc->semantics);
	printf("\"format\": \"%c\",\n", pt->desc->format);
	printf("    \"value\": %ju", (uintmax_t)val);
	printf("\n  }");

	if (*jp)
		printf("\n");
	return (0);
}

static void
do_json(struct VSM_data *vd)
{
	char time_stamp[20];
	time_t now;
	int jp;

	jp = 1;

	printf("{\n");
	now = time(NULL);

	(void)strftime(time_stamp, 20, "%Y-%m-%dT%H:%M:%S", localtime(&now));
	printf("  \"timestamp\": \"%s\",\n", time_stamp);
	(void)VSC_Iter(vd, NULL, do_json_cb, &jp);
	printf("\n}\n");
	fflush(stdout);
}


/*--------------------------------------------------------------------*/

struct once_priv {
	double	up;
	int pad;
};

static int
do_once_cb(void *priv, const struct VSC_point * const pt)
{
	struct once_priv *op;
	uint64_t val;
	int i;
	const struct VSC_section *sec;

	if (pt == NULL)
		return (0);
	op = priv;
	AZ(strcmp(pt->desc->ctype, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;
	sec = pt->section;
	i = 0;
	if (strcmp(sec->fantom->type, ""))
		i += printf("%s.", sec->fantom->type);
	if (strcmp(sec->fantom->ident, ""))
		i += printf("%s.", sec->fantom->ident);
	i += printf("%s", pt->desc->name);
	if (i >= op->pad)
		op->pad = i + 1;
	printf("%*.*s", op->pad - i, op->pad - i, "");
	if (pt->desc->semantics == 'c')
		printf("%12ju %12.2f %s\n",
		    (uintmax_t)val, val / op->up, pt->desc->sdesc);
	else
		printf("%12ju %12s %s\n",
		    (uintmax_t)val, ".  ", pt->desc->sdesc);
	return (0);
}

static void
do_once(struct VSM_data *vd, const struct VSC_C_main *VSC_C_main)
{
	struct once_priv op;

	memset(&op, 0, sizeof op);
	if (VSC_C_main != NULL)
		op.up = VSC_C_main->uptime;
	op.pad = 18;

	(void)VSC_Iter(vd, NULL, do_once_cb, &op);
}

/*--------------------------------------------------------------------*/

static int
do_list_cb(void *priv, const struct VSC_point * const pt)
{
	int i;
	const struct VSC_section * sec;

	(void)priv;

	if (pt == NULL)
		return (0);

	sec = pt->section;
	i = 0;
	if (strcmp(sec->fantom->type, ""))
		i += fprintf(stderr, "%s.", sec->fantom->type);
	if (strcmp(sec->fantom->ident, ""))
		i += fprintf(stderr, "%s.", sec->fantom->ident);
	i += fprintf(stderr, "%s", pt->desc->name);
	if (i < 30)
		fprintf(stderr, "%*s", i - 30, "");
	fprintf(stderr, " %s\n", pt->desc->sdesc);
	return (0);
}

static void
list_fields(struct VSM_data *vd)
{
	fprintf(stderr, "Varnishstat -f option fields:\n");
	fprintf(stderr, "Field name                     Description\n");
	fprintf(stderr, "----------                     -----------\n");

	(void)VSC_Iter(vd, NULL, do_list_cb, NULL);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
#define FMT "    %-28s # %s\n"
	fprintf(stderr, "usage: varnishstat "
	    "[-1lV] [-f <glob>] [-t <seconds>|<off>] "
	    VSC_n_USAGE "\n");
	fprintf(stderr, FMT, "-1", "Print the statistics to stdout.");
	fprintf(stderr, FMT, "-f <glob>", "Field inclusion glob");
	fprintf(stderr, FMT, "",
	    "If it starts with '^' it is used as an exclusion list.");
	fprintf(stderr, FMT, "-l",
	    "Lists the available fields to use with the -f option.");
	fprintf(stderr, FMT, "-n <varnish_name>",
	    "The varnishd instance to get logs from.");
	fprintf(stderr, FMT, "-N <filename>",
	    "Filename of a stale VSM instance.");
	fprintf(stderr, FMT, "-t <seconds>|<off>",
	    "Timeout before returning error on initial VSM connection.");
	fprintf(stderr, FMT, "-V", "Display the version number and exit.");
	fprintf(stderr, FMT, "-x",
	    "Print statistics to stdout as XML.");
	fprintf(stderr, FMT, "-j",
	    "Print statistics to stdout as JSON.");
#undef FMT
	exit(1);
}

int
main(int argc, char * const *argv)
{
	int c;
	struct VSM_data *vd;
	double t_arg = 5.0, t_start = NAN;
	int once = 0, xml = 0, json = 0, f_list = 0, curses = 0;
	int i;

	vd = VSM_New();
	AN(vd);

	while ((c = getopt(argc, argv, VSC_ARGS "1f:lVxjt:")) != -1) {
		switch (c) {
		case '1':
			once = 1;
			break;
		case 'l':
			f_list = 1;
			break;
		case 't':
			if (!strcasecmp(optarg, "off"))
				t_arg = -1.;
			else {
				t_arg = VNUM(optarg);
				if (isnan(t_arg)) {
					fprintf(stderr, "-t: Syntax error");
					exit(1);
				}
				if (t_arg < 0.) {
					fprintf(stderr, "-t: Range error");
					exit(1);
				}
			}
			break;
		case 'V':
			VCS_Message("varnishstat");
			exit(0);
		case 'x':
			xml = 1;
			break;
		case 'j':
			json = 1;
			break;
		default:
			if (VSC_Arg(vd, c, optarg) > 0)
				break;
			fprintf(stderr, "%s\n", VSM_Error(vd));
			usage();
		}
	}

	if (!(xml || json || once || f_list))
		curses = 1;

	while (1) {
		i = VSM_Open(vd);
		if (!i)
			break;
		if (isnan(t_start) && t_arg > 0.) {
			fprintf(stderr, "Can't open log -"
			    " retrying for %.0f seconds\n", t_arg);
			t_start = VTIM_real();
		}
		if (t_arg <= 0.)
			break;
		if (VTIM_real() - t_start > t_arg)
			break;
		VSM_ResetError(vd);
		VTIM_sleep(0.5);
	}

	if (curses) {
		if (i && t_arg >= 0.) {
			fprintf(stderr, "%s\n", VSM_Error(vd));
			exit(1);
		}
		do_curses(vd, 1.0);
		exit(0);
	}

	if (i) {
		fprintf(stderr, "%s\n", VSM_Error(vd));
		exit(1);
	}

	if (xml)
		do_xml(vd);
	else if (json)
		do_json(vd);
	else if (once)
		do_once(vd, VSC_Main(vd, NULL));
	else if (f_list)
		list_fields(vd);
	else
		assert(0);

	/* end of output block marker. */
	printf("\n");

	exit(0);
}
