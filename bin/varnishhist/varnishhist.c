/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VOPT_DEFINITION
#define VOPT_INC "varnishhist_options.h"

#include "vdef.h"
#include "vcurses.h"
#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "vut.h"
#include "vtim.h"

#define HIST_N 2000		/* how far back we remember */
#define HIST_RES 100		/* bucket resolution */

static struct VUT *vut;

static int hist_low;
static int hist_high;
static int hist_range;
static unsigned hist_buckets;

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static int end_of_file = 0;
static int ms_delay = 1000;
static unsigned rr_hist[HIST_N];
static unsigned nhist;
static unsigned next_hist;
static unsigned *bucket_miss;
static unsigned *bucket_hit;
static char *format;
static int match_tag;
static double timebend = 0, t0;
static double vsl_t0 = 0, vsl_to, vsl_ts = 0;
static pthread_cond_t timebend_cv;
static double log_ten;
static char *ident;

static const unsigned scales[] = {
	1,
	2,
	3,
	4,
	5,
	10,
	15,
	20,
	25,
	50,
	100,
	250,
	500,
	1000,
	2500,
	5000,
	10000,
	25000,
	50000,
	100000,
	UINT_MAX
};

struct profile {
	const char *name;
	char VSL_arg;
	enum VSL_tag_e tag;
	const char *prefix;
	int field;
	int hist_low;
	int hist_high;
};

#define HIS_PROF(name,vsl_arg,tag,prefix,field,hist_low,high_high,doc)	\
	{name,vsl_arg,tag,prefix,field,hist_low,high_high},
#define HIS_NO_PREFIX	NULL
#define HIS_CLIENT	'c'
#define HIS_BACKEND	'b'
static const struct profile profiles[] = {
#include "varnishhist_profiles.h"
	{ NULL }
};
#undef HIS_NO_PREFIX
#undef HIS_BACKEND
#undef HIS_CLIENT
#undef HIS_PROF

static const struct profile *active_profile;

static volatile sig_atomic_t quit = 0;

static void
update(void)
{
	char t[VTIM_FORMAT_SIZE];
	const unsigned w = COLS / hist_range;
	const unsigned n = w * hist_range;
	unsigned bm[n], bh[n];
	unsigned max;
	unsigned i, j, scale;
	unsigned k, l;

	erase();

	/* Draw horizontal axis */
	for (i = 0; i < n; ++i)
		(void)mvaddch(LINES - 2, i, '-');
	for (k = 0, l = hist_low; k < hist_range; ++k, ++l) {
		(void)mvaddch(LINES - 2, w * k, '+');
		mvprintw(LINES - 1, w * k, "|1e%d", l);
	}

	if (end_of_file)
		mvprintw(0, 0, "%*s", COLS - 1, "EOF");
	else
		mvprintw(0, 0, "%*s", COLS - 1, ident);

	/* count our flock */
	memset(bm, 0, sizeof bm);
	memset(bh, 0, sizeof bh);
	for (k = 0, max = 1; k < hist_buckets; ++k) {
		l = k * n / hist_buckets;
		assert(l < n);
		bm[l] += bucket_miss[k];
		bh[l] += bucket_hit[k];
		if (bm[l] + bh[l] > max)
			max = bm[l] + bh[l];
	}

	/* scale,time */
	assert(LINES - 3 >= 0);
	for (i = 0; max / scales[i] > (unsigned)(LINES - 3); ++i)
		/* nothing */ ;
	scale = scales[i];

	if (vsl_t0 > 0) {
		VTIM_format(vsl_ts, t);

		mvprintw(0, 0, "1:%u, n = %u, d = %g @ %s x %g",
		    scale, nhist, 1e-3 * ms_delay, t, timebend);
	} else
		mvprintw(0, 0, "1:%u, n = %u, d = %g",
		    scale, nhist, 1e-3 * ms_delay);

	for (j = 5; j < LINES - 2; j += 5)
		mvprintw((LINES - 2) - j, 0, "%u_", j * scale);

	/* show them */
	for (i = 0; i < n; ++i) {
		for (j = 0; j < bm[i] / scale; ++j)
			(void)mvaddch((LINES - 3) - j, i, '#');
		for (; j < (bm[i] + bh[i]) / scale; ++j)
			(void)mvaddch((LINES - 3) - j, i, '|');
	}

	refresh();
}

