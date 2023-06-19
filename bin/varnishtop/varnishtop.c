/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dag-Erling Smørgrav <des@des.no>
 * Author: Guillaume Quintard <guillaume.quintard@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VOPT_DEFINITION
#define VOPT_INC "varnishtop_options.h"

#include "vdef.h"

#include "vcurses.h"
#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "vtree.h"
#include "vut.h"
#include "vapi/vsig.h"

#if 0
#define AC(x) assert((x) != ERR)
#else
#define AC(x) x
#endif

static struct VUT *vut;

struct top {
	uint8_t			tag;
	const char		*rec_data;
	char			*rec_buf;
	int			clen;
	unsigned		hash;
	VRBT_ENTRY(top)		e_order;
	VRBT_ENTRY(top)		e_key;
	double			count;
};

static unsigned period = 60; /* seconds */
static int end_of_file = 0;
static unsigned ntop;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int f_flag = 0;
static unsigned maxfieldlen = 0;
static const char *ident;

static VRBT_HEAD(t_order, top) h_order = VRBT_INITIALIZER(&h_order);
static VRBT_HEAD(t_key, top) h_key = VRBT_INITIALIZER(&h_key);

static inline int
cmp_key(const struct top *a, const struct top *b)
{
	if (a->hash != b->hash)
		return (a->hash - b->hash);
	if (a->tag != b->tag)
		return (a->tag - b->tag);
	if (a->clen != b->clen)
		return (a->clen - b->clen);
	return (memcmp(a->rec_data, b->rec_data, a->clen));
}

static inline int
cmp_order(const struct top *a, const struct top *b)
{
	if (a->count > b->count)
		return (-1);
	else if (a->count < b->count)
		return (1);
	return (cmp_key(a, b));
}

VRBT_GENERATE_INSERT_COLOR(t_order, top, e_order, static)
VRBT_GENERATE_INSERT_FINISH(t_order, top, e_order, static)
VRBT_GENERATE_INSERT(t_order, top, e_order, cmp_order, static)
VRBT_GENERATE_REMOVE_COLOR(t_order, top, e_order, static)
VRBT_GENERATE_MINMAX(t_order, top, e_order, static)
VRBT_GENERATE_NEXT(t_order, top, e_order, static)
VRBT_GENERATE_REMOVE(t_order, top, e_order, static)

VRBT_GENERATE_INSERT_COLOR(t_key, top, e_key, static)
VRBT_GENERATE_REMOVE_COLOR(t_key, top, e_key, static)
VRBT_GENERATE_INSERT_FINISH(t_key, top, e_key, static)
VRBT_GENERATE_INSERT(t_key, top, e_key, cmp_key, static)
VRBT_GENERATE_REMOVE(t_key, top, e_key, static)
VRBT_GENERATE_FIND(t_key, top, e_key, cmp_key, static)

static int v_matchproto_(VSLQ_dispatch_f)
accumulate(struct VSL_data *vsl, struct VSL_transaction * const pt[],
	void *priv)
{
	struct top *tp, t;
	unsigned int u;
	unsigned tag;
	const char *b, *e, *p;
	unsigned len;
	struct VSL_transaction *tr;

	(void)priv;

	for (tr = pt[0]; tr != NULL; tr = *++pt) {
		while ((1 == VSL_Next(tr->c))) {
			tag = VSL_TAG(tr->c->rec.ptr);
			if (VSL_tagflags[tag])
				continue;
			if (!VSL_Match(vsl, tr->c))
				continue;
			b = VSL_CDATA(tr->c->rec.ptr);
			e = b + VSL_LEN(tr->c->rec.ptr);
			u = 0;
			for (p = b; p <= e; p++) {
				if (*p == '\0')
					break;
				if (f_flag && (*p == ':' || isspace(*p)))
					break;
				u += *p;
			}
			len = p - b;
			if (len == 0)
				continue;

			t.hash = u;
			t.tag = tag;
			t.clen = len;
			t.rec_data = VSL_CDATA(tr->c->rec.ptr);

			PTOK(pthread_mutex_lock(&mtx));
			tp = VRBT_FIND(t_key, &h_key, &t);
			if (tp) {
				VRBT_REMOVE(t_order, &h_order, tp);
				tp->count += 1.0;
				/* Reinsert to rebalance */
				VRBT_INSERT(t_order, &h_order, tp);
			} else {
				ntop++;
				tp = calloc(1, sizeof *tp);
				assert(tp != NULL);
				tp->hash = u;
				tp->count = 1.0;
				tp->clen = len;
				tp->tag = tag;
				tp->rec_buf = strdup(t.rec_data);
				tp->rec_data = tp->rec_buf;
				AN(tp->rec_data);
				VRBT_INSERT(t_key, &h_key, tp);
				VRBT_INSERT(t_order, &h_order, tp);
			}
			PTOK(pthread_mutex_unlock(&mtx));

		}
	}

	return (0);
}

