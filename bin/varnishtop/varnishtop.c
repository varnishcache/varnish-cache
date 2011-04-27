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
 * Log tailer for Varnish
 */

#include "config.h"

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
#include "vsl.h"
#include "varnishapi.h"

#if 0
#define AC(x) assert((x) != ERR)
#else
#define AC(x) x
#endif

struct top {
	uint8_t			tag;
	char			*rec_data;
	int			clen;
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
accumulate(uint32_t * const p)
{
	struct top *tp, *tp2;
	const char *q;
	unsigned int u, l;
	uint8_t t;
	int i;

	// fprintf(stderr, "%p %08x %08x\n", p, p[0], p[1]);

	u = 0;
	q = VSL_DATA(p);
	l = VSL_LEN(p);
	t = VSL_TAG(p);
	for (i = 0; i < l; i++, q++) {
		if (f_flag && (*q == ':' || isspace(*q))) {
			l = q - VSL_DATA(p);
			break;
		}
		u += *q;
	}

	VTAILQ_FOREACH(tp, &top_head, list) {
		if (tp->hash != u)
			continue;
		if (tp->tag != t)
			continue;
		if (tp->clen != l)
			continue;
		if (memcmp(VSL_DATA(p), tp->rec_data, l))
			continue;
		tp->count += 1.0;
		break;
	}
	if (tp == NULL) {
		ntop++;
		tp = calloc(sizeof *tp, 1);
		assert(tp != NULL);
		tp->rec_data = calloc(l + 1, 1);
		assert(tp->rec_data != NULL);
		tp->hash = u;
		tp->count = 1.0;
		tp->clen = l;
		tp->tag = t;
		memcpy(tp->rec_data, VSL_DATA(p), l);
		tp->rec_data[l] = '\0';
		VTAILQ_INSERT_TAIL(&top_head, tp, list);
	}
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
update(const struct VSM_data *vd, int period)
{
	struct top *tp, *tp2;
	int l, len;
	double t = 0;
	static time_t last = 0;
	static unsigned n;
	time_t now;

	now = time(NULL);
	if (now == last)
		return;
	last = now;

	l = 1;
	if (n < period)
		n++;
	AC(erase());
	AC(mvprintw(0, 0, "%*s", COLS - 1, VSM_Name(vd)));
	AC(mvprintw(0, 0, "list length %u", ntop));
	VTAILQ_FOREACH_SAFE(tp, &top_head, list, tp2) {
		if (++l < LINES) {
			len = tp->clen;
			if (len > COLS - 20)
				len = COLS - 20;
			AC(mvprintw(l, 0, "%9.2f %-*.*s %*.*s\n",
			    tp->count, maxfieldlen, maxfieldlen,
			    VSL_tags[tp->tag],
			    len, len, tp->rec_data));
			t = tp->count;
		}
		tp->count += (1.0/3.0 - tp->count) / (double)n;
		if (tp->count * 10 < t || l > LINES * 10) {
			VTAILQ_REMOVE(&top_head, tp, list);
			free(tp->rec_data);
			free(tp);
			ntop--;
		}
	}
	AC(refresh());
}

static void *
accumulate_thread(void *arg)
{
	struct VSM_data *vd = arg;
	uint32_t *p;
	int i;

	for (;;) {

		i = VSL_NextLog(vd, &p, NULL);
		if (i < 0)
			break;
		if (i == 0) {
			AZ(usleep(50000));
			continue;
		}

		AZ(pthread_mutex_lock(&mtx));
		accumulate(p);
		AZ(pthread_mutex_unlock(&mtx));
	}
	return (arg);
}

static void
do_curses(struct VSM_data *vd, int period)
{
	pthread_t thr;
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

	(void)initscr();
	AC(raw());
	AC(noecho());
	AC(nonl());
	AC(intrflush(stdscr, FALSE));
	(void)curs_set(0);
	AC(erase());
	for (;;) {
		AZ(pthread_mutex_lock(&mtx));
		update(vd, period);
		AZ(pthread_mutex_unlock(&mtx));

		timeout(1000);
		switch (getch()) {
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
			AC(endwin());
			AZ(raise(SIGTSTP));
			break;
		case '\021': /* Ctrl-Q */
		case 'Q':
		case 'q':
			AC(endwin());
			return;
		default:
			AC(beep());
			break;
		}
	}
}

static void
dump(void)
{
	struct top *tp, *tp2;

	VTAILQ_FOREACH_SAFE(tp, &top_head, list, tp2) {
		if (tp->count <= 1.0)
			break;
		printf("%9.2f %s %*.*s\n",
		    tp->count, VSL_tags[tp->tag],
		    tp->clen, tp->clen, tp->rec_data);
	}
}

static void
do_once(struct VSM_data *vd)
{
	uint32_t *p;

	while (VSL_NextLog(vd, &p, NULL) > 0)
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
	struct VSM_data *vd;
	int o, once = 0;
	float period = 60; /* seconds */

	vd = VSM_New();
	VSL_Setup(vd);

	while ((o = getopt(argc, argv, VSL_ARGS "1fVp:")) != -1) {
		switch (o) {
		case '1':
			AN(VSL_Arg(vd, 'd', NULL));
			once = 1;
			break;
		case 'f':
			f_flag = 1;
			break;
		case 'p':
			errno = 0;
			period = strtol(optarg, NULL, 0);
			if (errno != 0)  {
				fprintf(stderr, "Syntax error, %s is not a number", optarg);
				exit(1);
			}
			break;
		case 'V':
			varnish_version("varnishtop");
			exit(0);
		case 'o':
			fprintf(stderr, "-o is not supported\n");
			exit(1);
		default:
			if (VSL_Arg(vd, o, optarg) > 0)
				break;
			usage();
		}
	}

	if (VSL_Open(vd, 1))
		exit (1);

	if (once) {
		VSL_NonBlocking(vd, 1);
		do_once(vd);
	} else {
		do_curses(vd, period);
	}
	exit(0);
}
