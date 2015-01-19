/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dag-Erling Smørgrav <des@des.no>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Statistics output program
 */


#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <poll.h>
#include <stdint.h>
#include <math.h>

#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vapi/vsm.h"
#include "vapi/vsc.h"
#include "vtim.h"

#include "varnishstat.h"
#include "vcurses.h"

#define LINES_STATUS		3
#define LINES_BAR_T		1
#define LINES_BAR_B		1
#define LINES_INFO		3
#define LINES_POINTS_MIN	3

#define N_COL			6
#define COLW			14
#define COLW_NAME_MIN		24

struct ma {
	unsigned n, nmax;
	double acc;
};

struct pt {
	unsigned		magic;
#define PT_MAGIC		0x41698E4F
	VTAILQ_ENTRY(pt)	list;

	const struct VSC_point	*vpt;

	char			*key;
	char			*name;
	int			semantics;
	int			format;
	const volatile uint64_t	*ptr;

	char			seen;

	uint64_t		cur, last;
	double			t_cur, t_last;
	double			chg, avg;

	struct ma		ma_10, ma_100, ma_1000;
};

struct hitrate {
	double lt;
	uint64_t lhit, lmiss;
	struct ma hr_10;
	struct ma hr_100;
	struct ma hr_1000;
};
static struct hitrate hitrate;

static VTAILQ_HEAD(, pt) ptlist = VTAILQ_HEAD_INITIALIZER(ptlist);
static int n_ptlist = 0;
static int n_ptarray = 0;
static struct pt **ptarray = NULL;
static const struct VSC_C_mgt *VSC_C_mgt = NULL;
static const struct VSC_C_main *VSC_C_main = NULL;

static int l_status, l_bar_t, l_points, l_bar_b, l_info;
static int colw_name = COLW_NAME_MIN;
static WINDOW *w_status = NULL;
static WINDOW *w_bar_t = NULL;
static WINDOW *w_points = NULL;
static WINDOW *w_bar_b = NULL;
static WINDOW *w_info = NULL;

static int verbosity = VSC_level_info;
static int keep_running = 1;
static int show_info = 1;
static int hide_unseen = 1;
static int page_start = 0;
static int current = 0;
static int rebuild = 0;
static int redraw = 0;
static int sample = 0;
static int scale = 1;
static double t_sample = 0.;
static double interval = 1.;

static void
init_hitrate(void)
{
	memset(&hitrate, 0, sizeof (struct hitrate));
	if (VSC_C_main != NULL) {
		hitrate.lhit = VSC_C_main->cache_hit;
		hitrate.lmiss = VSC_C_main->cache_miss;
	}
	hitrate.hr_10.nmax = 10;
	hitrate.hr_100.nmax = 100;
	hitrate.hr_1000.nmax = 1000;
}

static void
update_ma(struct ma *ma, double val)
{
	AN(ma);
	AN(ma->nmax);
	if (ma->n < ma->nmax)
		ma->n++;
	ma->acc += (val - ma->acc) / (double)ma->n;
}

static void
update_position(void)
{
	int old_current, old_page_start;

	old_current = current;
	old_page_start = page_start;

	if (n_ptarray == 0) {
		current = 0;
		page_start = 0;
	} else {
		if (current < 0)
			current = 0;
		if (current > n_ptarray - 1)
			current = n_ptarray - 1;
		if (current < page_start)
			page_start = current;
		if (current > page_start + (l_points - 1))
			page_start = current - (l_points - 1);
		if (page_start < 0)
			page_start = 0;
		if (page_start > n_ptarray - 1)
			page_start = n_ptarray - 1;
	}

	if (current != old_current || page_start != old_page_start)
		redraw = 1;
}

static void
delete_pt_array(void)
{
	if (ptarray != NULL)
		free(ptarray);
	ptarray = NULL;
	n_ptarray = 0;

	update_position();
}

