/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * Log tailer for Varnish
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/time.h>

#include <curses.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"

#define FIELD_EXCLUSION_CHARACTER '^'


static void
myexp(double *acc, double val, unsigned *n, unsigned nmax)
{

	if (*n < nmax)
		(*n)++;
	(*acc) += (val - *acc) / (double)*n;
}

static int
show_field(const char* field, const char *fields)
{
	char* field_start;
	char* field_end;
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

static void
do_curses(struct varnish_stats *VSL_stats, int delay, const char *fields)
{
	struct varnish_stats copy;
	struct varnish_stats seen;
	intmax_t ju;
	struct timeval tv;
	double tt, lt, hit, miss, ratio, up;
	double a1, a2, a3;
	unsigned n1, n2, n3;
	time_t rt;
	int ch, line;

	memset(&copy, 0, sizeof copy);
	memset(&seen, 0, sizeof seen);

	a1 = a2 = a3 = 0.0;
	n1 = n2 = n3 = 0;

	initscr();
	raw();
	noecho();
	nonl();
	intrflush(stdscr, FALSE);
	curs_set(0);
	erase();

	lt = 0;
	while (1) {
		gettimeofday(&tv, NULL);
		tt = tv.tv_usec * 1e-6 + tv.tv_sec;
		lt = tt - lt;

		rt = VSL_stats->uptime;
		up = rt;

		mvprintw(0, 0, "%*s", COLS - 1, VSL_Name());
		mvprintw(0, 0, "%d+%02d:%02d:%02d", rt / 86400,
		    (rt % 86400) / 3600, (rt % 3600) / 60, rt % 60);

		hit = VSL_stats->cache_hit - copy.cache_hit;
		miss = VSL_stats->cache_miss - copy.cache_miss;
		hit /= lt;
		miss /= lt;
		if (hit + miss != 0) {
			ratio = hit / (hit + miss);
			myexp(&a1, ratio, &n1, 10);
			myexp(&a2, ratio, &n2, 100);
			myexp(&a3, ratio, &n3, 1000);
		}
		mvprintw(1, 0, "Hitrate ratio: %8u %8u %8u", n1, n2, n3);
		mvprintw(2, 0, "Hitrate avg:   %8.4f %8.4f %8.4f", a1, a2, a3);

		line = 3;
#define MAC_STAT(n, t, l, f, d) \
	if ((fields == NULL || show_field( #n, fields )) && line < LINES) { \
		ju = VSL_stats->n; \
		if (ju == 0 && !seen.n) { \
		} else if (f == 'a') { \
			seen.n = 1; \
			line++; \
			mvprintw(line, 0, "%12ju %12.2f %12.2f %s\n", \
			    ju, (ju - (intmax_t)copy.n)/lt, ju / up, d); \
			copy.n = ju; \
		} else { \
			seen.n = 1; \
			line++; \
			mvprintw(line, 0, "%12ju %12s %12s %s\n", \
			    ju, ".  ", ".  ", d); \
		} \
	}
#include "stat_field.h"
#undef MAC_STAT
		lt = tt;
		refresh();
		timeout(delay * 1000);
		switch ((ch = getch())) {
		case ERR:
			break;
#ifdef KEY_RESIZE
		case KEY_RESIZE:
			erase();
			break;
#endif
		case '\014': /* Ctrl-L */
		case '\024': /* Ctrl-T */
			redrawwin(stdscr);
			refresh();
			break;
		case '\003': /* Ctrl-C */
			raise(SIGINT);
			break;
		case '\032': /* Ctrl-Z */
			raise(SIGTSTP);
			break;
		case '\021': /* Ctrl-Q */
		case 'Q':
		case 'q':
			endwin();
			exit(0);
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			delay = 1 << (ch - '0');
			break;
		default:
			beep();
			break;
		}
	}
}

static void
do_xml(struct varnish_stats *VSL_stats, const char* fields)
{
	char time_stamp[20];
	time_t now;

	printf("<?xml version=\"1.0\"?>\n");
	now = time(NULL);
	strftime(time_stamp, 20, "%Y-%m-%dT%H:%M:%S", localtime(&now));
	printf("<varnishstat timestamp=\"%s\">\n", time_stamp);
#define MAC_STAT(n, t, l, f, d) \
	do { \
		if (fields != NULL && ! show_field( #n, fields )) break; \
		intmax_t ju = VSL_stats->n; \
		printf("\t<stat>\n"); \
		printf("\t\t<name>%s</name>\n", #n); \
		printf("\t\t<value>%ju</value>\n", ju); \
		printf("\t\t<description>%s</description>\n", d); \
		printf("\t</stat>\n"); \
	} while (0);
#include "stat_field.h"
#undef MAC_STAT
	printf("</varnishstat>\n");
}

static void
do_once(struct varnish_stats *VSL_stats, const char* fields)
{
	struct timeval tv;
	double up;

	gettimeofday(&tv, NULL);
	up = VSL_stats->uptime;

#define MAC_STAT(n, t, l, f, d) \
	do { \
		if (fields != NULL && ! show_field( #n, fields )) break; \
		intmax_t ju = VSL_stats->n; \
		if (f == 'a') \
			printf("%-16s %12ju %12.2f %s\n", #n, ju, ju / up, d); \
		else \
			printf("%-16s %12ju %12s %s\n", #n, ju, ".  ", d); \
	} while (0);
#include "stat_field.h"
#undef MAC_STAT
}

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

#define MAC_STAT(n, t, l, f, d) \
	do { \
		fprintf(stderr, "%-20s %s\n", #n, d);\
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
			fputs("The field '", stderr);
			fwrite(field_start, 1, field_length, stderr);
			fputs("' is not a valid field\n", stderr);
			return (0);
		}

		if (field_end == NULL || *field_end == '\0')
			break;
	}

	return (1);
}

int
main(int argc, char **argv)
{
	int c;
	struct varnish_stats *VSL_stats;
	int delay = 1, once = 0, xml = 0;
	const char *n_arg = NULL;
	const char *fields = NULL;

	while ((c = getopt(argc, argv, "1f:ln:Vw:x")) != -1) {
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
		case 'n':
			n_arg = optarg;
			break;
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
			usage();
		}
	}

	if ((VSL_stats = VSL_OpenStats(n_arg)) == NULL)
		exit(1);

	if (fields != NULL && !valid_fields(fields)) {
		usage();
		exit(1);
	}

	if (xml)
		do_xml(VSL_stats, fields);
	else if (once)
		do_once(VSL_stats, fields);
	else
		do_curses(VSL_stats, delay, fields);

	exit(0);
}
