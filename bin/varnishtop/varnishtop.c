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

#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vas.h"
#include "vcs.h"
#include "vtree.h"
#include "vsb.h"

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
	VRB_ENTRY(top)		entry;
	double			count;
};

static int
top_cmp(const struct top *tp, const struct top *tp2);

static VRB_HEAD(top_tree, top) top_tree_head = VRB_INITIALIZER(&top_tree_head);
VRB_PROTOTYPE(top_tree, top, entry, top_cmp);

static unsigned ntop;

/*--------------------------------------------------------------------*/

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static int f_flag = 0;

static unsigned maxfieldlen = 0;

VRB_GENERATE(top_tree, top, entry, top_cmp);

static int
top_cmp(const struct top *tp, const struct top *tp2)
{
	if (tp->count == tp2->count || tp->count == 0.0) {
		if (tp->hash != tp2->hash)
			return (tp->hash - tp2->hash);
		if (tp->tag != tp2->tag)
			return (tp->tag - tp2->tag);
		if (tp->clen != tp2->clen)
			return (tp->clen - tp2->clen);
		else
			return (memcmp(tp->rec_data, tp2->rec_data, tp->clen));
	} else {
		if (tp->count > tp2->count)
			return -1;
		else
			return 1;
	}
}


static int
accumulate(void *priv, enum VSL_tag_e tag, unsigned fd, unsigned len,
    unsigned spec, const char *ptr, uint64_t bm)
{
	struct top *tp, t;
	const char *q;
	char *rd;
	unsigned int u;
	int i;

	(void)priv;
	(void)fd;
	(void)spec;
	(void)bm;
	// fprintf(stderr, "%p %08x %08x\n", p, p[0], p[1]);

	u = 0;
	q = ptr;
	for (i = 0; i < len; i++, q++) {
		if (f_flag && (*q == ':' || isspace(*q))) {
			len = q - ptr;
			break;
		}
		u += *q;
	}
	t.hash = u;
	t.tag = tag;
	t.clen = len;
	rd = malloc(len);
	AN(rd);
	memcpy(rd, ptr, len);
	t.rec_data = rd;

	AZ(pthread_mutex_lock(&mtx));
	tp = VRB_FIND(top_tree, &top_tree_head, &t);
	if (tp) {
		VRB_REMOVE(top_tree, &top_tree_head, tp);
		tp->count += 1.0;
		/* Reinsert to rebalance */
		VRB_INSERT(top_tree, &top_tree_head, tp);
	} else {
		ntop++;
		tp = calloc(sizeof *tp, 1);
		assert(tp != NULL);
		tp->rec_data = calloc(len + 1, 1);
		assert(tp->rec_data != NULL);
		tp->hash = u;
		tp->count = 1.0;
		tp->clen = len;
		tp->tag = tag;
		memcpy(tp->rec_data, ptr, len);
		tp->rec_data[len] = '\0';
		VRB_INSERT(top_tree, &top_tree_head, tp);
	}
	AZ(pthread_mutex_unlock(&mtx));

	return (0);
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
	for (tp = VRB_MIN(top_tree, &top_tree_head); tp != NULL; tp = tp2) {
		tp2 = VRB_NEXT(top_tree, &top_tree_head, tp);
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
			VRB_REMOVE(top_tree, &top_tree_head, tp);
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
	int i;

	for (;;) {

		i = VSL_Dispatch(vd, accumulate, NULL);
		if (i < 0)
			break;
		if (i == 0)
			usleep(50000);
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
	printf("%d\n", ntop);
	printf("%p\n", VRB_MIN(top_tree, &top_tree_head));
	for (tp = VRB_MIN(top_tree, &top_tree_head); tp != NULL; tp = tp2) {
		tp2 = VRB_NEXT(top_tree, &top_tree_head, tp);
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
	while (VSL_Dispatch(vd, accumulate, NULL) > 0)
		;
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
				fprintf(stderr,
				    "Syntax error, %s is not a number", optarg);
				exit(1);
			}
			break;
		case 'V':
			VCS_Message("varnishtop");
			exit(0);
		case 'm':
			fprintf(stderr, "-m is not supported\n");
			exit(1);
		default:
			if (VSL_Arg(vd, o, optarg) > 0)
				break;
			usage();
		}
	}

	if (VSM_Open(vd)) {
		fprintf(stderr, "%s\n", VSM_Error(vd));
		exit (1);
	}

	if (once) {
		do_once(vd);
	} else {
		do_curses(vd, period);
	}
	exit(0);
}
