/*
 * $Id: varnishlog.c 833 2006-08-18 20:07:37Z phk $
 *
 * Log tailer for Varnish
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <regex.h>
#include <math.h>
#include <curses.h>

#include "compat/vis.h"

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"

#define HIST_LOW -55
#define HIST_HIGH 20
#define HIST_W (1 + (HIST_HIGH - HIST_LOW))
#define HIST_N 1000

static unsigned char rr_hist[HIST_N];
static unsigned next_hist;
static unsigned bucket_hist[HIST_W];
static double c_hist;

static void
r_hist(void)
{
	int x, y;
	double m, r;

	m = 0;
	r = 0;
	for (x = 0; x < HIST_W; x++) {
		if (bucket_hist[x] > m)
			m = bucket_hist[x];
		r += bucket_hist[x];
	}

	mvprintw(0, 0, "Max %.0f Scale %u Tot: %.0f", m, HIST_N, r);
	m = HIST_N / (LINES - 3);
	move(1,0);
	for (y = LINES - 3; y > 0; y--) {
		if (y == 1)
			r = 0;
		else
			r = y * m;
		for (x = 0; x < HIST_W; x++)
			addch(bucket_hist[x] > r ? '#' : ' ');
		addch('\n');
	}
	refresh();
}

static int 
h_hist(void *priv, unsigned tag, unsigned fd, unsigned len, unsigned spec, const char *ptr)
{
	double b;
	int i;
	
	(void)priv;
	(void)fd;
	(void)len;
	(void)spec;
	if (tag != SLT_ReqEnd)
		return (0);
	i = sscanf(ptr, "%*d %*f %*f %lf", &b);
	assert(i == 1);
	i = log(b) * c_hist;
	if (i < HIST_LOW)
		i = HIST_LOW;
	if (i > HIST_HIGH)
		i = HIST_HIGH;
	i -= HIST_LOW;
	bucket_hist[rr_hist[next_hist]]--;
	rr_hist[next_hist] = i;
	bucket_hist[rr_hist[next_hist]]++;
	if (++next_hist == HIST_N) {
		next_hist = 0;
	}
	if (!(next_hist % 100))
		r_hist();
	return (0);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr,
	    "usage: varnishhist");
	exit(1);
}

int
main(int argc, char **argv)
{
	int i, c, x;
	struct VSL_data *vd;

	vd = VSL_New();
	
	while ((c = getopt(argc, argv, VSL_ARGS)) != -1) {
		switch (c) {
		default:
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (VSL_OpenLog(vd))
		exit (1);

	c_hist = 10.0 / log(10.0);
	initscr();
	erase();

	bucket_hist[0] = HIST_N;
	move(LINES - 2, 0);
	for (x = 0; x < HIST_W; x++)
		addch('-');

	for (x = 0; x < HIST_W; x++) {
		if ((x + HIST_LOW) % 10 != 0)
			continue;
		mvprintw(LINES - 2, x, "+");
		mvprintw(LINES - 1, x, "|1e%d", (x + HIST_LOW) / 10);
	}

	while (1) {
		i = VSL_Dispatch(vd, h_hist, NULL);
		if (i < 0)
			break;
		if (i == 0)
			r_hist();
	} 

	return (0);
}
