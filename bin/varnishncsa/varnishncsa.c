/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Anders Berg <andersb@vgnett.no>
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Tollef Fog Heen <tfheen@varnish-software.com>
 * Author: Martin Blix Grydeland <mbgrydeland@varnish-software.com>
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
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>

#include "base64.h"
#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "vcs.h"
#include "vsb.h"
#include "vut.h"
#include "vqueue.h"
#include "miniobj.h"

#define TIME_FMT "[%d/%b/%Y:%T %z]"
#define FORMAT "%h %l %u %t \"%r\" %s %b \"%{Referer}i\" \"%{User-agent}i\""
static const char progname[] = "varnishncsa";

struct format;
struct fragment;

enum e_frag {
	F_H,			/* %H Proto */
	F_U,			/* %U URL path */
	F_q,			/* %q Query string */
	F_b,			/* %b Bytes */
	F_h,			/* %h Host name / IP Address */
	F_m,			/* %m Method */
	F_s,			/* %s Status */
	F_tstart,		/* Time start */
	F_tend,			/* Time end */
	F_ttfb,			/* %{Varnish:time_firstbyte}x */
	F_host,			/* Host header */
	F_auth,			/* Authorization header */
	F__MAX,
};

struct fragment {
	unsigned		gen;
	const char		*b, *e;
};

typedef int format_f(const struct format *format);

struct format {
	unsigned		magic;
#define FORMAT_MAGIC		0xC3119CDA

	VTAILQ_ENTRY(format)	list;
	format_f		*func;
	struct fragment		*frag;
	char			*string;
	const char *const	*strptr;
	char			time_type;
	char			*time_fmt;
};

struct watch {
	unsigned		magic;
#define WATCH_MAGIC		0xA7D4005C

	VTAILQ_ENTRY(watch)	list;
	char			*key;
	unsigned		keylen;
	struct fragment		frag;
};
VTAILQ_HEAD(watch_head, watch);

struct ctx {
	/* Options */
	int			a_opt;
	char			*w_arg;

	FILE			*fo;
	struct vsb		*vsb;
	unsigned		gen;
	VTAILQ_HEAD(,format)	format;

	/* State */
	struct watch_head	watch_vcl_log;
	struct watch_head	watch_reqhdr;
	struct watch_head	watch_resphdr;
	struct fragment		frag[F__MAX];
	const char		*hitmiss;
	const char		*handling;
} CTX;

static void
usage(int status)
{
	const char **opt;

	fprintf(stderr, "Usage: %s <options>\n\n", progname);
	fprintf(stderr, "Options:\n");
	for (opt = vopt_usage; *opt != NULL; opt += 2)
		fprintf(stderr, " %-25s %s\n", *opt, *(opt + 1));
	exit(status);
}

static void
openout(int append)
{

	AN(CTX.w_arg);
	CTX.fo = fopen(CTX.w_arg, append ? "a" : "w");
	if (CTX.fo == NULL)
		VUT_Error(1, "Can't open output file (%s)", strerror(errno));
}

static int __match_proto__(VUT_cb_f)
rotateout(void)
{

	AN(CTX.w_arg);
	AN(CTX.fo);
	fclose(CTX.fo);
	openout(1);
	AN(CTX.fo);
	return (0);
}

static int __match_proto__(VUT_cb_f)
flushout(void)
{

	AN(CTX.fo);
	if (fflush(CTX.fo))
		return (-5);
	return (0);
}

static inline int
vsb_fcat(struct vsb *vsb, const struct fragment *f, const char *dflt)
{

	if (f->gen == CTX.gen) {
		assert(f->b <= f->e);
		return (VSB_bcat(vsb, f->b, f->e - f->b));
	}
	if (dflt)
		return (VSB_cat(vsb, dflt));
	return (-1);
}

static int __match_proto__(format_f)
format_string(const struct format *format)
{

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	AN(format->string);
	AZ(VSB_cat(CTX.vsb, format->string));
	return (1);
}

static int __match_proto__(format_f)
format_strptr(const struct format *format)
{

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	AN(format->strptr);
	AN(*format->strptr);
	AZ(VSB_cat(CTX.vsb, *format->strptr));
	return (1);
}

static int __match_proto__(format_f)
format_fragment(const struct format *format)
{

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	AN(format->frag);

	if (format->frag->gen != CTX.gen) {
		if (format->string == NULL)
			return (-1);
		AZ(VSB_cat(CTX.vsb, format->string));
		return (0);
	}

	AZ(vsb_fcat(CTX.vsb, format->frag, NULL));
	return (1);
}

