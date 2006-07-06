/*
 * $Id: varnishlog.c 153 2006-04-25 08:17:43Z phk $
 *
 * Log tailer for Varnish
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <curses.h>
#include <time.h>

#include "shmlog.h"
#include "varnishapi.h"

static void
myexp(double *acc, double val, unsigned *n, unsigned nmax)
{

	if (*n < nmax)
		(*n)++;
	(*acc) += (val - *acc) / (double)*n;
}

int
main(int argc, char **argv)
{
	int c;
	struct shmloghead *lh;
	struct varnish_stats *VSL_stats, copy;
	int c_flag = 0;
	intmax_t ju;
	struct timespec ts;
	double tt, lt, hit, miss, ratio;
	double a1, a2, a3;
	unsigned n1, n2, n3;

	a1 = a2 = a3 = 0;
	n1 = n2 = n3 = 0;

	lh = VSL_OpenLog();

	VSL_stats = &lh->stats;

	while ((c = getopt(argc, argv, "c")) != -1) {
		switch (c) {
		case 'c':
			c_flag = 1;
			break;
		default:
			fprintf(stderr, "Usage:  varnishstat [-c]\n");
			exit (2);
		}
	}

	if (c_flag) {
		memset(&copy, 0, sizeof copy);
		initscr();
		erase();

		while (1) {
			clock_gettime(CLOCK_MONOTONIC, &ts);
			tt = ts.tv_nsec * 1e-9 + ts.tv_sec;
			lt = tt - lt;
			move(0,0);
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

#define MAC_STAT(n,t,f,d) \
			ju = VSL_stats->n; \
			printw("%12ju  %10.2f " d "\n", ju, (ju - (intmax_t)copy.n)/lt); \
			copy.n = ju;
#include "stat_field.h"
#undef MAC_STAT
			lt = tt;
			refresh();
			sleep(1);
		}
	} else {

#define MAC_STAT(n,t,f,d) \
		printf("%12ju  " d "\n", (VSL_stats->n));
#include "stat_field.h"
#undef MAC_STAT
	}

	exit (0);

}