static void
build_pt_array(void)
{
	int i;
	struct pt *pt;
	struct pt *pt_current = NULL;
	int current_line = 0;

	if (current < n_ptarray) {
		pt_current = ptarray[current];
		current_line = current - page_start;
	}

	if (ptarray != NULL)
		delete_pt_array();
	AZ(n_ptarray);
	ptarray = calloc(n_ptlist, sizeof *ptarray);
	AN(ptarray);

	VTAILQ_FOREACH(pt, &ptlist, list) {
		CHECK_OBJ_NOTNULL(pt, PT_MAGIC);
		if (!pt->seen && hide_unseen)
			continue;
		if (pt->vpt->desc->level->verbosity > verbosity)
			continue;
		assert(n_ptarray < n_ptlist);
		ptarray[n_ptarray++] = pt;
	}
	assert(n_ptarray <= n_ptlist);

	for (i = 0; pt_current != NULL && i < n_ptarray; i++)
		if (ptarray[i] == pt_current)
			break;
	current = i;
	page_start = current - current_line;
	update_position();

	rebuild = 0;
	redraw = 1;
}

static void
delete_pt_list(void)
{
	struct pt *pt;
	unsigned i = 0;

	delete_pt_array();

	while (!VTAILQ_EMPTY(&ptlist)) {
		pt = VTAILQ_FIRST(&ptlist);
		CHECK_OBJ_NOTNULL(pt, PT_MAGIC);
		VTAILQ_REMOVE(&ptlist, pt, list);
		free(pt->name);
		FREE_OBJ(pt);
		i++;
	}
	assert(i == n_ptlist);
	n_ptlist = 0;

	update_position();
}

struct pt_priv {
	unsigned		magic;
#define PT_PRIV_MAGIC		0x34ACBAD6
	VTAILQ_HEAD(, pt)	ptlist;
	unsigned		n_ptlist;
};

static int
build_pt_list_cb(void *priv, const struct VSC_point *vpt)
{
	struct pt_priv *pt_priv;
	struct pt *pt;
	char buf[128];

	if (vpt == NULL)
		return (0);

	CAST_OBJ_NOTNULL(pt_priv, priv, PT_PRIV_MAGIC);

	AZ(strcmp(vpt->desc->ctype, "uint64_t"));
	snprintf(buf, sizeof buf, "%s.%s.%s", vpt->section->type,
	    vpt->section->ident, vpt->desc->name);
	buf[sizeof buf - 1] = '\0';

	VTAILQ_FOREACH(pt, &ptlist, list) {
		CHECK_OBJ_NOTNULL(pt, PT_MAGIC);
		AN(pt->key);
		if (strcmp(buf, pt->key))
			continue;
		VTAILQ_REMOVE(&ptlist, pt, list);
		AN(n_ptlist);
		n_ptlist--;
		pt->vpt = vpt;
		VTAILQ_INSERT_TAIL(&pt_priv->ptlist, pt, list);
		pt_priv->n_ptlist++;
		return (0);
	}
	AZ(pt);

	ALLOC_OBJ(pt, PT_MAGIC);
	AN(pt);

	pt->key = strdup(buf);
	AN(pt->key);

	*buf = '\0';
	if (strcmp(vpt->section->type, "")) {
		strncat(buf, vpt->section->type, sizeof buf - strlen(buf) - 1);
		strncat(buf, ".", sizeof buf - strlen(buf) - 1);
	}
	if (strcmp(vpt->section->ident, "")) {
		strncat(buf, vpt->section->ident, sizeof buf - strlen(buf) - 1);
		strncat(buf, ".", sizeof buf - strlen(buf) - 1);
	}
	strncat(buf, vpt->desc->name, sizeof buf - strlen(buf) - 1);
	pt->name = strdup(buf);
	AN(pt->name);

	pt->vpt = vpt;

	pt->ptr = vpt->ptr;
	pt->last = *pt->ptr;
	pt->semantics = vpt->desc->semantics;
	pt->format = vpt->desc->format;

	pt->ma_10.nmax = 10;
	pt->ma_100.nmax = 100;
	pt->ma_1000.nmax = 1000;

	VTAILQ_INSERT_TAIL(&pt_priv->ptlist, pt, list);
	pt_priv->n_ptlist++;

	return (0);
}