inline static void
upd_vsl_ts(const char *p)
{
	double t;

	if (timebend == 0)
		return;

	p = strchr(p, ' ');

	if (p == NULL)
		return;

	t = strtod(p + 1, NULL);

	if (t > vsl_ts)
		vsl_ts = t;
}

static int v_matchproto_ (VSLQ_dispatch_f)
accumulate(struct VSL_data *vsl, struct VSL_transaction * const pt[],
    void *priv)
{
	int i, tag, skip, match, hit;
	unsigned u;
	double value = 0;
	struct VSL_transaction *tr;
	double t;
	const char *tsp;

	(void)vsl;
	(void)priv;

	for (tr = pt[0]; tr != NULL; tr = *++pt) {
		if (tr->reason == VSL_r_esi)
			/* Skip ESI requests */
			continue;

		hit = 0;
		skip = 0;
		match = 0;
		tsp = NULL;
		while (skip == 0) {
			i = VSL_Next(tr->c);
			if (i == -3) {
				/* overrun - need to skip forward */
				AZ(pthread_mutex_lock(&mtx));
				vsl_to = vsl_t0 = vsl_ts = 0;
				t0 = VTIM_mono();
				AZ(pthread_mutex_unlock(&mtx));
				break;
			}
			if (i != 1)
				break;

			/* get the value we want and register if it's a hit */
			tag = VSL_TAG(tr->c->rec.ptr);

			if (VSL_tagflags[tag])
				continue;

			switch (tag) {
			case SLT_Hit:
				hit = 1;
				break;
			case SLT_VCL_return:
				if (!strcasecmp(VSL_CDATA(tr->c->rec.ptr),
				    "restart") ||
				    !strcasecmp(VSL_CDATA(tr->c->rec.ptr),
				    "retry"))
					skip = 1;
				break;
			case SLT_Timestamp:
				tsp = VSL_CDATA(tr->c->rec.ptr);
				/* FALLTHROUGH */
			default:
				if (tag != match_tag)
					break;

				if (active_profile->prefix &&
				    strncmp(VSL_CDATA(tr->c->rec.ptr),
				    active_profile->prefix,
				    strlen(active_profile->prefix)) != 0)
					break;

				i = sscanf(VSL_CDATA(tr->c->rec.ptr),
				    format, &value);
				if (i != 1)
					break;
				match = 1;
				break;
			}
		}

		if (skip || !match)
			continue;

		/* select bucket */
		i = HIST_RES * lround(log(value) / log_ten);
		if (i < hist_low * HIST_RES)
			i = hist_low * HIST_RES;
		if (i >= hist_high * HIST_RES)
			i = hist_high * HIST_RES - 1;
		i -= hist_low * HIST_RES;
		assert(i >= 0);
		assert(i < hist_buckets);

		AZ(pthread_mutex_lock(&mtx));

		/*
		 * only parse the last tsp seen in this transaction -
		 * it should be the latest.
		 */
		if (tsp)
			upd_vsl_ts(tsp);

		/* phase out old data */
		if (nhist == HIST_N) {
			u = rr_hist[next_hist];
			if (u >= hist_buckets) {
				u -= hist_buckets;
				assert(u < hist_buckets);
				assert(bucket_hit[u] > 0);
				bucket_hit[u]--;
			} else {
				assert(bucket_miss[u] > 0);
				bucket_miss[u]--;
			}
		} else {
			++nhist;
		}

		/* phase in new data */
		if (hit) {
			bucket_hit[i]++;
			rr_hist[next_hist] = i + hist_buckets;
		} else {
			bucket_miss[i]++;
			rr_hist[next_hist] = i;
		}
		if (++next_hist == HIST_N) {
			next_hist = 0;
		}
		AZ(pthread_mutex_unlock(&mtx));
	}

	if (vsl_ts < vsl_to)
		return (0);

	t = VTIM_mono();

	AZ(pthread_mutex_lock(&mtx));
	if (vsl_t0 == 0)
		vsl_to = vsl_t0 = vsl_ts;

	assert(t > t0);
	vsl_to = vsl_t0 + (t - t0) * timebend;

	if (vsl_ts > vsl_to) {
		double when = VTIM_real() + vsl_ts - vsl_to;
		struct timespec ts;
		ts.tv_nsec = (long)(modf(when, &t) * 1e9);
		ts.tv_sec = (long)t;
		i = pthread_cond_timedwait(&timebend_cv, &mtx, &ts);
		assert(i == 0 || i == ETIMEDOUT);
	}
	AZ(pthread_mutex_unlock(&mtx));

	return (0);
}

