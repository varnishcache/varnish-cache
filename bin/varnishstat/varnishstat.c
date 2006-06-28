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

#include "shmlog.h"
#include "varnishapi.h"

int
main(int argc, char **argv)
{
	int c;
	struct shmloghead *lh;
	struct varnish_stats *VSL_stats;
	int c_flag = 0;

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
		initscr();
		erase();

		while (1) {
			move(0,0);
#define MAC_STAT(n,t,f,d) \
			printw("%12ju  " d "\n", (VSL_stats->n));
#include "stat_field.h"
#undef MAC_STAT
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