static void
build_pt_list(struct VSM_data *vd, struct VSM_fantom *fantom)
{
	struct pt_priv pt_priv;
	int i;
	struct pt *pt_current = NULL;
	int current_line = 0;

	if (current < n_ptarray) {
		pt_current = ptarray[current];
		current_line = current - page_start;
	}

	pt_priv.magic = PT_PRIV_MAGIC;
	VTAILQ_INIT(&pt_priv.ptlist);
	pt_priv.n_ptlist = 0;

	(void)VSC_Iter(vd, fantom, build_pt_list_cb, &pt_priv);
	delete_pt_list();
	AN(VTAILQ_EMPTY(&ptlist));
	AZ(n_ptlist);
	VTAILQ_CONCAT(&ptlist, &pt_priv.ptlist, list);
	n_ptlist = pt_priv.n_ptlist;
	build_pt_array();

	for (i = 0; pt_current != NULL && i < n_ptarray; i++)
		if (ptarray[i] == pt_current)
			break;
	current = i;
	page_start = current - current_line;
	update_position();
}

static void
sample_points(void)
{
	struct pt *pt;

	VTAILQ_FOREACH(pt, &ptlist, list) {
		AN(pt->vpt);
		AN(pt->ptr);
		if (*pt->ptr == 0 && !pt->seen)
			continue;
		if (!pt->seen) {
			pt->seen = 1;
			rebuild = 1;
		}
		pt->last = pt->cur;
		pt->cur = *pt->ptr;
		pt->t_last = pt->t_cur;
		pt->t_cur = VTIM_mono();

		if (pt->t_last)
			pt->chg = ((intmax_t)pt->cur - (intmax_t)pt->last) /
			    (pt->t_cur - pt->t_last);

		if (pt->semantics == 'g') {
			pt->avg = 0.;
			update_ma(&pt->ma_10, pt->cur);
			update_ma(&pt->ma_100, pt->cur);
			update_ma(&pt->ma_1000, pt->cur);
		} else if (pt->semantics == 'c') {
			if (VSC_C_main != NULL && VSC_C_main->uptime)
				pt->avg = pt->cur / VSC_C_main->uptime;
			else
				pt->avg = 0.;
			if (pt->t_last) {
				update_ma(&pt->ma_10, pt->chg);
				update_ma(&pt->ma_100, pt->chg);
				update_ma(&pt->ma_1000, pt->chg);
			}
		}
	}
}

static void
sample_hitrate(void)
{
	double tv,dt;
	double hr, mr, ratio;
	uint64_t hit, miss;

	if (VSC_C_main == NULL)
		return;

	tv = VTIM_mono();
	dt = tv - hitrate.lt;
	hitrate.lt= tv;

	hit = VSC_C_main->cache_hit;
	miss = VSC_C_main->cache_miss;
	hr = (hit - hitrate.lhit) / dt;
	mr = (miss - hitrate.lmiss) / dt;
	hitrate.lhit = hit;
	hitrate.lmiss = miss;

	if (hr + mr != 0)
		ratio = hr / (hr + mr);
	else
		ratio = 0;
	update_ma(&hitrate.hr_10, ratio);
	update_ma(&hitrate.hr_100, ratio);
	update_ma(&hitrate.hr_1000, ratio);
}

static void
sample_data(void)
{
	t_sample = VTIM_mono();
	sample = 0;
	redraw = 1;
	sample_points();
	sample_hitrate();
}

