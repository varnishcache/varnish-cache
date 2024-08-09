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
 * Obtain log data from the shared memory log, order it by session ID, and
 * display it in Apache / NCSA combined log format.
 *
 * See doc/sphinx/reference/varnishncsa.rst for the supported format
 * specifiers.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <inttypes.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#define VOPT_DEFINITION
#define VOPT_INC "varnishncsa_options.h"

#include "vdef.h"

#include "vapi/vsl.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "venc.h"
#include "vsb.h"
#include "vut.h"
#include "vqueue.h"
#include "miniobj.h"

#define TIME_FMT "[%d/%b/%Y:%T %z]"
#define FORMAT "%h %l %u %t \"%r\" %s %b \"%{Referer}i\" \"%{User-agent}i\""

static struct VUT *vut;

struct format;

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
	uint64_t		gen;
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
	int64_t			*int64;
};

struct watch {
	unsigned		magic;
#define WATCH_MAGIC		0xA7D4005C

	VTAILQ_ENTRY(watch)	list;
	char			*key;
	int			keylen;
	struct fragment		frag;
};
VTAILQ_HEAD(watch_head, watch);

struct vsl_watch {
	unsigned		magic;
#define VSL_WATCH_MAGIC		0xE3E27D23

	VTAILQ_ENTRY(vsl_watch)	list;
	enum VSL_tag_e		tag;
	int			idx;
	char			*prefix;
	int			prefixlen;
	struct fragment		frag;
};
VTAILQ_HEAD(vsl_watch_head, vsl_watch);

static struct ctx {
	/* Options */
	int			a_opt;
	char			*w_arg;

	FILE			*fo;
	struct vsb		*vsb;
	uint64_t		gen;
	VTAILQ_HEAD(,format)	format;
	int			quote_how;
	char			*missing_string;
	char			*missing_int;

	/* State */
	struct watch_head	watch_vcl_log;
	struct watch_head	watch_reqhdr; /* also bereqhdr */
	struct watch_head	watch_resphdr; /* also beresphdr */
	struct vsl_watch_head	watch_vsl;
	struct fragment		frag[F__MAX];
	const char		*hitmiss;
	const char		*handling;
	const char		*side;
	int64_t			vxid;
} CTX;

static void parse_format(const char *format);

static void
openout(int append)
{

	AN(CTX.w_arg);
	if (!strcmp(CTX.w_arg, "-"))
		CTX.fo = stdout;
	else
		CTX.fo = fopen(CTX.w_arg, append ? "a" : "w");
	if (CTX.fo == NULL)
		VUT_Error(vut, 1, "Can't open output file (%s)",
		    strerror(errno));
}

static int v_matchproto_(VUT_cb_f)
rotateout(struct VUT *v)
{

	assert(v == vut);
	AN(CTX.w_arg);
	AN(CTX.fo);
	(void)fclose(CTX.fo);
	openout(1);
	AN(CTX.fo);
	return (0);
}

static int v_matchproto_(VUT_cb_f)
flushout(struct VUT *v)
{

	assert(v == vut);
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
		VSB_quote(vsb, f->b, f->e - f->b, CTX.quote_how);
	} else if (dflt)
		VSB_quote(vsb, dflt, -1, CTX.quote_how);
	else
		return (-1);
	return (0);
}

static int v_matchproto_(format_f)
format_string(const struct format *format)
{

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	AN(format->string);
	AZ(VSB_cat(CTX.vsb, format->string));
	return (1);
}

static int v_matchproto_(format_f)
format_strptr(const struct format *format)
{

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	AN(format->strptr);
	AN(*format->strptr);
	AZ(VSB_cat(CTX.vsb, *format->strptr));
	return (1);
}

static int v_matchproto_(format_f)
format_int64(const struct format *format)
{

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	VSB_printf(CTX.vsb, "%jd", (intmax_t)*format->int64);
	return (1);
}

