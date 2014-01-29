/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dag-Erling Smørgrav <des@des.no>
 * Author: Guillaume Quintard <guillaume.quintard@gmail.com>
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
#include "vut.h"

#if 0
#define AC(x) assert((x) != ERR)
#else
#define AC(x) x
#endif

static const char progname[] = "varnishtop2";
static float period = 60; /* seconds */

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


static int __match_proto__(VSLQ_dispatch_f)
accumulate(struct VSL_data *vsl, struct VSL_transaction * const pt[],
	void *priv)
{
	struct top *tp, t;
	char *rd;
	unsigned int u;
	unsigned tag;
	const char *b, *e, *p;
	unsigned len;

	(void)vsl;
	(void)priv;
	struct VSL_transaction *tr;
	for (tr = pt[0]; tr != NULL; tr = *++pt) {
		while ((1 == VSL_Next(tr->c))) {
			tag = VSL_TAG(tr->c->rec.ptr);
			b = VSL_CDATA(tr->c->rec.ptr);
			len = VSL_LEN(tr->c->rec.ptr);
			assert(len > 0);
			e = b + len;
			u = 0;
			for (p = b; p <= e; p++) {
				u += *p;
			}

			t.hash = u;
			t.tag = tag;
			t.clen = len;
			t.count = 0;
			rd = calloc(len+1, 1);
			AN(rd);
			memcpy(rd, VSL_CDATA(tr->c->rec.ptr), len);
			rd[len] = '\0';
			t.rec_data = rd;

			AZ(pthread_mutex_lock(&mtx));
			tp = VRB_FIND(top_tree, &top_tree_head, &t);
			if (tp) {
				VRB_REMOVE(top_tree, &top_tree_head, tp);
				tp->count += 1.0;
				/* Reinsert to rebalance */
				VRB_INSERT(top_tree, &top_tree_head, tp);
				free(rd);
			} else {
				ntop++;
				tp = calloc(sizeof *tp, 1);
				assert(tp != NULL);
				tp->hash = u;
				tp->count = 1.0;
				tp->clen = len;
				tp->tag = tag;
				tp->rec_data = rd;
				VRB_INSERT(top_tree, &top_tree_head, tp);
			}
			AZ(pthread_mutex_unlock(&mtx));

		}
	}

	return (0);
}

static void
update(const struct VSM_data *vd, int p)
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
	if (n < p)
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
		(void)t;
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
do_curses(void *arg)
{
	int i;
	struct VSM_data *vd = (struct VSM_data *)arg;

	for (i = 0; i < 256; i++) {
		if (VSL_tags[i] == NULL)
			continue;
		if (maxfieldlen < strlen(VSL_tags[i]))
			maxfieldlen = strlen(VSL_tags[i]);
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
		case '\032': /* Ctrl-Z */
			AC(endwin());
			AZ(raise(SIGTSTP));
			break;
		case '\003': /* Ctrl-C */
			printf("got ctrl-C\r\n");
		case '\021': /* Ctrl-Q */
		case 'Q':
		case 'q':
			AZ(raise(SIGINT));
			AC(endwin());
			return NULL;
		default:
			AC(beep());
			break;
		}
	}
	return NULL;

}

static void
dump(void)
{
	struct top *tp, *tp2;
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
usage(void)
{
	fprintf(stderr,
		"usage: varnishtop [-1fV] [-n varnish_name]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	struct VSM_data *vd;
	int o, once = 0;
	pthread_t thr;

	vd = VSM_New();
	VUT_Init(progname);

	while ((o = getopt(argc, argv, "1fVp:")) != -1) {
		switch (o) {
		case '1':
			VUT_Arg('d', NULL);
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
			if (!VUT_Arg(o, optarg))
				break;
			usage();
		}
	}

	if (VSM_Open(vd)) {
		fprintf(stderr, "%s\n", VSM_Error(vd));
		exit (1);
	}

	VUT.dispatch_f = &accumulate;
	VUT.dispatch_priv = NULL;
	if (!once){
		if (pthread_create(&thr, NULL, do_curses, vd) != 0) {
			fprintf(stderr, "pthread_create(): %s\n", strerror(errno));
			exit(1);
		}
	}
	VUT_Setup();
	VUT_Main();
	VUT_Fini();
	if (once)
		dump();
	else
		pthread_join(thr, NULL);
	exit(0);
}
