/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 *	%h %l %u %t "%r" %s %b "%{Referer}i" "%{User-agent}i"
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
 * TODO:		- Maybe rotate/compress log
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
#include "vqueue.h"

#include "libvarnish.h"
#include "vsl.h"
#include "vre.h"
#include "varnishapi.h"
#include "base64.h"

static volatile sig_atomic_t reopen;

struct hdr {
	char *key;
	char *value;
	VTAILQ_ENTRY(hdr) list;
};

static struct logline {
	char *df_H;			/* %H, Protocol version */
	char *df_U;			/* %U, URL path */
	char *df_q;			/* %q, query string */
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
	VTAILQ_HEAD(, hdr) req_headers; /* Request headers */
	VTAILQ_HEAD(, hdr) resp_headers; /* Response headers */
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

static char *
req_header(struct logline *l, const char *name)
{
	struct hdr *h;
	VTAILQ_FOREACH(h, &l->req_headers, list) {
		if (strcasecmp(h->key, name) == 0) {
			return h->value;
			break;
		}
	}
	return NULL;
}

static char *
resp_header(struct logline *l, const char *name)
{
	struct hdr *h;
	VTAILQ_FOREACH(h, &l->resp_headers, list) {
		if (strcasecmp(h->key, name) == 0) {
			return h->value;
			break;
		}
	}
	return NULL;
}

static void
clean_logline(struct logline *lp)
{
	struct hdr *h, *h2;
#define freez(x) do { if (x) free(x); x = NULL; } while (0);
	freez(lp->df_H);
	freez(lp->df_U);
	freez(lp->df_q);
	freez(lp->df_b);
	freez(lp->df_h);
	freez(lp->df_m);
	freez(lp->df_s);
	freez(lp->df_u);
	freez(lp->df_ttfb);
	VTAILQ_FOREACH_SAFE(h, &lp->req_headers, list, h2) {
		VTAILQ_REMOVE(&lp->req_headers, h, list);
		freez(h->key);
		freez(h->value);
		freez(h);
	}
	VTAILQ_FOREACH_SAFE(h, &lp->resp_headers, list, h2) {
		VTAILQ_REMOVE(&lp->resp_headers, h, list);
		freez(h->key);
		freez(h->value);
		freez(h);
	}
#undef freez
	memset(lp, 0, sizeof *lp);
}

static int
collect_backend(struct logline *lp, enum VSL_tag_e tag, unsigned spec,
    const char *ptr, unsigned len)
{
	const char *end, *next, *split;

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
		split = strchr(ptr, ':');
		if (split == NULL)
			break;
		if (isprefix(ptr, "authorization:", end, &next) &&
		    isprefix(next, "basic", end, &next)) {
			lp->df_u = trimline(next, end);
		} else {
			struct hdr *h;
			size_t l;
			h = malloc(sizeof(struct hdr));
			AN(h);
			AN(split);
			l = strlen(split);
			h->key = trimline(ptr, split-1);
			h->value = trimline(split+1, split+l-1);
			VTAILQ_INSERT_HEAD(&lp->req_headers, h, list);
		}
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
collect_client(struct logline *lp, enum VSL_tag_e tag, unsigned spec,
    const char *ptr, unsigned len)
{
	const char *end, *next, *split;
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

	case SLT_TxHeader:
	case SLT_RxHeader:
		if (!lp->active)
			break;
		split = strchr(ptr, ':');
		if (split == NULL)
			break;
		if (tag == SLT_RxHeader &&
		    isprefix(ptr, "authorization:", end, &next) &&
		    isprefix(next, "basic", end, &next)) {
			free(lp->df_u);
			lp->df_u = trimline(next, end);
		} else {
			struct hdr *h;
			h = malloc(sizeof(struct hdr));
			AN(h);
			AN(split);
			h->key = trimline(ptr, split);
			h->value = trimline(split+1, end);
			if (tag == SLT_RxHeader)
				VTAILQ_INSERT_HEAD(&lp->req_headers, h, list);
			else
				VTAILQ_INSERT_HEAD(&lp->resp_headers, h, list);
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
h_ncsa(void *priv, enum VSL_tag_e tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr, uint64_t bitmap)
{
	struct logline *lp;
	FILE *fo = priv;
	char *q, tbuf[64];
	const char *p;
	struct vsb *os;

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
	os = VSB_new_auto();

	for (p = format; *p != '\0'; p++) {

		if (*p != '%') {
			VSB_putc(os, *p);
			continue;
		}
		p++;
		switch (*p) {

		case 'b':
			/* %b */
			VSB_cat(os, lp->df_b ? lp->df_b : "-");
			break;

		case 'H':
			VSB_cat(os, lp->df_H ? lp->df_H : "HTTP/1.0");
			break;

		case 'h':
			if (!lp->df_h && spec & VSL_S_BACKEND)
				VSB_cat(os, "127.0.0.1");
			else
				VSB_cat(os, lp->df_h ? lp->df_h : "-");
			break;
		case 'l':
			VSB_putc(os, '-');
			break;

		case 'm':
			VSB_cat(os, lp->df_m ? lp->df_m : "-");
			break;

		case 'q':
			VSB_cat(os, lp->df_q ? lp->df_q : "");
			break;

		case 'r':
			/*
			 * Fake "%r".  This would be a lot easier if Varnish
			 * normalized the request URL.
			 */
			VSB_cat(os, lp->df_m ? lp->df_m : "-");
			VSB_putc(os, ' ');
			if (req_header(lp, "Host")) {
				if (strncmp(req_header(lp, "Host"), "http://", 7) != 0)
					VSB_cat(os, "http://");
				VSB_cat(os, req_header(lp, "Host"));
			} else {
				VSB_cat(os, "http://localhost");
			}
			VSB_cat(os, lp->df_U ? lp->df_U : "-");
			VSB_cat(os, lp->df_q ? lp->df_q : "");
			VSB_putc(os, ' ');
			VSB_cat(os, lp->df_H ? lp->df_H : "HTTP/1.0");
			break;

		case 's':
			/* %s */
			VSB_cat(os, lp->df_s ? lp->df_s : "");
			break;

		case 't':
			/* %t */
			strftime(tbuf, sizeof tbuf, "[%d/%b/%Y:%T %z]", &lp->df_t);
			VSB_cat(os, tbuf);
			break;

		case 'U':
			VSB_cat(os, lp->df_U ? lp->df_U : "-");
			break;

		case 'u':
			/* %u: decode authorization string */
			if (lp->df_u != NULL) {
				char *rubuf;
				size_t rulen;

				VB64_init();
				rulen = ((strlen(lp->df_u) + 3) * 4) / 3;
				rubuf = malloc(rulen);
				assert(rubuf != NULL);
				VB64_decode(rubuf, rulen, lp->df_u);
				q = strchr(rubuf, ':');
				if (q != NULL)
					*q = '\0';
				VSB_cat(os, rubuf);
				free(rubuf);
			} else {
				VSB_putc(os, '-');
			}
			break;

		case '{': {
			const char *h, *tmp;
			char fname[100], type;
			tmp = p;
			type = 0;
			while (*tmp != '\0' && *tmp != '}')
				tmp++;
			if (*tmp == '}') {
				tmp++;
				type = *tmp;
				memcpy(fname, p+1, tmp-p-2);
				fname[tmp-p-2] = 0;
			}

			switch (type) {
			case 'i':
				h = req_header(lp, fname);
				VSB_cat(os, h ? h : "-");
				p = tmp;
				break;
			case 'o':
				h = resp_header(lp, fname);
				VSB_cat(os, h ? h : "-");
				p = tmp;
				break;
			case 'x':
				if (strcmp(fname, "Varnish:time_firstbyte") == 0) {
					VSB_cat(os, lp->df_ttfb);
					p = tmp;
					break;
				} else if (strcmp(fname, "Varnish:hitmiss") == 0) {
					VSB_cat(os, (lp->df_hitmiss ? lp->df_hitmiss : "-"));
					p = tmp;
					break;
				} else if (strcmp(fname, "Varnish:handling") == 0) {
					VSB_cat(os, (lp->df_handling ? lp->df_handling : "-"));
					p = tmp;
					break;
				}
			default:
				fprintf(stderr, "Unknown format starting at: %s\n", --p);
				exit(1);
			}
			break;
		}
			/* Fall through if we haven't handled something */
			/* FALLTHROUGH*/
		default:
			fprintf(stderr, "Unknown format starting at: %s\n", --p);
			exit(1);
		}
	}
	VSB_putc(os, '\n');

	/* flush the stream */
	VSB_finish(os);
	fprintf(fo, "%s", VSB_data(os));
	fflush(fo);

	/* clean up */
	clean_logline(lp);
	VSB_delete(os);
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
	struct vpf_fh *pfh = NULL;
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
			VCS_Message("varnishncsa");
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

	if (P_arg && (pfh = VPF_Open(P_arg, 0644, NULL)) == NULL) {
		perror(P_arg);
		exit(1);
	}

	if (D_flag && varnish_daemon(0, 0) == -1) {
		perror("daemon()");
		if (pfh != NULL)
			VPF_Remove(pfh);
		exit(1);
	}

	if (pfh != NULL)
		VPF_Write(pfh);

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