static int v_matchproto_(format_f)
format_fragment(const struct format *format)
{

	CHECK_OBJ_NOTNULL(format, FORMAT_MAGIC);
	AN(format->frag);

	if (format->frag->gen != CTX.gen) {
		if (format->string == NULL)
			return (-1);
		VSB_quote(CTX.vsb, format->string, -1, CTX.quote_how);
		return (0);
	}
	AZ(vsb_fcat(CTX.vsb, format->frag, NULL));
	return (1);
}

static int v_matchproto_(format_f)
format_time(const struct format *format)
{
	double t_start, t_end, d;
	char *p;
	char buf[64];
	time_t t;
	intmax_t l;
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

	AN(format->time_fmt);

	switch (format->time_type) {
	case 't':
		t = (intmax_t)floor(t_start);
		(void)localtime_r(&t, &tm);
		AN(strftime(buf, sizeof buf, format->time_fmt, &tm));
		AZ(VSB_cat(CTX.vsb, buf));
		return (1);
	case '3':
		l = (intmax_t)(modf(t_start, &d) * 1e3);
		break;
	case '6':
		l = (intmax_t)(modf(t_start, &d) * 1e6);
		break;
	case 'S':
		l = (intmax_t)t_start;
		break;
	case 'M':
		l = (intmax_t)(t_start * 1e3);
		break;
	case 'U':
		l = (intmax_t)(t_start * 1e6);
		break;
	case 's':
		l = (intmax_t)(t_end - t_start);
		break;
	case 'm':
		l = (intmax_t)((t_end - t_start) * 1e3);
		break;
	case 'u':
		l = (intmax_t)((t_end - t_start) * 1e6);
		break;
	default:
		WRONG("Time format specifier");
	}

#ifdef __FreeBSD__
	assert(fmtcheck(format->time_fmt, "%jd") == format->time_fmt);
#endif
	AZ(VSB_printf(CTX.vsb, format->time_fmt, l));

	return (1);
}

static int v_matchproto_(format_f)
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

static int v_matchproto_(format_f)
format_auth(const struct format *format)
{
	struct vsb *vsb = VSB_new_auto();
	AN(vsb);
	char *q;

	if (CTX.frag[F_auth].gen != CTX.gen ||
	    VENC_Decode_Base64(vsb, CTX.frag[F_auth].b, CTX.frag[F_auth].e)) {
		VSB_destroy(&vsb);
		if (format->string == NULL)
			return (-1);
		VSB_quote(CTX.vsb, format->string, -1, CTX.quote_how);
		return (0);
	}
	AZ(VSB_finish(vsb));
	q = strchr(VSB_data(vsb), ':');
	if (q != NULL)
		*q = '\0';
	VSB_quote(CTX.vsb, VSB_data(vsb), -1, CTX.quote_how);
	VSB_destroy(&vsb);
	return (1);
}