static int v_matchproto_(VUT_cb_f)
sighup(struct VUT *v)
{
	assert(v == vut);
	quit = 1;
	return (1);
}

static void * v_matchproto_(pthread_t)
do_curses(void *arg)
{
	int ch;
	(void)arg;

	initscr();
	raw();
	noecho();
	nonl();
	intrflush(stdscr, FALSE);
	curs_set(0);
	erase();
	while (!quit) {
		AZ(pthread_mutex_lock(&mtx));
		update();
		AZ(pthread_mutex_unlock(&mtx));

		assert(ms_delay > 0);
		timeout(ms_delay);
		switch ((ch = getch())) {
		case ERR:
			break;
#ifdef KEY_RESIZE
		case KEY_RESIZE:
			erase();
			break;
#endif
		case '\014':	/* Ctrl-L */
		case '\024':	/* Ctrl-T */
			redrawwin(stdscr);
			refresh();
			break;
		case '\032':	/* Ctrl-Z */
			endwin();
			AZ(raise(SIGTSTP));
			break;
		case '\003':	/* Ctrl-C */
		case '\021':	/* Ctrl-Q */
		case 'Q':
		case 'q':
			AZ(raise(SIGINT));
			endwin();
			return (NULL);
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
			ms_delay = 1000U << (ch - '0');
			break;
		case '+':
			ms_delay /= 2;
			if (ms_delay < 1)
				ms_delay = 1;
			break;
		case '-':
			ms_delay *= 2;
			break;
		case '>':
		case '<':
			/* see below */
			break;
		default:
			beep();
			break;
		}

		if (ch == '<' || ch == '>') {
			AZ(pthread_mutex_lock(&mtx));
			vsl_to = vsl_t0 = vsl_ts;
			t0 = VTIM_mono();
			if (timebend == 0)
				timebend = 1;
			else if (ch == '<')
				timebend /= 2;
			else
				timebend *= 2;
			AZ(pthread_cond_broadcast(&timebend_cv));
			AZ(pthread_mutex_unlock(&mtx));
		}
	}
	endwin();
	return (NULL);
}

/*--------------------------------------------------------------------*/

static void v_noreturn_
usage(int status)
{
	const char **opt;

	fprintf(stderr, "Usage: %s <options>\n\n", vut->progname);
	fprintf(stderr, "Options:\n");
	for (opt = vopt_spec.vopt_usage; *opt != NULL; opt += 2)
		fprintf(stderr, " %-25s %s\n", *opt, *(opt + 1));
	exit(status);
}

static void v_noreturn_
profile_error(const char *s)
{
	fprintf(stderr, "-P: '%s' is not a valid"
	    " profile name or definition\n", s);
	exit(1);
}

static void
vut_sighandler(int sig)
{

	if (vut != NULL)
		VUT_Signaled(vut, sig);
}

