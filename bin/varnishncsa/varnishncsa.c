/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
 * Copyright (c) 2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Anders Berg <andersb@vgnett.no>
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Tollef Fog Heen <tfheen@varnish-software.com>
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
 * Obtain log data from the shared memory log, order it by session ID, and
 * display it in Apache / NCSA combined log format:
 *
 *	%h %l %u %t "%r" %>s %b "%{Referer}i" "%{User-agent}i"
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
 * Actually, we cheat a little and replace "%r" with something close to
 * "%m http://%{Host}i%U%q %H", where the additional fields are:
 *
 *	%m		Request method
 *	%{Host}i	Contents of "Host" request header
 *	%U		URL path
 *	%q		Query string
 *	%H		Protocol version
 *
 * TODO:	- Make it possible to grab any request header
 *		- Maybe rotate/compress log
 */

#include "config.h"

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "compat/daemon.h"

#include "vsb.h"
#include "vpf.h"

#include "libvarnish.h"
#include "vsl.h"
#include "vre.h"
#include "varnishapi.h"

static volatile sig_atomic_t reopen;

static struct logline {
	char *df_H;			/* %H, Protocol version */
	char *df_Host;			/* %{Host}i */
	char *df_Referer;		/* %{Referer}i */
	char *df_U;			/* %U, URL path */
	char *df_q;			/* %q, query string */
	char *df_User_agent;		/* %{User-agent}i */
	char *df_X_Forwarded_For;	/* %{X-Forwarded-For}i */
	char *df_b;			/* %b, Bytes */
	char *df_h;			/* %h (host name / IP adress)*/
	char *df_m;			/* %m, Request method*/
	char *df_s;			/* %s, Status */
	struct tm df_t;			/* %t, Date and time */
	char *df_u;			/* %u, Remote user */
	char *df_ttfb;			/* Time to first byte */
	const char *df_hitmiss;		/* Whether this is a hit or miss */
	const char *df_handling;	/* How the request was handled (hit/miss/pass/pipe) */
	int active;			/* Is log line in an active trans */
	int complete;			/* Is log line complete */
	uint64_t bitmap;		/* Bitmap for regex matches */
} **ll;

struct VSM_data *vd;

static size_t nll;

static int m_flag = 0;

static const char *format;

static int
isprefix(const char *str, const char *prefix, const char *end,
    const char **next)
{

	while (str < end && *str && *prefix &&
	    tolower((int)*str) == tolower((int)*prefix))
		++str, ++prefix;
	if (*str && *str != ' ')
		return (0);
	if (next) {
		while (str < end && *str && *str == ' ')
			++str;
		*next = str;
	}
	return (1);
}

/*
 * Returns a copy of the first consecutive sequence of non-space
 * characters in the string.
 */
static char *
trimfield(const char *str, const char *end)
{
	size_t len;
	char *p;

	/* skip leading space */
	while (str < end && *str && *str == ' ')
		++str;

	/* seek to end of field */
	for (len = 0; &str[len] < end && str[len]; ++len)
		if (str[len] == ' ')
			break;

	/* copy and return */
	p = malloc(len + 1);
	assert(p != NULL);
	memcpy(p, str, len);
	p[len] = '\0';
	return (p);
}

/*
 * Returns a copy of the entire string with leading and trailing spaces
 * trimmed.
 */
static char *
trimline(const char *str, const char *end)
{
	size_t len;
	char *p;

	/* skip leading space */
	while (str < end && *str && *str == ' ')
		++str;

	/* seek to end of string */
	for (len = 0; &str[len] < end && str[len]; ++len)
		 /* nothing */ ;

	/* trim trailing space */
	while (len && str[len - 1] == ' ')
		--len;

	/* copy and return */
	p = malloc(len + 1);
	assert(p != NULL);
	memcpy(p, str, len);
	p[len] = '\0';
	return (p);
}

