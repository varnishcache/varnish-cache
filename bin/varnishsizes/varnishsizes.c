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

#include <sys/types.h>
#include <curses.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libvarnish.h"
#include "vsl.h"
#include "varnishapi.h"

#define HIST_N 2000 /* how far back we remember */
#define HIST_LOW 1 /* low end of log range */
#define HIST_HIGH 8 /* high end of log range */
#define HIST_RANGE (HIST_HIGH - HIST_LOW)
#define HIST_RES 100 /* bucket resolution */
#define HIST_BUCKETS (HIST_RANGE * HIST_RES)

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static int delay = 1;
static unsigned rr_hist[HIST_N];
static unsigned nhist;
static unsigned next_hist;
static unsigned bucket_miss[HIST_BUCKETS];
static unsigned bucket_hit[HIST_BUCKETS];
static unsigned char hh[FD_SETSIZE];

static double log_ten;

static int scales[] = {
	1,
	2,
	3,
	4,
	5,
	10,
	15,
	20,
	25,
	50,
	100,
	250,
	500,
	1000,
	2500,
	5000,
	10000,
	25000,
	50000,
	100000,
	INT_MAX
};

static void
update(struct VSM_data *vd)
{
	int w = COLS / HIST_RANGE;
	int n = w * HIST_RANGE;
	unsigned bm[n], bh[n];
	unsigned max;
	int i, j, scale;

	erase();

	/* Draw horizontal axis */
	w = COLS / HIST_RANGE;
	n = w * HIST_RANGE;
	for (i = 0; i < n; ++i)
		(void)mvaddch(LINES - 2, i, '-');
	for (i = 0, j = HIST_LOW; i < HIST_RANGE; ++i, ++j) {
		(void)mvaddch(LINES - 2, w * i, '+');
		mvprintw(LINES - 1, w * i, "|1e%d", j);
	}

	mvprintw(0, 0, "%*s", COLS - 1, VSM_Name(vd));

	/* count our flock */
	for (i = 0; i < n; ++i)
		bm[i] = bh[i] = 0;
	for (i = 0, max = 1; i < HIST_BUCKETS; ++i) {
		j = i * n / HIST_BUCKETS;
		bm[j] += bucket_miss[i];
		bh[j] += bucket_hit[i];
		if (bm[j] + bh[j] > max)
			max = bm[j] + bh[j];
	}

	/* scale */
	for (i = 0; max / scales[i] > LINES - 3; ++i)
		/* nothing */ ;
	scale = scales[i];

	mvprintw(0, 0, "1:%d, n = %d", scale, nhist);

	/* show them */
	for (i = 0; i < n; ++i) {
		for (j = 0; j < bm[i] / scale; ++j)
			(void)mvaddch(LINES - 3 - j, i, '#');
		for (; j < (bm[i] + bh[i]) / scale; ++j)
			(void)mvaddch(LINES - 3 - j, i, '|');
	}

	refresh();
}

static int
h_hist(void *priv, enum vsl_tag tag, unsigned fd, unsigned len,
    unsigned spec, const char *ptr)
{
	double b;
	int i, j, tmp;

	(void)priv;
	(void)len;
	(void)spec;

	if (fd >= FD_SETSIZE)
		/* oops */
		return (0);

	if (tag == SLT_Hit) {
		hh[fd] = 1;
		return (0);
	}
	if (tag != SLT_Length)
		return (0);

	/* determine processing time */
	i = sscanf(ptr, "%d", &tmp);
	assert(i == 1);

	/* Typically 304s and tend to throw the scaling off */
	if (tmp == 0)
		return 0;

	b = tmp;
	/* select bucket */
	i = HIST_RES * (log(b) / log_ten);
	if (i < HIST_LOW * HIST_RES)
		i = HIST_LOW * HIST_RES;
	if (i >= HIST_HIGH * HIST_RES)
		i = HIST_HIGH * HIST_RES - 1;
	i -= HIST_LOW * HIST_RES;
	assert(i >= 0);
	assert(i < HIST_BUCKETS);

	pthread_mutex_lock(&mtx);

	/* phase out old data */
	if (nhist == HIST_N) {
		j = rr_hist[next_hist];
		if (j < 0)  {
			assert(bucket_miss[-j] > 0);
			bucket_miss[-j]--;
		} else {
			assert(bucket_hit[j] > 0);
			bucket_hit[j]--;
		}
	} else {
		++nhist;
	}

	/* phase in new data */
	if (hh[fd] || i == 0) {
		bucket_hit[i]++;
		rr_hist[next_hist] = i;
	} else {
		bucket_miss[i]++;
		rr_hist[next_hist] = -i;
	}
	if (++next_hist == HIST_N) {
		next_hist = 0;
	}
	hh[fd] = 0;

	pthread_mutex_unlock(&mtx);

	return (0);
}

static void *
accumulate_thread(void *arg)
{
	struct VSM_data *vd = arg;
	int i;

	for (;;) {
		i = VSL_Dispatch(vd, h_hist, NULL);
		if (i < 0)
			break;
		if (i == 0)
			usleep(50000);
	}
	return (arg);
}

static void
do_curses(struct VSM_data *vd)
{
	pthread_t thr;
	int ch;

	if (pthread_create(&thr, NULL, accumulate_thread, vd) != 0) {
		fprintf(stderr, "pthread_create(): %s\n", strerror(errno));
		exit(1);
	}

	initscr();
	raw();
	noecho();
	nonl();
	intrflush(stdscr, FALSE);
	curs_set(0);
	erase();
	for (;;) {
		pthread_mutex_lock(&mtx);
		update(vd);
		pthread_mutex_unlock(&mtx);

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
			endwin();
			raise(SIGTSTP);
			break;
		case '\021': /* Ctrl-Q */
		case 'Q':
		case 'q':
			endwin();
			return;
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

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishsizes "
	    "%s [-n varnish_name] [-V] [-w delay]\n", VSL_USAGE);
	exit(1);
}

int
main(int argc, char **argv)
{
	int o;
	struct VSM_data *vd;

	vd = VSM_New();
	VSL_Setup(vd);

	while ((o = getopt(argc, argv, VSL_ARGS "Vw:")) != -1) {
		switch (o) {
		case 'V':
			varnish_version("varnishsizes");
			exit(0);
		case 'w':
			delay = atoi(optarg);
			break;
		default:
			if (VSL_Arg(vd, o, optarg) > 0)
				break;
			usage();
		}
	}

	if (VSL_Open(vd, 1))
		exit(1);

	log_ten = log(10.0);

	do_curses(vd);
	exit(0);
}
