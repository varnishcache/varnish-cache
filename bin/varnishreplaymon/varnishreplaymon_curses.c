/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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
 * Statistics output program
 */

#include "config.h"

#include <sys/time.h>

#include <vcurses.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vas.h"
#include "vtcp.h"
#include "vapi/vsc.h"
#include "vqueue.h"
#include "../varnishreplay/rsm.h"
#include "../varnishreplay/stat_cnt.h"
#include "varnishreplaymon.h"

#if 0
#define AC(x) assert((x) != ERR)
#else
#define AC(x) x
#endif

typedef enum {
	undef_sort_thr,
	asc_sort_thr,
	dsc_sort_thr,
} sort_thr_t;

static int cmp_rsm_thr(const void *v1, const void *v2)
{
	// ascending order, default
	return (((rsm_thr_entry*) v1)->reqs < ((rsm_thr_entry*) v2)->reqs);
}

static int cmp_rsm_thr_dsc(const void *v1, const void *v2)
{
	// descending order
	return (((rsm_thr_entry*) v1)->reqs > ((rsm_thr_entry*) v2)->reqs);
}

static void
myexp(double *acc, double val, unsigned *n, unsigned nmax)
{

	if (*n < nmax)
		(*n)++;
	(*acc) += (val - *acc) / (double)*n;
}

