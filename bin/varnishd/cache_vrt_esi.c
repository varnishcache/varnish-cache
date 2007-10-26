/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
 * All rights reserved.
 *
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
 * $Id: cache_vrt.c 2067 2007-09-30 20:57:30Z phk $
 *
 * Runtime support for compiled VCL programs ESI processing.
 *
 * The basic ESI 1.0 is a very simple specification:
 *	http://www.w3.org/TR/esi-lang
 * But it seems that Oracle and Akamai has embrodiered it to be almost a new
 * layer of scripting language in HTTP transmission chains.
 *
 * It is not obvious how much help the "advanced" features of ESI really
 * are to users, so our aim is to pick the fruit starting with the lowest
 * hanging, esi:include
 */

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "shmlog.h"
#include "heritage.h"
#include "vrt.h"
#include "vcl.h"
#include "cache.h"

#define NDEFELEM		10

/*--------------------------------------------------------------------*/

struct esi_bit {
	VTAILQ_ENTRY(esi_bit)	list;
	char			chunk_length[20];
	txt			verbatim;
	txt			host;
	txt			include;
	int			free_this;
};

VTAILQ_HEAD(esibithead, esi_bit);

struct esi_work {
	struct sess		*sp;
	size_t			off;
	struct storage		*st;
	txt			dst;
	struct esi_bit		*eb;
	struct esi_bit		*ebl;	/* list of */
	int			neb;
};

/*--------------------------------------------------------------------
 * Report a parsing error
 */

static void
esi_error(const struct esi_work *ew, const char *p, int i, const char *err)
{
	int ellipsis = 0;
	char buf[256], *q;
	txt t;

	if (i == 0) 
		i = p - ((char *)ew->st->ptr + ew->st->len);
	if (i > 20) {
		i = 20;
		ellipsis = 1;
	}
	q = buf;
	q += sprintf(buf, "at %zd: %s \"",
	    ew->off + (p - (char*)ew->st->ptr), err);
	while (i > 0) {
		if (*p >= ' ' && *p <= '~') {
			*q++ = *p;
		} else if (*p == '\n') {
			*q++ = '\\';
			*q++ = 'n';
		} else if (*p == '\r') {
			*q++ = '\\';
			*q++ = 'r';
		} else if (*p == '\t') {
			*q++ = '\\';
			*q++ = 't';
		} else {
			/* XXX: use %%%02x instead ? */
			q += sprintf(q, "\\x%02x", *p);
		}
		p++;
		i--;
	}
	if (ellipsis) {
		*q++ = '[';
		*q++ = '.';
		*q++ = '.';
		*q++ = '.';
		*q++ = ']';
	}
	*q++ = '"';
	*q++ = '\0';
	t.b = buf;
	t.e = q;
	WSPR(ew->sp, SLT_ESI_xmlerror, t);
}

/*--------------------------------------------------------------------
 * Add ESI bit to object
 */

static struct esi_bit *
esi_addbit(struct esi_work *ew)
{

	if (ew->neb == 0) {
		ew->ebl = calloc(NDEFELEM, sizeof(struct esi_bit));
		XXXAN(ew->ebl);
		ew->neb = NDEFELEM;
		ew->ebl->free_this = 1;
	}
	ew->eb = ew->ebl;
	ew->ebl++;
	ew->neb--;


	VTAILQ_INSERT_TAIL(&ew->sp->obj->esibits, ew->eb, list);
	ew->eb->verbatim = ew->dst;
	sprintf(ew->eb->chunk_length, "%x\r\n", Tlen(ew->dst));
	VSL(SLT_Debug, ew->sp->fd, "AddBit: %.*s", Tlen(ew->dst), ew->dst.b);
	return(ew->eb);
}


/*--------------------------------------------------------------------
 * Add verbatim piece to output
 */

static void
esi_addverbatim(struct esi_work *ew, txt t)
{

	if (t.b != ew->dst.e)
		memmove(ew->dst.e, t.b, Tlen(t));
	ew->dst.e += Tlen(t);
}

/*--------------------------------------------------------------------
 * Tease the next attribute and value out of an XML element.
 *
 * XXX: is the syntax correct ?
 */