static void
clean_logline(struct logline *lp)
{
#define freez(x) do { if (x) free(x); x = NULL; } while (0);
	freez(lp->df_H);
	freez(lp->df_Host);
	freez(lp->df_Referer);
	freez(lp->df_U);
	freez(lp->df_q);
	freez(lp->df_User_agent);
	freez(lp->df_X_Forwarded_For);
	freez(lp->df_b);
	freez(lp->df_h);
	freez(lp->df_m);
	freez(lp->df_s);
	freez(lp->df_u);
	freez(lp->df_ttfb);
#undef freez
	memset(lp, 0, sizeof *lp);
}

static int
collect_backend(struct logline *lp, enum vsl_tag tag, unsigned spec,
    const char *ptr, unsigned len)
{
	const char *end, *next;

	assert(spec & VSL_S_BACKEND);
	end = ptr + len;

	switch (tag) {
	case SLT_BackendOpen:
		if (lp->active || lp->df_h != NULL) {
			/* New start for active line,
			   clean it and start from scratch */
			clean_logline(lp);
		}
		lp->active = 1;
		if (isprefix(ptr, "default", end, &next))
			lp->df_h = trimfield(next, end);
		else
			lp->df_h = trimfield(ptr, end);
		break;

	case SLT_TxRequest:
		if (!lp->active)
			break;
		if (lp->df_m != NULL) {
			clean_logline(lp);
			break;
		}
		lp->df_m = trimline(ptr, end);
		break;

	case SLT_TxURL: {
		char *qs;

		if (!lp->active)
			break;
		if (lp->df_U != NULL || lp->df_q != NULL) {
			clean_logline(lp);
			break;
		}
		qs = index(ptr, '?');
		if (qs) {
			lp->df_U = trimline(ptr, qs);
			lp->df_q = trimline(qs, end);
		} else {
			lp->df_U = trimline(ptr, end);
		}
		break;
	}

	case SLT_TxProtocol:
		if (!lp->active)
			break;
		if (lp->df_H != NULL) {
			clean_logline(lp);
			break;
		}
		lp->df_H = trimline(ptr, end);
		break;

	case SLT_RxStatus:
		if (!lp->active)
			break;
		if (lp->df_s != NULL) {
			clean_logline(lp);
			break;
		}
		lp->df_s = trimline(ptr, end);
		break;

	case SLT_RxHeader:
		if (!lp->active)
			break;
		if (isprefix(ptr, "content-length:", end, &next))
			lp->df_b = trimline(next, end);
		else if (isprefix(ptr, "date:", end, &next) &&
			 strptime(next, "%a, %d %b %Y %T", &lp->df_t) == NULL) {
			clean_logline(lp);
		}
		break;

	case SLT_TxHeader:
		if (!lp->active)
			break;
		if (isprefix(ptr, "user-agent:", end, &next))
			lp->df_User_agent = trimline(next, end);
		else if (isprefix(ptr, "referer:", end, &next))
			lp->df_Referer = trimline(next, end);
		else if (isprefix(ptr, "authorization:", end, &next) &&
		    isprefix(next, "basic", end, &next))
			lp->df_u = trimline(next, end);
		else if (isprefix(ptr, "x-forwarded-for:", end, &next))
			lp->df_X_Forwarded_For = trimline(next, end);
		else if (isprefix(ptr, "host:", end, &next))
			lp->df_Host = trimline(next, end);
		break;

	case SLT_BackendReuse:
	case SLT_BackendClose:
		if (!lp->active)
			break;
		/* got it all */
		lp->complete = 1;
		break;

	default:
		break;
	}

	return (1);
}

