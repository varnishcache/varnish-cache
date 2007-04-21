/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
 *
 * Author: Anders Berg <andersb@vgnett.no>
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * $Id$
 *
 * Obtain log data from the shared memory log, order it by session ID, and
 * display it in Apache / NCSA combined log format:
 *
 *	"%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-agent}i\""
 *
 * where the fields are defined as follows:
 *
 *	%h		Client host name or IP address (always the latter)
 *	%l		Client user ID as reported by identd (always "-")
 *	%u		User ID if using HTTP authentication, or "-"
 *	%t		Date and time of request
 *	%r		Request line
 *	%s		Status code
 *	%b		Length of reply body, or "-"
 *	%{Referer}i	Contents of "Referer" request header
 *	%{User-agent}i	Contents of "User-agent" request header
 *
 * TODO:	- Log in any format one wants
 *		- Maybe rotate/compress log
 */

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "compat/vis.h"

#include "vsb.h"

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"

static struct logline {
	char df_h[4 * (3 + 1)];		/* %h (host name / IP adress)*/
	char df_s[4];			/* %s, Status */
	char df_b[12];			/* %b, Bytes */
	char *df_R;			/* %{Referer}i */
	char *df_U;			/* %{User-agent}i */
	char *df_RU;			/* %u, Remote user */
	int bogus_req;			/* bogus request */
	struct vsb *sb;
} *ll[65536];

/* Check if string starts with pfx */
static int
ispfx(const char *ptr, unsigned len, const char *pfx)
{
	unsigned l;

	l = strlen(pfx);
	if (l > len)
		return (0);
	if (strncasecmp(ptr, pfx, l))
		return (0);
	return (1);
}

static int
extended_log_format(void *priv, unsigned tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr)
{
	const char *p;
	char *q;
	FILE *fo;
	time_t t;
	long l;
	unsigned lu;
	struct tm tm;
	char tbuf[40];
	char rubuf[128];
	struct logline *lp;

	if (!(spec &VSL_S_CLIENT))
		return (0);

	if (ll[fd] == NULL) {
		ll[fd] = calloc(sizeof *ll[fd], 1);
		assert(ll[fd] != NULL);
		ll[fd]->sb = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
		assert(ll[fd]->sb != NULL);
		strcpy(ll[fd]->df_h, "-");
	}
	lp = ll[fd];

	switch (tag) {

	case SLT_SessionOpen:
	case SLT_ReqStart:
		for (p = ptr, q = lp->df_h; *p && *p != ' ';)
			*q++ = *p++;
		*q = '\0';
		vsb_clear(lp->sb);
		break;

	case SLT_RxRequest:
		if (ispfx(ptr, len, "HEAD")) {
			vsb_bcat(lp->sb, ptr, len);
		} else if (ispfx(ptr, len, "POST")) {
			vsb_bcat(lp->sb, ptr, len);
		} else if (ispfx(ptr, len, "GET")) {
			vsb_bcat(lp->sb, ptr, len);
		} else if (ispfx(ptr, len, "PURGE")) {
			vsb_bcat(lp->sb, ptr, len);
		} else {
			lp->bogus_req = 1;
		}
		break;

	case SLT_RxURL:
		vsb_cat(lp->sb, " ");
		vsb_bcat(lp->sb, ptr, len);
		break;

	case SLT_RxProtocol:
		vsb_cat(lp->sb, " ");
		vsb_bcat(lp->sb, ptr, len);
		break;

	case SLT_TxStatus:
		strcpy(lp->df_s, ptr);
		break;

	case SLT_RxHeader:
		if (ispfx(ptr, len, "user-agent:"))
			lp->df_U = strdup(ptr + 12);
		else if (ispfx(ptr, len, "referer:"))
			lp->df_R = strdup(ptr + 9);
		else if (ispfx(ptr, len, "authorization:"))
			lp->df_RU = strdup(ptr + 21);
		break;

	case SLT_Length:
		if (strcmp(ptr, "0"))
			strcpy(lp->df_b, ptr);
		else
			strcpy(lp->df_b, "-");
		break;

	default:
		break;
	}

	if (tag != SLT_ReqEnd)
		return (0);

	fo = priv;
	assert(1 == sscanf(ptr, "%*u %*u.%*u %ld.", &l));
	t = l;
	localtime_r(&t, &tm);

	strftime(tbuf, sizeof tbuf, "%d/%b/%Y:%T %z", &tm);
	fprintf(fo, "%s", lp->df_h);

	if (lp->df_RU != NULL) {
		base64_init();
		lu = sizeof rubuf;
		base64_decode(rubuf, lu, lp->df_RU);
		q = strchr(rubuf, ':');
		if (q != NULL){
			*q = '\0';
		}
		fprintf(fo, " %s", rubuf);
		free(lp->df_RU);
		lp->df_RU = NULL;
	} else {
		fprintf(fo, " -");
	}
	fprintf(fo, " - [%s]", tbuf);
	vsb_finish(lp->sb);
	fprintf(fo, " \"%s\"", vsb_data(lp->sb));
	fprintf(fo, " %s", lp->df_s);
	fprintf(fo, " %s", lp->df_b);
	if (lp->df_R != NULL) {
		fprintf(fo, " \"%s\"", lp->df_R);
		free(lp->df_R);
		lp->df_R = NULL;
	} else {
		fprintf(fo, " \"-\"");
	}

	if (lp->df_U != NULL) {
		fprintf(fo, " \"%s\"", lp->df_U);
		free(lp->df_U);
		lp->df_U = NULL;
	} else {
		fprintf(fo, " \"-\"");
	}
	fprintf(fo, "\n");

	return (0);
}

/*--------------------------------------------------------------------*/

static sig_atomic_t reopen;

static void
sighup(int sig)
{

	(void)sig;
	reopen = 1;
}

static FILE *
open_log(const char *ofn, int append)
{
	FILE *of;

	if ((of = fopen(ofn, append ? "a" : "w")) == NULL) {
		perror(ofn);
		exit(1);
	}
	return (of);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{

	fprintf(stderr, "usage: varnishncsa %s [-aV] [-w file]\n", VSL_ARGS);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int i, c;
	struct VSL_data *vd;
	const char *ofn = NULL;
	int append = 0;
	FILE *of;

	vd = VSL_New();

	while ((c = getopt(argc, argv, VSL_ARGS "aVw:")) != -1) {
		i = VSL_Arg(vd, c, optarg);
		if (i < 0)
			exit (1);
		if (i > 0)
			continue;
		switch (c) {
		case 'a':
			append = 1;
			break;
		case 'V':
			varnish_version("varnishncsa");
			exit(0);
		case 'w':
			ofn = optarg;
			break;
		default:
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (VSL_OpenLog(vd))
		exit(1);

	if (ofn) {
		of = open_log(ofn, append);
		signal(SIGHUP, sighup);
	} else {
		ofn = "stdout";
		of = stdout;
	}

	while (VSL_Dispatch(vd, extended_log_format, of) == 0) {
		if (fflush(of) != 0) {
			perror(ofn);
			exit(1);
		}
		if (reopen && of != stdout) {
			fclose(of);
			of = open_log(ofn, append);
			reopen = 0;
		}
	}

	exit(0);
}
