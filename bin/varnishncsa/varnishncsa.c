/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
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
 * display it in Apache / NCSA combined log format.
 *
 * See doc/sphinx/reference/varnishncsa.rst for the supported format
 * specifiers.
 *
 */

#include "config.h"

#define _WITH_GETLINE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <inttypes.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include "base64.h"
#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "vdef.h"
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
	F_b,			/* %b Body bytes sent */
	F_h,			/* %h Host name / IP Address */
	F_m,			/* %m Method */
	F_s,			/* %s Status */
	F_I,			/* %I Bytes received */
	F_O,			/* %O Bytes sent */
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

	char			time_type;
	VTAILQ_ENTRY(format)	list;
	format_f		*func;
	struct fragment		*frag;
	char			*string;
	const char *const	*strptr;
	char			*time_fmt;
	int32_t			*int32;
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

struct vsl_watch {
	unsigned		magic;
#define VSL_WATCH_MAGIC		0xE3E27D23

	VTAILQ_ENTRY(vsl_watch)	list;
	enum VSL_tag_e		tag;
	int			idx;
	struct fragment		frag;
};
VTAILQ_HEAD(vsl_watch_head, vsl_watch);

struct ctx {
	/* Options */
	int			a_opt;
	int			b_opt;
	int			c_opt;
	char			*w_arg;

	FILE			*fo;
	struct vsb		*vsb;
	unsigned		gen;
	VTAILQ_HEAD(,format)	format;