static int __match_proto__(format_f)
format_time(const struct format *format)
{
	double t_start, t_end;
	char *p;
	char buf[64];
	time_t t;
	struct tm tm;

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	if (CTX.frag[F_tstart].gen != CTX.gen ||
	    CTX.frag[F_tend].gen != CTX.gen) {
		if (format->string == NULL)
			return (-1);
		AZ(VSB_cat(CTX.vsb, format->string));
		return (0);
	}

	t_start = strtod(CTX.frag[F_tstart].b, &p);
	if (p != CTX.frag[F_tstart].e)
		return (-1);
	t_end = strtod(CTX.frag[F_tend].b, &p);
	if (p != CTX.frag[F_tend].e)
		return (-1);

	switch (format->time_type) {
	case 'D':
		AZ(VSB_printf(CTX.vsb, "%f", (t_end - t_start) * 1e6));
		break;
	case 't':
		AN(format->time_fmt);
		t = t_start;
		localtime_r(&t, &tm);
		strftime(buf, sizeof buf, format->time_fmt, &tm);
		AZ(VSB_cat(CTX.vsb, buf));
		break;
	case 'T':
		AZ(VSB_printf(CTX.vsb, "%d", (int)(t_end - t_start)));
		break;
	default:
		WRONG("Time format specifier");
	}

	return (1);
}

static int __match_proto__(format_f)
format_requestline(const struct format *format)
{

	(void)format;
	AZ(vsb_fcat(CTX.vsb, &CTX.frag[F_m], "-"));
	AZ(VSB_putc(CTX.vsb, ' '));
	if (CTX.frag[F_host].gen == CTX.gen) {
		if (strncmp(CTX.frag[F_host].b, "http://", 7))
			AZ(VSB_cat(CTX.vsb, "http://"));
		AZ(vsb_fcat(CTX.vsb, &CTX.frag[F_host], NULL));
	} else
		AZ(VSB_cat(CTX.vsb, "http://localhost"));
	AZ(vsb_fcat(CTX.vsb, &CTX.frag[F_U], "-"));
	AZ(vsb_fcat(CTX.vsb, &CTX.frag[F_q], ""));
	AZ(VSB_putc(CTX.vsb, ' '));
	AZ(vsb_fcat(CTX.vsb, &CTX.frag[F_H], "HTTP/1.0"));
	return (1);
}

static int __match_proto__(format_f)
format_auth(const struct format *format)
{
	char buf[128];
	char *q;

	if (CTX.frag[F_auth].gen != CTX.gen ||
	    VB64_decode(buf, sizeof buf, CTX.frag[F_auth].b,
		CTX.frag[F_auth].e)) {
		if (format->string == NULL)
			return (-1);
		AZ(VSB_cat(CTX.vsb, format->string));
		return (0);
	}
	q = strchr(buf, ':');
	if (q != NULL)
		*q = '\0';
	AZ(VSB_cat(CTX.vsb, buf));
	return (1);
}

static int
print(void)
{
	const struct format *f;
	int i, r = 1;

	VSB_clear(CTX.vsb);
	VTAILQ_FOREACH(f, &CTX.format, list) {
		i = (f->func)(f);
		if (r > i)
			r = i;
	}
	AZ(VSB_putc(CTX.vsb, '\n'));
	AZ(VSB_finish(CTX.vsb));
	if (r >= 0) {
		i = fwrite(VSB_data(CTX.vsb), 1, VSB_len(CTX.vsb), CTX.fo);
		if (i != VSB_len(CTX.vsb))
			return (-5);
	}
	return (0);
}

