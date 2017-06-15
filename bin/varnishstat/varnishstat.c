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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#include "vapi/voptget.h"
#include "vapi/vsl.h"
#include "vdef.h"
#include "vnum.h"
#include "vtim.h"
#include "vut.h"

#include "varnishstat.h"

static const char progname[] = "varnishstat";

/*--------------------------------------------------------------------*/

static int
do_xml_cb(void *priv, const struct VSC_point * const pt)
{
	uint64_t val;

	(void)priv;
	if (pt == NULL)
		return (0);
	AZ(strcmp(pt->ctype, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;

	printf("\t<stat>\n");
	printf("\t\t<name>%s</name>\n", pt->name);
	printf("\t\t<value>%ju</value>\n", (uintmax_t)val);
	printf("\t\t<flag>%c</flag>\n", pt->semantics);
	printf("\t\t<format>%c</format>\n", pt->format);
	printf("\t\t<description>%s</description>\n", pt->sdesc);
	printf("\t</stat>\n");
	return (0);
}

static void
do_xml(struct vsm *vd)
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

	if (pt == NULL)
		return (0);

	jp = priv;
	AZ(strcmp(pt->ctype, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;

	if (*jp)
		*jp = 0;
	else
		printf(",\n");

	printf("  \"");
	/* build the JSON key name.  */
	printf("%s\": {\n", pt->name);
	printf("    \"description\": \"%s\",\n", pt->sdesc);

	printf("    \"flag\": \"%c\", ", pt->semantics);
	printf("\"format\": \"%c\",\n", pt->format);
	printf("    \"value\": %ju", (uintmax_t)val);
	printf("\n  }");

	if (*jp)
		printf("\n");
	return (0);
}

static void
do_json(struct vsm *vd)
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
}


/*--------------------------------------------------------------------*/

struct once_priv {
	double	up;
	int pad;
};

static int
do_once_cb_first(void *priv, const struct VSC_point * const pt)
{
	struct once_priv *op;
	uint64_t val;

	if (pt == NULL)
		return (0);
	op = priv;
	AZ(strcmp(pt->ctype, "uint64_t"));
	if (strcmp(pt->name, "MAIN.uptime"))
		return (0);
	val = *(const volatile uint64_t*)pt->ptr;
	op->up = (double)val;
	return (1);
}

static int
do_once_cb(void *priv, const struct VSC_point * const pt)
{
	struct once_priv *op;
	uint64_t val;
	int i;

	if (pt == NULL)
		return (0);
	op = priv;
	AZ(strcmp(pt->ctype, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;
	i = 0;
	i += printf("%s", pt->name);
	if (i >= op->pad)
		op->pad = i + 1;
	printf("%*.*s", op->pad - i, op->pad - i, "");
	if (pt->semantics == 'c')
		printf("%12ju %12.2f %s\n",
		    (uintmax_t)val, op->up ? val / op->up : 0,
		    pt->sdesc);
	else
		printf("%12ju %12s %s\n",
		    (uintmax_t)val, ".  ", pt->sdesc);
	return (0);
}

static void
do_once(struct vsm *vd)
{
	struct once_priv op;

	memset(&op, 0, sizeof op);
	op.pad = 18;

	(void)VSC_Iter(vd, NULL, do_once_cb_first, &op);
	(void)VSC_Iter(vd, NULL, do_once_cb, &op);
}

/*--------------------------------------------------------------------*/

static int
do_list_cb(void *priv, const struct VSC_point * const pt)
{
	int i;

	(void)priv;

	if (pt == NULL)
		return (0);

	i = 0;
	i += printf("%s", pt->name);
	if (i < 30)
		printf("%*s", i - 30, "");
	printf(" %s\n", pt->sdesc);
	return (0);
}

static void
list_fields(struct vsm *vd)
{
	printf("Varnishstat -f option fields:\n");
	printf("Field name                     Description\n");
	printf("----------                     -----------\n");

	(void)VSC_Iter(vd, NULL, do_list_cb, NULL);
}

/*--------------------------------------------------------------------*/

static void __attribute__((__noreturn__))
usage(int status)
{
	const char **opt;

	fprintf(stderr, "Usage: %s <options>\n\n", progname);
	fprintf(stderr, "Options:\n");
	for (opt = vopt_spec.vopt_usage; *opt != NULL; opt +=2)
		fprintf(stderr, " %-25s %s\n", *opt, *(opt + 1));
	exit(status);
}

int
main(int argc, char * const *argv)
{
	struct vsm *vd;
	double t_arg = 5.0, t_start = NAN;
	int once = 0, xml = 0, json = 0, f_list = 0, curses = 0;
	signed char opt;
	int i;

	VUT_Init(progname, argc, argv, &vopt_spec);
	vd = VSM_New();
	AN(vd);

	while ((opt = getopt(argc, argv, vopt_spec.vopt_optstring)) != -1) {
		switch (opt) {
		case '1':
			once = 1;
			break;
		case 'h':
			/* Usage help */
			usage(0);
		case 'l':
			f_list = 1;
			break;
		case 't':
			if (!strcasecmp(optarg, "off"))
				t_arg = -1.;
			else {
				t_arg = VNUM(optarg);
				if (isnan(t_arg))
					VUT_Error(1, "-t: Syntax error");
				if (t_arg < 0.)
					VUT_Error(1, "-t: Range error");
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
			i = VSC_Arg(vd, opt, optarg);
			if (i < 0)
				VUT_Error(1, "%s", VSM_Error(vd));
			if (!i)
				usage(1);
		}
	}

	if (optind != argc)
		usage(1);

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
		if (i && t_arg >= 0.)
			VUT_Error(1, "%s", VSM_Error(vd));
		do_curses(vd, 1.0);
		exit(0);
	}

	if (i)
		VUT_Error(1, "%s", VSM_Error(vd));

	if (xml)
		do_xml(vd);
	else if (json)
		do_json(vd);
	else if (once)
		do_once(vd);
	else if (f_list)
		list_fields(vd);
	else
		assert(0);

	exit(0);
}