void
do_curses(struct replay_shm* rsmh,
    int delay)
{
	struct timeval tv;
	double tt, lt, reqs, lreqs, rrate;
	double a0, a1, a2, a3;
	unsigned n0, n1, n2, n3;

	time_t rt;
	int ch = 0, line, i;

	int x, y;
	int stat_cnt, neq_stat_cnt, thr_cnt, thr_sh_cnt, thr_req_cnt, thr_resp_cnt, thr_lns_cnt;
	static void* thr_arr = NULL;
	rsm_err_entry* perr;
	rsm_thr_entry* pthr;
	sort_thr_t sort_thr = undef_sort_thr;
	bool show_err = false;
	bool show_thr = true;

	(void)initscr();
	AC(raw());
	AC(noecho());
	AC(nonl());
	AC(intrflush(stdscr, FALSE));
	(void)curs_set(0);	/* XXX: too many implementations are bogus */

	if (! thr_arr)
		thr_arr = calloc(1, sizeof(rsm_thr_entry) * MAX_THR_CNT);

	while (1) {
		/*
		 * Initialization goes in outher loop
		 */
		AC(erase());
		AC(refresh());

		a0 = a1 = a2 = a3 = 0.0;
		n0 = n1 = n2 = n3 = 0;
		lt = 0;
		lreqs = 0;

		while (1) {
			AZ(gettimeofday(&tv, NULL));
			tt = tv.tv_usec * 1e-6 + tv.tv_sec;
			lt = tt - lt;

			if (rsm_reopen())
				break;

			rt = rsmh->rsm_gen.uptime;

			AC(mvprintw(0, 0, "%*s", COLS - 1, rsmh->rsm_gen.appname));
			AC(mvprintw(0, 0, "%d+%02d:%02d:%02d", (int)(rt / 86400),
			    (int)((rt % 86400) / 3600), (int)((rt % 3600) / 60), (int)rt % 60));

			reqs = rsmh->rsm_gen.nreqs;
			rrate = (reqs - lreqs) / lt;
			lreqs = reqs;
			myexp(&a0, rrate, &n0, 1);
			myexp(&a1, rrate, &n1, 10);
			myexp(&a2, rrate, &n2, 100);
			myexp(&a3, rrate, &n3, 1000);

			AC(mvprintw(1, 0, " REQs ratio:   %8u %8u %8u %8u",
			    n0, n1, n2, n3));
			AC(mvprintw(2, 0, " REQs avg  :   %8.4f %8.4f %8.4f %8.4f",
			    a0, a1, a2, a3));
			line = 4;

			clrtobot();

			stat_cnt = 0;
			neq_stat_cnt = 0;

			AC(mvprintw(line, 0, " STATUSes  :   %8s %8s", "CNT", "NE_CNT"));
			for (i = 0; i < MAX_STT_CNT; i++) {
				if (rsmh->rsm_stt[i].status > 0) {
					if (line < LINES-2) {
						++line;
						AC(mvprintw(line, 0, "        %3d:   %8d %8d %8s", i, rsmh->rsm_stt[i].status, rsmh->rsm_stt[i].neq_status, ""));
					}
					stat_cnt += rsmh->rsm_stt[i].status;
					neq_stat_cnt += rsmh->rsm_stt[i].neq_status;
				}
			}
			AC(mvprintw(++line, 0, "        SUM:   %8d %8d", stat_cnt, neq_stat_cnt));
			clrtoeol();
			++line;

			if (show_err) {
				perr = rsmh->rsm_err;
				AC(mvprintw(++line, 0, " CNTs      :   %8s %8s %8s %8s %8s", 
					rsm_err_labels[0], rsm_err_labels[1], rsm_err_labels[2], rsm_err_labels[3], rsm_err_labels[4]));
				AC(mvprintw(++line, 0, "               %8d %8d %8d %8d %8d", 
					perr[0].count, perr[1].count, perr[2].count, perr[3].count, perr[4].count));
				clrtoeol();
				++line;
			}

			thr_cnt = 0;
			thr_sh_cnt = 0;
			thr_req_cnt = 0;
			thr_resp_cnt = 0;
			thr_lns_cnt = 0;

			if (show_thr) {
				if (sort_thr) {
					memcpy(thr_arr, rsmh->rsm_thr, sizeof(rsmh->rsm_thr));
					qsort(thr_arr, MAX_THR_CNT, sizeof(rsm_thr_entry), ((sort_thr == asc_sort_thr) ? cmp_rsm_thr : cmp_rsm_thr_dsc));
					pthr = thr_arr;
				} else 
					pthr = rsmh->rsm_thr;

				AC(mvprintw(++line, 0, " THREADs   :   %8s %8s %8s %8s %3s/%3s/%4s", "REQ", "RESP", "FD", "LINES", "SNT", "CNT", "NTHR"));
				for (i = 0; i < MAX_THR_CNT; i++, pthr++) {
					if (pthr->reqs > 0) {
						if (line < LINES-3) {
							++line;
							thr_sh_cnt++;
							AC(mvprintw(line, 0, "   %8d:   %8d %8d %8d %8d", pthr->id, pthr->reqs, pthr->resp, pthr->fd, pthr->lines));
						}
						thr_cnt++;
						thr_req_cnt += pthr->reqs;
						thr_resp_cnt += pthr->resp;
						thr_lns_cnt += pthr->lines;
					}
				}
				AC(mvprintw(++line, 0, "        SUM:   %8d %8d %8s %8d %3d/%3d/%4d", thr_req_cnt, thr_resp_cnt, "-", thr_lns_cnt, thr_sh_cnt, thr_cnt, rsmh->rsm_gen.nthreads));
			}

			if (debug) {
				getyx(stdscr, y, x);
				AC(mvprintw(LINES-1, 0, "%d/%d ch:%c sort:%d -- sh_er:%d sh_th:%d -- dly:%d", y, x, ch, sort_thr, show_err, show_thr, delay));
			}

			lt = tt;

			AC(refresh());
			timeout(delay * 1000);
			switch ((ch = getch())) {
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
				AZ(raise(SIGTSTP));
				break;
			case '\021': /* Ctrl-Q */
			case 'Q':
			case 'q':
				AC(endwin());
				exit(0);
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				delay = 1U << (ch - '0');
				break;
			case 'O':
				sort_thr = (sort_thr == dsc_sort_thr) ? undef_sort_thr : dsc_sort_thr;
				break;
			case 'o':
				sort_thr = (sort_thr == asc_sort_thr) ? undef_sort_thr : asc_sort_thr;
				break;
			case 'e':
				show_err = !show_err;
				break;
			case 't':
				show_thr = !show_thr;
				break;
			default:
				AC(beep());
				break;
			}
		}
	}
}