static int
collect_client(struct logline *lp, enum vsl_tag tag, unsigned spec,
    const char *ptr, unsigned len)
{
	const char *end, *next;
	long l;
	time_t t;

	assert(spec & VSL_S_CLIENT);
	end = ptr + len;

	switch (tag) {
	case SLT_ReqStart:
		if (lp->active || lp->df_h != NULL) {
			/* New start for active line,
			   clean it and start from scratch */
			clean_logline(lp);
		}
		lp->active = 1;
		lp->df_h = trimfield(ptr, end);
		break;

	case SLT_RxRequest:
		if (!lp->active)
			break;
		if (lp->df_m != NULL) {
			clean_logline(lp);
			break;
		}
		lp->df_m = trimline(ptr, end);
		break;

	case SLT_RxURL: {
		char *qs;

		if (!lp->active)
			break;
		if (lp->df_U != NULL || lp->df_q != NULL) {
			clean_logline(lp);
			break;
		}
		qs = index(ptr, '?');
		if (qs) {
			lp->df_U = trimline(ptr, qs);
			lp->df_q = trimline(qs, end);
		} else {
			lp->df_U = trimline(ptr, end);
		}
		break;
	}

	case SLT_RxProtocol:
		if (!lp->active)
			break;
		if (lp->df_H != NULL) {
			clean_logline(lp);
			break;
		}
		lp->df_H = trimline(ptr, end);
		break;

	case SLT_TxStatus:
		if (!lp->active)
			break;
		if (lp->df_s != NULL)
			clean_logline(lp);
		else
			lp->df_s = trimline(ptr, end);
		break;

	case SLT_RxHeader:
		if (!lp->active)
			break;
		if (isprefix(ptr, "user-agent:", end, &next)) {
			free(lp->df_User_agent);
			lp->df_User_agent = trimline(next, end);
		} else if (isprefix(ptr, "referer:", end, &next)) {
			free(lp->df_Referer);
			lp->df_Referer = trimline(next, end);
		} else if (isprefix(ptr, "authorization:", end, &next) &&
			   isprefix(next, "basic", end, &next)) {
			free(lp->df_u);
			lp->df_u = trimline(next, end);
		} else if (isprefix(ptr, "x-forwarded-for:", end, &next)) {
			free(lp->df_X_Forwarded_For);
			lp->df_X_Forwarded_For = trimline(next, end);
		} else if (isprefix(ptr, "host:", end, &next)) {
			free(lp->df_Host);
			lp->df_Host = trimline(next, end);
		}
		break;

	case SLT_VCL_call:
		if(!lp->active)
			break;
		if (strncmp(ptr, "hit", len) == 0) {
			lp->df_hitmiss = "hit";
			lp->df_handling = "hit";
		} else if (strncmp(ptr, "miss", len) == 0) {
			lp->df_hitmiss = "miss";
			lp->df_handling = "miss";
		} else if (strncmp(ptr, "pass", len) == 0) {
			lp->df_hitmiss = "miss";
			lp->df_handling = "pass";
		} else if (strncmp(ptr, "pipe", len) == 0) {
			/* Just skip piped requests, since we can't
			 * print their status code */
			clean_logline(lp);
			break;
		}
		break;

	case SLT_Length:
		if (!lp->active)
			break;
		if (lp->df_b != NULL) {
			clean_logline(lp);
			break;
		}
		lp->df_b = trimline(ptr, end);
		break;

	case SLT_SessionClose:
		if (!lp->active)
			break;
		if (strncmp(ptr, "pipe", len) == 0 ||
		    strncmp(ptr, "error", len) == 0) {
			clean_logline(lp);
			break;
		}
		break;

	case SLT_ReqEnd:
	{
		char ttfb[64];
		if (!lp->active)
			break;
		if (lp->df_ttfb != NULL || sscanf(ptr, "%*u %*u.%*u %ld.%*u %*u.%*u %s", &l, ttfb) != 2) {
			clean_logline(lp);
			break;
		}
		lp->df_ttfb = strdup(ttfb);
		t = l;
		localtime_r(&t, &lp->df_t);
		/* got it all */
		lp->complete = 1;
		break;
	}

	default:
		break;
	}

	return (1);
}