static int
esi_attrib(const struct esi_work *ew, txt *in, txt *attrib, txt *val)
{

	/* Skip leading blanks */
	while(in->b < in->e && isspace(*in->b))
		in->b++;

	/* Nothing found */
	if (in->b >= in->e)
		return (0);

	if (!isalpha(*in->b)) {
		/* XXX error */
		esi_error(ew, in->b, 1, "XML 1.0 Illegal attribute character");
		return (-1);
	}

	/* Attribute name until '=' or space */
	*attrib = *in;
	while(in->b < in->e && *in->b != '=' && !isspace(*in->b)) {
		if (!isalnum(*in->b)) {
			esi_error(ew, attrib->b, 1 + (in->b - attrib->b),
			    "XML 1.0 Illegal attribute character");
			return (-1);
		}
		in->b++;
	}
	attrib->e = in->b;

	if (in->b >= in->e || isspace(*in->b)) {
		/* Attribute without value */
		val->b = val->e = in->b;
		return (1);
	}

	/* skip '=' */
	in->b++;

	/* Value, if any ? */
	*val = *in;
	if (in->b >= in->e) 
		return (1);

	if (*in->b == '"') {
		/* Skip quote */
		in->b++;
		val->b++;

		/* Anything goes, until next quote */
		while(in->b < in->e && *in->b != '"')
			in->b++;
		val->e = in->b;

		if (in->b >= in->e) {
			esi_error(ew, val->b, in->e - val->b,
			    "XML 1.0 missing ending quote");
			return (-1);
		}

		/* Skip quote */
		in->b++;
	} else {
		/* Anything until whitespace */
		while(in->b < in->e && !isspace(*in->b))
			in->b++;
		val->e = in->b;
	}
	*val->e = '\0';
	return (1);
}

/*--------------------------------------------------------------------
 * Add one piece to the output, either verbatim or include
 */

static void
esi_addinclude(struct esi_work *ew, txt t)
{
	struct esi_bit *eb;
	char *p, *q;
	txt tag;
	txt val;

	VSL(SLT_Debug, 0, "Incl \"%.*s\"", t.e - t.b, t.b);
	eb = esi_addbit(ew);
	while (esi_attrib(ew, &t, &tag, &val) == 1) {
		VSL(SLT_Debug, 0, "<%.*s> -> <%.*s>",
		    tag.e - tag.b, tag.b,
		    val.e - val.b, val.b);
		if (Tlen(tag) != 3 && memcmp(tag.b, "src", 3))
			continue;

		assert(Tlen(val) > 0);	/* XXX */

		if (Tlen(val) > 7 && !memcmp(val.b, "http://", 7)) {
			/*  Rewrite to Host: header inplace */
			eb->host.b = val.b;
			memcpy(eb->host.b, "Host: ", 6);
			q = eb->host.b + 6;
			for (p = eb->host.b + 7; p < val.e && *p != '/'; p++)
				*q++ = *p;
			*q++ = '\0';
			eb->host.e = q;
			assert(*p == '/');	/* XXX */
			/* The rest is the URL */
			eb->include.b = p;
			eb->include.e = val.e;
		} else if (Tlen(val) > 0 && *val.b == '/') {
			/* Absolute on this host */
			eb->include = val;
		} else {
			/* Relative to current URL */
			/* XXX: search forward to '?' use previous / */
			/* XXX: where to store edited result ? */
			eb->include = val;
			INCOMPL();
		}
	}
}

/*--------------------------------------------------------------------
 * Zoom over a piece of object and dike out all releveant esi: pieces.
 * The entire txt may not be processed because an interesting part 
 * could possibly span into the next chunk of storage.
 * Return value: number of bytes processed.
 */

