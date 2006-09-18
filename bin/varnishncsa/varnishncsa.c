/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
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
 * Program that will get data from the shared memory log. When it has the data
 * it will order the data based on the sessionid. When the data is ordered
 * and session is finished it will write the data into disk. Logging will be
 * in NCSA extended/combined access log format.
 *
 *	"%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-agent}i\""
 * 
 * TODO:	- Log in any format one wants
 *		- Maybe rotate/compress log
 */

#include <stdio.h>
#include <errno.h>
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
	char df_h[4 * (3 + 1)];		/* Datafield for %h (IP adress)	*/
	char df_s[4]; 			/* Datafield for %s, Status	*/
	char df_b[12];			/* Datafield for %b, Bytes	*/
	char *df_R; 			/* Datafield for %{Referer}	*/
	char *df_U; 			/* Datafield for %{User-agent}	*/
	int bogus_req; 			/* bogus request		*/
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
extended_log_format(void *priv, unsigned tag, unsigned fd, unsigned len, unsigned spec, const char *ptr)
{
	const char *p;
	char *q;
	FILE *fo;
	time_t t;
	long l;
	struct tm tm;
	char tbuf[40];
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
	fprintf(fo, "%s - - %s", lp->df_h, tbuf);
	vsb_finish(lp->sb);
	fprintf(fo, " \"%s\"", vsb_data(lp->sb));
	fprintf(fo, " %s", lp->df_b);
	if (lp->df_R != NULL) {
		fprintf(fo, " \"%s\"", lp->df_R);
		free(lp->df_R);
		lp->df_R = NULL;
	}
	if (lp->df_U != NULL) {
		fprintf(fo, " \"%s\"", lp->df_U);
		free(lp->df_U);
		lp->df_U = NULL;
	}
	fprintf(fo, "\n");

	return (0);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishncsa %s [-aV] [-w file]\n", VSL_ARGS);
	exit(1);
}

int
main(int argc, char **argv)
{
	int i, c;
	struct VSL_data *vd;
	const char *ofn = NULL;
	FILE *of = stdout;
	int append = 0;

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

	if (ofn && (of = fopen(ofn, append ? "a" : "w")) == NULL) {
		perror(ofn);
		exit(1);
	} else {
		ofn = "stdout";
	}

	while (VSL_Dispatch(vd, extended_log_format, of) == 0) {
		if (fflush(of) != 0) {
			perror(ofn);
			exit(1);
		}
	}

	exit(0);
}