static void
addf_string(const char *str)
{
	struct format *f;

	AN(str);
	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_string;
	f->string = strdup(str);
	AN(f->string);
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_strptr(const char *const *strptr)
{
	struct format *f;

	AN(strptr);
	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_strptr;
	f->strptr = strptr;
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_fragment(struct fragment *frag, const char *str)
{
	struct format *f;

	AN(frag);
	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_fragment;
	f->frag = frag;
	if (str != NULL) {
		f->string = strdup(str);
		AN(f->string);
	}
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_time(char type, const char *fmt, const char *str)
{
	struct format *f;

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_time;
	f->time_type = type;
	if (fmt != NULL) {
		f->time_fmt = strdup(fmt);
		AN(f->time_fmt);
	}
	if (str != NULL) {
		f->string = strdup(str);
		AN(f->string);
	}
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_requestline(void)
{
	struct format *f;

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_requestline;
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_vcl_log(const char *key, const char *str)
{
	struct watch *w;
	struct format *f;

	AN(key);
	ALLOC_OBJ(w, WATCH_MAGIC);
	AN(w);
	w->key = strdup(key);
	AN(w->key);
	w->keylen = strlen(w->key);
	VTAILQ_INSERT_TAIL(&CTX.watch_vcl_log, w, list);

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_fragment;
	f->frag = &w->frag;
	if (str != NULL) {
		f->string = strdup(str);
		AN(f->string);
	}
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_hdr(struct watch_head *head, const char *key, const char *str)
{
	struct watch *w;
	struct format *f;

	AN(head);
	AN(key);
	ALLOC_OBJ(w, WATCH_MAGIC);
	AN(w);
	w->key = strdup(key);
	AN(w->key);
	w->keylen = strlen(w->key);
	VTAILQ_INSERT_TAIL(head, w, list);

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_fragment;
	f->frag = &w->frag;
	if (str != NULL) {
		f->string = strdup(str);
		AN(f->string);
	}
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_auth(const char *str)
{
	struct format *f;

	ALLOC_OBJ(f, FORMAT_MAGIC);
	f->func = &format_auth;
	if (str != NULL) {
		f->string = strdup(str);
		AN(f->string);
	}
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
parse_format(const char *format)
{
	const char *p, *q;
	struct vsb *vsb;
	char buf[256];

	vsb = VSB_new_auto();
	AN(vsb);

	for (p = format; *p != '\0'; p++) {

		/* Allow the most essential escape sequences in format */
		if (*p == '\\') {
			p++;
			if (*p == 't')
				AZ(VSB_putc(vsb, '\t'));
			else if (*p == 'n')
				AZ(VSB_putc(vsb, '\n'));
			else if (*p != '\0')
				AZ(VSB_putc(vsb, *p));
			continue;
		}

		if (*p != '%') {
			AZ(VSB_putc(vsb, *p));
			continue;
		}

		if (VSB_len(vsb) > 0) {
			AZ(VSB_finish(vsb));
			addf_string(VSB_data(vsb));
			VSB_clear(vsb);
		}

		p++;
		switch (*p) {
		case 'b':	/* Bytes */
			addf_fragment(&CTX.frag[F_b], "-");
			break;
		case 'D':	/* Float request time */
			addf_time(*p, NULL, NULL);
			break;
		case 'h':	/* Client host name / IP Address */
			addf_fragment(&CTX.frag[F_h], "-");
			break;
		case 'H':	/* Protocol */
			addf_fragment(&CTX.frag[F_H], "HTTP/1.0");
			break;
		case 'l':	/* Client user ID (identd) always '-' */
			AZ(VSB_putc(vsb, '-'));
			break;
		case 'm':	/* Method */
			addf_fragment(&CTX.frag[F_m], "-");
			break;
		case 'q':	/* Query string */
			addf_fragment(&CTX.frag[F_q], "");
			break;
		case 'r':	/* Request line */
			addf_requestline();
			break;
		case 's':	/* Status code */
			addf_fragment(&CTX.frag[F_s], "");
			break;
		case 't':	/* strftime */
			addf_time(*p, TIME_FMT, NULL);
			break;
		case 'T':	/* Int request time */
			addf_time(*p, NULL, NULL);
			break;
		case 'u':
			addf_auth("-");
			break;
		case 'U':	/* URL */
			addf_fragment(&CTX.frag[F_U], "-");
			break;
		case '{':
			p++;
			q = p;
			while (*q && *q != '}')
				q++;
			if (!*q)
				VUT_Error(1, "Unmatched bracket at: %s", p - 2);
			assert(q - p < sizeof buf - 1);
			strncpy(buf, p, q - p);
			buf[q - p] = '\0';
			q++;
			switch (*q) {
			case 'i':
				strcat(buf, ":");
				addf_hdr(&CTX.watch_reqhdr, buf, "-");
				break;
			case 'o':
				strcat(buf, ":");
				addf_hdr(&CTX.watch_resphdr, buf, "-");
				break;
			case 't':
				addf_time(*q, buf, NULL);
				break;
			case 'x':
				if (!strcmp(buf, "Varnish:time_firstbyte")) {
					addf_fragment(&CTX.frag[F_ttfb], "");
					break;
				}
				if (!strcmp(buf, "Varnish:hitmiss")) {
					addf_strptr(&CTX.hitmiss);
					break;
				}
				if (!strcmp(buf, "Varnish:handling")) {
					addf_strptr(&CTX.handling);
					break;
				}
				if (!strncmp(buf, "VCL_Log:", 8)) {
					addf_vcl_log(buf + 8, "");
					break;
				}
				/* FALLTHROUGH */
			default:
				VUT_Error(1, "Unknown format specifier at: %s",
				    p - 2);
			}
			p = q;
			break;
		default:
			VUT_Error(1, "Unknown format specifier at: %s", p - 1);
		}
	}

	if (VSB_len(vsb) > 0) {
		/* Add any remaining static */
		AZ(VSB_finish(vsb));
		addf_string(VSB_data(vsb));
		VSB_clear(vsb);
	}

	VSB_delete(vsb);
}

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

static void
frag_fields(const char *b, const char *e, ...)
{
	va_list ap;
	const char *p, *q;
	int n, field;
	struct fragment *frag;

	AN(b);
	AN(e);
	va_start(ap, e);

	field = va_arg(ap, int);
	frag = va_arg(ap, struct fragment *);
	for (n = 1, q = b; q < e; n++) {
		assert(field > 0);
		AN(frag);

		p = q;
		/* Skip WS */
		while (p < e && isspace(*p))
			p++;
		q = p;
		/* Skip non-WS */
		while (q < e && !isspace(*q))
			q++;

		if (field == n) {
			if (frag->gen != CTX.gen) {
				/* We only grab the same matching field once */
				frag->gen = CTX.gen;
				frag->b = p;
				frag->e = q;
			}
			field = va_arg(ap, int);
			if (field == 0)
				break;
			frag = va_arg(ap, struct fragment *);
		}
	}
	va_end(ap);
}

static void
frag_line(const char *b, const char *e, struct fragment *f)
{

	if (f->gen == CTX.gen)
		/* We only grab the same matching record once */
		return;

	/* Skip leading space */
	while (b < e && isspace(*b))
		++b;

	/* Skip trailing space */
	while (e > b && isspace(*(e - 1)))
		--e;

	f->gen = CTX.gen;
	f->b = b;
	f->e = e;
}

static void
process_hdr(const struct watch_head *head, const char *b, const char *e)
{
	struct watch *w;

	VTAILQ_FOREACH(w, head, list) {
		if (strncasecmp(b, w->key, w->keylen))
			continue;
		frag_line(b + w->keylen, e, &w->frag);
	}
}

static int __match_proto__(VSLQ_dispatch_f)
dispatch_f(struct VSL_data *vsl, struct VSL_transaction * const pt[],
    void *priv)
{
	struct VSL_transaction *t;
	unsigned tag;
	const char *b, *e, *p;
	struct watch *w;
	int i, skip;

	(void)vsl;
	(void)priv;

	for (t = pt[0]; t != NULL; t = *++pt) {
		CTX.gen++;
		if (t->type != VSL_t_req)
			/* Only look at client requests */
			continue;
		if (t->reason == VSL_r_esi)
			/* Skip ESI requests */
			continue;
		CTX.hitmiss = "-";
		CTX.handling = "-";
		skip = 0;
		while (skip == 0 && 1 == VSL_Next(t->c)) {
			tag = VSL_TAG(t->c->rec.ptr);
			b = VSL_CDATA(t->c->rec.ptr);
			e = b + VSL_LEN(t->c->rec.ptr);
			while (e > b && e[-1] == '\0')
				e--;

			switch (tag) {
			case SLT_ReqStart:
				frag_fields(b, e, 1, &CTX.frag[F_h], 0, NULL);
				break;
			case SLT_ReqMethod:
				frag_line(b, e, &CTX.frag[F_m]);
				break;
			case SLT_ReqURL:
				p = memchr(b, '?', e - b);
				if (p == NULL)
					p = e;
				frag_line(b, p, &CTX.frag[F_U]);
				frag_line(p, e, &CTX.frag[F_q]);
				break;
			case SLT_ReqProtocol:
				frag_line(b, e, &CTX.frag[F_H]);
				break;
			case SLT_RespStatus:
				frag_line(b, e, &CTX.frag[F_s]);
				break;
			case SLT_ReqAcct:
				frag_fields(b, e, 5, &CTX.frag[F_b], 0, NULL);
				break;
			case SLT_Timestamp:
				if (isprefix(b, "Start:", e, &p)) {
					frag_fields(p, e, 1,
					    &CTX.frag[F_tstart], 0, NULL);

				} else if (isprefix(b, "Resp:", e, &p)) {
					frag_fields(p, e, 1,
					    &CTX.frag[F_tend], 0, NULL);

				} else if (isprefix(b, "Process:", e, &p)) {
					frag_fields(p, e, 2,
					    &CTX.frag[F_ttfb], 0, NULL);
				}
				break;
			case SLT_ReqHeader:
				if (isprefix(b, "Host:", e, &p))
					frag_line(p, e, &CTX.frag[F_host]);
				else if (isprefix(b, "Authorization:", e, &p) &&
				    isprefix(p, "basic", e, &p))
					frag_line(p, e, &CTX.frag[F_auth]);
				break;
			case SLT_VCL_call:
				if (!strcasecmp(b, "recv")) {
					CTX.hitmiss = "-";
					CTX.handling = "-";
				} else if (!strcasecmp(b, "hit")) {
					CTX.hitmiss = "hit";
					CTX.handling = "hit";
				} else if (!strcasecmp(b, "miss")) {
					CTX.hitmiss = "miss";
					CTX.handling = "miss";
				} else if (!strcasecmp(b, "pass")) {
					CTX.hitmiss = "miss";
					CTX.handling = "pass";
				} else if (!strcasecmp(b, "error")) {
					/* Arguably, error isn't a hit or
					   a miss, but miss is less
					   wrong */
					CTX.hitmiss = "miss";
					CTX.handling = "error";
				} else if (!strcasecmp(b, "pipe")) {
					CTX.hitmiss = "miss";
					CTX.handling = "pipe";
				}
				break;
			case SLT_VCL_return:
				if (!strcasecmp(b, "restart"))
					skip = 1;
				break;
			default:
				break;
			}

			if (tag == SLT_VCL_Log) {
				VTAILQ_FOREACH(w, &CTX.watch_vcl_log, list) {
					CHECK_OBJ_NOTNULL(w, WATCH_MAGIC);
					if (strncmp(b, w->key, w->keylen))
						continue;
					p = b + w->keylen;
					if (*p != ':')
						continue;
					p++;
					if (p > e)
						continue;
					frag_line(p, e, &w->frag);
				}
			}
			if (tag == SLT_ReqHeader)
				process_hdr(&CTX.watch_reqhdr, b, e);
			if (tag == SLT_RespHeader)
				process_hdr(&CTX.watch_resphdr, b, e);
		}
		if (skip)
			continue;
		i = print();
		if (i)
			return (i);
	}

	return (0);
}

int
main(int argc, char * const *argv)
{
	signed char opt;
	char *format = NULL;

	memset(&CTX, 0, sizeof CTX);
	VTAILQ_INIT(&CTX.format);
	VTAILQ_INIT(&CTX.watch_vcl_log);
	VTAILQ_INIT(&CTX.watch_reqhdr);
	VTAILQ_INIT(&CTX.watch_resphdr);
	CTX.vsb = VSB_new_auto();
	AN(CTX.vsb);
	VB64_init();
	VUT_Init(progname);

	while ((opt = getopt(argc, argv, vopt_optstring)) != -1) {
		switch (opt) {
		case 'a':
			/* Append to file */
			CTX.a_opt = 1;
			break;
		case 'F':
			/* Format string */
			if (format != NULL)
				free(format);
			format = strdup(optarg);
			AN(format);
			break;
		case 'h':
			/* Usage help */
			usage(0);
		case 'w':
			/* Write to file */
			REPLACE(CTX.w_arg, optarg);
			break;
		default:
			if (!VUT_Arg(opt, optarg))
				usage(1);
		}
	}

	if (optind != argc)
		usage(1);

	/* Check for valid grouping mode */
	assert(VUT.g_arg < VSL_g__MAX);
	if (VUT.g_arg != VSL_g_vxid && VUT.g_arg != VSL_g_request)
		VUT_Error(1, "Invalid grouping mode: %s",
		    VSLQ_grouping[VUT.g_arg]);

	/* Prepare output format */
	if (format == NULL)
		format = strdup(FORMAT);
	parse_format(format);
	free(format);
	format = NULL;

	/* Setup output */
	VUT.dispatch_f = &dispatch_f;
	VUT.dispatch_priv = NULL;
	if (CTX.w_arg) {
		openout(CTX.a_opt);
		AN(CTX.fo);
		VUT.sighup_f = &rotateout;
	} else
		CTX.fo = stdout;
	VUT.idle_f = &flushout;

	VUT_Setup();
	VUT_Main();
	VUT_Fini();

	exit(0);
}
