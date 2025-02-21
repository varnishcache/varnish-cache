/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dag-Erling Smørgrav <des@des.no>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Statistics output program
 */

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vtim.h"
#include "vapi/vsig.h"

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

#define VALUE_MAX		999999999999

#define REBUILD_NEXT		(1u << 0)
#define REBUILD_FIRST		(1u << 1)

enum kb_e {
#define BINDING(name, desc) KB_ ## name,
#define BINDING_SIG
#include "varnishstat_bindings.h"
};

struct ma {
	unsigned n, nmax;
	double acc;
};

struct pt {
	unsigned		magic;
#define PT_MAGIC		0x41698E4F
	VTAILQ_ENTRY(pt)	list;

	const struct VSC_point	*vpt;

	char			seen;

	uint64_t		cur, last;
	double			t_cur, t_last;
	double			chg, avg;

	struct ma		ma_10, ma_100, ma_1000;
};

struct hitrate {
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
static const volatile uint64_t *mgt_uptime;
static const volatile uint64_t *main_uptime;
static const volatile uint64_t *main_cache_hit;
static const volatile uint64_t *main_cache_miss;

static int l_status, l_bar_t, l_points, l_bar_b, l_info;
static unsigned colw_name = COLW_NAME_MIN;
static WINDOW *w_status = NULL;
static WINDOW *w_bar_t = NULL;
static WINDOW *w_points = NULL;
static WINDOW *w_bar_b = NULL;
static WINDOW *w_info = NULL;

static const struct VSC_level_desc *verbosity;
static int show_help = 0;
static int help_line = 0;
static int keep_running = 1;
static int hide_unseen = 1;
static int raw_vsc = 0;
static int page_start = 0;
static int current = 0;
static int rebuild = 0;
static int redraw = 0;
static int sample = 0;
static int reset_averages = 0;
static int scale = 1;
static double t_sample = 0.;
static double interval = 1.;
static unsigned vsm_status = 0;

#define NOTIF_MAXLEN 256
static char notification_message[NOTIF_MAXLEN] = "";
static vtim_mono notification_eol = 0.0;

static void
init_hitrate(void)
{
	memset(&hitrate, 0, sizeof (struct hitrate));
	if (main_cache_hit != NULL) {
		hitrate.lhit = *main_cache_hit;
		hitrate.lmiss = *main_cache_miss;
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
		current = vlimit_t(int, current, 0, n_ptarray - 1);
		page_start = vmin(page_start, current);
		if (current > page_start + (l_points - 1))
			page_start = current - (l_points - 1);
		page_start = vlimit_t(int, page_start, 0, n_ptarray - 1);
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
		if (pt->vpt->level > verbosity) {
			if (has_f && (rebuild & REBUILD_FIRST))
				verbosity = VSC_ChangeLevel(verbosity,
				    pt->vpt->level - verbosity);
			else
				continue;
		}
		if (!pt->seen && hide_unseen)
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
sample_points(void)
{
	struct pt *pt;
	uint64_t v;

	VTAILQ_FOREACH(pt, &ptlist, list) {
		AN(pt->vpt);
		AN(pt->vpt->ptr);
		v = VSC_Value(pt->vpt);
		if (v == 0 && !pt->seen)
			continue;
		if (!pt->seen) {
			pt->seen = 1;
			rebuild = REBUILD_NEXT;
		}
		pt->last = pt->cur;
		pt->cur = v;
		pt->t_last = pt->t_cur;
		pt->t_cur = VTIM_mono();

		if (reset_averages) {
			pt->chg = 0;
			pt->ma_10.n = 0;
			pt->ma_100.n = 0;
			pt->ma_1000.n = 0;
		}
		if (pt->t_last)
			pt->chg = ((int64_t)pt->cur - (int64_t)pt->last) /
			    (pt->t_cur - pt->t_last);

		if (pt->vpt->semantics == 'g') {
			pt->avg = 0.;
			update_ma(&pt->ma_10, (int64_t)pt->cur);
			update_ma(&pt->ma_100, (int64_t)pt->cur);
			update_ma(&pt->ma_1000, (int64_t)pt->cur);
		} else if (pt->vpt->semantics == 'c') {
			if (main_uptime != NULL && *main_uptime)
				pt->avg = pt->cur / (double)*main_uptime;
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
	double hr, mr, ratio;
	uint64_t hit, miss;

	if (main_cache_hit == NULL)
		return;

	hit = *main_cache_hit;
	miss = *main_cache_miss;
	hr = hit - hitrate.lhit;
	mr = miss - hitrate.lmiss;
	hitrate.lhit = hit;
	hitrate.lmiss = miss;

	if (hr + mr != 0)
		ratio = hr / (hr + mr);
	else
		ratio = 0;
	if (reset_averages) {
		hitrate.hr_10.n = 0;
		hitrate.hr_100.n = 0;
		hitrate.hr_1000.n = 0;
	}
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
	reset_averages = 0;
}

static void
destroy_window(WINDOW **w)
{

	AN(w);
	if (*w == NULL)
		return;
	AC(delwin(*w));
	*w = NULL;
}

static void
make_windows(void)
{
	int Y, X;
	int y;
	int y_status, y_bar_t, y_points, y_bar_b, y_info;

	destroy_window(&w_status);
	destroy_window(&w_bar_t);
	destroy_window(&w_points);
	destroy_window(&w_bar_b);
	destroy_window(&w_info);

	Y = LINES;
	X = COLS;

	l_status = LINES_STATUS;
	l_bar_t = LINES_BAR_T;
	l_bar_b = LINES_BAR_B;
	l_info = LINES_INFO;
	l_points = Y - (l_status + l_bar_t + l_bar_b + l_info);
	if (l_points < LINES_POINTS_MIN) {
		l_points += l_info;
		l_info = 0;
	}
	l_points = vmax(l_points, LINES_POINTS_MIN);

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
	AC(nodelay(w_status, 1));
	AC(keypad(w_status, 1));
	AC(wnoutrefresh(w_status));

	w_bar_t = newwin(l_bar_t, X, y_bar_t, 0);
	AN(w_bar_t);
	wbkgd(w_bar_t, A_REVERSE);
	AC(wnoutrefresh(w_bar_t));

	w_points = newwin(l_points, X, y_points, 0);
	AN(w_points);
	AC(wnoutrefresh(w_points));

	w_bar_b = newwin(l_bar_b, X, y_bar_b, 0);
	AN(w_bar_b);
	wbkgd(w_bar_b, A_REVERSE);
	AC(wnoutrefresh(w_bar_b));

	if (l_info) {
		w_info = newwin(l_info, X, y_info, 0);
		AN(w_info);
		AC(wnoutrefresh(w_info));
	}

	if (X - COLW_NAME_MIN > N_COL * COLW)
		colw_name = X - (N_COL * COLW);
	else
		colw_name = COLW_NAME_MIN;

	redraw = 1;
}

static void
print_duration(WINDOW *w, uint64_t t)
{

	IC(wprintw(w, "%4ju+%02ju:%02ju:%02ju",
	    (uintmax_t)t / 86400, (uintmax_t)(t % 86400) / 3600,
	    (uintmax_t)(t % 3600) / 60, (uintmax_t)t % 60));
}

static void
running(WINDOW *w, uint64_t up, int flg)
{
	if (vsm_status & flg) {
		print_duration(w_status, up);
	} else {
		wattron(w, A_STANDOUT);
		IC(wprintw(w, "  Not Running"));
		wattroff(w, A_STANDOUT);
	}
}

static void
draw_status(void)
{
	uint64_t up_mgt = 0;
	uint64_t up_chld = 0;

	AN(w_status);

	AC(werase(w_status));

	if (mgt_uptime != NULL)
		up_mgt = *mgt_uptime;
	if (main_uptime != NULL)
		up_chld = *main_uptime;

	IC(mvwprintw(w_status, 0, 0, "Uptime mgt:   "));
	running(w_status, up_mgt, VSM_MGT_RUNNING);
	IC(mvwprintw(w_status, 1, 0, "Uptime child: "));
	running(w_status, up_chld, VSM_WRK_RUNNING);
	IC(mvwprintw(w_status, 2, 0, "Press <h> to toggle help screen"));

	if (VTIM_mono() < notification_eol)
		mvwaddstr(w_status, 2, 0, notification_message);

	if (COLS > 70) {
		IC(mvwprintw(w_status, 0, getmaxx(w_status) - 37,
		    "Hitrate n: %8u %8u %8u", hitrate.hr_10.n, hitrate.hr_100.n,
		    hitrate.hr_1000.n));
		IC(mvwprintw(w_status, 1, getmaxx(w_status) - 37,
		    "   avg(n): %8.4f %8.4f %8.4f", hitrate.hr_10.acc,
		    hitrate.hr_100.acc, hitrate.hr_1000.acc));
	}

	AC(wnoutrefresh(w_status));
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
	AC(werase(w_bar_t));
	if (page_start > 0)
		IC(mvwprintw(w_bar_t, 0, x, "^^^"));
	x += 4;
	IC(mvwprintw(w_bar_t, 0, x, "%.*s", colw_name - 4, "NAME"));
	x += colw_name - 4;
	col = COL_CUR;
	while (col < COL_LAST) {
		if (X - x < COLW)
			break;
		switch (col) {
		case COL_CUR:
			IC(mvwprintw(w_bar_t, 0, x, " %12.12s", "CURRENT"));
			break;
		case COL_CHG:
			IC(mvwprintw(w_bar_t, 0, x, " %12.12s", "CHANGE"));
			break;
		case COL_AVG:
			IC(mvwprintw(w_bar_t, 0, x, " %12.12s", "AVERAGE"));
			break;
		case COL_MA10:
			IC(mvwprintw(w_bar_t, 0, x, " %12.12s", "AVG_10"));
			break;
		case COL_MA100:
			IC(mvwprintw(w_bar_t, 0, x, " %12.12s", "AVG_100"));
			break;
		case COL_MA1000:
			IC(mvwprintw(w_bar_t, 0, x, " %12.12s", "AVG_1000"));
			break;
		default:
			break;
		}
		x += COLW;
		col++;
	}

	AC(wnoutrefresh(w_bar_t));
}

static void
draw_line_default(WINDOW *w, int y, int x, int X, const struct pt *pt)
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

	col = COL_CUR;
	while (col < COL_LAST) {
		if (X - x < COLW)
			break;
		switch (col) {
		case COL_CUR:
			IC(mvwprintw(w, y, x, " %12ju", (uintmax_t)pt->cur));
			break;
		case COL_CHG:
			if (pt->t_last)
				IC(mvwprintw(w, y, x, " %12.2f", pt->chg));
			else
				IC(mvwprintw(w, y, x, " %12s", ".  "));
			break;
		case COL_AVG:
			if (pt->avg)
				IC(mvwprintw(w, y, x, " %12.2f", pt->avg));
			else
				IC(mvwprintw(w, y, x, " %12s", ".  "));
			break;
		case COL_MA10:
			IC(mvwprintw(w, y, x, " %12.2f", pt->ma_10.acc));
			break;
		case COL_MA100:
			IC(mvwprintw(w, y, x, " %12.2f", pt->ma_100.acc));
			break;
		case COL_MA1000:
			IC(mvwprintw(w, y, x, " %12.2f", pt->ma_1000.acc));
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
	IC(wprintw(w, " %12.2f%c", val, q));
}

static void
print_trunc(WINDOW *w, uintmax_t val)
{
	if (val > VALUE_MAX) {
		while (val > VALUE_MAX)
			val /= 1000;
		IC(wprintw(w, " %9ju...", val));
	} else
		IC(wprintw(w, " %12ju", val));
}

static void
draw_line_bytes(WINDOW *w, int y, int x, int X, const struct pt *pt)
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

	col = COL_CUR;
	while (col < COL_LAST) {
		if (X - x < COLW)
			break;
		wmove(w, y, x);
		switch (col) {
		case COL_CUR:
			if (scale && pt->cur > 1024)
				print_bytes(w, (double)pt->cur);
			else
				print_trunc(w, (uintmax_t)pt->cur);
			break;
		case COL_CHG:
			if (pt->t_last)
				print_bytes(w, pt->chg);
			else
				IC(wprintw(w, " %12s", ".  "));
			break;
		case COL_AVG:
			if (pt->avg)
				print_bytes(w, pt->avg);
			else
				IC(wprintw(w, " %12s", ".  "));
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
draw_line_bitmap(WINDOW *w, int y, int x, int X, const struct pt *pt)
{
	unsigned ch;
	enum {
		COL_VAL,
		COL_MAP,
		COL_LAST
	} col;

	AN(w);
	AN(pt);
	assert(pt->vpt->format == 'b');

	col = COL_VAL;
	while (col < COL_LAST) {
		switch (col) {
		case COL_VAL:
			if (X - x < COLW)
				return;
			IC(mvwprintw(w, y, x, "   %10.10jx",
			    (uintmax_t)((pt->cur >> 24) & 0xffffffffffLL)));
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
draw_line_duration(WINDOW *w, int y, int x, int X, const struct pt *pt)
{
	enum {
		COL_DUR,
		COL_LAST
	} col;

	AN(w);
	AN(pt);

	col = COL_DUR;
	while (col < COL_LAST) {
		if (X - x < COLW)
			break;
		switch (col) {
		case COL_DUR:
			wmove(w, y, x);
			if (scale)
				print_duration(w, pt->cur);
			else
				IC(wprintw(w, " %12ju", (uintmax_t)pt->cur));
			break;
		default:
			break;
		}
		x += COLW;
		col++;
	}
}

static void
draw_line(WINDOW *w, int y, const struct pt *pt)
{
	int x, X;

	assert(colw_name >= COLW_NAME_MIN);
	X = getmaxx(w);
	x = 0;
	if (strlen(pt->vpt->name) > colw_name)
		IC(mvwprintw(w, y, x, "%.*s...", colw_name - 3, pt->vpt->name));
	else
		IC(mvwprintw(w, y, x, "%.*s", colw_name, pt->vpt->name));
	x += colw_name;

	switch (pt->vpt->format) {
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
	int line;
	int n;

	AN(w_points);

	AC(werase(w_points));
	if (n_ptarray == 0) {
		AC(wnoutrefresh(w_points));
		return;
	}

	assert(current >= 0);
	assert(current < n_ptarray);
	assert(page_start >= 0);
	assert(page_start < n_ptarray);
	assert(current >= page_start);
	assert(current - page_start < l_points);

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
	AC(wnoutrefresh(w_points));
}

static void
draw_help(void)
{
	const char *const *p;
	int l, y, X;

	if (l_points >= bindings_help_len) {
		assert(help_line == 0);
		l = bindings_help_len;
	} else {
		assert(help_line >= 0);
		assert(help_line <= bindings_help_len - l_points);
		l = l_points;
	}

	X = getmaxx(w_points);
	AC(werase(w_points));

	for (y = 0, p = bindings_help + help_line; y < l; y++, p++) {
		if (**p == '\t') {
			IC(mvwprintw(w_points, y, 0, "    %.*s", X - 4, *p + 1));
		} else {
			wattron(w_points, A_BOLD);
			IC(mvwprintw(w_points, y, 0, "%.*s", X, *p));
			wattroff(w_points, A_BOLD);
		}
	}

	AC(wnoutrefresh(w_points));
}

static void
draw_bar_b(void)
{
	int x, X;
	char buf[64];

	AN(w_bar_b);

	x = 0;
	X = getmaxx(w_bar_b);
	AC(werase(w_bar_b));
	if (page_start + l_points < n_ptarray)
		IC(mvwprintw(w_bar_b, 0, x, "vvv"));
	x += 4;
	if (current < n_ptarray)
		IC(mvwprintw(w_bar_b, 0, x, "%s", ptarray[current]->vpt->name));

	bprintf(buf, "%d-%d/%d", page_start + 1,
	    page_start + l_points < n_ptarray ?
		page_start + l_points : n_ptarray,
	    n_ptarray);
	IC(mvwprintw(w_bar_b, 0, X - strlen(buf), "%s", buf));
	X -= strlen(buf) + 2;

	if (verbosity != NULL) {
		IC(mvwprintw(w_bar_b, 0, X - strlen(verbosity->label), "%s",
		    verbosity->label));
		X -= strlen(verbosity->label) + 2;
	}
	if (!hide_unseen) {
		IC(mvwprintw(w_bar_b, 0, X - 6, "%s", "UNSEEN"));
		X -= 8;
	}
	if (raw_vsc)
		IC(mvwprintw(w_bar_b, 0, X - 3, "%s", "RAW"));

	AC(wnoutrefresh(w_bar_b));
}

static void
draw_info(void)
{

	if (w_info == NULL)
		return;

	AC(werase(w_info));
	if (current < n_ptarray) {
		/* XXX: Word wrapping, and overflow handling? */
		IC(mvwprintw(w_info, 0, 0, "%s:",
		    ptarray[current]->vpt->sdesc));
		IC(mvwprintw(w_info, 1, 0, "%s",
		    ptarray[current]->vpt->ldesc));
	}
	AC(wnoutrefresh(w_info));
}

static void
draw_screen(void)
{
	draw_status();
	if (show_help) {
		AC(werase(w_bar_t));
		AC(werase(w_bar_b));
		AC(werase(w_info));
		AC(wnoutrefresh(w_bar_t));
		AC(wnoutrefresh(w_bar_b));
		AC(wnoutrefresh(w_info));
		draw_help();
	} else {
		draw_bar_t();
		draw_points();
		draw_bar_b();
		draw_info();
	}
	AC(doupdate());
	redraw = 0;
}

static void
handle_common_keypress(enum kb_e kb)
{

	switch (kb) {
	case KB_QUIT:
		keep_running = 0;
		return;
	case KB_SIG_INT:
		AZ(raise(SIGINT));
		return;
	case KB_SIG_TSTP:
		AZ(raise(SIGTSTP));
		return;
	default:
		WRONG("unexpected key binding");
	}
}

static void
handle_points_keypress(struct vsc *vsc, enum kb_e kb)
{

	switch (kb) {
	case KB_HELP:
		show_help = 1;
		help_line = 0;
		redraw = 1;
		return;
	case KB_UP:
		if (current == 0)
			return;
		current--;
		break;
	case KB_DOWN:
		if (current == n_ptarray - 1)
			return;
		current++;
		break;
	case KB_PAGEUP:
		current -= l_points;
		page_start -= l_points;
		break;
	case KB_PAGEDOWN:
		current += l_points;
		if (page_start + l_points < n_ptarray - 1)
			page_start += l_points;
		break;
	case KB_TOP:
		current = 0;
		break;
	case KB_BOTTOM:
		current = n_ptarray - 1;
		break;
	case KB_UNSEEN:
		hide_unseen = 1 - hide_unseen;
		rebuild = REBUILD_NEXT;
		break;
	case KB_RAW:
		AN(VSC_Arg(vsc, 'r', NULL));
		raw_vsc = VSC_IsRaw(vsc);
		rebuild = REBUILD_NEXT;
		break;
	case KB_SCALE:
		scale = 1 - scale;
		rebuild = REBUILD_NEXT;
		break;
	case KB_ACCEL:
		interval += 0.1;
		(void)snprintf(notification_message, NOTIF_MAXLEN,
		    "Refresh interval set to %.1f seconds.", interval);

		notification_eol = VTIM_mono() + 1.25;
		break;
	case KB_DECEL:
		interval -= 0.1;
		if (interval < 0.1)
			interval = 0.1;
		(void)snprintf(notification_message, NOTIF_MAXLEN,
		    "Refresh interval set to %.1f seconds.", interval);

		notification_eol = VTIM_mono() + 1.25;
		break;
	case KB_VERBOSE:
		verbosity = VSC_ChangeLevel(verbosity, 1);
		rebuild = REBUILD_NEXT;
		break;
	case KB_QUIET:
		verbosity = VSC_ChangeLevel(verbosity, -1);
		rebuild = REBUILD_NEXT;
		break;
	case KB_SAMPLE:
		sample = 1;
		return;
	case KB_RESET_AVERAGES:
		reset_averages = 1;
		return;
	case KB_QUIT:
	case KB_SIG_INT:
	case KB_SIG_TSTP:
		handle_common_keypress(kb);
		return;
	default:
		WRONG("unhandled key binding");
	}

	update_position();
	redraw = 1;
}

static void
handle_help_keypress(enum kb_e kb)
{
	int hl = help_line;

	switch (kb) {
	case KB_HELP:
		show_help = 0;
		redraw = 1;
		return;
	case KB_UP:
		help_line--;
		break;
	case KB_DOWN:
		help_line++;
		break;
	case KB_PAGEUP:
		help_line -= l_points;
		break;
	case KB_PAGEDOWN:
		help_line += l_points;
		break;
	case KB_TOP:
		help_line = 0;
		break;
	case KB_BOTTOM:
		help_line = bindings_help_len;
		break;
	case KB_UNSEEN:
	case KB_RAW:
	case KB_SCALE:
	case KB_ACCEL:
	case KB_DECEL:
	case KB_VERBOSE:
	case KB_QUIET:
	case KB_SAMPLE:
	case KB_RESET_AVERAGES:
		break;
	case KB_QUIT:
	case KB_SIG_INT:
	case KB_SIG_TSTP:
		handle_common_keypress(kb);
		return;
	default:
		WRONG("unhandled key binding");
	}

	help_line = vlimit_t(int, help_line, 0, bindings_help_len - l_points);
	redraw = (help_line != hl);
}

static void
handle_keypress(struct vsc *vsc, int ch)
{
	enum kb_e kb;

	switch (ch) {
#define BINDING_KEY(chr, name, or)	\
	case chr:
#define BINDING(name, desc)		\
		kb = KB_ ## name;	\
		break;
#define BINDING_SIG
#include "varnishstat_bindings.h"
	default:
		return;
	}

	if (show_help)
		handle_help_keypress(kb);
	else
		handle_points_keypress(vsc, kb);
}

static void * v_matchproto_(VSC_new_f)
newpt(void *priv, const struct VSC_point *const vpt)
{
	struct pt *pt;

	AZ(priv);
	ALLOC_OBJ(pt, PT_MAGIC);
	rebuild |= REBUILD_NEXT;
	AN(pt);
	pt->vpt = vpt;
	pt->last = VSC_Value(vpt);
	pt->ma_10.nmax = 10;
	pt->ma_100.nmax = 100;
	pt->ma_1000.nmax = 1000;

	VTAILQ_INSERT_TAIL(&ptlist, pt, list);
	n_ptlist++;

	AZ(strcmp(vpt->ctype, "uint64_t"));

	if (!strcmp(vpt->name, "MGT.uptime"))
		mgt_uptime = vpt->ptr;
	if (!strcmp(vpt->name, "MAIN.uptime"))
		main_uptime = vpt->ptr;
	if (!strcmp(vpt->name, "MAIN.cache_hit"))
		main_cache_hit = vpt->ptr;
	if (!strcmp(vpt->name, "MAIN.cache_miss"))
		main_cache_miss = vpt->ptr;
	return (pt);
}

static void v_matchproto_(VSC_destroy_f)
delpt(void *priv, const struct VSC_point *const vpt)
{
	struct pt *pt;

	AZ(priv);
	CAST_OBJ_NOTNULL(pt, vpt->priv, PT_MAGIC);
	rebuild |= REBUILD_NEXT;
	VTAILQ_REMOVE(&ptlist, pt, list);
	n_ptlist--;
	FREE_OBJ(pt);
	if (vpt->ptr == mgt_uptime)
		mgt_uptime = NULL;
	if (vpt->ptr == main_uptime)
		main_uptime = NULL;
	if (vpt->ptr == main_cache_hit)
		main_cache_hit = NULL;
	if (vpt->ptr == main_cache_miss)
		main_cache_miss = NULL;
}

void
do_curses(struct vsm *vsm, struct vsc *vsc)
{
	long t;
	int ch;
	double now;

	verbosity = VSC_ChangeLevel(NULL, 0);

	(void)initscr();
	AC(raw());
	AC(noecho());
	AC(nonl());
	IC(curs_set(0));

	make_windows();
	AC(doupdate());

	VSC_State(vsc, newpt, delpt, NULL);

	raw_vsc = VSC_IsRaw(vsc);
	rebuild |= REBUILD_FIRST;
	(void)VSC_Iter(vsc, vsm, NULL, NULL);
	build_pt_array();
	init_hitrate();

	while (keep_running && !VSIG_int && !VSIG_term && !VSIG_hup) {
		(void)VSC_Iter(vsc, vsm, NULL, NULL);
		vsm_status = VSM_Status(vsm);
		if (vsm_status & (VSM_MGT_RESTARTED|VSM_WRK_RESTARTED))
			init_hitrate();
		if (rebuild)
			build_pt_array();

		now = VTIM_mono();
		if (now - t_sample > interval)
			sample = 1;
		if (sample)
			sample_data();
		if (redraw)
			draw_screen();

		t = (long)((t_sample + interval - now) * 1000);
		wtimeout(w_status, t);

		ch = wgetch(w_status);
		switch (ch) {
		case ERR:
			break;
#ifdef KEY_RESIZE /* sigh, Solaris lacks this.. */
		case KEY_RESIZE:
			make_windows();
			update_position();
			break;
#endif
		default:
			handle_keypress(vsc, ch);
			break;
		}
	}
	VSC_Destroy(&vsc, vsm);
	AN(VTAILQ_EMPTY(&ptlist));
	VSM_Destroy(&vsm);
	AZ(endwin());
}
