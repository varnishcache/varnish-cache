/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
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

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "shmlog.h"
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
	txt			t;
	txt			o;
	txt			dst;
	struct esi_bit		*eb;
	struct esi_bit		*ebl;	/* list of */
	int			neb;
	int			is_esi;
	int			remflg;	/* inside <esi:remove> </esi:remove> */
	int			incmt;	/* inside <!--esi ... --> comment */
	int			incdata; /* inside <![CCDATA[ ... ]]> */
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
		i = p - ew->t.b;
	if (i > 20) {
		i = 20;
		ellipsis = 1;
	}
	q = buf;
	q += sprintf(buf, "at %zd: %s \"", ew->off + (p - ew->t.b), err);
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
			q += sprintf(q, "\\x%02x", *p & 0xff);
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
	VSL(SLT_Debug, ew->sp->fd, "AddBit: %d <%.*s>",
	    Tlen(ew->dst), Tlen(ew->dst), ew->dst.b);
	return(ew->eb);
}


/*--------------------------------------------------------------------
 * Add verbatim piece to output
 */

static void
esi_addverbatim(struct esi_work *ew)
{

	VSL(SLT_Debug, ew->sp->fd, "AddVer: %d <%.*s>",
	    Tlen(ew->o), Tlen(ew->o), ew->o.b);
	if (ew->o.b != ew->dst.e)
		memmove(ew->dst.e, ew->o.b, Tlen(ew->o));
	ew->dst.e += Tlen(ew->o);
}

/*--------------------------------------------------------------------
 * Tease the next attribute and value out of an XML element.
 *
 * XXX: is the syntax correct ?
 */

static int
esi_attrib(const struct esi_work *ew, txt *in, txt *attrib, txt *val)
{

	AN(*in->b);
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

	if (isspace(*in->b)) {
		val->e = val->b = in->b;;
		*val->e = '\0';
		in->b++;
		return (1);
	}

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
		in->b++;
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
	unsigned u, v;
	struct ws *ws;

	VSL(SLT_Debug, 0, "Incl \"%.*s\"", t.e - t.b, t.b);
	eb = esi_addbit(ew);
	while (esi_attrib(ew, &t, &tag, &val) == 1) {
		VSL(SLT_Debug, 0, "<%.*s> -> <%.*s>",
		    tag.e - tag.b, tag.b,
		    val.e - val.b, val.b);
		if (Tlen(tag) != 3 || memcmp(tag.b, "src", 3))
			continue;
		if (Tlen(val) == 0) {
			esi_error(ew, tag.b, Tlen(tag),
			    "ESI esi:include src attribute withou value");
			continue;
		}

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

			/*
			 * Decision:  We interpret the relative URL against
			 * the actual URL we asked the backend for.
			 * The client's request URL may be entirely
			 * different and have been rewritten underway.
			 */
			CHECK_OBJ_NOTNULL(ew->sp, SESS_MAGIC);
			CHECK_OBJ_NOTNULL(ew->sp->bereq, BEREQ_MAGIC);
			CHECK_OBJ_NOTNULL(ew->sp->bereq->http, HTTP_MAGIC);
			tag = ew->sp->bereq->http->hd[HTTP_HDR_URL];

			/* Use the objects WS to store the result */
			CHECK_OBJ_NOTNULL(ew->sp->obj, OBJECT_MAGIC);
			ws = ew->sp->obj->ws_o;
			WS_Assert(ws);

			/* Look for the last '/' before a '?' */
			q = NULL;
			for (p = tag.b; p < tag.e && *p != '?'; p++)
				if (*p == '/')
					q = p;
			if (q != NULL)
				tag.e = q + 1;

			u = WS_Reserve(ws, 0);
			v = snprintf(ws->f, u - 1, "%.*s%.*s",
			    pdiff(tag.b, tag.e), tag.b,
			    pdiff(val.b, val.e), val.b);
			v++;
			xxxassert(v < u);
			eb->include.b = ws->f;
			eb->include.e = ws->f + v;
			WS_Release(ws, v);
		}
	}
}

/*--------------------------------------------------------------------
 * Zoom over a piece of object and dike out all releveant esi: pieces.
 * The entire txt may not be processed because an interesting part
 * could possibly span into the next chunk of storage.
 * Return value: number of bytes processed.
 */

