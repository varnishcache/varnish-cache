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
 * Add ESI bit to object
 */

static void
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

printf("FIRST: %p\n", VTAILQ_FIRST(&ew->sp->obj->esibits));
printf("ADD %p->%p\n", ew->sp->obj, ew->eb);

	VTAILQ_INSERT_TAIL(&ew->sp->obj->esibits, ew->eb, list);
	ew->eb->verbatim = ew->dst;
	sprintf(ew->eb->chunk_length, "%x\r\n", Tlen(ew->dst));
	VSL(SLT_Debug, ew->sp->fd, "AddBit: %.*s", Tlen(ew->dst), ew->dst.b);
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
 * Add one piece to the output, either verbatim or include
 */

static void
esi_addinclude(struct esi_work *ew, txt t)
{

	VSL(SLT_Debug, 0, "Incl \"%.*s\"", t.e - t.b, t.b);
	esi_addbit(ew);
	ew->eb->include = t;
}

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
				o.e = q;
				esi_addinclude(ew, o);
				if (q[-1] != '/') {
					esi_error(ew, p, 1 + q - p,
					    "ESI 1.0 wants emtpy esi:include");
				}
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
	ew = eww;
	memset(eww, 0, sizeof eww);
	ew->sp = sp;
	ew->off = 1;

	VTAILQ_FOREACH(st, &sp->obj->store, list) {
		ew->st = st;
		i = esi_parse(ew);
		ew->off += st->len;
		printf("VXML(%p+%d) = %d", st->ptr, st->len, i);
		if (i < st->len) {
			/* XXX: Handle complications */
			printf(" \"%.*s\"", st->len - i, st->ptr + i);
			if (VTAILQ_NEXT(st, list))
				INCOMPL();
		}
		printf("\n");
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

	VTAILQ_FOREACH(eb, &sp->obj->esibits, list) {
		WRK_Write(sp->wrk, eb->chunk_length, -1);
		WRK_Write(sp->wrk, eb->verbatim.b, Tlen(eb->verbatim));
		if (VTAILQ_NEXT(eb, list))
			WRK_Write(sp->wrk, "\r\n", -1);
		else
			WRK_Write(sp->wrk, "\r\n0\r\n", -1);
	}
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

