/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <sys/types.h>

#include <curses.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <regex.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vas.h"
#include "vcs.h"

#define HIST_N 2000 /* how far back we remember */
#define HIST_RES 100 /* bucket resolution */

static int hist_low;
static int hist_high;
static int hist_range;
static int hist_buckets;

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static int delay = 1;
static unsigned rr_hist[HIST_N];
static unsigned nhist;
static unsigned next_hist;
static unsigned *bucket_miss;
static unsigned *bucket_hit;
static unsigned char hh[FD_SETSIZE];
static uint64_t bitmap[FD_SETSIZE];
static double  values[FD_SETSIZE];
static char *format;
static int match_tag;

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

struct profile {
	const char *name;
	enum VSL_tag_e tag;
	int field;
	int hist_low;
	int hist_high;
} profiles[] = {
	{ .name = "responsetime", .tag = SLT_ReqEnd, .field = 5, .hist_low = -6, .hist_high = 3 },
	{ .name = "size", .tag = SLT_Length, .field = 1, .hist_low = 1, .hist_high = 8 },
	{ 0 }
};

static struct profile *active_profile;

static void
update(struct VSM_data *vd)
{
	int w = COLS / hist_range;
	int n = w * hist_range;
	unsigned bm[n], bh[n];
	unsigned max;
	int i, j, scale;

	erase();

	/* Draw horizontal axis */
	w = COLS / hist_range;
	n = w * hist_range;
	for (i = 0; i < n; ++i)
		(void)mvaddch(LINES - 2, i, '-');
	for (i = 0, j = hist_low; i < hist_range; ++i, ++j) {
		(void)mvaddch(LINES - 2, w * i, '+');
		mvprintw(LINES - 1, w * i, "|1e%d", j);
	}

	mvprintw(0, 0, "%*s", COLS - 1, VSM_Name(vd));

	/* count our flock */
	for (i = 0; i < n; ++i)
		bm[i] = bh[i] = 0;
	for (i = 0, max = 1; i < hist_buckets; ++i) {
		j = i * n / hist_buckets;
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
h_hist(void *priv, enum VSL_tag_e tag, unsigned fd, unsigned len,
    unsigned spec, const char *ptr, uint64_t bm)
{
	int i, j;
	struct VSM_data *vd = priv;
	(void)spec;

	if (fd >= FD_SETSIZE)
		/* oops */
		return (0);

	bitmap[fd] |= bm;

	if (tag == SLT_Hit) {
		hh[fd] = 1;
		return (0);
	}
	if (tag == match_tag) {
		char buf[1024]; /* size? */
		assert(len < sizeof(buf));
		memcpy(buf, ptr, len);
		buf[len] = '\0';
		i = sscanf(buf, format, &values[fd]);
		assert(i == 1);
	}

	if (tag != SLT_ReqEnd)
		return (0);

	if (!VSL_Matched(vd, bitmap[fd])) {
		bitmap[fd] = 0;
		hh[fd] = 0;
		return (0);
	}

	/* select bucket */
	i = HIST_RES * (log(values[fd]) / log_ten);
	if (i < hist_low * HIST_RES)
		i = hist_low * HIST_RES;
	if (i >= hist_high * HIST_RES)
		i = hist_high * HIST_RES - 1;
	i -= hist_low * HIST_RES;
	assert(i >= 0);
	assert(i < hist_buckets);
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
	bitmap[fd] = 0;

	pthread_mutex_unlock(&mtx);

	return (0);
}

static void *
accumulate_thread(void *arg)
{
	struct VSM_data *vd = arg;
	int i;

	for (;;) {
		i = VSL_Dispatch(vd, h_hist, vd);
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
	fprintf(stderr, "usage: varnishhist "
	    "%s [-p profile] [-f field_num] [-R max] [-r min] [-V] [-w delay]\n", VSL_USAGE);
	exit(1);
}

int
main(int argc, char **argv)
{
	int o, i;
	struct VSM_data *vd;
	const char *profile = "responsetime";
	int fnum = -1;
	hist_low = -1;
	hist_high = -1;
	match_tag = -1;

	vd = VSM_New();

	while ((o = getopt(argc, argv, VSL_ARGS "Vw:r:R:f:p:")) != -1) {
		switch (o) {
		case 'V':
			VCS_Message("varnishhist");
			exit(0);
		case 'i':
			match_tag = VSL_Name2Tag(optarg, -1);
			if (match_tag < 0) {
				fprintf(stderr, "No such tag %s\n", optarg);
				exit(1);
			}
			break;
		case 'w':
			delay = atoi(optarg);
			break;
		case 'f':
			fnum = atoi(optarg);
			break;
		case 'R':
			hist_high = atoi(optarg);
			break;
		case 'r':
			hist_low = atoi(optarg);
			break;
		case 'p':
			profile = optarg;
			break;
		default:
			if (VSL_Arg(vd, o, optarg) > 0)
				break;
			usage();
		}
	}
	if (profile) {
		for (active_profile = profiles; active_profile->name; active_profile++) {
			if (strcmp(active_profile->name, profile) == 0) {
				break;
			}
		}
	}
	if (! active_profile->name) {
		fprintf(stderr, "No such profile %s\n", profile);
		exit(1);
	}
	if (match_tag < 0) {
		match_tag = active_profile->tag;
	}

	if (fnum < 0) {
		fnum = active_profile->field;
	}

	if (hist_low < 0) {
		hist_low = active_profile->hist_low;
	}

	if (hist_high < 0) {
		hist_high = active_profile->hist_high;
	}

	hist_range = hist_high - hist_low;
	hist_buckets = hist_range * HIST_RES;
	bucket_hit = calloc(sizeof bucket_hit, hist_buckets);
	bucket_miss = calloc(sizeof bucket_miss, hist_buckets);

	format = malloc(4 * fnum);
	for (i = 0; i < fnum-1; i++) {
		strcpy(format + 4*i, "%*s ");
	}
	strcpy(format + 4*(fnum-1), "%lf");

	if (VSM_Open(vd)) {
		fprintf(stderr, "%s\n", VSM_Error(vd));
		exit(1);
	}

	log_ten = log(10.0);

	do_curses(vd);
	exit(0);
}