int
main(int argc, char **argv)
{
	int i;
	char *colon;
	const char *ptag, *profile = "responsetime";
	pthread_t thr;
	int fnum;
	struct profile cli_p = {0};

	vut = VUT_InitProg(argc, argv, &vopt_spec);
	AN(vut);
	AZ(pthread_cond_init(&timebend_cv, NULL));

	while ((i = getopt(argc, argv, vopt_spec.vopt_optstring)) != -1) {
		switch (i) {
		case 'h':
			/* Usage help */
			usage(0);
		case 'p':
			ms_delay = lround(1e3 * strtod(optarg, NULL));
			if (ms_delay <= 0)
				VUT_Error(vut, 1, "-p: invalid '%s'", optarg);
			break;
		case 'P':
			colon = strchr(optarg, ':');
			/* no colon, take the profile as a name */
			if (colon == NULL) {
				profile = optarg;
				break;
			}
			/* else check if valid definition */
			if (colon == optarg + 1 &&
			    (*optarg == 'b' || *optarg == 'c')) {
				cli_p.VSL_arg = *optarg;
				ptag = colon + 1;
				colon = strchr(colon + 1, ':');
				if (colon == NULL)
					profile_error(optarg);
			} else {
				ptag = optarg;
				cli_p.VSL_arg = 'c';
			}

			assert(colon);

			match_tag = VSL_Name2Tag(ptag, colon - ptag);
			if (match_tag < 0)
				VUT_Error(vut, 1,
				    "-P: '%s' is not a valid tag name",
				    optarg);

			if (VSL_tagflags[match_tag])
				VUT_Error(vut, 1,
				    "-P: '%s' is an unsafe or binary record",
				    optarg);

			cli_p.prefix = colon + 1;

			colon = strchr(colon + 1, ':');
			if (colon == NULL)
				profile_error(optarg);

			*colon = '\0';
			if (*cli_p.prefix == '\0')
				cli_p.prefix = NULL;

			if (sscanf(colon + 1, "%d",  &cli_p.field) != 1)
				profile_error(optarg);

			cli_p.name = "custom";
			cli_p.tag = (enum VSL_tag_e)match_tag;
			cli_p.hist_low = -6;
			cli_p.hist_high = 3;
			profile = NULL;
			active_profile = &cli_p;

			colon = strchr(colon + 1, ':');
			if (colon == NULL)
				break;

			if (sscanf(colon + 1, "%d:%d",  &cli_p.hist_low,
				   &cli_p.hist_high) != 2)
				profile_error(optarg);

			break;
		case 'B':
			timebend = strtod(optarg, NULL);
			if (timebend == 0)
				VUT_Error(vut, 1,
				    "-B: being able to bend time does not"
				    " mean we can stop it"
				    " (invalid factor '%s')", optarg);
			if (timebend < 0)
				VUT_Error(vut, 1,
				    "-B: being able to bend time does not"
				    " mean we can make it go backwards"
				    " (invalid factor '%s')", optarg);
			break;
		default:
			if (!VUT_Arg(vut, i, optarg))
				usage(1);
		}
	}

	if (optind != argc)
		usage(1);

	/* Check for valid grouping mode */
	assert(vut->g_arg < VSL_g__MAX);
	if (vut->g_arg != VSL_g_vxid && vut->g_arg != VSL_g_request)
		VUT_Error(vut, 1, "Invalid grouping mode: %s"
		    " (only vxid and request are supported)",
		    VSLQ_grouping[vut->g_arg]);

	if (profile) {
		for (active_profile = profiles; active_profile->name;
		     active_profile++) {
			if (strcmp(active_profile->name, profile) == 0)
				break;
		}
	}
	AN(active_profile);
	if (!active_profile->name)
		VUT_Error(vut, 1, "-P: No such profile '%s'", profile);

	assert(active_profile->VSL_arg == 'b' ||
	    active_profile->VSL_arg == 'c');
	assert(VUT_Arg(vut, active_profile->VSL_arg, NULL));
	match_tag = active_profile->tag;
	fnum = active_profile->field;
	hist_low = active_profile->hist_low;
	hist_high = active_profile->hist_high;

	hist_range = hist_high - hist_low;
	hist_buckets = hist_range * HIST_RES;
	bucket_hit = calloc(hist_buckets, sizeof *bucket_hit);
	bucket_miss = calloc(hist_buckets, sizeof *bucket_miss);

	if (timebend > 0)
		t0 = VTIM_mono();

	format = malloc(4L * fnum);
	AN(format);
	for (i = 0; i < fnum - 1; i++)
		strcpy(format + 4 * i, "%*s ");
	strcpy(format + 4 * (fnum - 1), "%lf");

	log_ten = log(10.0);

	VUT_Signal(vut_sighandler);
	VUT_Setup(vut);
	if (vut->vsm)
		ident = VSM_Dup(vut->vsm, "Arg", "-i");
	else
		ident = strdup("");
	if (pthread_create(&thr, NULL, do_curses, NULL) != 0)
		VUT_Error(vut, 1, "pthread_create(): %s", strerror(errno));
	vut->dispatch_f = accumulate;
	vut->dispatch_priv = NULL;
	vut->sighup_f = sighup;
	VUT_Main(vut);
	end_of_file = 1;
	AZ(pthread_join(thr, NULL));
	VUT_Fini(&vut);
	exit(0);
}
