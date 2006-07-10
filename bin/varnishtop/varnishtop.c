/*
 * $Id: varnishlog.c 412 2006-07-10 20:27:52Z phk $
 *
 * Log tailer for Varnish
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <curses.h>
#include <sbuf.h>
#include <vis.h>

#include "shmlog.h"
#include "queue.h"
#include "varnishapi.h"

struct top {
	unsigned char		*rec;
	unsigned		hash;
	TAILQ_ENTRY(top)	list;
	double			count;
};

static TAILQ_HEAD(tophead, top) top_head = TAILQ_HEAD_INITIALIZER(top_head);

static unsigned ntop;

/*--------------------------------------------------------------------*/

static void
Usage(void)
{
	fprintf(stderr, "Usage: varnishtop\n");
	exit(2);
}

static void
upd(void)
{
	struct top *tp, *tp2;
	int l;
	double t = 0;
	unsigned u = 0;

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
	unsigned char *p;
	struct VSL_data *vd;
	unsigned u, v;
	struct top *tp, *tp2;

	vd = VSL_New();
	
	while ((c = getopt(argc, argv, VSL_ARGS "how:")) != -1) {
		i = VSL_Arg(vd, c, optarg);
		if (i < 0)
			exit (1);
		if (i > 0)
			continue;
		switch (c) {
		default:
			Usage();
		}
	}

	if (VSL_OpenLog(vd))
		exit (1);

	initscr();
	v = 0;
	while (1) {
		p = VSL_NextLog(vd);
		if (p == NULL) {
			upd();
			usleep(50000);
			continue;
		}
		if (++v > 100) {
			upd();
			v = 0;
		}
		u = 0;
		for (i = 4; i < 4 + p[1]; i++)
			u += p[i];
		TAILQ_FOREACH(tp, &top_head, list) {
			if (tp->hash != u)
				continue;
			if (tp->rec[0] != p[0])
				continue;
			if (tp->rec[1] != p[1])
				continue;
			if (memcmp(p + 4, tp->rec + 4, p[1]))
				continue;
			tp->count += 1.0;
			break;
		}
		if (tp == NULL) {
			ntop++;
			tp = calloc(sizeof *tp + 4 + p[1], 1);
			assert(tp != NULL);
			tp->hash = u;
			tp->rec = (void *)(tp + 1);
			memcpy(tp->rec, p, 4 + p[1]);
			tp->count = 1.0;
			TAILQ_INSERT_TAIL(&top_head, tp, list);
		}
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