static int
print(void)
{
	const struct format *f;
	int i, r = 1;

	VSB_clear(CTX.vsb);
	VTAILQ_FOREACH(f, &CTX.format, list) {
		CHECK_OBJ_NOTNULL(f, FORMAT_MAGIC);
		i = (f->func)(f);
		AZ(VSB_error(CTX.vsb));
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
	f->func = format_string;
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
	f->func = format_strptr;
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
	f->func = format_fragment;
	f->frag = frag;
	if (str != NULL) {
		f->string = strdup(str);
		AN(f->string);
	}
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_int64(int64_t *i)
{
	struct format *f;

	AN(i);
	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = format_int64;
	f->int64 = i;
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_time(char type, const char *fmt)
{
	struct format *f;

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	AN(fmt);
	f->func = format_time;
	f->time_type = type;
	f->time_fmt = strdup(fmt);

	if (f->time_type == 'T') {
		if (!strcmp(fmt, "s"))
			f->time_type = 's';
		else if (!strcmp(fmt, "ms"))
			f->time_type = 'm';
		else if (!strcmp(fmt, "us"))
			f->time_type = 'u';
		else
			VUT_Error(vut, 1, "Unknown specifier: %%{%s}T",
			    fmt);
		REPLACE(f->time_fmt, "%jd");
	} else if (f->time_type == 't') {
		if (!strcmp(fmt, "sec")) {
			f->time_type = 'S';
			REPLACE(f->time_fmt, "%jd");
		} else if (!strncmp(fmt, "msec", 4)) {
			fmt += 4;
			if (!strcmp(fmt, "_frac")) {
				f->time_type = '3';
				REPLACE(f->time_fmt, "%03jd");
			} else if (*fmt == '\0') {
				f->time_type = 'M';
				REPLACE(f->time_fmt, "%jd");
			}
		} else if (!strncmp(fmt, "usec", 4)) {
			fmt += 4;
			if (!strcmp(fmt, "_frac")) {
				f->time_type = '6';
				REPLACE(f->time_fmt, "%06jd");
			} else if (*fmt == '\0') {
				f->time_type = 'U';
				REPLACE(f->time_fmt, "%jd");
			}
		}
	}

	AN(f->time_fmt);
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_requestline(void)
{
	struct format *f;

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = format_requestline;
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_vcl_log(const char *key)
{
	struct watch *w;
	struct format *f;

	AN(key);
	ALLOC_OBJ(w, WATCH_MAGIC);
	AN(w);
	w->keylen = asprintf(&w->key, "%s:", key);
	assert(w->keylen > 0);
	VTAILQ_INSERT_TAIL(&CTX.watch_vcl_log, w, list);

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = format_fragment;
	f->frag = &w->frag;
	f->string = strdup("");
	AN(f->string);
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_hdr(struct watch_head *head, const char *key)
{
	struct watch *w;
	struct format *f;

	AN(head);
	AN(key);
	ALLOC_OBJ(w, WATCH_MAGIC);
	AN(w);
	w->keylen = asprintf(&w->key, "%s:", key);
	assert(w->keylen > 0);
	VTAILQ_INSERT_TAIL(head, w, list);

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = format_fragment;
	f->frag = &w->frag;
	f->string = strdup(CTX.missing_string);
	AN(f->string);
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
addf_vsl(enum VSL_tag_e tag, long i, const char *prefix)
{
	struct vsl_watch *w;

	ALLOC_OBJ(w, VSL_WATCH_MAGIC);
	AN(w);
	if (VSL_tagflags[tag] && CTX.quote_how != VSB_QUOTE_JSON)
		VUT_Error(vut, 1, "Tag %s can contain control characters",
		    VSL_tags[tag]);
	w->tag = tag;
	assert(i <= INT_MAX);
	w->idx = i;
	if (prefix != NULL) {
		w->prefixlen = asprintf(&w->prefix, "%s:", prefix);
		assert(w->prefixlen > 0);
	}
	VTAILQ_INSERT_TAIL(&CTX.watch_vsl, w, list);
	addf_fragment(&w->frag, CTX.missing_string);
}

static void
addf_auth(void)
{
	struct format *f;

	ALLOC_OBJ(f, FORMAT_MAGIC);
	AN(f);
	f->func = format_auth;
	f->string = strdup("-");
	AN(f->string);
	VTAILQ_INSERT_TAIL(&CTX.format, f, list);
}

static void
parse_x_format(char *buf)
{
	char *e, *r, *s;
	long lval;
	int slt;

	if (!strcmp(buf, "Varnish:time_firstbyte")) {
		addf_fragment(&CTX.frag[F_ttfb], CTX.missing_int);
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
		addf_int64(&CTX.vxid);
		return;
	}
	if (!strncmp(buf, "VCL_Log:", 8)) {
		addf_vcl_log(buf + 8);
		return;
	}
	if (!strncmp(buf, "VSL:", 4)) {
		buf += 4;
		e = buf;
		while (*e != '\0')
			e++;
		if (e == buf)
			VUT_Error(vut, 1, "Missing tag in VSL:");
		if (e[-1] == ']') {
			r = e - 1;
			while (r > buf && *r != '[')
				r--;
			if (r == buf || r[1] == ']')
				VUT_Error(vut, 1, "Syntax error: VSL:%s", buf);
			e[-1] = '\0';
			lval = strtol(r + 1, &s, 10);
			if (s != e - 1)
				VUT_Error(vut, 1, "Syntax error: VSL:%s]", buf);
			if (lval <= 0 || lval > 255) {
				VUT_Error(vut, 1,
				    "Syntax error. Field specifier must be"
				    " between 1 and 255: %s]",
				    buf);
			}
			*r = '\0';
		} else
			lval = 0;
		r = buf;
		while (r < e && *r != ':')
			r++;
		if (r != e) {
			slt = VSL_Name2Tag(buf, r - buf);
			r++;
		} else {
			slt = VSL_Name2Tag(buf, -1);
			r = NULL;
		}
		if (slt == -2)
			VUT_Error(vut, 1, "Tag not unique: %s", buf);
		if (slt == -1)
			VUT_Error(vut, 1, "Unknown log tag: %s", buf);
		assert(slt >= 0);

		addf_vsl((enum VSL_tag_e)slt, lval, r);
		return;
	}
	if (!strcmp(buf, "Varnish:default_format")) {
		parse_format(FORMAT);
		return;
	}
	VUT_Error(vut, 1, "Unknown formatting extension: %s", buf);
}

static void
parse_format(const char *format)
{
	const char *p, *q;
	struct vsb *vsb;
	char buf[256];
	int b;

	if (format == NULL)
		format = FORMAT;

	vsb = VSB_new_auto();
	AN(vsb);

	for (p = format; *p != '\0'; p++) {

		/* Allow the most essential escape sequences in format */
		if (*p == '\\' && p[1] != '\0') {
			if (*++p == 't')
				AZ(VSB_putc(vsb, '\t'));
			else if (*p == 'n')
				AZ(VSB_putc(vsb, '\n'));
			else
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
			addf_fragment(&CTX.frag[F_b], CTX.missing_int);
			break;
		case 'D':	/* Float request time */
			addf_time('T', "us");
			break;
		case 'h':	/* Client host name / IP Address */
			addf_fragment(&CTX.frag[F_h], CTX.missing_string);
			break;
		case 'H':	/* Protocol */
			addf_fragment(&CTX.frag[F_H], "HTTP/1.0");
			break;
		case 'I':	/* Bytes received */
			addf_fragment(&CTX.frag[F_I], CTX.missing_int);
			break;
		case 'l':	/* Client user ID (identd) always '-' */
			AZ(VSB_putc(vsb, '-'));
			break;
		case 'm':	/* Method */
			addf_fragment(&CTX.frag[F_m], CTX.missing_string);
			break;
		case 'O':	/* Bytes sent */
			addf_fragment(&CTX.frag[F_O], CTX.missing_int);
			break;
		case 'q':	/* Query string */
			addf_fragment(&CTX.frag[F_q], "");
			break;
		case 'r':	/* Request line */
			addf_requestline();
			break;
		case 's':	/* Status code */
			addf_fragment(&CTX.frag[F_s], CTX.missing_int);
			break;
		case 't':	/* strftime */
			addf_time(*p, TIME_FMT);
			break;
		case 'T':	/* Int request time */
			addf_time(*p, "s");
			break;
		case 'u':	/* Remote user from auth */
			addf_auth();
			break;
		case 'U':	/* URL */
			addf_fragment(&CTX.frag[F_U], CTX.missing_string);
			break;
		case '{':
			p++;
			q = p;
			b = 1;
			while (*q) {
				if (*q == '{')
					b++;
				else if (*q == '}')
					if (--b == 0)
						break;
				q++;
			}
			if (b > 0)
				VUT_Error(vut, 1, "Unmatched bracket at: %s",
				    p - 2);
			assert((unsigned)(q - p) < sizeof buf - 1);
			strncpy(buf, p, q - p);
			buf[q - p] = '\0';
			q++;
			switch (*q) {
			case 'i':
				addf_hdr(&CTX.watch_reqhdr, buf);
				break;
			case 'o':
				addf_hdr(&CTX.watch_resphdr, buf);
				break;
			case 't':
				addf_time(*q, buf);
				break;
			case 'T':
				addf_time(*q, buf);
				break;
			case 'x':
				parse_x_format(buf);
				break;
			default:
				VUT_Error(vut, 1,
				    "Unknown format specifier at: %s",
				    p - 2);
			}
			p = q;
			break;
		default:
			VUT_Error(vut, 1, "Unknown format specifier at: %s",
			    p - 1);
		}
	}

	if (VSB_len(vsb) > 0) {
		/* Add any remaining static */
		AZ(VSB_finish(vsb));
		addf_string(VSB_data(vsb));
		VSB_clear(vsb);
	}

	VSB_destroy(&vsb);
}

static int
isprefix(const char *prefix, size_t len, const char *b,
    const char *e, const char **next)
{
	assert(len > 0);
	if (e < b + len || strncasecmp(b, prefix, len))
		return (0);
	b += len;
	if (next) {
		while (b < e && *b == ' ')
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

	n = 0;
	while (1) {
		field = va_arg(ap, int);
		frag = va_arg(ap, struct fragment *);
		if (field == 0) {
			AZ(frag);
			break;
		}
		p = q = NULL;
		while (n < field) {
			while (b < e && isspace(*b))
				b++;
			p = b;
			while (b < e && !isspace(*b))
				b++;
			q = b;
			n++;
		}
		assert(p != NULL && q != NULL);
		if (p >= e || q <= p)
			continue;
		if (frag->gen != CTX.gen || force) {
			/* We only grab the same matching field once */
			frag->gen = CTX.gen;
			frag->b = p;
			frag->e = q;
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
	while (e > b && isspace(e[-1]))
		--e;

	f->gen = CTX.gen;
	f->b = b;
	f->e = e;
}

static void
process_hdr(const struct watch_head *head, const char *b, const char *e)
{
	struct watch *w;
	const char *p;

	VTAILQ_FOREACH(w, head, list) {
		CHECK_OBJ_NOTNULL(w, WATCH_MAGIC);
		if (!isprefix(w->key, w->keylen, b, e, &p))
			continue;
		frag_line(1, p, e, &w->frag);
	}
}

static void
process_vsl(const struct vsl_watch_head *head, enum VSL_tag_e tag,
    const char *b, const char *e)
{
	struct vsl_watch *w;
	const char *p;
	VTAILQ_FOREACH(w, head, list) {
		CHECK_OBJ_NOTNULL(w, VSL_WATCH_MAGIC);
		if (tag != w->tag)
			continue;
		p = b;
		if (w->prefixlen > 0 &&
		    !isprefix(w->prefix, w->prefixlen, b, e, &p))
			continue;
		if (w->idx == 0)
			frag_line(0, p, e, &w->frag);
		else
			frag_fields(0, p, e, w->idx, &w->frag, 0, NULL);
	}
}

static int v_matchproto_(VSLQ_dispatch_f)
dispatch_f(struct VSL_data *vsl, struct VSL_transaction * const pt[],
    void *priv)
{
	struct VSL_transaction *t;
	enum VSL_tag_e tag;
	const char *b, *e, *p;
	struct watch *w;
	int i, skip;

	(void)vsl;
	(void)priv;

	for (t = pt[0]; t != NULL; t = *++pt) {
		CTX.gen++;

		if (t->type == VSL_t_req) {
			CTX.side = "c";
		} else if (t->type == VSL_t_bereq) {
			CTX.side = "b";
		} else
			continue;

		CTX.hitmiss = "-";
		CTX.handling = "-";
		CTX.vxid = t->vxid;
		skip = 0;
		while (skip == 0 && 1 == VSL_Next(t->c)) {
			tag = VSL_TAG(t->c->rec.ptr);
			if (VSL_tagflags[tag] &&
			    CTX.quote_how != VSB_QUOTE_JSON)
				continue;

			b = VSL_CDATA(t->c->rec.ptr);
			e = b + VSL_LEN(t->c->rec.ptr);
			if (!VSL_tagflags[tag]) {
				while (e > b && e[-1] == '\0')
					e--;
			}

			switch (tag) {
			case SLT_HttpGarbage:
				skip = 1;
				break;
			case SLT_PipeAcct:
				frag_fields(0, b, e,
				    3, &CTX.frag[F_I],
				    4, &CTX.frag[F_O],
				    0, NULL);
				break;
			case SLT_ConnectAcct:
				frag_fields(0, b, e,
				    2, &CTX.frag[F_I],
				    3, &CTX.frag[F_O],
				    0, NULL);
				break;
			case SLT_BackendOpen:
				frag_fields(1, b, e,
				    3, &CTX.frag[F_h],
				    0, NULL);
				break;
			case SLT_ReqStart:
				frag_fields(0, b, e,
				    1, &CTX.frag[F_h],
				    0, NULL);
				break;
			case SLT_BereqMethod:
			case SLT_ReqMethod:
				frag_line(0, b, e, &CTX.frag[F_m]);
				break;
			case SLT_BereqURL:
			case SLT_ReqURL:
				p = memchr(b, '?', e - b);
				if (p == NULL)
					p = e;
				frag_line(0, b, p, &CTX.frag[F_U]);
				frag_line(0, p, e, &CTX.frag[F_q]);
				break;
			case SLT_BereqProtocol:
			case SLT_ReqProtocol:
				frag_line(0, b, e, &CTX.frag[F_H]);
				break;
			case SLT_BerespStatus:
			case SLT_RespStatus:
				frag_line(1, b, e, &CTX.frag[F_s]);
				break;
			case SLT_BereqAcct:
			case SLT_ReqAcct:
				frag_fields(0, b, e,
				    3, &CTX.frag[F_I],
				    5, &CTX.frag[F_b],
				    6, &CTX.frag[F_O],
				    0, NULL);
				break;
			case SLT_Timestamp:
#define ISPREFIX(a, b, c, d)	isprefix(a, strlen(a), b, c, d)
				if (ISPREFIX("Start:", b, e, &p)) {
					frag_fields(0, p, e, 1,
					    &CTX.frag[F_tstart], 0, NULL);

				} else if (ISPREFIX("Resp:", b, e, &p) ||
				    ISPREFIX("PipeSess:", b, e, &p) ||
				    ISPREFIX("ConnectSess:", b, e, &p) ||
				    ISPREFIX("BerespBody:", b, e, &p)) {
					frag_fields(0, p, e, 1,
					    &CTX.frag[F_tend], 0, NULL);

				} else if (ISPREFIX("Process:", b, e, &p) ||
				    ISPREFIX("Pipe:", b, e, &p) ||
				    ISPREFIX("Connect:", b, e, &p) ||
				    ISPREFIX("Beresp:", b, e, &p)) {
					frag_fields(0, p, e, 2,
					    &CTX.frag[F_ttfb], 0, NULL);
				}
				break;
			case SLT_BereqHeader:
			case SLT_ReqHeader:
				process_hdr(&CTX.watch_reqhdr, b, e);
				if (ISPREFIX("Authorization:", b, e, &p) &&
				    ISPREFIX("basic ", p, e, &p))
					frag_line(0, p, e,
					    &CTX.frag[F_auth]);
				else if (ISPREFIX("Host:", b, e, &p))
					frag_line(0, p, e,
					    &CTX.frag[F_host]);
#undef ISPREFIX
				break;
			case SLT_BerespHeader:
			case SLT_RespHeader:
				process_hdr(&CTX.watch_resphdr, b, e);
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
			case SLT_VCL_Log:
				VTAILQ_FOREACH(w, &CTX.watch_vcl_log, list) {
					CHECK_OBJ_NOTNULL(w, WATCH_MAGIC);
					if (e - b < w->keylen ||
					    strncmp(b, w->key, w->keylen))
						continue;
					p = b + w->keylen;
					frag_line(0, p, e, &w->frag);
				}
				break;
			default:
				break;
			}

			process_vsl(&CTX.watch_vsl, tag, b, e);
		}
		if (skip)
			continue;
		i = print();
		if (i)
			return (i);
	}

	return (0);
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
		VUT_Error(vut, 1, "Can't open format file (%s)",
		    strerror(errno));
	AN(fmtfile);
	fmtlen = getline(&fmt, &len, fmtfile);
	if (fmtlen == -1) {
		free(fmt);
		if (feof(fmtfile))
			VUT_Error(vut, 1, "Empty format file");
		VUT_Error(vut, 1, "Can't read format from file (%s)",
		    strerror(errno));
	}
	AZ(fclose(fmtfile));
	if (fmt[fmtlen - 1] == '\n')
		fmt[fmtlen - 1] = '\0';
	return (fmt);
}

int
main(int argc, char * const *argv)
{
	signed char opt;
	char *format = NULL;
	int mode_opt = 0;

	vut = VUT_InitProg(argc, argv, &vopt_spec);
	AN(vut);
	memset(&CTX, 0, sizeof CTX);
	VTAILQ_INIT(&CTX.format);
	VTAILQ_INIT(&CTX.watch_vcl_log);
	VTAILQ_INIT(&CTX.watch_reqhdr);
	VTAILQ_INIT(&CTX.watch_resphdr);
	VTAILQ_INIT(&CTX.watch_vsl);
	CTX.vsb = VSB_new_auto();
	AN(CTX.vsb);
	CTX.quote_how = VSB_QUOTE_ESCHEX;
	REPLACE(CTX.missing_string, "-");
	REPLACE(CTX.missing_int, "-");

	tzset();		// We use localtime_r(3)

	while ((opt = getopt(argc, argv, vopt_spec.vopt_optstring)) != -1) {
		switch (opt) {
		case 'a':
			/* Append to file */
			CTX.a_opt = 1;
			break;
		case 'b': /* backend mode */
		case 'c': /* client mode */
		case 'E': /* show ESI */
			AN(VUT_Arg(vut, opt, NULL));
			mode_opt = 1;
			break;
		case 'F':
			if (format != NULL)
				VUT_Error(vut, 1, "Format already set");
			REPLACE(format, optarg);
			break;
		case 'f':
			if (format != NULL)
				VUT_Error(vut, 1, "Format already set");
			/* Format string from file */
			format = read_format(optarg);
			AN(format);
			break;
		case 'h':
			/* Usage help */
			VUT_Usage(vut, &vopt_spec, 0);
			break;
		case 'j':
			REPLACE(CTX.missing_string, "");
			REPLACE(CTX.missing_int, "0");
			CTX.quote_how = VSB_QUOTE_JSON;
			break;
		case 'w':
			/* Write to file */
			REPLACE(CTX.w_arg, optarg);
			break;
		default:
			if (!VUT_Arg(vut, opt, optarg))
				VUT_Usage(vut, &vopt_spec, 1);
		}
	}

	/* default is client mode: */
	if (!mode_opt)
		AN(VUT_Arg(vut, 'c', NULL));

	if (optind != argc)
		VUT_Usage(vut, &vopt_spec, 1);

	if (vut->D_opt && !CTX.w_arg)
		VUT_Error(vut, 1, "Missing -w option");

	if (vut->D_opt && !strcmp(CTX.w_arg, "-"))
		VUT_Error(vut, 1, "Daemon cannot write to stdout");

	/* Check for valid grouping mode */
	assert(vut->g_arg < VSL_g__MAX);
	if (vut->g_arg != VSL_g_vxid && vut->g_arg != VSL_g_request)
		VUT_Error(vut, 1, "Invalid grouping mode: %s",
		    VSLQ_grouping[vut->g_arg]);

	/* Prepare output format */
	parse_format(format);
	REPLACE(format, NULL);

	/* Setup output */
	vut->dispatch_f = dispatch_f;
	vut->dispatch_priv = NULL;
	if (CTX.w_arg) {
		openout(CTX.a_opt);
		AN(CTX.fo);
		if (vut->D_opt)
			vut->sighup_f = rotateout;
	} else
		CTX.fo = stdout;
	vut->idle_f = flushout;

	VUT_Setup(vut);
	(void)VUT_Main(vut);
	VUT_Fini(&vut);

	exit(0);
}