	/* State */
	struct watch_head	watch_vcl_log;
	struct watch_head	watch_reqhdr; /* also bereqhdr */
	struct watch_head	watch_resphdr; /* also beresphdr */
	struct vsl_watch_head	watch_vsl;
	struct fragment		frag[F__MAX];
	const char		*hitmiss;
	const char		*handling;
	const char		*side;
	int32_t			vxid;
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
		VUT_Error(1, "Can't open output file (%s)",
		    strerror(errno));
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

static int
vsb_esc_cat(struct vsb *sb, const char *b, const char *e)
{
	AN(b);

	for (; b < e; b++) {
		if (isspace(*b)) {
			switch (*b) {
			case '\n':
				VSB_cat(sb, "\\n");
				break;
			case '\t':
				VSB_cat(sb, "\\t");
				break;
			case '\f':
				VSB_cat(sb, "\\f");
				break;
			case '\r':
				VSB_cat(sb, "\\r");
				break;
			case '\v':
				VSB_cat(sb, "\\v");
				break;
			default:
				VSB_putc(sb, *b);
				break;
			}
		} else if (isprint(*b)) {
			switch (*b) {
			case '"':
				VSB_cat(sb, "\\\"");
				break;
			case '\\':
				VSB_cat(sb, "\\\\");
				break;
			default:
				VSB_putc(sb, *b);
				break;
			}
		} else
			VSB_printf(sb, "\\x%02hhx", *b);
	}

	return (VSB_error(sb));
}

static inline int
vsb_fcat(struct vsb *vsb, const struct fragment *f, const char *dflt)
{
	if (f->gen == CTX.gen) {
		assert(f->b <= f->e);
		return (vsb_esc_cat(vsb, f->b, f->e));
	}
	if (dflt)
		return (vsb_esc_cat(vsb, dflt, dflt + strlen(dflt)));
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
format_int32(const struct format *format)
{

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	VSB_printf(CTX.vsb, "%" PRIi32, *format->int32);
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
		AZ(vsb_esc_cat(CTX.vsb, format->string,
		    format->string + strlen(format->string)));
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
	if (CTX.frag[F_tstart].gen == CTX.gen) {
		t_start = strtod(CTX.frag[F_tstart].b, &p);
		if (p != CTX.frag[F_tstart].e)
			t_start = NAN;
	} else
		t_start = NAN;
	if (isnan(t_start)) {
		/* Missing t_start is a no go */
		if (format->string == NULL)
			return (-1);
		AZ(VSB_cat(CTX.vsb, format->string));
		return (0);
	}

	/* Missing t_end defaults to t_start */
	if (CTX.frag[F_tend].gen == CTX.gen) {
		t_end = strtod(CTX.frag[F_tend].b, &p);
		if (p != CTX.frag[F_tend].e)
			t_end = t_start;
	} else
		t_end = t_start;

	switch (format->time_type) {
	case 'D':
		AZ(VSB_printf(CTX.vsb, "%d",
		    (int)((t_end - t_start) * 1e6)));
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
	AZ(vsb_fcat(CTX.vsb, &CTX.frag[F_U], ""));
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
		AZ(vsb_esc_cat(CTX.vsb, format->string,
		    format->string + strlen(format->string)));
		return (0);
	}
	q = strchr(buf, ':');
	if (q != NULL)
		*q = '\0';
	AZ(vsb_esc_cat(CTX.vsb, buf, buf + strlen(buf)));
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
addf_int32(int32_t *i)
{
	struct format *f;

	AN(i);
	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_int32;
	f->int32 = i;
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
addf_vsl(enum VSL_tag_e tag, long i)
{
	struct vsl_watch *w;

	ALLOC_OBJ(w, VSL_WATCH_MAGIC);
	AN(w);
	w->tag = tag;
	assert(i <= INT_MAX);
	w->idx = i;
	VTAILQ_INSERT_TAIL(&CTX.watch_vsl, w, list);

	addf_fragment(&w->frag, "-");
}

static void
addf_auth(const char *str)
{
	struct format *f;

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = &format_auth;
	if (str != NULL) {
		f->string = strdup(str);
		AN(f->string);
	}
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
parse_x_format(char *buf)
{
	char *e, *r, *s;
	int slt;
	long i;

	if (!strcmp(buf, "Varnish:time_firstbyte")) {
		addf_fragment(&CTX.frag[F_ttfb], "");
		return;
	}
	if (!strcmp(buf, "Varnish:hitmiss")) {
		addf_strptr(&CTX.hitmiss);
		return;
	}
	if (!strcmp(buf, "Varnish:handling")) {
		addf_strptr(&CTX.handling);
		return;
	}
	if (!strcmp(buf, "Varnish:side")) {
		addf_strptr(&CTX.side);
		return;
	}
	if (!strcmp(buf, "Varnish:vxid")) {
		addf_int32(&CTX.vxid);
		return;
	}
	if (!strncmp(buf, "VCL_Log:", 8)) {
		addf_vcl_log(buf + 8, "");
		return;
	}
	if (!strncmp(buf, "VSL:", 4)) {
		buf += 4;
		e = buf;
		while (*e != '\0')
			e++;
		if (e == buf)
			VUT_Error(1, "Missing tag in VSL:");
		if (e[-1] == ']') {
			r = e - 1;
			while (r > buf && *r != '[')
				r--;
			if (r == buf || r[1] == ']')
				VUT_Error(1, "Syntax error: VSL:%s", buf);
			e[-1] = '\0';
			i = strtol(r + 1, &s, 10);
			if (s != e - 1)
				VUT_Error(1, "Syntax error: VSL:%s]", buf);
			if (i <= 0)
				VUT_Error(1,
				    "Syntax error. Field specifier must be"
				    " positive: %s]",
				    buf);
			if (i > INT_MAX) {
				VUT_Error(1,
				    "Field specifier %ld for the tag VSL:%s]"
				    " is probably too high",
				    i, buf);
			}
			*r = '\0';
		} else
			i = 0;
		slt = VSL_Name2Tag(buf, -1);
		if (slt == -2)
			VUT_Error(1, "Tag not unique: %s", buf);
		if (slt == -1)
			VUT_Error(1, "Unknown log tag: %s", buf);
		assert(slt >= 0);

		addf_vsl(slt, i);
		return;
	}
	VUT_Error(1, "Unknown formatting extension: %s", buf);
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
		case 'b':	/* Body bytes sent */
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
		case 'I':	/* Bytes received */
			addf_fragment(&CTX.frag[F_I], "-");
			break;
		case 'l':	/* Client user ID (identd) always '-' */
			AZ(VSB_putc(vsb, '-'));
			break;
		case 'm':	/* Method */
			addf_fragment(&CTX.frag[F_m], "-");
			break;
		case 'O':	/* Bytes sent */
			addf_fragment(&CTX.frag[F_O], "-");
			break;
		case 'q':	/* Query string */
			addf_fragment(&CTX.frag[F_q], "");
			break;
		case 'r':	/* Request line */
			addf_requestline();
			break;
		case 's':	/* Status code */
			addf_fragment(&CTX.frag[F_s], "-");
			break;
		case 't':	/* strftime */
			addf_time(*p, TIME_FMT, NULL);
			break;
		case 'T':	/* Int request time */
			addf_time(*p, NULL, NULL);
			break;
		case 'u':	/* Remote user from auth */
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
				VUT_Error(1, "Unmatched bracket at: %s",
				    p - 2);
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
				parse_x_format(buf);
				break;
			default:
				VUT_Error(1,
				    "Unknown format specifier at: %s",
				    p - 2);
			}
			p = q;
			break;
		default:
			VUT_Error(1, "Unknown format specifier at: %s",
			    p - 1);
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
isprefix(const char *prefix, const char *b, const char *e, const char **next)
{
	size_t len;

	len = strlen(prefix);
	if (e - b < len || strncasecmp(b, prefix, len))
		return (0);
	b += len;
	if (next) {
		while (b < e && *b && *b == ' ')
			b++;
		*next = b;
	}
	return (1);
}

static void
frag_fields(int force, const char *b, const char *e, ...)
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
		/* caller must sort the fields, or this loop will not work: */
		assert(field >= n);
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
			if (frag->gen != CTX.gen || force) {
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
frag_line(int force, const char *b, const char *e, struct fragment *f)
{

	if (f->gen == CTX.gen && !force)
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
		if (e - b < w->keylen || strncasecmp(b, w->key, w->keylen))
			continue;
		frag_line(1, b + w->keylen, e, &w->frag);
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
	struct vsl_watch *vslw;
	int i, skip, be_mark;
	(void)vsl;
	(void)priv;

#define BACKEND_MARKER (INT_MAX / 2 + 1)
	assert(BACKEND_MARKER >= VSL_t__MAX);

	for (t = pt[0]; t != NULL; t = *++pt) {
		CTX.gen++;

		/* Consider client requests only if in client mode.
		   Consider backend requests only if in backend mode. */
		if (t->type == VSL_t_req && CTX.c_opt) {
			CTX.side = "c";
			be_mark = 0;
		} else if (t->type == VSL_t_bereq && CTX.b_opt) {
			CTX.side = "b";
			be_mark = BACKEND_MARKER;
		} else
			continue;
		if (t->reason == VSL_r_esi)
			/* Skip ESI requests */
			continue;
		CTX.hitmiss = "-";
		CTX.handling = "-";
		CTX.vxid = t->vxid;
		skip = 0;
		while (skip == 0 && 1 == VSL_Next(t->c)) {
			tag = VSL_TAG(t->c->rec.ptr);
			b = VSL_CDATA(t->c->rec.ptr);
			e = b + VSL_LEN(t->c->rec.ptr);
			while (e > b && e[-1] == '\0')
				e--;

			switch (tag + be_mark) {
			case SLT_HttpGarbage + BACKEND_MARKER:
			case SLT_HttpGarbage:
				skip = 1;
				break;
			case SLT_PipeAcct:
				frag_fields(0, b, e,
				    3, &CTX.frag[F_I],
				    4, &CTX.frag[F_O],
				    0, NULL);
				break;
			case (SLT_BackendStart + BACKEND_MARKER):
				frag_fields(1, b, e,
				    1, &CTX.frag[F_h],
				    0, NULL);
				break;
			case SLT_ReqStart:
				frag_fields(0, b, e,
				    1, &CTX.frag[F_h],
				    0, NULL);
				break;
			case (SLT_BereqMethod + BACKEND_MARKER):
			case SLT_ReqMethod:
				frag_line(0, b, e, &CTX.frag[F_m]);
				break;
			case (SLT_BereqURL + BACKEND_MARKER):
			case SLT_ReqURL:
				p = memchr(b, '?', e - b);
				if (p == NULL)
					p = e;
				frag_line(0, b, p, &CTX.frag[F_U]);
				frag_line(0, p, e, &CTX.frag[F_q]);
				break;
			case (SLT_BereqProtocol + BACKEND_MARKER):
			case SLT_ReqProtocol:
				frag_line(0, b, e, &CTX.frag[F_H]);
				break;
			case (SLT_BerespStatus + BACKEND_MARKER):
			case SLT_RespStatus:
				frag_line(1, b, e, &CTX.frag[F_s]);
				break;
			case (SLT_BereqAcct + BACKEND_MARKER):
			case SLT_ReqAcct:
				frag_fields(0, b, e,
				    3, &CTX.frag[F_I],
				    5, &CTX.frag[F_b],
				    6, &CTX.frag[F_O],
				    0, NULL);
				break;
			case (SLT_Timestamp + BACKEND_MARKER):
			case SLT_Timestamp:
				if (isprefix("Start:", b, e, &p)) {
					frag_fields(0, p, e, 1,
					    &CTX.frag[F_tstart], 0, NULL);

				} else if (isprefix("Resp:", b, e, &p) ||
				    isprefix("PipeSess:", b, e, &p) ||
				    isprefix("BerespBody:", b, e, &p)) {
					frag_fields(0, p, e, 1,
					    &CTX.frag[F_tend], 0, NULL);

				} else if (isprefix("Process:", b, e, &p) ||
				    isprefix("Pipe:", b, e, &p) ||
				    isprefix("Beresp:", b, e, &p)) {
					frag_fields(0, p, e, 2,
					    &CTX.frag[F_ttfb], 0, NULL);
				}
				break;
			case (SLT_BereqHeader + BACKEND_MARKER):
			case SLT_ReqHeader:
				if (isprefix("Authorization:", b, e, &p) &&
				    isprefix("basic ", p, e, &p))
					frag_line(0, p, e,
					    &CTX.frag[F_auth]);
				else if (isprefix("Host:", b, e, &p))
					frag_line(0, p, e,
					    &CTX.frag[F_host]);
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
				} else if (!strcasecmp(b, "synth")) {
					/* Arguably, synth isn't a hit or
					   a miss, but miss is less
					   wrong */
					CTX.hitmiss = "miss";
					CTX.handling = "synth";
				}
				break;
			case SLT_VCL_return:
				if (!strcasecmp(b, "pipe")) {
					CTX.hitmiss = "miss";
					CTX.handling = "pipe";
				} else if (!strcasecmp(b, "restart"))
					skip = 1;
				break;
			case (SLT_VCL_Log + BACKEND_MARKER):
			case SLT_VCL_Log:
				VTAILQ_FOREACH(w, &CTX.watch_vcl_log, list) {
					CHECK_OBJ_NOTNULL(w, WATCH_MAGIC);
					if (e - b <= w->keylen ||
					    strncmp(b, w->key, w->keylen))
						continue;
					p = b + w->keylen;
					if (*p != ':')
						continue;
					p++;
					if (p > e)
						continue;
					frag_line(0, p, e, &w->frag);
				}
				break;
			default:
				break;
			}

			if ((tag == SLT_ReqHeader && CTX.c_opt) ||
			    (tag == SLT_BereqHeader && CTX.b_opt))
				process_hdr(&CTX.watch_reqhdr, b, e);
			else if ((tag == SLT_RespHeader && CTX.c_opt) ||
			    (tag == SLT_BerespHeader && CTX.b_opt))
				process_hdr(&CTX.watch_resphdr, b, e);

			VTAILQ_FOREACH(vslw, &CTX.watch_vsl, list) {
				CHECK_OBJ_NOTNULL(vslw, VSL_WATCH_MAGIC);
				if (tag == vslw->tag) {
					if (vslw->idx == 0)
						frag_line(0, b, e,
						    &vslw->frag);
					else
						frag_fields(0, b, e,
						    vslw->idx, &vslw->frag,
						    0, NULL);
				}
			}
		}
		if (skip)
			continue;
		i = print();
		if (i)
			return (i);
	}

	return (0);
}

static int __match_proto__(VUT_cb_f)
sighup(void)
{
	return (1);
}

static char *
read_format(const char *formatfile)
{
	FILE *fmtfile;
	size_t len = 0;
	int fmtlen;
	char *fmt = NULL;

	fmtfile = fopen(formatfile, "r");
	if (fmtfile == NULL)
		VUT_Error(1, "Can't open format file (%s)",
		    strerror(errno));
	fmtlen = getline(&fmt, &len, fmtfile);
	if (fmtlen == -1) {
		free(fmt);
		if (feof(fmtfile))
			VUT_Error(1, "Empty format file");
		else
			VUT_Error(1, "Can't read format from file (%s)",
			    strerror(errno));
	}
	fclose(fmtfile);
	if (fmt[fmtlen - 1] == '\n')
		fmt[fmtlen - 1] = '\0';
	return (fmt);
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
	VTAILQ_INIT(&CTX.watch_vsl);
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
		case 'b':
			/* backend mode */
			CTX.b_opt = 1;
			break;
		case 'c':
			/* client mode */
			CTX.c_opt = 1;
			break;
		case 'F':
			/* Format string */
			if (format != NULL)
				free(format);
			format = strdup(optarg);
			AN(format);
			break;
		case 'f':
			/* Format string from file */
			if (format != NULL)
				free(format);
			format = read_format(optarg);
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
	/* default is client mode: */
	if (!CTX.b_opt)
		CTX.c_opt = 1;

	if (optind != argc)
		usage(1);

	if (VUT.D_opt && !CTX.w_arg)
		VUT_Error(1, "Missing -w option");

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
	VUT.sighup_f = sighup;
	if (CTX.w_arg) {
		openout(CTX.a_opt);
		AN(CTX.fo);
		if (VUT.D_opt)
			VUT.sighup_f = &rotateout;
	} else
		CTX.fo = stdout;
	VUT.idle_f = &flushout;

	VUT_Setup();
	VUT_Main();
	VUT_Fini();

	exit(0);
}
