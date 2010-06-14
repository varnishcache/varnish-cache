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

#include <curses.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libvarnish.h"
#include "vsc.h"
#include "vqueue.h"
#include "varnishapi.h"
#include "varnishstat.h"

#define AC(x) assert((x) != ERR)

struct pt {
	VTAILQ_ENTRY(pt)	next;
	const volatile uint64_t	*ptr;
	uint64_t		ref;
	int			type;
	char			seen;
	char			*name;
};

static VTAILQ_HEAD(, pt) pthead = VTAILQ_HEAD_INITIALIZER(pthead);

static int
do_curses_cb(void *priv, const struct vsc_point * const sp)
{
	struct pt *pt;
	char buf[128];

	(void)priv;
	assert(!strcmp(sp->fmt, "uint64_t"));

	pt = calloc(sizeof *pt, 1);
	AN(pt);
	VTAILQ_INSERT_TAIL(&pthead, pt, next);

	pt->ptr = sp->ptr;
	pt->ref = *pt->ptr;
	pt->type = sp->flag;

	*buf = '\0';
	if (strcmp(sp->class, "")) {
		strcat(buf, sp->class);
		strcat(buf, ".");
	}
	if (strcmp(sp->ident, "")) {
		strcat(buf, sp->ident);
		strcat(buf, ".");
	}
	strcat(buf, sp->name);
	strcat(buf, " - ");
	strcat(buf, sp->desc);
	pt->name = strdup(buf);
	AN(pt->name);
	return (0);
}

static void
prep_pts(const struct VSM_data *vd)
{
	struct pt *pt, *pt2;

	VTAILQ_FOREACH_SAFE(pt, &pthead, next, pt2) {
		VTAILQ_REMOVE(&pthead, pt, next);
		free(pt->name);
		free(pt);
	}

	(void)VSC_Iter(vd, do_curses_cb, NULL);
}

static void
myexp(double *acc, double val, unsigned *n, unsigned nmax)
{

	if (*n < nmax)
		(*n)++;
	(*acc) += (val - *acc) / (double)*n;
}

void
do_curses(struct VSM_data *vd, const struct vsc_main *VSL_stats,
    int delay)
{
	intmax_t ju;
	struct timeval tv;
	double tt, lt, lhit, hit, lmiss, miss, hr, mr, ratio, up;
	double a1, a2, a3;
	unsigned n1, n2, n3;
	time_t rt;
	int ch, line;
	struct pt *pt;
	double act, lact;

	(void)initscr();
	AC(raw());
	AC(noecho());
	AC(nonl());
	AC(intrflush(stdscr, FALSE));
	(void)curs_set(0);	/* XXX: too many implementations are bogus */

	while (1) {
		/*
		 * Initialization goes in outher loop
		 */
		prep_pts(vd);
		AC(erase());
		AC(refresh());

		a1 = a2 = a3 = 0.0;
		n1 = n2 = n3 = 0;
		lt = 0;
		lhit = 0;
		lmiss = 0;
		lact = 0;

		while (1) {
			/*
			 * Break to outher loop if we need to re-read file.
			 * Only check if it looks like nothing is happening.
			 */
			act = VSL_stats->cache_hit + VSL_stats->cache_miss + 1;
			if (act == lact && VSM_ReOpen(vd, 1))
				break;
			lact = act;

			AZ(gettimeofday(&tv, NULL));
			tt = tv.tv_usec * 1e-6 + tv.tv_sec;
			lt = tt - lt;

			rt = VSL_stats->uptime;
			up = rt;

			AC(mvprintw(0, 0, "%*s", COLS - 1, VSM_Name(vd)));
			AC(mvprintw(0, 0, "%d+%02d:%02d:%02d", rt / 86400,
			    (rt % 86400) / 3600, (rt % 3600) / 60, rt % 60));

			hit = VSL_stats->cache_hit;
			miss = VSL_stats->cache_miss;
			hr = (hit - lhit) / lt;
			mr = (miss - lmiss) / lt;
			lhit = hit;
			lmiss = miss;
			if (hr + mr != 0) {
				ratio = hr / (hr + mr);
				myexp(&a1, ratio, &n1, 10);
				myexp(&a2, ratio, &n2, 100);
				myexp(&a3, ratio, &n3, 1000);
			}
			AC(mvprintw(1, 0, "Hitrate ratio: %8u %8u %8u",
			    n1, n2, n3));
			AC(mvprintw(2, 0, "Hitrate avg:   %8.4f %8.4f %8.4f",
			    a1, a2, a3));

			line = 3;
			VTAILQ_FOREACH(pt, &pthead, next) {
				if (line >= LINES)
					break;
				ju = *pt->ptr;
				if (ju == 0 && !pt->seen)
					continue;
				pt->seen = 1;
				line++;
				if (pt->type == 'a') {
					AC(mvprintw(line, 0,
					    "%12ju %12.2f %12.2f %s\n",
					    ju, (ju - (intmax_t)pt->ref)/lt,
					    ju / up, pt->name));
					pt->ref = ju;
				} else {
					AC(mvprintw(line, 0,
					    "%12ju %12s %12s %s\n",
					    ju, ".  ", ".  ", pt->name));
				}
			}
			lt = tt;
			AC(refresh());
			timeout(delay * 1000);
			switch ((ch = getch())) {
			case ERR:
				break;
#ifdef KEY_RESIZE
			case KEY_RESIZE:
				AC(erase());
				break;
#endif
			case '\014': /* Ctrl-L */
			case '\024': /* Ctrl-T */
				AC(redrawwin(stdscr));
				AC(refresh());
				break;
			case '\003': /* Ctrl-C */
				AZ(raise(SIGINT));
				break;
			case '\032': /* Ctrl-Z */
				AZ(raise(SIGTSTP));
				break;
			case '\021': /* Ctrl-Q */
			case 'Q':
			case 'q':
				AC(endwin());
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
				delay = 1U << (ch - '0');
				break;
			default:
				AC(beep());
				break;
			}
		}
	}
}
