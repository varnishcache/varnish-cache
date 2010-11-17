/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "vrt.h"
#include "vcl.h"
#include "vct.h"
#include "cache.h"
#include "stevedore.h"

/*--------------------------------------------------------------------*/

struct esi_bit {
	VTAILQ_ENTRY(esi_bit)	list;
	char			chunk_length[20];
	txt			verbatim;
	txt			host;
	txt			include;
};

struct esi_ptr {
	const char		*p;
	const char              *e;
	struct storage		*st;
};

struct esi_work {
	struct sess		*sp;
	size_t			off;

	struct esi_ptr		s;
	struct esi_ptr		p;

	txt			tag;

	txt			t;
	struct esi_bit		*eb;
	int			remflg;	/* inside <esi:remove> </esi:remove> */
	int			incmt;	/* inside <!--esi ... --> comment */

	unsigned		space;	/* ... needed */

	VTAILQ_HEAD(, esi_bit)	esibits;

};

struct esidata	{
	unsigned		magic;
#define ESIDATA_MAGIC		0x7255277f
	VTAILQ_HEAD(, esi_bit)	esibits;
	struct storage		*storage;
};

/*--------------------------------------------------------------------
 * Move an esi_ptr one char forward
 */

static void
Nep(struct esi_ptr *ep)
{
	static const char * const finis = "";

	if (ep->p == finis)
		return;
	ep->p++;
	if (ep->p < (char*)ep->st->ptr + ep->st->len)
		return;
	ep->st = VTAILQ_NEXT(ep->st, list);
	if (ep->st != NULL) {
		ep->p = (char *)ep->st->ptr;
		ep->e = ep->p + ep->st->len;
		return;
	}
	ep->p = finis;
	ep->e = finis;
	return;
}

/*--------------------------------------------------------------------
 * Consume one input character.
 */

static void
N(struct esi_work *ew)
{

	if (ew->p.p < ew->p.e)
		ew->off++;
	Nep(&ew->p);
}

/*--------------------------------------------------------------------
 * Strcmp for objects pointers
 */

static int
CMP(const struct esi_ptr *ep, const char *str)
{
	struct esi_ptr p2;

	for (p2 = *ep; *str == *p2.p; str++)
		Nep(&p2);
	return (*str);
}


/*--------------------------------------------------------------------
 * Replace the mandatory XML 1.0 entity references, in place.
 */

static void
XMLentity(txt *t)
{
	char *s, *d;

	for (s = d = t->b; s < t->e; ) {
		if (*s == '&') {
#define R(l,f,r)							\
			if (s + l <= t->e && !memcmp(s, f, l)) {	\
				*d++ = r;				\
				s += l;					\
				continue;				\
			}
			R(6, "&apos;", '\'');
			R(6, "&quot;", '"');
			R(4, "&lt;", '<');
			R(4, "&gt;", '>');
			R(5, "&amp;", '&');
		}
#undef R
		*d++ = *s++;
	}
	t->e = d;
	t->e[0] = '\0';
}


/*--------------------------------------------------------------------
 * Report a parsing error
 *
 * XXX: The "at xxx" count is usually the tail of the sequence.  Since we
 * XXX: wander over the storage in an oderly manner now, we could keep
 * XXX: track of line+pos and record the beginning of the stuff that
 * XXX: offends os in the central dispatch loop.
 * XXX: This is left a an excercise for the reader.
 */