static void
make_windows(void)
{
	int Y, X;
	int y;
	int y_status, y_bar_t, y_points, y_bar_b, y_info;

	if (w_status) {
		delwin(w_status);
		w_status = NULL;
	}
	if (w_bar_t) {
		delwin(w_bar_t);
		w_bar_t = NULL;
	}
	if (w_points) {
		delwin(w_points);
		w_points = NULL;
	}
	if (w_bar_b) {
		delwin(w_bar_b);
		w_bar_b = NULL;
	}
	if (w_info) {
		delwin(w_info);
		w_info = NULL;
	}

	Y = LINES;
	X = COLS;

	l_status = LINES_STATUS;
	l_bar_t = LINES_BAR_T;
	l_bar_b = LINES_BAR_B;
	l_info = (show_info ? LINES_INFO : 0);
	l_points = Y - (l_status + l_bar_t + l_bar_b + l_info);
	if (l_points < LINES_POINTS_MIN) {
		l_points += l_info;
		l_info = 0;
	}
	if (l_points < LINES_POINTS_MIN)
		l_points = LINES_POINTS_MIN;

	y = 0;
	y_status = y;
	y += l_status;
	y_bar_t = y;
	y += l_bar_t;
	y_points = y;
	y += l_points;
	y_bar_b = y;
	y += l_bar_b;
	y_info = y;
	y += l_info;
	assert(y >= Y);

	w_status = newwin(l_status, X, y_status, 0);
	AN(w_status);
	nodelay(w_status, 1);
	keypad(w_status, 1);
	wnoutrefresh(w_status);

	w_bar_t = newwin(l_bar_t, X, y_bar_t, 0);
	AN(w_bar_t);
	wbkgd(w_bar_t, A_REVERSE);
	wnoutrefresh(w_bar_t);

	w_points = newwin(l_points, X, y_points, 0);
	AN(w_points);
	wnoutrefresh(w_points);

	w_bar_b = newwin(l_bar_b, X, y_bar_b, 0);
	AN(w_bar_b);
	wbkgd(w_bar_b, A_REVERSE);
	wnoutrefresh(w_bar_b);

	if (l_info) {
		w_info = newwin(l_info, X, y_info, 0);
		AN(w_info);
		wnoutrefresh(w_info);
	}

	if (X - COLW_NAME_MIN > N_COL * COLW)
		colw_name = X - (N_COL * COLW);
	else
		colw_name = COLW_NAME_MIN;

	redraw = 1;
}

static void
print_duration(WINDOW *w, time_t t)
{

	wprintw(w, "%4d+%02d:%02d:%02d",
	    t / 86400, (t % 86400) / 3600, (t % 3600) / 60, t % 60);
}

static void
draw_status(void)
{
	time_t up_mgt = 0;
	time_t up_chld = 0;
	static const char discon[] = "*** DISCONNECTED ***";

	AN(w_status);

	werase(w_status);

	if (VSC_C_mgt != NULL)
		up_mgt = VSC_C_mgt->uptime;
	if (VSC_C_main != NULL)
		up_chld = VSC_C_main->uptime;

	mvwprintw(w_status, 0, 0, "Uptime mgt:  ");
	print_duration(w_status, up_mgt);
	mvwprintw(w_status, 1, 0, "Uptime child:");
	print_duration(w_status, up_chld);

	if (VSC_C_mgt == NULL)
		mvwprintw(w_status, 0, COLS - strlen(discon), discon);
	else if (COLS > 70) {
		mvwprintw(w_status, 0, getmaxx(w_status) - 37,
		    "Hitrate n: %8u %8u %8u", hitrate.hr_10.n, hitrate.hr_100.n,
		    hitrate.hr_1000.n);
		mvwprintw(w_status, 1, getmaxx(w_status) - 37,
		    "   avg(n): %8.4f %8.4f %8.4f", hitrate.hr_10.acc,
		    hitrate.hr_100.acc, hitrate.hr_1000.acc);
	}

	wnoutrefresh(w_status);
}

static void
draw_bar_t(void)
{
	int X, x;
	enum {
		COL_CUR,
		COL_CHG,
		COL_AVG,
		COL_MA10,
		COL_MA100,
		COL_MA1000,
		COL_LAST
	} col;

	AN(w_bar_t);

	X = getmaxx(w_bar_t);
	x = 0;
	werase(w_bar_t);
	if (page_start > 0)
		mvwprintw(w_bar_t, 0, x, "^^^");
	x += 4;
	mvwprintw(w_bar_t, 0, x, "%.*s", colw_name - 4, "NAME");
	x += colw_name - 4;
	col = 0;
	while (col < COL_LAST) {
		if (X - x < COLW)
			break;
		switch (col) {
		case COL_CUR:
			mvwprintw(w_bar_t, 0, x, " %12.12s", "CURRENT");
			break;
		case COL_CHG:
			mvwprintw(w_bar_t, 0, x, " %12.12s", "CHANGE");
			break;
		case COL_AVG:
			mvwprintw(w_bar_t, 0, x, " %12.12s", "AVERAGE");
			break;
		case COL_MA10:
			mvwprintw(w_bar_t, 0, x, " %12.12s", "AVG_10");
			break;
		case COL_MA100:
			mvwprintw(w_bar_t, 0, x, " %12.12s", "AVG_100");
			break;
		case COL_MA1000:
			mvwprintw(w_bar_t, 0, x, " %12.12s", "AVG_1000");
			break;
		default:
			break;
		}
		x += COLW;
		col++;
	}

	wnoutrefresh(w_bar_t);
}