static int
esi_parse(struct esi_work *ew)
{
	char *p, *q, *r;
	txt t, o;
	int celem;		/* closing element */
	int remflg;		/* inside <esi:remove> </esi:remove> */
	int incmt;		/* inside <!--esi ... --> comment */
	int i;

	t.b = (char *)ew->st->ptr;
	t.e = t.b + ew->st->len;
	ew->dst.b = t.b;
	ew->dst.e = t.b;
	remflg = 0;
	incmt = 0;
	o.b = t.b;
	for (p = t.b; p < t.e; ) {
		if (incmt && *p == '-') {
			/*
			 * We are inside an <!--esi comment and need to zap
			 * the end comment marker --> when we see it.
			 */
			if (p + 2 >= t.e) {
				/* XXX: need to return pending incmt  */
				return (p - t.b);
			}
			if (!memcmp(p, "-->", 3)) {
				incmt = 0;
				o.e = p;
				esi_addverbatim(ew, o);
				p += 3;
				o.b = p;
			} else
				p++;
			continue;
		}

		if (*p != '<') {
			/* nothing happens until next element or comment */
			p++;
			continue;
		}

		i = t.e - p;

		if (i < 2)
			return (p - t.b);

		if (remflg == 0 && !memcmp(p, "<!--esi", i > 7 ? 7 : i)) {
			/*
			 * ESI comment. <!--esi...-->
			 * at least 10 char, but we only test on the
			 * first seven because the tail is handled
			 * by the incmt flag.
			 */
			if (i < 7)
				return (p - t.b);

			o.e = p;
			esi_addverbatim(ew, o);

			p += 7;
			o.b = p;
			incmt = 1;
			continue;
		}

		if (!memcmp(p, "<!--", i > 4 ? 4 : i)) {
			/*
			 * plain comment <!--...--> at least 7 char
			 */
			if (i < 7)
				return (p - t.b);
			for (q = p + 4; ; q++) {
				if (q + 2 >= t.e)
					return (p - t.b);
				if (!memcmp(q, "-->", 3))
					break;
			}
			p = q + 3;
			continue;
		}

		if (!memcmp(p, "<![CDATA[", i > 9 ? 9 : i)) {
			/*
			 * cdata <![CDATA[...]]> at least 12 char
			 */
			if (i < 12)
				return (p - t.b);
			for (q = p + 9; ; q++) {
				if (q + 2 >= t.e)
					return (p - t.b);
				if (!memcmp(q, "]]>", 3))
					break;
			}
			p = q + 3;
			continue;
		} 

		if (p[1] == '!') {
			/*
			 * Unrecognized <! sequence, ignore
			 */
			esi_error(ew, p, i,
			    "XML 1.0 Unknown <! sequence");
			p += 2;
			continue;
		}

		/* Find end of this element */
		for (q = p + 1; q < t.e && *q != '>'; q++)
			continue;
		if (*q != '>')
			return (p - t.b);

		/* Opening/empty or closing element ? */
		if (p[1] == '/') {
			celem = 1;
			r = p + 2;
			if (q[-1] == '/') {
				esi_error(ew, p, 1 + q - p,
				    "XML 1.0 empty and closing element");
			}
		} else {
			celem = 0;
			r = p + 1;
		}

		if (r + 9 < q && !memcmp(r, "esi:remove", 10)) {

			if (celem != remflg) {
				/*
				 * ESI 1.0 violation, ignore element
				 */
				esi_error(ew, p, 1 + q - p,
				    remflg ? "ESI 1.0 forbids nested esi:remove"
				    : "ESI 1.0 esi:remove not opened");
					
				if (!remflg) {
					o.e = p;
					esi_addverbatim(ew, o);
				}
			} else if (!celem && q[-1] == '/') {
				/* empty element */
				o.e = p;
				esi_addverbatim(ew, o);
			} else if (!celem) {
				/* open element */
				o.e = p;
				esi_addverbatim(ew, o);
				remflg = !celem;
			} else {
				/* close element */
				remflg = !celem;
			}
			p = q + 1;
			o.b = p;
			continue;
		}

		if (remflg && r + 3 < q && !memcmp(r, "esi:", 4)) {
			/*
			 * ESI 1.0 violation, no esi: elements in esi:remove
			 */
			esi_error(ew, p, 1 + q - p,
			    "ESI 1.0 forbids esi: elements inside esi:remove");
			p = q + 1;
			continue;
		}

		if (r + 10 < q && !memcmp(r, "esi:include", 11)) {
			
			o.e = p;
			esi_addverbatim(ew, o);

			if (celem == 0) {
				o.b = r + 11;
				if (q[-1] != '/') {
					esi_error(ew, p, 1 + q - p,
					    "ESI 1.0 wants emtpy esi:include");
					o.e = q;
				} else {
					o.e = q - 1;
				}
				esi_addinclude(ew, o);
				ew->dst.b = q + 1;
				ew->dst.e = q + 1;
			} else {
				esi_error(ew, p, 1 + q - p,
				    "ESI 1.0 closing esi:include illegal");
			}
			p = q + 1;
			o.b = p;
			continue;
		}

		if (r + 3 < q && !memcmp(r, "esi:", 4)) {
			/*
			 * Unimplemented ESI element, ignore
			 */
			esi_error(ew, p, 1 + q - p,
			    "ESI 1.0 unimplemented element");
			o.e = p;
			esi_addverbatim(ew, o);
			p = q + 1;
			o.b = p;
			continue;
		}

		/* Not an element we care about */
		p = q + 1;
	}
	o.e = p;
	esi_addverbatim(ew, o);
	return (p - t.b);
}