static void
update(unsigned p)
{
	struct top *tp, *tp2;
	int l, len;
	double t = 0;
	static time_t last = 0;
	static unsigned n = 0;
	const char *q;
	time_t now;

	now = time(NULL);
	if (now == last)
		return;
	last = now;

	l = 1;
	if (n < p)
		n++;
	AC(erase());
	q = ident;
	len = COLS - strlen(q);
	if (end_of_file)
		AC(mvprintw(0, len - (1 + 6), "%s (EOF)", q));
	else
		AC(mvprintw(0, len - 1, "%s", q));
	AC(mvprintw(0, 0, "list length %u", ntop));
	for (tp = VRBT_MIN(t_order, &h_order); tp != NULL; tp = tp2) {
		tp2 = VRBT_NEXT(t_order, &h_order, tp);

		if (++l < LINES) {
			len = vmin(tp->clen, COLS - 20);
			AC(mvprintw(l, 0, "%9.2f %-*.*s %*.*s\n",
				tp->count, maxfieldlen, maxfieldlen,
				VSL_tags[tp->tag],
				len, len, tp->rec_data));
			t = tp->count;
		}
		if (end_of_file)
			continue;
		tp->count += (1.0/3.0 - tp->count) / (double)n;
		if (tp->count * 10 < t || l > LINES * 10) {
			VRBT_REMOVE(t_key, &h_key, tp);
			VRBT_REMOVE(t_order, &h_order, tp);
			free(tp->rec_buf);
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

	(void)arg;
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
	timeout(1000);
	while (!VSIG_int && !VSIG_term && !VSIG_hup) {
		PTOK(pthread_mutex_lock(&mtx));
		update(period);
		PTOK(pthread_mutex_unlock(&mtx));

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
		case '\021': /* Ctrl-Q */
		case 'Q':
		case 'q':
			AZ(raise(SIGINT));
			break;
		default:
			AC(beep());
			break;
		}
	}
	AC(endwin());
	return (NULL);
}

static void
dump(void)
{
	struct top *tp, *tp2;
	for (tp = VRBT_MIN(t_order, &h_order); tp != NULL; tp = tp2) {
		tp2 = VRBT_NEXT(t_order, &h_order, tp);
		printf("%9.2f %s %*.*s\n",
			tp->count, VSL_tags[tp->tag],
			tp->clen, tp->clen, tp->rec_data);
	}
}

int
main(int argc, char **argv)
{
	int o, once = 0;
	pthread_t thr;
	char *e = NULL;

	vut = VUT_InitProg(argc, argv, &vopt_spec);
	AN(vut);

	while ((o = getopt(argc, argv, vopt_spec.vopt_optstring)) != -1) {
		switch (o) {
		case '1':
			AN(VUT_Arg(vut, 'd', NULL));
			once = 1;
			break;
		case 'f':
			f_flag = 1;
			break;
		case 'h':
			/* Usage help */
			VUT_Usage(vut, &vopt_spec, 0);
		case 'p':
			errno = 0;
			e = NULL;
			period = strtoul(optarg, &e, 0);
			if (errno != 0 || e == NULL || *e != '\0') {
				fprintf(stderr,
				    "Syntax error, %s is not a number", optarg);
				exit(1);
			}
			break;
		default:
			if (!VUT_Arg(vut, o, optarg))
				VUT_Usage(vut, &vopt_spec, 1);
		}
	}

	if (optind != argc)
		VUT_Usage(vut, &vopt_spec, 1);

	VUT_Setup(vut);
	if (vut->vsm)
		ident = VSM_Dup(vut->vsm, "Arg", "-i");
	else
		ident = strdup("");
	AN(ident);
	vut->dispatch_f = accumulate;
	vut->dispatch_priv = NULL;
	if (once) {
		(void)VUT_Main(vut);
		dump();
	} else {
		PTOK(pthread_create(&thr, NULL, do_curses, NULL));
		(void)VUT_Main(vut);
		end_of_file = 1;
		PTOK(pthread_join(thr, NULL));
	}
	VUT_Fini(&vut);
	return (0);
}