static void
esi_error(const struct esi_work *ew, const char *p, int i, const char *err)
{
	int ellipsis = 0;
	char buf[256], *q;
	txt t;

	VSC_main->esi_errors++;
	if (i == 0)
		i = p - ew->t.b;
	if (i > 20) {
		i = 20;
		ellipsis = 1;
	}
	q = buf;
	q += sprintf(buf, "at %zu: %s \"", ew->off, err);
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

static void
esi_addbit(struct esi_work *ew, const char *verbatim, unsigned len)
{

	ew->space += sizeof(*ew->eb);
	ew->eb = (void*)WS_Alloc(ew->sp->wrk->ws, sizeof *ew->eb);
	AN(ew->eb);
	memset(ew->eb, 0, sizeof *ew->eb);

	VTAILQ_INSERT_TAIL(&ew->esibits, ew->eb, list);
	if (verbatim != NULL) {
		ew->eb->verbatim.b = TRUST_ME(verbatim);
		if (len > 0)
			ew->eb->verbatim.e = TRUST_ME(verbatim + len);
		sprintf(ew->eb->chunk_length, "%x\r\n", Tlen(ew->eb->verbatim));
		if (params->esi_syntax & 0x4)
			WSP(ew->sp, SLT_Debug, "AddBit: %d <%.*s>",
			    Tlen(ew->eb->verbatim),
			    Tlen(ew->eb->verbatim),
			    ew->eb->verbatim.b);
	} else {
		AN(ew->s.p);
		ew->eb->verbatim.b = ew->eb->verbatim.e = TRUST_ME(ew->s.p);
	}
}

/*--------------------------------------------------------------------*/

static void
esi_addpfx(struct esi_work *ew)
{
	const char *ep;

	if (ew->remflg) {
		/* In <esi:remove...> don't add anything */
		ew->s = ew->p;
		return;
	}
	while (ew->s.st != ew->p.st) {
		ep = (const char *)(ew->s.st->ptr + ew->s.st->len);
		esi_addbit(ew, ew->s.p, ep - ew->s.p);
		ew->s.p = ep;
		Nep(&ew->s);
	}
	if (ew->s.st != NULL && ew->p.p != ew->s.p)
		esi_addbit(ew, ew->s.p, ew->p.p - ew->s.p);
	ew->s.p = ew->p.p;
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

	if (!vct_isxmlnamestart(*in->b)) {
		/* XXX error */
		esi_error(ew, in->b, 1,
		    "XML 1.0 Illegal attribute start character");
		return (-1);
	}

	/* Attribute name until '=' or space */
	*attrib = *in;
	while(in->b < in->e && *in->b != '=' && !isspace(*in->b)) {
		if (!vct_isxmlname(*in->b)) {
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
		val->e = val->b = in->b;
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
	return (1);
}

/*--------------------------------------------------------------------
 * Add one piece to the output, either verbatim or include
 */

static void
esi_handle_include(struct esi_work *ew)
{
	struct esi_bit *eb;
	char *p, *q, *c;
	txt t = ew->tag;
	txt tag;
	txt val;
	unsigned u, v, s;

	if (ew->eb == NULL || ew->eb->include.b != NULL)
		esi_addbit(ew, NULL, 0);
	eb = ew->eb;
	WSP(ew->sp, SLT_Debug, "Incl \"%.*s\"", t.e - t.b, t.b);
	while (esi_attrib(ew, &t, &tag, &val) == 1) {
		if (params->esi_syntax & 0x4)
			WSP(ew->sp, SLT_Debug, "<%.*s> -> <%.*s>",
			    tag.e - tag.b, tag.b, val.e - val.b, val.b);
		if (Tlen(tag) != 3 || memcmp(tag.b, "src", 3))
			continue;
		if (Tlen(val) == 0) {
			esi_error(ew, tag.b, Tlen(tag),
			    "ESI esi:include src attribute without value");
			continue;
		}

		/* We are saving the original string */
		s = 0;

		if (val.b != val.e) {
			s = Tlen(val) + 1;
			c = WS_Alloc(ew->sp->wrk->ws, s);
			XXXAN(c);
			memcpy(c, val.b, Tlen(val));
			val.b = c;
			val.e = val.b + s;
			val.e[-1] = '\0';
		}

		if (strchr(val.b, '&'))
			XMLentity(&val);

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
			CHECK_OBJ_NOTNULL(ew->sp->wrk->bereq, HTTP_MAGIC);
			tag = ew->sp->wrk->bereq->hd[HTTP_HDR_URL];

			/* Use the objects WS to store the result */
			CHECK_OBJ_NOTNULL(ew->sp->obj, OBJECT_MAGIC);

			/* Look for the last '/' before a '?' */
			q = NULL;
			for (p = tag.b; p < tag.e && *p != '?'; p++)
				if (*p == '/')
					q = p;
			if (q != NULL)
				tag.e = q + 1;

			u = WS_Reserve(ew->sp->wrk->ws, 0);
			v = snprintf(ew->sp->wrk->ws->f, u - 1, "%.*s%.*s",
			    pdiff(tag.b, tag.e), tag.b,
			    pdiff(val.b, val.e), val.b);
			v++;
			xxxassert(v < u);
			eb->include.b = ew->sp->wrk->ws->f;
			eb->include.e = ew->sp->wrk->ws->f + v;
			WS_Release(ew->sp->wrk->ws, v);
		}
		if (eb->include.b != NULL)
			ew->space += Tlen(eb->include);
		if (eb->host.b != NULL)
			ew->space += Tlen(eb->host);
	}
}

/*--------------------------------------------------------------------
 * See if this looks like XML: first non-white char must be '<'
 */

static int
looks_like_xml(const struct object *obj) {
	struct storage *st;
	unsigned u;

	VTAILQ_FOREACH(st, &obj->store, list) {
		AN(st);
		for (u = 0; u < st->len; u++) {
			if (isspace(st->ptr[u]))
				continue;
			if (st->ptr[u] == '<')
				return (1);
			else
				return (0);
		}
	}
	return (0);
}

/*--------------------------------------------------------------------
 * A quick stroll through the object, to find out if it contains any
 * esi sequences at all.
 */

static int
contain_esi(const struct object *obj) {
	struct storage *st;
	unsigned u;
	const char *r, *r2;
	static const char * const wanted = "<esi:";
	static const char * const wanted2 = "<!--esi";

	/*
	 * Do a fast check to see if there is any '<esi:' sequences at all
	 */
	r = wanted;
	r2 = wanted2;
	VTAILQ_FOREACH(st, &obj->store, list) {
		AN(st);
		for (u = 0; u < st->len; u++) {
			if (st->ptr[u] != *r) {
				r = wanted;
			} else if (*++r == '\0')
				return (1);
			if (st->ptr[u] != *r2) {
				r2 = wanted2;
			} else if (*++r2 == '\0')
				return (1);
		}
	}
	return (0);
}

/*--------------------------------------------------------------------*/

static void
parse_esi_comment(struct esi_work *ew)
{

	esi_addpfx(ew);

	N(ew); N(ew); N(ew); N(ew); N(ew); N(ew); N(ew);
	assert(!ew->incmt);
	ew->incmt = 1;
	ew->s.p = ew->p.p;
}

/*--------------------------------------------------------------------*/

static void
parse_comment(struct esi_work *ew)
{

	do {
		N(ew);
		if (*ew->p.p == '-' && !CMP(&ew->p, "-->")) {
			N(ew);
			N(ew);
			N(ew);
			break;
		}
	} while (ew->p.p < ew->p.e);
}

/*--------------------------------------------------------------------*/

static void
parse_cdata(struct esi_work *ew)
{

	esi_addpfx(ew);

	do {
		N(ew);
		if (*ew->p.p == ']' && !CMP(&ew->p, "]]>")) {
			N(ew);
			N(ew);
			N(ew);
			break;
		}
	} while (ew->p.p < ew->p.e);
}

/*--------------------------------------------------------------------*/

static void
parse_esi_tag(struct esi_work *ew, int closing)
{
	int l, ll, empty;
	struct esi_ptr px;
	char *q;

	esi_addpfx(ew);

	do
		N(ew);
	while (*ew->p.p != '>' && ew->p.p < ew->p.e);
	if (ew->p.p == ew->p.e) {
		esi_addpfx(ew);
		esi_error(ew, ew->s.p, 0,
		    "XML 1.0 incomplete language element");
		return;
	}
	N(ew);

	if (ew->p.st == ew->s.st) {
		ew->tag.b = TRUST_ME(ew->s.p);
		ew->tag.e = TRUST_ME(ew->p.p);
	} else {
		/*
		 * The element is spread over more than one storage
		 * segment, pull it together in the object workspace
		 * XXX: Ideally, we should only pull together the bits
		 * XXX: we need, like the filename.
		 */
		ew->tag.b = ew->sp->wrk->ws->f;
		ew->tag.e = ew->tag.b + WS_Reserve(ew->sp->wrk->ws, 0);
		px = ew->s;
		q = ew->tag.b;
		while (px.p != ew->p.p) {
			xxxassert(q < ew->tag.e);
			*q++ = *px.p;
			Nep(&px);
		}
		ew->tag.e = q;
		WS_Release(ew->sp->wrk->ws, Tlen(ew->tag));
	}
	ll = Tlen(ew->tag);
	ew->tag.b++;
	ew->tag.e--;
	empty = (ew->tag.e[-1] == '/') ? 1 : 0;
	if (empty)
		ew->tag.e--;

	if (empty && closing)
		esi_error(ew, ew->s.p, ll,
		    "XML 1.0 empty and closing element");

	ew->tag.b += 4 + (closing ? 1 : 0);
	l = Tlen(ew->tag);
	WSP(ew->sp, SLT_Debug,
	    "tag {%.*s} %d %d %d", l, ew->tag.b, ew->remflg, empty, closing);
	if (l >= 6 && !memcmp(ew->tag.b, "remove", 6)) {
		if (empty) {
			/* XXX ?? */
		} else if (closing) {
			if (!ew->remflg)
				esi_error(ew, ew->s.p, ll,
				    "ESI 1.0 esi:remove not opened");
			ew->remflg = 0;
		} else {
			if (ew->remflg)
				esi_error(ew, ew->s.p, ll,
				    "ESI 1.0 forbids nested esi:remove");
			ew->remflg = 1;
		}
	} else if (ew->remflg) {
		esi_error(ew, ew->s.p, ll,
		    "ESI 1.0 forbids esi: elements inside esi:remove");
	} else if (l >= 7 && !memcmp(ew->tag.b, "comment", 7)) {
		if (closing)
			esi_error(ew, ew->s.p, ll,
			    "ESI 1.0 closing esi:comment illegal");
		else if (!empty)
			esi_error(ew, ew->s.p, ll,
			    "ESI 1.0 wants empty esi:comment");
	} else if (l >= 7 && !memcmp(ew->tag.b, "include", 7)) {
		if (closing) {
			esi_error(ew, ew->s.p, ll,
			    "ESI 1.0 closing esi:include illegal");
		} else if (!empty) {
			esi_error(ew, ew->s.p, ll,
			    "ESI 1.0 wants empty esi:include");
		}
		ew->tag.b += 7;
		esi_handle_include(ew);
	} else {
		esi_error(ew, ew->s.p, ll,
		    "ESI 1.0 unimplemented element");
	}
	ew->s = ew->p;
}

/*--------------------------------------------------------------------*/

void
ESI_Parse(struct sess *sp)
{
	struct esi_work *ew, eww[1];
	struct esi_bit *eb;
	struct esidata *ed;
	struct storage *st;
	unsigned u;
	char *hack;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	AssertObjPassOrBusy(sp->obj);
	if (VTAILQ_EMPTY(&sp->obj->store))
		return;

	if (!(params->esi_syntax & 0x00000001)) {
		/*
		 * By default, we will not ESI process an object where
		 *  the first non-space character is different from '<'
		 */
		if (!looks_like_xml(sp->obj)) {
			WSP(sp, SLT_ESI_xmlerror,
			    "No ESI processing, first char not '<'");
			return;
		}
	}

	/*
	 * Do a fast check to see if there is any '<esi:' sequences at all
	 */
	if (!contain_esi(sp->obj))
		return;

	/* XXX: debugging hack */
	hack = sp->wrk->ws->f;

	VSC_main->esi_parse++;
	/* XXX: only if GET ? */
	ew = eww;
	memset(eww, 0, sizeof eww);
	VTAILQ_INIT(&ew->esibits);
	ew->sp = sp;
	ew->off = 1;

	ew->space += sizeof(struct esidata);

	ew->p.st = VTAILQ_FIRST(&sp->obj->store);
	AN(ew->p.st);
	ew->p.p = (char *)ew->p.st->ptr;
	ew->p.e = ew->p.p + ew->p.st->len;

	/* ->s points to the first un-dealt-with byte */
	ew->s = ew->p;

	while (ew->p.p < ew->p.e) {

		if (ew->incmt && *ew->p.p == '-' && !CMP(&ew->p, "-->")) {
			/* End of ESI comment */
			esi_addpfx(ew);
			N(ew);
			N(ew);
			N(ew);
			ew->s = ew->p;
			ew->incmt = 0;
			continue;
		}
		/* Skip forward to the first '<' */
		if (*ew->p.p != '<') {
			N(ew);
			continue;
		}

		if (!CMP(&ew->p, "<!--esi")) {
			parse_esi_comment(ew);
		} else if (!CMP(&ew->p, "<!--")) {
			parse_comment(ew);
		} else if (!CMP(&ew->p, "</esi")) {
			parse_esi_tag(ew, 1);
		} else if (!CMP(&ew->p, "<esi:")) {
			parse_esi_tag(ew, 0);
		} else if (!CMP(&ew->p, "<![CDATA[")) {
			parse_cdata(ew);
		} else {
			/*
			 * Something we don't care about, just skip it.
			 */
			N(ew);
			if (!(params->esi_syntax & 0x2)) {
				/* XXX: drop this ? */
				do {
					N(ew);
				} while (*ew->p.p != '>' && ew->p.p < ew->p.e);
			}
		}
	}
	esi_addpfx(ew);

	/*
	 * XXX: we could record the starting point of these elements
	 * XXX: so that the char-index were more useful, but we are
	 * XXX: not trivially able to print their contents, so leave
	 * XXX: it like this for now, pending more thought about the
	 * XXX: proper way to report these errors.
	 */
	if (ew->remflg)
		esi_error(ew, ew->t.e, -1,
		    "ESI 1.0 unterminated <esi:remove> element");
	if (ew->incmt)
		esi_error(ew, ew->t.e, -1,
		    "ESI 1.0 unterminated <!--esi comment");

	st = STV_alloc(sp, ew->space, sp->obj->objcore);
	AN(st);
	assert(st->space >= ew->space);

	ed = (void*)st->ptr;
	st->len += sizeof(*ed);
	memset(ed, 0, sizeof *ed);
	ed->magic = ESIDATA_MAGIC;
	ed->storage = st;
	VTAILQ_INIT(&ed->esibits);

	while (!VTAILQ_EMPTY(&ew->esibits)) {
		eb = VTAILQ_FIRST(&ew->esibits);
		VTAILQ_REMOVE(&ew->esibits, eb, list);
		memcpy(st->ptr + st->len, eb, sizeof *eb);
		eb = (void*)(st->ptr + st->len);
		st->len += sizeof(*eb);

		if (eb->include.b != NULL) {
			u = Tlen(eb->include);
			memcpy(st->ptr + st->len, eb->include.b, u);
			eb->include.b = (void*)(st->ptr + st->len);
			eb->include.e = eb->include.b + u;
			st->len += u;
		}
		if (eb->host.b != NULL) {
			u = Tlen(eb->host);
			memcpy(st->ptr + st->len, eb->host.b, u);
			eb->host.b = (void*)(st->ptr + st->len);
			eb->host.e = eb->host.b + u;
			st->len += u;
		}

		VTAILQ_INSERT_TAIL(&ed->esibits, eb, list);
	}

	assert(st->len <= st->space);
	assert(st->len == ew->space);
	sp->obj->esidata = ed;

	memset(hack, 0xaa, sp->wrk->ws->f - hack);
}

/*--------------------------------------------------------------------*/

void
ESI_Deliver(struct sess *sp)
{
	struct esi_bit *eb;
	struct object *obj;
	struct worker *w;
	char *ws_wm;
	struct http http_save;
	struct esidata *ed;
	unsigned sxid;

	w = sp->wrk;
	WRW_Reserve(w, &sp->fd);
	http_save.magic = 0;
	ed = sp->obj->esidata;
	CHECK_OBJ_NOTNULL(ed, ESIDATA_MAGIC);
	VTAILQ_FOREACH(eb, &ed->esibits, list) {
		if (Tlen(eb->verbatim)) {
			if (sp->http->protover >= 1.1)
				(void)WRW_Write(w, eb->chunk_length, -1);
			sp->acct_tmp.bodybytes += WRW_Write(w,
			    eb->verbatim.b, Tlen(eb->verbatim));
			if (sp->http->protover >= 1.1)
				(void)WRW_Write(w, "\r\n", -1);
		}
		if (eb->include.b == NULL ||
		    sp->esis >= params->max_esi_includes)
			continue;

		if (WRW_FlushRelease(w)) {
			vca_close_session(sp, "remote closed");
			return;
		}

		sp->esis++;
		obj = sp->obj;
		sp->obj = NULL;

		/* Save the master objects HTTP state, we may need it later */
		if (http_save.magic == 0)
			http_save = *sp->http;

		/* Reset request to status before we started messing with it */
		HTTP_Copy(sp->http, sp->http0);

		/* Take a workspace snapshot */
		ws_wm = WS_Snapshot(sp->ws);

		http_SetH(sp->http, HTTP_HDR_URL, eb->include.b);
		if (eb->host.b != NULL)  {
			http_Unset(sp->http, H_Host);
			http_Unset(sp->http, H_If_Modified_Since);
			http_SetHeader(w, sp->fd, sp->http, eb->host.b);
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

		sxid = sp->xid;
		while (1) {
			sp->wrk = w;
			CNT_Session(sp);
			if (sp->step == STP_DONE)
				break;
			AZ(sp->wrk);
			WSL_Flush(w, 0);
			DSL(0x20, SLT_Debug, sp->id, "loop waiting for ESI");
			(void)usleep(10000);
		}
		sp->xid = sxid;
		AN(sp->wrk);
		assert(sp->step == STP_DONE);
		sp->esis--;
		sp->obj = obj;

		/* Reset the workspace */
		WS_Reset(sp->ws, ws_wm);

		WRW_Reserve(sp->wrk, &sp->fd);
		if (sp->fd < 0)
			break;
	}
	/* Restore master objects HTTP state */
	if (http_save.magic)
		*sp->http = http_save;
	if (sp->esis == 0 && sp->http->protover >= 1.1)
		(void)WRW_Write(sp->wrk, "0\r\n\r\n", -1);
	if (WRW_FlushRelease(sp->wrk))
		vca_close_session(sp, "remote closed");
}

/*--------------------------------------------------------------------*/

void
ESI_Destroy(struct object *o)
{
	struct esidata *ed;

	ed = o->esidata;
	o->esidata = NULL;
	CHECK_OBJ_NOTNULL(ed, ESIDATA_MAGIC);
	STV_free(ed->storage);
}
