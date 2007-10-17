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

/*--------------------------------------------------------------------
 * Add one piece to the output, either verbatim or include
 */

static void
add_piece(txt t, int kind)
{

	printf("K%d \"%.*s\"\n", kind, t.e - t.b, t.b);
}

static void
vxml_error(struct sess *sp, const char *p, txt t, size_t off, int i, const char *err)
{
	int ellipsis = 0;
	char buf[256], *q;

	if (i == 0) {
		i = t.e - p;
	}
	if (i > 20) {
		i = 20;
		ellipsis = 1;
	}
	q = buf;
	q += sprintf(buf, "at %d: %s \"", off + (p - t.b), err);
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
	WSPR(sp, SLT_ESI_xmlerror, t);
}

/*--------------------------------------------------------------------
 * Zoom over a piece of object and dike out all releveant esi: pieces.
 * The entire txt may not be processed because an interesting part 
 * could possibly span into the next chunk of storage.
 * Return value: number of bytes processed.
 */

static int
vxml(struct sess *sp, txt t, size_t off)
{
	char *p, *q, *r;
	txt o;
	int celem;		/* closing element */
	int remflg;		/* inside <esi:remove> </esi:remove> */
	int incmt;		/* inside <!--esi ... --> comment */
	int i;

	o.b = t.b;
	remflg = 0;
	incmt = 0;
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
				add_piece(o, 0);
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
			add_piece(o, 0);

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
			vxml_error(sp, p, t, off, i,
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
				vxml_error(sp, p, t, off, 1 + q - p,
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
				vxml_error(sp, p, t, off, 1 + q - p,
				    remflg ? "ESI 1.0 forbids nested esi:remove"
				    : "ESI 1.0 esi:remove not opened");
					
				if (!remflg) {
					o.e = p;
					add_piece(o, 0);
				}
			} else if (!celem && q[-1] == '/') {
				/* empty element */
				o.e = p;
				add_piece(o, 0);
			} else if (!celem) {
				/* open element */
				o.e = p;
				add_piece(o, 0);
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
			vxml_error(sp, p, t, off, 1 + q - p,
			    "ESI 1.0 forbids esi: elements inside esi:remove");
			p = q + 1;
			continue;
		}

		if (r + 10 < q && !memcmp(r, "esi:include", 11)) {
			
			o.e = p;
			add_piece(o, 0);

			if (celem == 0) {
				o.b = r + 11;
				o.e = q;
				add_piece(o, 1);
				if (q[-1] != '/') {
					vxml_error(sp, p, t, off, 1 + q - p,
					    "ESI 1.0 wants emtpy esi:include");
				}
			} else {
				vxml_error(sp, p, t, off, 1 + q - p,
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
			vxml_error(sp, p, t, off, 1 + q - p,
			    "ESI 1.0 unimplemented element");
			o.e = p;
			add_piece(o, 0);
			p = q + 1;
			o.b = p;
			continue;
		}

		/* Not an element we care about */
		p = q + 1;
	}
	o.e = p;
	add_piece(o, 0);
	return (p - t.b);
}

/*--------------------------------------------------------------------*/

void
VRT_ESI(struct sess *sp)
{
	struct storage *st;
	txt t;
	int i;
	size_t o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	o = 1;
	VTAILQ_FOREACH(st, &sp->obj->store, list) {
		t.b = (void*)st->ptr;
		t.e = t.b + st->len;
		i = vxml(sp, t, o);
		o += st->len;
		printf("VXML(%p+%d) = %d", st->ptr, st->len, i);
		if (i < st->len) 
			printf(" \"%.*s\"", st->len - i, st->ptr + i);
		printf("\n");
	}
}

/*--------------------------------------------------------------------*/
