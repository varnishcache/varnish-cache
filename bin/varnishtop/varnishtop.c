/*
 * $Id$
 *
 * Log tailer for Varnish
 */

#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compat/vis.h"

#include "vsb.h"

#include "libvarnish.h"
#include "shmlog.h"
#include "queue.h"
#include "varnishapi.h"

struct top {
	unsigned char		rec[4 + 255];
	unsigned		clen;
	unsigned		hash;
	TAILQ_ENTRY(top)	list;
	double			count;
};

static TAILQ_HEAD(tophead, top) top_head = TAILQ_HEAD_INITIALIZER(top_head);

static unsigned ntop;

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishtop %s [-1V]\n", VSL_USAGE);
	exit(1);
}

static void
upd(void)
{
	struct top *tp, *tp2;
	int l;
	double t = 0;
	unsigned u = 0;
	static time_t last;
	time_t now;

	now = time(NULL);
	if (now == last)
		return;
	last = now;

	erase();
	l = 0;
	mvprintw(0, 0, "list length %u\n", ntop);
	TAILQ_FOREACH_SAFE(tp, &top_head, list, tp2) {
		if (++l < LINES) {
			printw("%10.2f %*.*s\n",
			    tp->count, tp->rec[1], tp->rec[1], tp->rec + 4);
			t = tp->count;
		}
		tp->count *= .999;
		if (tp->count * 10 < t || l > LINES * 10) {
			TAILQ_REMOVE(&top_head, tp, list);
			free(tp);
			ntop--;
			u++;
		}
	}
	mvprintw(0, 40, "cleaned %u\n", u);
	refresh();
}

int
main(int argc, char **argv)
{
	int i, c;
	unsigned char *p, *q;
	struct VSL_data *vd;
	unsigned u, v;
	struct top *tp, *tp2;
	int f_flag = 0;


	vd = VSL_New();
	
	while ((c = getopt(argc, argv, VSL_ARGS "1fV")) != -1) {
		i = VSL_Arg(vd, c, optarg);
		if (i < 0)
			exit (1);
		if (i > 0)
			continue;
		switch (c) {
		case '1':
			VSL_NonBlocking(vd, 1);
			break;
		case 'f':
			f_flag = 1;
			break;
		case 'V':
			varnish_version("varnishtop");
			exit(0);
		default:
			usage();
		}
	}

	if (VSL_OpenLog(vd))
		exit (1);

	initscr();
	v = 0;
	while (1) {
		i = VSL_NextLog(vd, &p);
		if (i < 0)
			break;
		if (i == 0) {
			upd();
			usleep(50000);
			continue;
		}
		if (++v > 100) {
			upd();
			v = 0;
		}
		u = 0;
		q = p + 4;
		for (i = 0; i < p[1]; i++, q++) {
			if (f_flag && (*q == ':' || isspace(*q)))
				break;
			u += *q;
		}
		TAILQ_FOREACH(tp, &top_head, list) {
			if (tp->hash != u)
				continue;
			if (tp->rec[0] != p[0])
				continue;
			if (tp->clen != q - p)
				continue;
			if (memcmp(p + 4, tp->rec + 4, q - (p + 4)))
				continue;
			tp->count += 1.0;
			break;
		}
		if (tp == NULL) {
			ntop++;
			tp = calloc(sizeof *tp, 1);
			assert(tp != NULL);
			tp->hash = u;
			tp->count = 1.0;
			tp->clen = q - p;
			TAILQ_INSERT_TAIL(&top_head, tp, list);
		} 
		memcpy(tp->rec, p, 4 + p[1]);
		while (1) {
			tp2 = TAILQ_PREV(tp, tophead, list);
			if (tp2 == NULL || tp2->count >= tp->count)
				break;
			TAILQ_REMOVE(&top_head, tp2, list);
			TAILQ_INSERT_AFTER(&top_head, tp, tp2, list);
		}
		while (1) {
			tp2 = TAILQ_NEXT(tp, list);
			if (tp2 == NULL || tp2->count <= tp->count)
				break;
			TAILQ_REMOVE(&top_head, tp2, list);
			TAILQ_INSERT_BEFORE(tp, tp2, list);
		}
	}
	return (0);
}