static void
draw_line_default(WINDOW *w, int y, int x, int X, struct pt *pt)
{
	enum {
		COL_CUR,
		COL_CHG,
		COL_AVG,
		COL_MA10,
		COL_MA100,
		COL_MA1000,
		COL_LAST
	} col;

	AN(w);
	AN(pt);

	col = 0;
	while (col < COL_LAST) {
		if (X - x < COLW)
			break;
		switch (col) {
		case COL_CUR:
			mvwprintw(w, y, x, " %12ju", (uintmax_t)pt->cur);
			break;
		case COL_CHG:
			if (pt->t_last)
				mvwprintw(w, y, x, " %12.2f", pt->chg);
			else
				mvwprintw(w, y, x, " %12s", ".  ");
			break;
		case COL_AVG:
			if (pt->avg)
				mvwprintw(w, y, x, " %12.2f", pt->avg);
			else
				mvwprintw(w, y, x, " %12s", ".  ");
			break;
		case COL_MA10:
			mvwprintw(w, y, x, " %12.2f", pt->ma_10.acc);
			break;
		case COL_MA100:
			mvwprintw(w, y, x, " %12.2f", pt->ma_100.acc);
			break;
		case COL_MA1000:
			mvwprintw(w, y, x, " %12.2f", pt->ma_1000.acc);
			break;
		default:
			break;
		}
		x += COLW;
		col++;
	}
}

static double
scale_bytes(double val, char *q)
{
	const char *p;

	for (p = " KMGTPEZY"; *p; p++) {
		if (fabs(val) < 1024.)
			break;
		val /= 1024.;
	}
	*q = *p;
	return (val);
}

static void
print_bytes(WINDOW *w, double val)
{
	char q = ' ';

	if (scale)
		val = scale_bytes(val, &q);
	wprintw(w, " %12.2f%c", val, q);
}

static void
draw_line_bytes(WINDOW *w, int y, int x, int X, struct pt *pt)
{
	enum {
		COL_CUR,
		COL_CHG,
		COL_AVG,
		COL_MA10,
		COL_MA100,
		COL_MA1000,
		COL_LAST
	} col;

	AN(w);
	AN(pt);

	col = 0;
	while (col < COL_LAST) {
		if (X - x < COLW)
			break;
		wmove(w, y, x);
		switch (col) {
		case COL_CUR:
			if (scale && pt->cur > 1024)
				print_bytes(w, (double)pt->cur);
			else
				wprintw(w, " %12ju", (uintmax_t)pt->cur);
			break;
		case COL_CHG:
			if (pt->t_last)
				print_bytes(w, pt->chg);
			else
				wprintw(w, " %12s", ".  ");
			break;
		case COL_AVG:
			if (pt->avg)
				print_bytes(w, pt->avg);
			else
				wprintw(w, " %12s", ".  ");
			break;
		case COL_MA10:
			print_bytes(w, pt->ma_10.acc);
			break;
		case COL_MA100:
			print_bytes(w, pt->ma_100.acc);
			break;
		case COL_MA1000:
			print_bytes(w, pt->ma_1000.acc);
			break;
		default:
			break;
		}
		x += COLW;
		col++;
	}
}