static int
h_ncsa(void *priv, enum vsl_tag tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr, uint64_t bitmap)
{
	struct logline *lp;
	FILE *fo = priv;
	char *q, tbuf[64];
	const char *p;

	if (fd >= nll) {
		struct logline **newll = ll;
		size_t newnll = nll;

		while (fd >= newnll)
			newnll += newnll + 1;
		newll = realloc(newll, newnll * sizeof *newll);
		assert(newll != NULL);
		memset(newll + nll, 0, (newnll - nll) * sizeof *newll);
		ll = newll;
		nll = newnll;
	}
	if (ll[fd] == NULL) {
		ll[fd] = calloc(sizeof *ll[fd], 1);
		assert(ll[fd] != NULL);
	}
	lp = ll[fd];

	if (spec & VSL_S_BACKEND) {
		collect_backend(lp, tag, spec, ptr, len);
	} else if (spec & VSL_S_CLIENT) {
		collect_client(lp, tag, spec, ptr, len);
	} else {
		/* huh? */
		return (reopen);
	}

	lp->bitmap |= bitmap;

	if (!lp->complete)
		return (reopen);

	if (m_flag && !VSL_Matched(vd, lp->bitmap))
		/* -o is in effect matching rule failed. Don't display */
		return (reopen);

#if 0
	/* non-optional fields */
	if (!lp->df_m || !lp->df_U || !lp->df_H || !lp->df_s) {
		clean_logline(lp);
		return (reopen);
	}
#endif

	/* We have a complete data set - log a line */

	fo = priv;

	for (p = format; *p != '\0'; p++) {


		if (*p != '%') {
			fprintf(fo, "%c", *p);
			continue;
		}
		p++;
		switch (*p) {

		case 'b':
			/* %b */
			fprintf(fo, "%s", lp->df_b ? lp->df_b : "-");
			break;

		case 'H':
			fprintf(fo, "%s", lp->df_H);
			break;

		case 'h':
			if (!lp->df_h && spec & VSL_S_BACKEND)
				fprintf(fo, "127.0.0.1");
			else
				fprintf(fo, "%s", lp->df_h ? lp->df_h : "-");
			break;
		case 'l':
			fprintf(fo, "-");
			break;

		case 'm':
			fprintf(fo, "%s", lp->df_m);
			break;

		case 'q':
			fprintf(fo, "%s", lp->df_q ? lp->df_q : "");
			break;

		case 'r':
			/*
			 * Fake "%r".  This would be a lot easier if Varnish
			 * normalized the request URL.
			 */
			fprintf(fo, "%s ", lp->df_m);
			if (lp->df_Host) {
				if (strncmp(lp->df_Host, "http://", 7) != 0)
					fprintf(fo, "http://");
				fprintf(fo, "%s", lp->df_Host);
			}
			fprintf(fo, "%s", lp->df_U);
			fprintf(fo, "%s ", lp->df_q ? lp->df_q : "");
			fprintf(fo, "%s", lp->df_H);
			break;

		case 's':
			/* %s */
			fprintf(fo, "%s", lp->df_s ? lp->df_s : "");
			break;

		case 't':
			/* %t */
			strftime(tbuf, sizeof tbuf, "[%d/%b/%Y:%T %z]", &lp->df_t);
			fprintf(fo, "%s", tbuf);
			break;

		case 'U':
			fprintf(fo, "%s", lp->df_U);
			break;

		case 'u':
			/* %u: decode authorization string */
			if (lp->df_u != NULL) {
				char *rubuf;
				size_t rulen;

				base64_init();
				rulen = ((strlen(lp->df_u) + 3) * 4) / 3;
				rubuf = malloc(rulen);
				assert(rubuf != NULL);
				base64_decode(rubuf, rulen, lp->df_u);
				q = strchr(rubuf, ':');
				if (q != NULL)
					*q = '\0';
				fprintf(fo, "%s", rubuf);
				free(rubuf);
			} else {
				fprintf(fo, "-");
			}
			break;

		case '{':
			if (strncmp(p, "{Referer}i", 10) == 0) {
				fprintf(fo, "%s",
					lp->df_Referer ? lp->df_Referer : "-");
				p += 9;
				break;
			} else if (strncmp(p, "{Host}i", 7) == 0) {
				fprintf(fo, "%s",
					lp->df_Host ? lp->df_Host : "-");
				p += 6;
				break;
			} else if (strncmp(p, "{X-Forwarded-For}i", 18) == 0) {
				/* %{Referer}i */
				fprintf(fo, "%s",
					lp->df_X_Forwarded_For ? lp->df_X_Forwarded_For : "-");
				p += 17;
				break;
			} else if (strncmp(p, "{User-agent}i", 13) == 0) {
				/* %{User-agent}i */
				fprintf(fo, "%s",
					lp->df_User_agent ? lp->df_User_agent : "-");
				p += 12;
				break;
			} else if (strncmp(p, "{Varnish:", 9) == 0) {
				/* Scan for what we're looking for */
				const char *what = p+9;
				if (strncmp(what, "time_firstbyte}x", 16) == 0) {
					fprintf(fo, "%s", lp->df_ttfb);
					p += 9+15;
					break;
				} else if (strncmp(what, "hitmiss}x", 9) == 0) {
					fprintf(fo, "%s", lp->df_hitmiss);
					p += 9+8;
					break;
				} else if (strncmp(what, "handling}x", 10) == 0) {
					fprintf(fo, "%s", lp->df_handling);
					p += 9+9;
					break;
				}
			}
			/* Fall through if we haven't handled something */
			/* FALLTHROUGH*/
		default:
			fprintf(stderr, "Unknown format character: %c\n", *p);
			exit(1);
		}
	}
	fprintf(fo, "\n");

	/* flush the stream */
	fflush(fo);

	/* clean up */
	clean_logline(lp);

	return (reopen);
}