static char *
esi_parse2(struct esi_work *ew)
{
	char *p, *q, *r;
	txt t;
	int celem;		/* closing element */
	int i;

	t = ew->t;
	ew->dst.b = t.b;
	ew->dst.e = t.b;
	ew->o.b = t.b;
	ew->o.e = t.b;
	for (p = t.b; p < t.e; ) {
		assert(p >= t.b);
		assert(p < t.e);
		if (ew->incdata) {
			/*
			 * We are inside an <![CDATA[ constuct and mus skip
			 * to the end marker ]]>.
			 */
			if (*p != ']') {
				p++;
			} else {
				if (p + 2 >= t.e)
					return (p);
				if (!memcmp(p, "]]>", 3)) {
					ew->incdata = 0;
					p += 3;
				} else
					p++;
			}
			continue;
		}
		if (ew->incmt && *p == '-') {
			/*
			 * We are inside an <!--esi comment and need to zap
			 * the end comment marker --> when we see it.
			 */
			if (p + 2 >= t.e)
				return (p);
			if (!memcmp(p, "-->", 3)) {
				ew->incmt = 0;
				ew->o.e = p;
				esi_addverbatim(ew);
				p += 3;
				ew->o.b = p;
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
			return (p);

		if (ew->remflg == 0 && !memcmp(p, "<!--esi", i > 7 ? 7 : i)) {
			/*
			 * ESI comment. <!--esi...-->
			 * at least 10 char, but we only test on the
			 * first seven because the tail is handled
			 * by the ew->incmt flag.
			 */
			ew->is_esi++;
			if (i < 7)
				return (p);

			ew->o.e = p;
			esi_addverbatim(ew);

			p += 7;
			ew->o.b = p;
			ew->incmt = 1;
			continue;
		}

		if (!memcmp(p, "<!--", i > 4 ? 4 : i)) {
			/*
			 * plain comment <!--...--> at least 7 char
			 */
			if (i < 7)
				return (p);
			for (q = p + 4; ; q++) {
				if (q + 2 >= t.e)
					return (p);
				if (!memcmp(q, "-->", 3))
					break;
			}
			p = q + 3;
			continue;
		}

		if (!memcmp(p, "<![CDATA[", i > 9 ? 9 : i)) {
			/*
			 * cdata <![CDATA[ at least 9 char
			 */
			if (i < 9)
				return (p);
			ew->incdata = 1;
			p += 9;
			continue;
		}

		/* Ignore non esi elements, if so instructed */
		if ((params->esi_syntax & 0x02)) {
			if (memcmp(p, "<esi:", i > 5 ? 5 : i) &&
			    memcmp(p, "</esi:", i > 6 ? 6 : i)) {
				p += 1;
				continue;
			}
			if (i < 6)
				return (p);
		}

		/* Find end of this element */
		for (q = p + 1; q < t.e && *q != '>'; q++)
			continue;
		if (q >= t.e || *q != '>')
			return (p);

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

		VSL(SLT_Debug, ew->sp->fd, "Element: clos=%d [%.*s]",
		    celem, q - r, r);

		if (r + 9 < q && !memcmp(r, "esi:remove", 10)) {

			ew->is_esi++;

			if (celem != ew->remflg) {
				/*
				 * ESI 1.0 violation, ignore element
				 */
				esi_error(ew, p, 1 + q - p, ew->remflg ?
				    "ESI 1.0 forbids nested esi:remove"
				    : "ESI 1.0 esi:remove not opened");

				if (!ew->remflg) {
					ew->o.e = p;
					esi_addverbatim(ew);
				}
			} else if (!celem && q[-1] == '/') {
				/* empty element */
				ew->o.e = p;
				esi_addverbatim(ew);
			} else if (!celem) {
				/* open element */
				ew->o.e = p;
				esi_addverbatim(ew);
				ew->remflg = !celem;
			} else {
				/* close element */
				ew->remflg = !celem;
			}
			p = q + 1;
			ew->o.b = p;
			continue;
		}

		if (ew->remflg && r + 3 < q && !memcmp(r, "esi:", 4)) {
			/*
			 * ESI 1.0 violation, no esi: elements in esi:remove
			 */
			esi_error(ew, p, 1 + q - p,
			    "ESI 1.0 forbids esi: elements inside esi:remove");
			p = q + 1;
			continue;
		}
		ew->is_esi++;

		if (r + 10 < q && !memcmp(r, "esi:comment", 11)) {

			ew->o.e = p;
			esi_addverbatim(ew);

			if (celem == 1) {
				esi_error(ew, p, 1 + q - p,
				    "ESI 1.0 closing esi:comment illegal");
			} else if (q[-1] != '/') {
				esi_error(ew, p, 1 + q - p,
				    "ESI 1.0 wants empty esi:comment");
			}
			p = q + 1;
			ew->o.b = p;
			continue;
		}
		if (r + 10 < q && !memcmp(r, "esi:include", 11)) {

			ew->o.e = p;
			esi_addverbatim(ew);

			if (celem == 0) {
				ew->o.b = r + 11;
				if (q[-1] != '/') {
					esi_error(ew, p, 1 + q - p,
					    "ESI 1.0 wants empty esi:include");
					ew->o.e = q;
				} else {
					ew->o.e = q - 1;
				}
				esi_addinclude(ew, ew->o);
				ew->dst.b = q + 1;
				ew->dst.e = q + 1;
			} else {
				esi_error(ew, p, 1 + q - p,
				    "ESI 1.0 closing esi:include illegal");
			}
			p = q + 1;
			ew->o.b = p;
			continue;
		}

		if (r + 3 < q && !memcmp(r, "esi:", 4)) {
			/*
			 * Unimplemented ESI element, ignore
			 */
			esi_error(ew, p, 1 + q - p,
			    "ESI 1.0 unimplemented element");
			ew->o.e = p;
			esi_addverbatim(ew);
			p = q + 1;
			ew->o.b = p;
			continue;
		}

		/* Not an element we care about */
		assert(q < t.e);
		p = q + 1;
	}
	assert(p == t.e);
	return (p);
}

static char *
esi_parse(struct esi_work *ew)
{
	char *p;

	VSL(SLT_Debug, ew->sp->fd, "Parse: %d <%.*s>",
	    Tlen(ew->t), Tlen(ew->t), ew->t.b);
	p = esi_parse2(ew);
	assert(ew->o.b >= ew->t.b);
	assert(ew->o.e <= ew->t.e);
	ew->o.e = p;
	if (Tlen(ew->o) && !ew->remflg)
		esi_addverbatim(ew);
	if (Tlen(ew->dst))
		esi_addbit(ew);
	ew->off += (p - ew->t.b);
	return (p);
}

/*--------------------------------------------------------------------*/

void
VRT_ESI(struct sess *sp)
{
	struct storage *st, *st2;
	struct esi_work *ew, eww[1];
	txt t;
	unsigned u;
	char *p, *q;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(sp->obj->busy);
	if (sp->cur_method != VCL_MET_FETCH) {
		/* XXX: we should catch this at compile time */
		WSP(sp, SLT_VCL_error,
		    "esi can only be called from vcl_fetch");
		return;
	}

	if (VTAILQ_EMPTY(&sp->obj->store))
		return;

	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);

	if (!(params->esi_syntax & 0x00000001)) {
		/*
		 * By default, we will not ESI process an object where
		 *  the first non-space character is different from '<'
		 */
		st = VTAILQ_FIRST(&sp->obj->store);
		AN(st);
		for (u = 0; u < st->len; u++) {
			if (isspace(st->ptr[u]))
				continue;
			if (st->ptr[u] == '<')
				break;
			WSP(sp, SLT_ESI_xmlerror,
			    "No ESI processing, "
			    "binary object: 0x%02x at pos %u.",
			    st->ptr[u], u);
			return;
		}
	}

	/* XXX: only if GET ? */
	ew = eww;
	memset(eww, 0, sizeof eww);
	ew->sp = sp;
	ew->off = 1;

	p = NULL;
	VTAILQ_FOREACH(st, &sp->obj->store, list) {
		if (p != NULL) {
			assert ((void*)p > (void *)st->ptr);
			assert ((void*)p <= (void *)(st->ptr + st->len));
			if (p == (void*)(st->ptr + st->len))
				break;
			ew->t.b = p;
			p = NULL;
		} else
			ew->t.b = (void *)st->ptr;
		ew->t.e = (void *)(st->ptr + st->len);
		p = esi_parse(ew);
		if (p == ew->t.e) {
			p = NULL;
			continue;
		}

		if (VTAILQ_NEXT(st, list) == NULL) {
			/*
			 * XXX: illegal XML input, but did we handle
			 * XXX: all of it, or do we leave the final
			 * XXX: element dangling ?
			 */
			esi_error(ew, p, ew->t.e -p,
			    "XML 1.0 incomplete language element");
			ew->dst.b = p;
			ew->dst.e = ew->t.e;
			esi_addbit(ew);
			break;
		}

		/* Move remainder to workspace */
		u = ew->t.e - p;
		t.b = sp->obj->ws_o->f;
		t.e = t.b + WS_Reserve(sp->obj->ws_o, 0);
		if (t.b + u >= t.e) {
			esi_error(ew, p, ew->t.e - p,
			    "XML 1.0 unreasonably long element");
			WS_Release(sp->obj->ws_o, 0);
			ew->dst.b = p;
			ew->dst.e = ew->t.e;
			esi_addbit(ew);
			p = NULL;
			continue;
		}
		assert(t.e > t.b + u);	/* XXX incredibly long element ? */
		memcpy(t.b, p, u);

		/* Peel start off next chunk, until and including '<' */
		st2 = VTAILQ_NEXT(st, list);
		AN(st2);
		q = t.b + u;
		p = (void*)st2->ptr;
		while (1) {
			if (p >= (char *)st2->ptr + st2->len || q >= t.e) {
				esi_error(ew, t.b, q - t.b,
				    "XML 1.0 unreasonably long element");
				WS_Release(sp->obj->ws_o, 0);
				ew->dst.b = t.b;
				ew->dst.e = q;
				esi_addbit(ew);
				p = NULL;
				break;
			}
			*q = *p++;
			if (*q++ == '>')
				break;
		}
		if (p == NULL)
			continue;
		WS_ReleaseP(sp->obj->ws_o, q);
		t.e = q;

		/* Parse this bit */
		ew->t = t;
		q = esi_parse(ew);
		assert(q == ew->t.e);	/* XXX */

		/* 'p' is cached starting point for next storage part */
	}

	/*
	 * XXX: we could record the starting point of these elements
	 * XXX: so that the char-index were more useful, but we are
	 * XXX: not trivially able to print their contents, so leave
	 * XXX: it like this for now, pending more thought about the
	 * XXX: proper way to report these errors.
	 */
	if (ew->incdata)
		esi_error(ew, ew->t.e, -1,
		    "ESI 1.0 unterminated <![CDATA[ element");
	if (ew->remflg)
		esi_error(ew, ew->t.e, -1,
		    "ESI 1.0 unterminated <esi:remove> element");
	if (ew->incmt)
		esi_error(ew, ew->t.e, -1,
		    "ESI 1.0 unterminated <!--esi comment");

	if (!ew->is_esi) {
		ESI_Destroy(sp->obj);
		return;
	}

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
		assert(sp->wrk->wfd = &sp->fd);
		if (Tlen(eb->verbatim)) {
			if (sp->http->protover >= 1.1)
				(void)WRK_Write(sp->wrk, eb->chunk_length, -1);
			sp->wrk->acct.bodybytes += WRK_Write(sp->wrk,
			    eb->verbatim.b, Tlen(eb->verbatim));
			if (sp->http->protover >= 1.1)
				(void)WRK_Write(sp->wrk, "\r\n", -1);
		}
		if (eb->include.b == NULL ||
		    sp->esis >= params->max_esi_includes)
			continue;

		/*
		 * We flush here, because the next transaction is
		 * quite likely to take some time, so we should get
		 * as many bits to the client as we can already.
		 */
		if (WRK_Flush(sp->wrk))
			break;

		sp->esis++;
		obj = sp->obj;
		sp->obj = NULL;
		*sp->http = *sp->http0;
		/* XXX: reset sp->ws */
		http_SetH(sp->http, HTTP_HDR_URL, eb->include.b);
		if (eb->host.b != NULL)  {
			http_Unset(sp->http, H_Host);
			http_Unset(sp->http, H_If_Modified_Since);
			http_SetHeader(sp->wrk, sp->fd, sp->http, eb->host.b);
		}
		/*
		 * XXX: We should decide if we should cache the director
		 * XXX: or not (for session/backend coupling).  Until then
		 * XXX: make sure we don't trip up the check in vcl_recv.
		 */
		sp->director = NULL;
		sp->step = STP_RECV;
		http_ForceGet(sp->http);

		/* Don't do conditionals */
		sp->http->conds = 0;
		http_Unset(sp->http, H_If_Modified_Since);

		/* Client content already taken care of */
		http_Unset(sp->http, H_Content_Length);

		while (1) {
			CNT_Session(sp);
			if (sp->step == STP_DONE)
				break;
			AN(sp->wrk);
			WSL_Flush(sp->wrk, 0);
			DSL(0x20, SLT_Debug, sp->id, "loop waiting for ESI");
			usleep(10000);
		}
		assert(sp->step == STP_DONE);
		sp->esis--;
		sp->obj = obj;

	}
	assert(sp->wrk->wfd = &sp->fd);
	if (sp->esis == 0 && sp->http->protover >= 1.1)
		(void)WRK_Write(sp->wrk, "0\r\n\r\n", -1);
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