static void
draw_line_bitmap(WINDOW *w, int y, int x, int X, struct pt *pt)
{
	int ch;
	enum {
		COL_VAL,
		COL_MAP,
		COL_LAST
	} col;

	AN(w);
	AN(pt);
	assert(pt->format == 'b');

	col = 0;
	while (col < COL_LAST) {
		switch (col) {
		case COL_VAL:
			if (X - x < COLW)
				return;
			mvwprintw(w, y, x, "   %010.10jx",
			    (pt->cur >> 24) & 0xffffffffffLL);
			x += COLW;
			break;
		case COL_MAP:
			if (X - x < 2 * COLW)
				return;
			x += (2 * COLW) - 24;
			for (ch = 0x800000; ch; ch >>= 1) {
				if (pt->cur & ch)
					mvwaddch(w, y, x, 'V');
				else
					mvwaddch(w, y, x, '_');
				x++;
			}
			break;
		default:
			break;
		}
		col++;
	}
}

static void
draw_line_duration(WINDOW *w, int y, int x, int X, struct pt *pt)
{
	enum {
		COL_DUR,
		COL_LAST
	} col;

	AN(w);
	AN(pt);

	col = 0;
	while (col < COL_LAST) {
		if (X - x < COLW)
			break;
		switch (col) {
		case COL_DUR:
			wmove(w, y, x);
			if (scale)
				print_duration(w, pt->cur);
			else
				wprintw(w, " %12ju", (uintmax_t)pt->cur);
			break;
		default:
			break;
		}
		x += COLW;
		col++;
	}
}

static void
draw_line(WINDOW *w, int y, struct pt *pt)
{
	int x, X;

	assert(colw_name >= COLW_NAME_MIN);
	X = getmaxx(w);
	x = 0;
	if (strlen(pt->name) > colw_name)
		mvwprintw(w, y, x, "%.*s...", colw_name - 3, pt->name);
	else
		mvwprintw(w, y, x, "%.*s", colw_name, pt->name);
	x += colw_name;

	switch (pt->format) {
	case 'b':
		draw_line_bitmap(w, y, x, X, pt);
		break;
	case 'B':
		draw_line_bytes(w, y, x, X, pt);
		break;
	case 'd':
		draw_line_duration(w, y, x, X, pt);
		break;
	default:
		draw_line_default(w, y, x, X, pt);
		break;
	}
}

static void
draw_points(void)
{
	int Y, X;
	int line;
	int n;

	AN(w_points);

	werase(w_points);
	if (n_ptarray == 0) {
		wnoutrefresh(w_points);
		return;
	}

	assert(current >= 0);
	assert(current < n_ptarray);
	assert(page_start >= 0);
	assert(page_start < n_ptarray);
	assert(current >= page_start);
	assert(current - page_start < l_points);

	getmaxyx(w_points, Y, X);
	(void)Y;
	(void)X;
	for (line = 0; line < l_points; line++) {
		n = line + page_start;
		if (n >= n_ptarray)
			break;
		if (n == current)
			wattron(w_points, A_BOLD);
		draw_line(w_points, line, ptarray[n]);
		if (n == current)
			wattroff(w_points, A_BOLD);
	}
	wnoutrefresh(w_points);
}

static void
draw_bar_b(void)
{
	int x, X;
	const struct VSC_level_desc *level;
	char buf[64];

	AN(w_bar_b);

	x = 0;
	X = getmaxx(w_bar_b);
	werase(w_bar_b);
	if (page_start + l_points < n_ptarray)
		mvwprintw(w_bar_b, 0, x, "vvv");
	x += 4;
	if (current < n_ptarray - 1)
		mvwprintw(w_bar_b, 0, x, "%s", ptarray[current]->name);

	snprintf(buf, sizeof(buf) - 1, "%d-%d/%d", page_start + 1,
	    page_start + l_points < n_ptarray ?
		page_start + l_points : n_ptarray,
	    n_ptarray);
	mvwprintw(w_bar_b, 0, X - strlen(buf), buf);
	X -= strlen(buf) + 2;

	level = VSC_LevelDesc(verbosity);
	if (level != NULL) {
		mvwprintw(w_bar_b, 0, X - strlen(level->label), "%s",
		    level->label);
		X -= strlen(level->label) + 2;
	}
	if (!hide_unseen) {
		mvwprintw(w_bar_b, 0, X - 6, "%s", "UNSEEN");
		X -= 8;
	}

	wnoutrefresh(w_bar_b);
}