/*--------------------------------------------------------------------*/

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

	fprintf(stderr,
	    "usage: varnishncsa %s [-aDV] [-n varnish_name] "
	    "[-P file] [-w file]\n", VSL_USAGE);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int c;
	int a_flag = 0, D_flag = 0, format_flag = 0;
	const char *P_arg = NULL;
	const char *w_arg = NULL;
	struct pidfh *pfh = NULL;
	FILE *of;
	format = "%h %l %u %t \"%r\" %s %b \"%{Referer}i\" \"%{User-agent}i\"";

	vd = VSM_New();
	VSL_Setup(vd);

	while ((c = getopt(argc, argv, VSL_ARGS "aDP:Vw:fF:")) != -1) {
		switch (c) {
		case 'a':
			a_flag = 1;
			break;
		case 'f':
			if (format_flag) {
				fprintf(stderr, "-f and -F can not be combined\n");
				exit(1);
			}
			format = "%{X-Forwarded-For}i %l %u %t \"%r\" %s %b \"%{Referer}i\" \"%{User-agent}i\"";
			format_flag = 1;
			break;
		case 'F':
			if (format_flag) {
				fprintf(stderr, "-f and -F can not be combined\n");
				exit(1);
			}
			format_flag = 1;
			format = optarg;
			break;
		case 'D':
			D_flag = 1;
			break;
		case 'P':
			P_arg = optarg;
			break;
		case 'V':
			varnish_version("varnishncsa");
			exit(0);
		case 'w':
			w_arg = optarg;
			break;
		case 'b':
			fprintf(stderr, "-b is not valid for varnishncsa\n");
			exit(1);
			break;
		case 'i':
			fprintf(stderr, "-i is not valid for varnishncsa\n");
			exit(1);
			break;
		case 'I':
			fprintf(stderr, "-I is not valid for varnishncsa\n");
			exit(1);
			break;
		case 'c':
			/* XXX: Silently ignored: it's required anyway */
			break;
		case 'm':
			m_flag = 1; /* Fall through */
		default:
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	VSL_Arg(vd, 'c', optarg);

	if (VSL_Open(vd, 1))
		exit(1);

	if (P_arg && (pfh = vpf_open(P_arg, 0644, NULL)) == NULL) {
		perror(P_arg);
		exit(1);
	}

	if (D_flag && varnish_daemon(0, 0) == -1) {
		perror("daemon()");
		if (pfh != NULL)
			vpf_remove(pfh);
		exit(1);
	}

	if (pfh != NULL)
		vpf_write(pfh);

	if (w_arg) {
		of = open_log(w_arg, a_flag);
		signal(SIGHUP, sighup);
	} else {
		w_arg = "stdout";
		of = stdout;
	}

	while (VSL_Dispatch(vd, h_ncsa, of) >= 0) {
		if (fflush(of) != 0) {
			perror(w_arg);
			exit(1);
		}
		if (reopen && of != stdout) {
			fclose(of);
			of = open_log(w_arg, a_flag);
			reopen = 0;
		}
	}

	exit(0);
}
