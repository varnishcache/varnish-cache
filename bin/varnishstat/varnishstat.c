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

#include "svnid.h"
SVNID("$Id$")

#include <sys/time.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"
#include "varnishstat.h"

#define FIELD_EXCLUSION_CHARACTER '^'

int
show_field(const char* field, const char *fields)
{
	const char* field_start;
	const char* field_end;
	int field_length;
	int match_value = 1;

	if (fields[0] == FIELD_EXCLUSION_CHARACTER) {
		match_value = 0;
		fields++;
	}

	field_start = strstr(fields, field);
	if (field_start != NULL) {
		field_length = strlen( field );

		while (field_start != NULL) {
			field_end = field_start + field_length;
			if ((field_start == fields ||
			    *(field_start - 1) == ',') &&
			    (*field_end == ',' || *field_end == '\0'))
				return (match_value);
			field_start = strstr( field_end, field );
		}
	}

	return (!match_value);
}

/*--------------------------------------------------------------------*/

struct xml_priv {
	const char *fields;
};

static int
do_xml_cb(void *priv, const struct vsl_statpt * const pt)
{
	uint64_t val;
	struct xml_priv *xp;

	xp = priv;
	if (xp->fields != NULL && !show_field(pt->nm, xp->fields))
		return (0);
	assert(!strcmp(pt->fmt, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;

	printf("\t<stat>\n");
	if (strcmp(pt->type, ""))
		printf("\t\t<type>%s</type>\n", pt->type);
	if (strcmp(pt->ident, ""))
		printf("\t\t<ident>%s</ident>\n", pt->ident);
	printf("\t\t<name>%s</name>\n", pt->nm);
	printf("\t\t<value>%ju</value>\n", val);
	printf("\t\t<flag>%c</flag>\n", pt->flag);
	printf("\t\t<description>%s</description>\n", pt->desc);
	printf("\t</stat>\n");
	return (0);
}

static void
do_xml(const struct VSL_data *vd, const char* fields)
{
	char time_stamp[20];
	time_t now;
	struct xml_priv xp;

	xp.fields = fields;

	printf("<?xml version=\"1.0\"?>\n");
	now = time(NULL);
	(void)strftime(time_stamp, 20, "%Y-%m-%dT%H:%M:%S", localtime(&now));
	printf("<varnishstat timestamp=\"%s\">\n", time_stamp);
	(void)VSL_IterStat(vd, do_xml_cb, &xp);
	printf("</varnishstat>\n");
}

/*--------------------------------------------------------------------*/

struct once_priv {
	double	up;
	const char *fields;
	int pad;
};

static int
do_once_cb(void *priv, const struct vsl_statpt * const pt)
{
	struct once_priv *op;
	uint64_t val;
	int i;

	op = priv;
	if (op->fields != NULL && !show_field(pt->nm, op->fields))
		return (0);
	assert(!strcmp(pt->fmt, "uint64_t"));
	val = *(const volatile uint64_t*)pt->ptr;
	i = 0;
	if (strcmp(pt->type, "")) 
		i += printf("%s.", pt->type);
	if (strcmp(pt->ident, ""))
		i += printf("%s.", pt->ident);
	i += printf("%s", pt->nm);
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
do_once(const struct VSL_data *vd, const struct varnish_stats *VSL_stats, const char* fields)
{
	struct once_priv op;

	memset(&op, 0, sizeof op);
	op.up = VSL_stats->uptime;
	op.fields = fields;
	op.pad = 18;

	(void)VSL_IterStat(vd, do_once_cb, &op);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
#define FMT "    %-28s # %s\n"
	fprintf(stderr, "usage: varnishstat "
	    "[-1lV] [-f field_list] [-n varnish_name] [-w delay]\n");
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

static void
list_fields(void)
{
	fprintf(stderr, "Varnishstat -f option fields:\n");
	fprintf(stderr, "Field name           Description\n");
	fprintf(stderr, "----------           -----------\n");

#define MAC_STAT(n, t, l, f, d)						\
	do {								\
		fprintf(stderr, "%-20s %s\n", #n, d);			\
	} while (0);
#include "stat_field.h"
#undef MAC_STAT
}

static int
valid_fields(const char* fields)
{
	int i, valid_field, field_length;
	const char *all_fields[] = {
#define MAC_STAT(n, t, l, f, d) \
	#n,
#include "stat_field.h"
#undef MAC_STAT
	NULL };
	const char *field_start, *field_end;

	if (fields[0] == FIELD_EXCLUSION_CHARACTER)
		fields++;

	for (field_start = fields; ; field_start = field_end + 1) {
		field_end = strchr(field_start, ',');
		if (field_end != NULL)
			field_length = field_end - field_start;
		else
			field_length = strlen(field_start);

		valid_field = 0;
		for (i = 0; all_fields[i] != NULL; i++) {
			if (strncmp(field_start, all_fields[i], field_length)
			     == 0 && field_length == strlen( all_fields[i])) {
				valid_field = 1;
				break;
			}
		}

		if (!valid_field) {
			(void)fputs("The field '", stderr);
			(void)fwrite(field_start, 1, field_length, stderr);
			(void)fputs("' is not a valid field\n", stderr);
			return (0);
		}

		if (field_end == NULL || *field_end == '\0')
			break;
	}

	return (1);
}

int
main(int argc, char * const *argv)
{
	int c;
	struct VSL_data *vd;
	const struct varnish_stats *VSL_stats;
	int delay = 1, once = 0, xml = 0;
	const char *fields = NULL;

	vd = VSL_New();

	while ((c = getopt(argc, argv, VSL_STAT_ARGS "1f:lVw:x")) != -1) {
		switch (c) {
		case '1':
			once = 1;
			break;
		case 'f':
			fields = optarg;
			break;
		case 'l':
			list_fields();
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
			if (VSL_Stat_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (VSL_Open(vd))
		exit(1);

	if ((VSL_stats = VSL_OpenStats(vd)) == NULL)
		exit(1);

	if (fields != NULL && !valid_fields(fields)) {
		usage();
		exit(1);
	}

	if (xml)
		do_xml(vd, fields);
	else if (once)
		do_once(vd, VSL_stats, fields);
	else
		do_curses(vd, VSL_stats, delay, fields);

	exit(0);
}
