/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 *
 * $Id$
 *
 * Log tailer for Varnish
 */

#include <curses.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef HAVE_CLOCK_GETTIME
#include "compat/clock_gettime.h"
#endif

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"

static void
myexp(double *acc, double val, unsigned *n, unsigned nmax)
{

	if (*n < nmax)
		(*n)++;
	(*acc) += (val - *acc) / (double)*n;
}

static void
do_curses(struct varnish_stats *VSL_stats, int delay)
{
	struct varnish_stats copy;
	intmax_t ju;
	struct timespec ts;
	double tt, lt, hit, miss, ratio, up;
	double a1, a2, a3;
	unsigned n1, n2, n3;
	time_t rt;
	int ch, line;

	memset(&copy, 0, sizeof copy);

	a1 = a2 = a3 = 0.0;
	n1 = n2 = n3 = 0;

	initscr();
	raw();
	noecho();
	nonl();
	intrflush(stdscr, false);
	erase();

	lt = 0;
	while (1) {
		clock_gettime(CLOCK_REALTIME, &ts);
		tt = ts.tv_nsec * 1e-9 + ts.tv_sec;
		lt = tt - lt;

		rt = ts.tv_sec - VSL_stats->start_time;
		up = rt;

		move(0, 0);
		printw("%*s\n", COLS - 1, VSL_Name());
		move(0, 0);
		if (rt > 86400) {
			printw("%dd+", rt / 86400);
			rt %= 86400;
		}
		printw("%02d:", rt / 3600);
		rt %= 3600;
		printw("%02d:", rt / 60);
		rt %= 60;
		printw("%02d", rt);
		move(1, 0);
		hit = (intmax_t)VSL_stats->cache_hit -
		    (intmax_t)copy.cache_hit;
		miss = (intmax_t)VSL_stats->cache_miss -
		    (intmax_t)copy.cache_miss;
		hit /= lt;
		miss /= lt;
		if (hit + miss != 0) {
			ratio = hit / (hit + miss);
			myexp(&a1, ratio, &n1, 10);
			myexp(&a2, ratio, &n2, 100);
			myexp(&a3, ratio, &n3, 1000);
		}
		printw("Hitrate ratio: %8u %8u %8u\n", n1, n2, n3);
		printw("Hitrate avg:   %8.4f %8.4f %8.4f\n", a1, a2, a3);
		printw("\n");

		line = 0;
#define MAC_STAT(n, t, f, d) \
	if (++line < LINES - 4) { \
		ju = VSL_stats->n; \
		if (f == 'a') { \
			printw("%12ju %12.2f %12.2f %s\n", \
			    ju, (ju - (intmax_t)copy.n)/lt, ju / up, d); \
			copy.n = ju; \
		} else { \
			printw("%12ju %12s %12s %s\n", ju, ".  ", ".  ", d); \
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
		case KEY_RESIZE:
			erase();
			break;
		case '\014':
			redrawwin(stdscr);
			refresh();
			break;
		case '\003': /* Ctrl-C */
		case '\021': /* Ctrl-Q */
		case 'Q':
		case 'q':
			endwin();
			return;
		default:
			break;
		}
	}
}

static void
do_once(struct varnish_stats *VSL_stats)
{
	struct timespec ts;
	double up;

	clock_gettime(CLOCK_REALTIME, &ts);
	up = ts.tv_sec - VSL_stats->start_time;

#define MAC_STAT(n, t, f, d) \
	do { \
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
	fprintf(stderr, "usage: varnishstat [-1V] [-n varnish_name] [-w delay]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int c;
	struct varnish_stats *VSL_stats;
	int delay = 1, once = 0;
	const char *n_arg = NULL;

	while ((c = getopt(argc, argv, "1n:Vw:")) != -1) {
		switch (c) {
		case '1':
			once = 1;
			break;
		case 'n':
			n_arg = optarg;
			break;
		case 'V':
			varnish_version("varnishstat");
			exit(0);
		case 'w':
			delay = atoi(optarg);
			break;
		default:
			usage();
		}
	}

	if ((VSL_stats = VSL_OpenStats(n_arg)) == NULL)
		exit(1);

	if (once)
		do_once(VSL_stats);
	else
		do_curses(VSL_stats, delay);

	exit(0);
}