/*--------------------------------------------------------------------*/

void
VRT_ESI(struct sess *sp)
{
	struct storage *st;
	struct esi_work *ew, eww[1];
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);

	/* XXX: only if GET ? */
	ew = eww;
	memset(eww, 0, sizeof eww);
	ew->sp = sp;
	ew->off = 1;

	VTAILQ_FOREACH(st, &sp->obj->store, list) {
		ew->st = st;
		i = esi_parse(ew);
		ew->off += st->len;
		if (i < st->len) {
			/* XXX: Handle complications */
			if (VTAILQ_NEXT(st, list))
				INCOMPL();
		}
		i = Tlen(ew->dst);
	}
	if (Tlen(ew->dst))
		esi_addbit(ew);

	/*
	 * Our ESI implementation needs chunked encoding
	 * XXX: We should only do this if we find any ESI directives
	 */
	http_Unset(sp->obj->http, H_Content_Length);
	http_PrintfHeader(sp->wrk, sp->fd, sp->obj->http,
	    "Transfer-Encoding: chunked");

}

/*--------------------------------------------------------------------*/

void
ESI_Deliver(struct sess *sp)
{

	struct esi_bit *eb;
	struct object *obj;

	VTAILQ_FOREACH(eb, &sp->obj->esibits, list) {
		WRK_Write(sp->wrk, eb->chunk_length, -1);
		WRK_Write(sp->wrk, eb->verbatim.b, Tlen(eb->verbatim));
		WRK_Write(sp->wrk, "\r\n", -1);
		if (eb->include.b == NULL ||
		    sp->esis >= params->max_esi_includes)
			continue;

		/*
		 * We flush here, because the next transaction is
		 * quite likely to take some time, so we should get
		 * as many bits to the client as we can already
		 */
		WRK_Flush(sp->wrk);

		sp->esis++;
		obj = sp->obj;
		sp->obj = NULL;
		*sp->http = *sp->http0;
		/* XXX: reset sp->ws */
		http_SetH(sp->http, HTTP_HDR_URL, eb->include.b);
		if (eb->host.b != NULL)  {
			http_Unset(sp->http, H_Host);
			http_SetHeader(sp->wrk, sp->fd, sp->http, eb->host.b);
		}
		sp->step = STP_RECV;
		CNT_Session(sp);
		sp->esis--;
		sp->obj = obj;

	}
	WRK_Write(sp->wrk, "0\r\n", -1);
}

/*--------------------------------------------------------------------*/

void
ESI_Destroy(struct object *o)
{
	struct esi_bit *eb;

	/*
	 * Delete esi_bits from behind and free(3) the ones that want to be.
	 */
	while (!VTAILQ_EMPTY(&o->esibits)) {
		eb = VTAILQ_LAST(&o->esibits, esibithead);
		VTAILQ_REMOVE(&o->esibits, eb, list);
		if (eb->free_this)
			free(eb);
	}
}

