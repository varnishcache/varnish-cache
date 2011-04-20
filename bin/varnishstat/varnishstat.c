/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libvarnish.h"
#include "vsc.h"
#include "varnishapi.h"
#include "varnishstat.h"

/*--------------------------------------------------------------------*/

static int
do_xml_cb(void *priv, const struct vsc_point * const pt)
{
	uint64_t val;

	(void)priv;
	assert(!strcmp(pt->fmt, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;

	printf("\t<stat>\n");
	if (strcmp(pt->class, ""))
		printf("\t\t<type>%s</type>\n", pt->class);
	if (strcmp(pt->ident, ""))
		printf("\t\t<ident>%s</ident>\n", pt->ident);
	printf("\t\t<name>%s</name>\n", pt->name);
	printf("\t\t<value>%ju</value>\n", val);
	printf("\t\t<flag>%c</flag>\n", pt->flag);
	printf("\t\t<description>%s</description>\n", pt->desc);
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
	(void)VSC_Iter(vd, do_xml_cb, NULL);
	printf("</varnishstat>\n");
}

/*--------------------------------------------------------------------*/

struct once_priv {
	double	up;
	int pad;
};

static int
do_once_cb(void *priv, const struct vsc_point * const pt)
{
	struct once_priv *op;
	uint64_t val;
	int i;

	op = priv;
	assert(!strcmp(pt->fmt, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;
	i = 0;
	if (strcmp(pt->class, ""))
		i += printf("%s.", pt->class);
	if (strcmp(pt->ident, ""))
		i += printf("%s.", pt->ident);
	i += printf("%s", pt->name);
	if (i > op->pad)
		op->pad = i + 1;
	printf("%*.*s", op->pad - i, op->pad - i, "");
	if (pt->flag == 'a')
		printf("%12ju %12.2f %s\n", val, val / op->up, pt->desc);
	else
		printf("%12ju %12s %s\n", val, ".  ", pt->desc);
	return (0);
}

static void
do_once(struct VSM_data *vd, const struct vsc_main *VSC_main)
{
	struct once_priv op;

	memset(&op, 0, sizeof op);
	op.up = VSC_main->uptime;
	op.pad = 18;

	(void)VSC_Iter(vd, do_once_cb, &op);
}

/*--------------------------------------------------------------------*/

static int
do_list_cb(void *priv, const struct vsc_point * const pt)
{
	int i;

	(void)priv;
	i = 0;
	if (strcmp(pt->class, ""))
		i += fprintf(stderr, "%s.", pt->class);
	if (strcmp(pt->ident, ""))
		i += fprintf(stderr, "%s.", pt->ident);
	i += fprintf(stderr, "%s", pt->name);
	if (i < 30)
		fprintf(stderr, "%*s", i - 30, "");
	fprintf(stderr, " %s\n", pt->desc);
	return (0);
}

static void
list_fields(struct VSM_data *vd)
{
	fprintf(stderr, "Varnishstat -f option fields:\n");
	fprintf(stderr, "Field name                     Description\n");
	fprintf(stderr, "----------                     -----------\n");

	(void)VSC_Iter(vd, do_list_cb, NULL);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
#define FMT "    %-28s # %s\n"
	fprintf(stderr, "usage: varnishstat "
	    "[-1lV] [-f field_list] "
	    VSC_n_USAGE " "
	    "[-w delay]\n");
	fprintf(stderr, FMT, "-1", "Print the statistics once and exit");
	fprintf(stderr, FMT, "-f field_list",
	    "Comma separated list of fields to display. ");
	fprintf(stderr, FMT, "",
	    "If it starts with '^' it is used as an exclusion list");
	fprintf(stderr, FMT, "-l",
	    "Lists the available fields to use with the -f option");
	fprintf(stderr, FMT, "-n varnish_name",
	    "The varnishd instance to get logs from");
	fprintf(stderr, FMT, "-V", "Display the version number and exit");
	fprintf(stderr, FMT, "-w delay",
	    "Wait delay seconds between updates.  The default is 1.");
	fprintf(stderr, FMT, "-x",
	    "Print statistics once as XML and exit.");
#undef FMT
	exit(1);
}

int
main(int argc, char * const *argv)
{
	int c;
	struct VSM_data *vd;
	const struct vsc_main *VSC_main;
	int delay = 1, once = 0, xml = 0;

	vd = VSM_New();
	VSC_Setup(vd);

	while ((c = getopt(argc, argv, VSC_ARGS "1f:lVw:x")) != -1) {
		switch (c) {
		case '1':
			once = 1;
			break;
		case 'l':
			if (VSC_Open(vd, 1))
				exit(1);
			list_fields(vd);
			exit(0);
		case 'V':
			varnish_version("varnishstat");
			exit(0);
		case 'w':
			delay = atoi(optarg);
			break;
		case 'x':
			xml = 1;
			break;
		default:
			if (VSC_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (VSC_Open(vd, 1))
		exit(1);

	VSC_main = VSC_Main(vd);

	if (xml)
		do_xml(vd);
	else if (once)
		do_once(vd, VSC_main);
	else
		do_curses(vd, VSC_main, delay);

	exit(0);
}