static void
draw_info(void)
{

	if (w_info == NULL)
		return;

	werase(w_info);
	if (current < n_ptarray - 1) {
		/* XXX: Word wrapping, and overflow handling? */
		mvwprintw(w_info, 0, 0, "%s:",
		    ptarray[current]->vpt->desc->sdesc);
		mvwprintw(w_info, 1, 0, "%s",
		    ptarray[current]->vpt->desc->ldesc);
	}
	wnoutrefresh(w_info);
}

static void
draw_screen(void)
{
	draw_status();
	draw_bar_t();
	draw_points();
	draw_bar_b();
	draw_info();
	doupdate();
	redraw = 0;
}

static void
handle_keypress(int ch)
{
	switch (ch) {
	case KEY_UP:
		if (current == 0)
			return;
		current--;
		break;
	case KEY_DOWN:
		if (current == n_ptarray - 1)
			return;
		current++;
		break;
	case KEY_PPAGE:
	case 'b':
		current -= l_points;
		page_start -= l_points;
		break;
	case KEY_NPAGE:
	case ' ':
		current += l_points;
		if (page_start + l_points < n_ptarray - 1)
			page_start += l_points;
		break;
	case 'd':
		hide_unseen = 1 - hide_unseen;
		rebuild = 1;
		break;
	case 'e':
		scale = 1 - scale;
		rebuild = 1;
		break;
	case 'g':
		current = 0;
		page_start = 0;
		break;
	case 'G':
		current = n_ptarray - 1;
		page_start = current - l_points + 1;
		break;
	case 'v':
		verbosity++;
		if (VSC_LevelDesc(verbosity) == NULL)
			verbosity = 0;
		rebuild = 1;
		break;
	case 'q':
		keep_running = 0;
		return;
	case '\003':		/* Ctrl-C */
		AZ(raise(SIGINT));
		return;
	case '\024':		/* Ctrl-T */
		sample = 1;
		return;
	case '\032':		/* Ctrl-Z */
		AZ(raise(SIGTSTP));
		return;
	default:
		return;
	}

	update_position();
	redraw = 1;
}

void
do_curses(struct VSM_data *vd, double delay)
{
	struct pollfd pollfd;
	long t;
	int ch;
	double now;
	struct VSM_fantom f_main = VSM_FANTOM_NULL;
	struct VSM_fantom f_mgt = VSM_FANTOM_NULL;
	struct VSM_fantom f_iter = VSM_FANTOM_NULL;

	interval = delay;

	initscr();
	raw();
	noecho();
	nonl();
	curs_set(0);

	pollfd.fd = STDIN_FILENO;
	pollfd.events = POLLIN;

	make_windows();
	doupdate();

	VSC_C_mgt = VSC_Mgt(vd, &f_mgt);
	VSC_C_main = VSC_Main(vd, &f_main);
	init_hitrate();
	while (keep_running) {
		if (VSM_Abandoned(vd)) {
			init_hitrate();
			delete_pt_list();
			VSM_Close(vd);
			VSM_Open(vd);
		}
		VSC_C_mgt = VSC_Mgt(vd, &f_mgt);
		VSC_C_main = VSC_Main(vd, &f_main);
		if (VSM_valid != VSM_StillValid(vd, &f_iter))
			build_pt_list(vd, &f_iter);

		now = VTIM_mono();
		if (now - t_sample > interval)
			sample = 1;
		if (sample)
			sample_data();
		if (rebuild)
			build_pt_array();
		if (redraw)
			draw_screen();

		t = (t_sample + interval - now) * 1000;
		if (t > 0)
			(void)poll(&pollfd, 1, t);

		switch (ch = wgetch(w_status)) {
		case ERR:
			break;
#ifdef KEY_RESIZE /* sigh, Solaris lacks this.. */
		case KEY_RESIZE:
			make_windows();
			update_position();
			break;
#endif
		default:
			handle_keypress(ch);
			break;
		}
	}
	VSM_Close(vd);
	AZ(endwin());
}
