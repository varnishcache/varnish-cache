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

#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "vqueue.h"

#include "vsb.h"

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"

struct top {
	unsigned char		rec[4];
	unsigned char		*rec_data;
	unsigned		clen;
	unsigned		hash;
	VTAILQ_ENTRY(top)	list;
	double			count;
};

static VTAILQ_HEAD(tophead, top) top_head = VTAILQ_HEAD_INITIALIZER(top_head);

static unsigned ntop;

/*--------------------------------------------------------------------*/

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static int f_flag = 0;

static unsigned maxfieldlen = 0;

static void
accumulate(const unsigned char *p)
{
	struct top *tp, *tp2;
	const unsigned char *q;
	unsigned int u, l;
	int i;

	// fprintf(stderr, "%*.*s\n", p[1], p[1], p + 4);

	u = 0;
	q = p + SHMLOG_DATA;
	l = SHMLOG_LEN(p);
	for (i = 0; i < l; i++, q++) {
		if (f_flag && (*q == ':' || isspace(*q)))
			break;
		u += *q;
	}

	VTAILQ_FOREACH(tp, &top_head, list) {
		if (tp->hash != u)
			continue;
		if (tp->rec[SHMLOG_TAG] != p[SHMLOG_TAG])
			continue;
		if (tp->clen != q - p)
			continue;
		if (memcmp(p + SHMLOG_DATA, tp->rec_data,
		    q - (p + SHMLOG_DATA)))
			continue;
		tp->count += 1.0;
		break;
	}
	if (tp == NULL) {
		ntop++;
		tp = calloc(sizeof *tp, 1);
		assert(tp != NULL);
		tp->rec_data = calloc(l, 1);
		assert(tp->rec_data != NULL);
		tp->hash = u;
		tp->count = 1.0;
		tp->clen = q - p;
		VTAILQ_INSERT_TAIL(&top_head, tp, list);
	}
	memcpy(tp->rec, p, SHMLOG_DATA - 1);
	memcpy(tp->rec_data, p + SHMLOG_DATA, l);
	while (1) {
		tp2 = VTAILQ_PREV(tp, tophead, list);
		if (tp2 == NULL || tp2->count >= tp->count)
			break;
		VTAILQ_REMOVE(&top_head, tp2, list);
		VTAILQ_INSERT_AFTER(&top_head, tp, tp2, list);
	}
	while (1) {
		tp2 = VTAILQ_NEXT(tp, list);
		if (tp2 == NULL || tp2->count <= tp->count)
			break;
		VTAILQ_REMOVE(&top_head, tp2, list);
		VTAILQ_INSERT_BEFORE(tp, tp2, list);
	}
}

static void
update(void)
{
	struct top *tp, *tp2;
	int l, len;
	double t = 0;
	static time_t last;
	time_t now;

	now = time(NULL);
	if (now == last)
		return;
	last = now;

	erase();
	l = 1;
	mvprintw(0, 0, "%*s", COLS - 1, VSL_Name());
	mvprintw(0, 0, "list length %u", ntop);
	VTAILQ_FOREACH_SAFE(tp, &top_head, list, tp2) {
		if (++l < LINES) {
			len = SHMLOG_LEN(tp->rec);
			if (len > COLS - 20)
				len = COLS - 20;
			mvprintw(l, 0, "%9.2f %-*.*s %*.*s\n",
			    tp->count, maxfieldlen, maxfieldlen,
			    VSL_tags[tp->rec[SHMLOG_TAG]],
			    len, len, tp->rec_data);
			t = tp->count;
		}
		tp->count *= .999;
		if (tp->count * 10 < t || l > LINES * 10) {
			VTAILQ_REMOVE(&top_head, tp, list);
			free(tp->rec_data);
			free(tp);
			ntop--;
		}
	}
	refresh();
}

static void *
accumulate_thread(void *arg)
{
	struct VSL_data *vd = arg;

	for (;;) {
		unsigned char *p;
		int i;

		i = VSL_NextLog(vd, &p);
		if (i < 0)
			break;
		if (i == 0) {
			usleep(50000);
			continue;
		}

		pthread_mutex_lock(&mtx);
		accumulate(p);
		pthread_mutex_unlock(&mtx);
	}
	return (arg);
}

static void
do_curses(struct VSL_data *vd)
{
	pthread_t thr;
	int ch;
	int i;

	for (i = 0; i < 256; i++) {
		if (VSL_tags[i] == NULL)
			continue;
		if (maxfieldlen < strlen(VSL_tags[i]))
			maxfieldlen = strlen(VSL_tags[i]);
	}

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
		update();
		pthread_mutex_unlock(&mtx);

		timeout(1000);
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
		default:
			beep();
			break;
		}
	}
}

static void
dump(void)
{
	struct top *tp, *tp2;
	int len;

	VTAILQ_FOREACH_SAFE(tp, &top_head, list, tp2) {
		if (tp->count <= 1.0)
			break;
		len = SHMLOG_LEN(tp->rec);
		printf("%9.2f %s %*.*s\n",
		    tp->count, VSL_tags[tp->rec[SHMLOG_TAG]],
		    len, len, tp->rec + SHMLOG_DATA);
	}
}

static void
do_once(struct VSL_data *vd)
{
	unsigned char *p;

	while (VSL_NextLog(vd, &p) > 0)
		accumulate(p);
	dump();
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: varnishtop %s [-1fV] [-n varnish_name]\n", VSL_USAGE);
	exit(1);
}

int
main(int argc, char **argv)
{
	struct VSL_data *vd;
	const char *n_arg = NULL;
	int o, once = 0;

	vd = VSL_New();

	while ((o = getopt(argc, argv, VSL_ARGS "1fn:V")) != -1) {
		switch (o) {
		case '1':
			VSL_Arg(vd, 'd', NULL);
			once = 1;
			break;
		case 'n':
			n_arg = optarg;
			break;
		case 'f':
			f_flag = 1;
			break;
		case 'V':
			varnish_version("varnishtop");
			exit(0);
		default:
			if (VSL_Arg(vd, o, optarg) > 0)
				break;
			usage();
		}
	}

	if (VSL_OpenLog(vd, n_arg))
		exit (1);

	if (once) {
		VSL_NonBlocking(vd, 1);
		do_once(vd);
	} else {
		do_curses(vd);
	}
	exit(0);
}
