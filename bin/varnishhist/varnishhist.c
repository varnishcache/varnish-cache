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
#include <math.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"

#define HIST_LOW -50
#define HIST_HIGH 25
#define HIST_W (1 + (HIST_HIGH - HIST_LOW))
#define HIST_N 2000

static int delay = 1;
static volatile sig_atomic_t redraw;
static char rr_hist[HIST_N];
static unsigned next_hist;
static unsigned bucket_miss[HIST_W];
static unsigned bucket_hit[HIST_W];
static unsigned char hh[65536];
static double scale = 10;
static double c_hist;

static void
sigalrm(int sig)
{

	(void)sig;
	redraw = 1;
}

static void
r_hist(void)
{
	int x, y;
	double m, r;

	m = 0;
	r = 0;
	for (x = 1; x < HIST_W; x++) {
		if (bucket_hit[x] + bucket_miss[x] > m)
			m = bucket_hit[x] + bucket_miss[x];
		r += bucket_hit[x];
		r += bucket_miss[x];
	}

	while (m > HIST_N / scale)
		scale--;

	mvprintw(0, 0, "Max %.0f Scale %.0f Tot: %.0f", m, HIST_N / scale, r);
	m = (HIST_N / scale) / (LINES - 3);
	move(1,0);
	for (y = LINES - 3; y > 0; y--) {
		if (y == 1)
			r = 0;
		else
			r = y * m;
		for (x = 0; x < HIST_W; x++) {
			if (bucket_miss[x] > r)
				addch('#');
			else if (bucket_hit[x] + bucket_miss[x] > r)
				addch('|');
			else
				addch(' ');
		}
		addch('\n');
	}
	refresh();
	redraw = 0;
	alarm(delay);
}

static int
h_hist(void *priv, enum shmlogtag tag, unsigned fd, unsigned len, unsigned spec, const char *ptr)
{
	double b;
	int i, j;

	(void)priv;
	(void)fd;
	(void)len;
	(void)spec;
	if (tag == SLT_Hit) {
		hh[fd] = 1;
		return (0);
	}
	if (tag != SLT_ReqEnd)
		return (0);
#if 1
	i = sscanf(ptr, "%*d %*f %*f %*f %lf", &b);
#else
	i = sscanf(ptr, "%*d %*f %*f %lf", &b);
#endif
	assert(i == 1);
	i = log(b) * c_hist;
	if (i < HIST_LOW)
		i = HIST_LOW;
	if (i > HIST_HIGH)
		i = HIST_HIGH;
	i -= HIST_LOW;
	assert(i < HIST_W);

	j = rr_hist[next_hist];
	if (j < 0)  {
		assert(bucket_miss[-j] > 0);
		bucket_miss[-j]--;
	} else {
		assert(bucket_hit[j] > 0);
		bucket_hit[j]--;
	}

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
	if (redraw)
		r_hist();
	return (0);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr,
	    "usage: varnishhist %s [-n varnish_name] [-V] [-w delay]\n", VSL_USAGE);
	exit(1);
}

int
main(int argc, char **argv)
{
	int i, c, x;
	struct VSL_data *vd;
	char *n_arg = NULL;

	vd = VSL_New();

	while ((c = getopt(argc, argv, VSL_ARGS "n:Vw:")) != -1) {
		switch (c) {
		case 'n':
			n_arg = optarg;
			break;
		case 'V':
			varnish_version("varnishhist");
			exit(0);
		case 'w':
			delay = atoi(optarg);
			break;
		default:
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (n_arg == NULL) {
		n_arg = malloc(HOST_NAME_MAX+1);
		gethostname(n_arg, HOST_NAME_MAX+1);
	}
	
	if (VSL_OpenLog(vd, n_arg))
		exit (1);

	c_hist = 10.0 / log(10.0);
	initscr();
	erase();

	bucket_hit[0] = HIST_N;
	move(LINES - 2, 0);
	for (x = 0; x < HIST_W; x++)
		addch('-');

	for (x = 0; x < HIST_W; x++) {
		if ((x + HIST_LOW) % 10 != 0)
			continue;
		mvprintw(LINES - 2, x, "+");
		mvprintw(LINES - 1, x, "|1e%d", (x + HIST_LOW) / 10);
	}

	signal(SIGALRM, sigalrm);
	redraw = 1;
	while (1) {
		i = VSL_Dispatch(vd, h_hist, NULL);
		if (i < 0)
			break;
	}

	exit(0);
}
