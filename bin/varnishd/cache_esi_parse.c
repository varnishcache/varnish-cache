/*-
 * Copyright (c) 2011 Varnish Software AS
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id")

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "cache_esi.h"
#include "vend.h"
#include "vct.h"
#include "zlib.h"
#include "stevedore.h"

#ifndef OLD_ESI

//#define Debug(fmt, ...) printf(fmt, __VA_ARGS__)
#define Debug(fmt, ...) /**/

struct vep_state;

enum dowhat {DO_ATTR, DO_TAG};
typedef void dostuff_f(struct vep_state *, enum dowhat);

struct vep_match {
	const char	*match;
	const char	* const *state;
};

enum vep_mark { VERBATIM = 0, SKIP };

struct vep_state {
	unsigned		magic;
#define VEP_MAGIC		0x55cb9b82
	vfp_bytes_f		*bytes;
	struct vsb		*vsb;

	struct sess		*sp;

	/* parser state */
	const char		*state;

	unsigned		endtag;
	unsigned		emptytag;
	unsigned		canattr;

	unsigned		remove;

	ssize_t			o_wait;
	ssize_t			o_pending;
	ssize_t			o_total;
	uint32_t		crc;
	ssize_t			o_crc;
	uint32_t		crcp;

const char		*hack_p;
	const char		*ver_p;

	const char		*until;
	const char		*until_p;
	const char		*until_s;

	int			in_esi_tag;

	const char		*esicmt;
	const char		*esicmt_p;

	struct vep_match	*attr;
	struct vsb		*attr_vsb;
	int			attr_delim;

	struct vep_match	*match;
	struct vep_match	*match_hit;

	char			tag[10];
	int			tag_i;

	dostuff_f		*dostuff;

	struct vsb		*include_src;

	unsigned		nm_skip;
	unsigned		nm_verbatim;
	unsigned		nm_pending;
	enum vep_mark		last_mark;
};

/*---------------------------------------------------------------------*/

static const char * const VEP_START =		"[Start]";
static const char * const VEP_TESTXML = 	"[TestXml]";
static const char * const VEP_NOTXML =	 	"[NotXml]";

static const char * const VEP_NEXTTAG = 	"[NxtTag]";
static const char * const VEP_NOTMYTAG =	"[NotMyTag]";

static const char * const VEP_STARTTAG = 	"[StartTag]";
static const char * const VEP_COMMENT =		"[Comment]";
static const char * const VEP_CDATA =		"[CDATA]";
static const char * const VEP_ESITAG =		"[ESITag]";

static const char * const VEP_ESIREMOVE =	"[ESI:Remove]";
static const char * const VEP_ESIINCLUDE =	"[ESI:Include]";
static const char * const VEP_ESICOMMENT =	"[ESI:Comment]";
static const char * const VEP_ESIBOGON =	"[ESI:Bogon]";

static const char * const VEP_INTAG =		"[InTag]";
static const char * const VEP_TAGERROR =	"[TagError]";

static const char * const VEP_ATTR =		"[Attribute]";
static const char * const VEP_SKIPATTR =	"[SkipAttribute]";
static const char * const VEP_ATTRDELIM =	"[AttrDelim]";
static const char * const VEP_ATTRGETVAL =	"[AttrGetValue]";
static const char * const VEP_ATTRVAL =		"[AttrValue]";

static const char * const VEP_UNTIL =		"[Until]";
static const char * const VEP_MATCHBUF = 	"[MatchBuf]";
static const char * const VEP_MATCH =		"[Match]";

/*---------------------------------------------------------------------*/

static struct vep_match vep_match_starttag[] = {
	{ "!--",	&VEP_COMMENT },
	{ "esi:",	&VEP_ESITAG },
	{ "![CDATA[",	&VEP_CDATA },
	{ NULL,		&VEP_NOTMYTAG }
};

/*---------------------------------------------------------------------*/

static struct vep_match vep_match_esi[] = {
	{ "include",	&VEP_ESIINCLUDE },
	{ "remove",	&VEP_ESIREMOVE },
	{ "comment",	&VEP_ESICOMMENT },
	{ NULL,		&VEP_ESIBOGON }
};

/*---------------------------------------------------------------------*/

static struct vep_match vep_match_attr_include[] = {
	{ "src=",	&VEP_ATTRGETVAL },
	{ NULL,		&VEP_SKIPATTR }
};

/*--------------------------------------------------------------------
 * Report a parsing error
 */

static void
vep_error(const struct vep_state *vep, const char *p)
{
	intmax_t l;

	VSC_main->esi_errors++;
	l = (intmax_t)(vep->ver_p - vep->hack_p);
	printf("ERROR at %jd %s\n", l , p);
	WSP(vep->sp, SLT_ESI_xmlerror, "ERR at %jd %s", l, p);
	
}

/*--------------------------------------------------------------------
 * Report a parsing warning
 */

static void
vep_warn(const struct vep_state *vep, const char *p)
{
	intmax_t l;

	VSC_main->esi_warnings++;
	l = (intmax_t)(vep->ver_p - vep->hack_p);
	printf("WARNING at %jd %s\n", l, p);
	WSP(vep->sp, SLT_ESI_xmlerror, "WARN at %jd %s", l, p);
	
}

/*---------------------------------------------------------------------
 * return match or NULL if more input needed.
 */

static struct vep_match *
vep_match(struct vep_state *vep, const char *b, const char *e)
{
	struct vep_match *vm;
	const char *q, *r;
	ssize_t l;

	for (vm = vep->match; vm->match; vm++) {
		r = b;
		for (q = vm->match; *q && r < e; q++, r++)
			if (*q != *r)
				break;
		if (*q != '\0' && r == e) {
			if (b != vep->tag) {
				l = e - b;
				assert(l < sizeof vep->tag);
				memmove(vep->tag, b, l);
				vep->tag_i = l;
			}
			return (NULL);
		}
		if (*q == '\0')
			return (vm);
	}
	return (vm);
}

/*---------------------------------------------------------------------
 *
 */

static void
vep_emit_len(const struct vep_state *vep, ssize_t l, int m8, int m16, int m64)
{
	uint8_t buf[9];

	assert(l > 0);
	if (l < 256) {
		buf[0] = (uint8_t)m8;
		buf[1] = (uint8_t)l;
		assert((ssize_t)buf[1] == l);
		vsb_bcat(vep->vsb, buf, 2);
	} else if (l < 65536) {
		buf[0] = (uint8_t)m16;
		vbe16enc(buf + 1, (uint16_t)l);
		assert((ssize_t)vbe16dec(buf + 1) == l);
		vsb_bcat(vep->vsb, buf, 3);
	} else {
		buf[0] = (uint8_t)m64;
		vbe64enc(buf + 1, l);
		assert((ssize_t)vbe64dec(buf + 1) == l);
		vsb_bcat(vep->vsb, buf, 9);
	}
} 

static void
vep_emit_skip(const struct vep_state *vep, ssize_t l)
{

	if (params->esi_syntax & 0x20) {
		Debug("---> SKIP(%jd)\n", (intmax_t)l);
	}
	vep_emit_len(vep, l, VEC_S1, VEC_S2, VEC_S8);
} 

static void
vep_emit_verbatim(const struct vep_state *vep, ssize_t l)
{
	uint8_t buf[4];

	if (params->esi_syntax & 0x20) {
		Debug("---> VERBATIM(%jd)\n", (intmax_t)l);
	}
	vep_emit_len(vep, l, VEC_V1, VEC_V2, VEC_V8);
	vbe32enc(buf, vep->crc);
	vsb_bcat(vep->vsb, buf, sizeof buf);
	vsb_printf(vep->vsb, "%lx\r\n%c", l, 0);
} 

static void
vep_emit_common(struct vep_state *vep, ssize_t *l, enum vep_mark mark)
{
	assert(*l > 0);
	assert(*l == vep->o_crc);

	assert(mark == SKIP || mark == VERBATIM);
	if (mark == SKIP)
		vep_emit_skip(vep, *l);
	else
		vep_emit_verbatim(vep, *l);

	vep->crc = crc32(0L, Z_NULL, 0);
	vep->o_crc = 0;
	vep->o_total += *l;
	*l = 0;
}

/*---------------------------------------------------------------------
 *
 */

static void
vep_mark_common(struct vep_state *vep, const char *p, enum vep_mark mark)
{
	ssize_t l;

	assert(mark == SKIP || mark == VERBATIM);

	/* The NO-OP case, no data, no pending data & no change of mode */
	if (vep->last_mark == mark && p == vep->ver_p && vep->o_pending == 0)
		return;

	/*
	 * If we changed mode, emit whatever the opposite mode
	 * assembled before the pending bytes.
	 */

	if (vep->last_mark != mark && vep->o_wait > 0)
		vep_emit_common(vep, &vep->o_wait, vep->last_mark);

	/* Transfer pending bytes CRC into active mode CRC */
	if (vep->o_pending) {
		if (vep->o_crc == 0) {
			vep->crc = vep->crcp;
			vep->o_crc = vep->o_pending;
		} else {
			vep->crc = crc32_combine(vep->crc,
			    vep->crcp, vep->o_pending);
			vep->o_crc += vep->o_pending;
		}
		vep->crcp = crc32(0L, Z_NULL, 0);
	}

	/* * Process this bit of input */
	AN(vep->ver_p);
	l = p - vep->ver_p;
	assert(l >= 0);
	vep->crc = crc32(vep->crc, (const void*)vep->ver_p, l);
	vep->o_crc += l;
	vep->ver_p = p;

	vep->o_wait += vep->o_pending;
	vep->o_wait += l;
	vep->o_pending = 0;
	vep->last_mark = mark;
}

static void
vep_mark_verbatim(struct vep_state *vep, const char *p)
{

	vep_mark_common(vep, p, VERBATIM);
	vep->nm_verbatim++;
} 

static void
vep_mark_skip(struct vep_state *vep, const char *p)
{

	vep_mark_common(vep, p, SKIP);
	vep->nm_skip++;
} 

static void
vep_mark_pending(struct vep_state *vep, const char *p)
{
	ssize_t l;

	AN(vep->ver_p);
	l = p - vep->ver_p;
	assert(l > 0);
	assert(l >= 0);
	vep->crcp = crc32(vep->crcp, (const void *)vep->ver_p, l);
	vep->ver_p = p;

	vep->o_pending += l;
	vep->nm_pending++;
}

/*---------------------------------------------------------------------
 */

static void __match_proto__()
vep_do_comment(struct vep_state *vep, enum dowhat what)
{
	Debug("DO_COMMENT(%d)\n", what);
	assert(what == DO_TAG);
	if (!vep->emptytag) 
		vep_error(vep, "ESI 1.0 <esi:comment> needs final '/'");
}

/*---------------------------------------------------------------------
 */

static void __match_proto__()
vep_do_remove(struct vep_state *vep, enum dowhat what)
{
	Debug("DO_REMOVE(%d, end %d empty %d remove %d)\n",
	    what, vep->endtag, vep->emptytag, vep->remove);
	assert(what == DO_TAG);
	if (vep->emptytag) {
		vep_error(vep,
		    "ESI 1.0 <esi:remove/> not legal");
	} else {
		if (vep->remove && !vep->endtag)
			vep_error(vep,
			    "ESI 1.0 <esi:remove> already open");
		else if (!vep->remove && vep->endtag)
			vep_error(vep,
			    "ESI 1.0 <esi:remove> not open");
		else 
			vep->remove = !vep->endtag;
	}
}

/*---------------------------------------------------------------------
 */

static void __match_proto__()
vep_do_include(struct vep_state *vep, enum dowhat what)
{
	char *p, *q, *h;
	ssize_t l;
	txt url;

	Debug("DO_INCLUDE(%d)\n", what);
	if (what == DO_ATTR) {
		Debug("ATTR (%s) (%s)\n", vep->match_hit->match,
			vsb_data(vep->attr_vsb));
		XXXAZ(vep->include_src);	/* multiple src= */
		vep->include_src = vep->attr_vsb;
		return;
	}
	assert(what == DO_TAG);
	if (!vep->emptytag) 
		vep_warn(vep,
		    "ESI 1.0 <esi:include> lacks final '/'");
	if (vep->include_src == NULL) {
		vep_error(vep,
		    "ESI 1.0 <esi:include> lacks src attr");
		return;
	}

	/*
	 * Strictly speaking, we ought to spit out any piled up skip before
	 * emitting the VEC for the include, but objectively that makes no
	 * difference and robs us of a chance to collapse another skip into
	 * this on so we don't do that.
	 * However, we cannot tolerate any verbatim stuff piling up.
	 * The mark_skip() before calling dostuff should have taken
	 * care of that.  Make sure.
	 */
	assert(vep->o_wait == 0 || vep->last_mark == SKIP);
	/* XXX: what if it contains NUL bytes ?? */
	p = vsb_data(vep->include_src);
	l = vsb_len(vep->include_src);
	h = 0;

	vsb_printf(vep->vsb, "%c", VEC_INCL);
	if (l > 7 && !memcmp(p, "http://", 7)) {
		h = p + 7;
		p = strchr(h, '/');
		AN(p);
		Debug("HOST <%.*s> PATH <%s>\n", (int)(p-h),h, p);
		vsb_printf(vep->vsb, "Host: %.*s%c",
		    (int)(p-h), h, 0);
	} else if (*p == '/') {
		vsb_printf(vep->vsb, "%c", 0);
	} else {
		vsb_printf(vep->vsb, "%c", 0);
		url = vep->sp->wrk->bereq->hd[HTTP_HDR_URL];
		/* Look for the last / before a '?' */
		h = NULL;
		for (q = url.b; q < url.e && *q != '?'; q++)
			if (*q == '/')
				h = q;
		if (h == NULL)
			h = q + 1;
			
		Debug("INCL:: [%.*s]/[%s]\n",
		    (int)(h - url.b), url.b, p);
		vsb_printf(vep->vsb, "%.*s/", (int)(h - url.b), url.b);
	}
	l -= (p - vsb_data(vep->include_src));
	for (q = p; *q != '\0'; ) {
		if (*q == '&') {
#define R(w,f,r)							\
			if (q + w <= p + l && !memcmp(q, f, w)) { \
				vsb_printf(vep->vsb, "%c", r);	\
				q += l;				\
				continue;			\
			}
			R(6, "&apos;", '\'');
			R(6, "&quot;", '"');
			R(4, "&lt;", '<');
			R(4, "&gt;", '>');
			R(5, "&amp;", '&');
		}
		vsb_printf(vep->vsb, "%c", *q++);
	}
#undef R
	vsb_printf(vep->vsb, "%c", 0);

	vsb_delete(vep->include_src);
	vep->include_src = NULL;
}

/*---------------------------------------------------------------------
 * Lex/Parse object for ESI instructions
 *
 * This function is called with the input object piecemal so do not
 * assume that we have more than one char available at at time, but
 * optimize for getting huge chunks. 
 *
 * NB: At the bottom of this source-file, there is a dot-diagram matching
 * NB: the state-machine.  Please maintain it along with the code.
 */

static void
vep_parse(struct vep_state *vep, const char *p, size_t l)
{
	const char *e;
	struct vep_match *vm;
	int i;

	CHECK_OBJ_NOTNULL(vep, VEP_MAGIC);
	assert(l > 0);

	e = p + l;

	while (p < e) {
		AN(vep->state);
		i = e - p;
		if (i > 10)
			i = 10;
		Debug("EP %s %d (%.*s) [%.*s]\n",
		    vep->state,
		    vep->remove,
		    vep->tag_i, vep->tag,
		    i, p);
		assert(p >= vep->ver_p);

		/******************************************************
		 * SECTION A
		 */

		if (vep->state == VEP_START) {
			if (params->esi_syntax & 0x1)
				vep->state = VEP_NEXTTAG;
			else
				vep->state = VEP_TESTXML;
		} else if (vep->state == VEP_TESTXML) {
			/*
			 * If the first non-whitespace char is different
			 * from '<' we assume this is not XML.
			 */
			while (p < e && vct_islws(*p)) 
				p++;
			vep_mark_verbatim(vep, p);
			if (p < e && *p == '<') {
				p++;
				vep->state = VEP_STARTTAG;
			} else if (p < e) {
				WSP(vep->sp, SLT_ESI_xmlerror,
				    "No ESI processing, first char not '<'");
				vep->state = VEP_NOTXML;
			}
		} else if (vep->state == VEP_NOTXML) {
			/*
			 * This is not recognized as XML, just skip thru
			 * vfp_esi_end() will handle the rest
			 */
			p = e;
			vep_mark_verbatim(vep, p);

		/******************************************************
		 * SECTION B
		 */

		} else if (vep->state == VEP_NOTMYTAG) {
			if (params->esi_syntax & 0x2) {
				p++;
				vep->state = VEP_NEXTTAG;
			} else {
				vep->tag_i = 0;
				while (p < e) {
					if (*p++ == '>') {
						vep->state = VEP_NEXTTAG;
						break;
					}
				}
			}
			if (p == e && !vep->remove)
				vep_mark_verbatim(vep, p);
		} else if (vep->state == VEP_NEXTTAG) {
			/*
			 * Hunt for start of next tag and keep an eye
			 * out for end of EsiCmt if armed.
			 */
			vep->emptytag = 0;
			vep->endtag = 0;
			vep->attr = NULL;
			vep->dostuff = NULL;
			while (p < e && *p != '<') {
				if (vep->esicmt_p == NULL) {
					p++;
					continue;
				}
				if (*p != *vep->esicmt_p) {
					p++;
					vep->esicmt_p = vep->esicmt;
					continue;
				}
				if (!vep->remove &&
				    vep->esicmt_p == vep->esicmt)
					vep_mark_verbatim(vep, p);
				p++;
				if (*++vep->esicmt_p == '\0') {
					vep->esicmt = NULL;
					vep->esicmt_p = NULL;
					/*
					 * The end of the esicmt
					 * should not be emitted.
					 * But the stuff before should
					 */
					vep_mark_skip(vep, p);
				}
			}
			if (p < e) {
				if (!vep->remove)
					vep_mark_verbatim(vep, p);
				assert(*p == '<');
				p++;
				vep->state = VEP_STARTTAG;
			} else if (vep->esicmt_p == vep->esicmt && !vep->remove)
				vep_mark_verbatim(vep, p);

		/******************************************************
		 * SECTION C
		 */

		} else if (vep->state == VEP_STARTTAG) {
			/*
			 * Start of tag, set up match table
			 */
			if (p < e) {
				if (*p == '/') {
					vep->endtag = 1;
					p++;
				} 
				vep->match = vep_match_starttag;
				vep->state = VEP_MATCH;
			}
		} else if (vep->state == VEP_COMMENT) {
			/*
			 * We are in a comment, find out if it is an
			 * ESI comment or a regular comment
			 */
			if (vep->esicmt == NULL)
				vep->esicmt_p = vep->esicmt = "esi";
			while (p < e) {
				if (*p != *vep->esicmt_p) {
					vep->esicmt_p = vep->esicmt = NULL; 
					vep->until_p = vep->until = "-->";
					vep->until_s = VEP_NEXTTAG;
					vep->state = VEP_UNTIL;
					break;
				}
				p++;
				if (*++vep->esicmt_p != '\0')
					continue;
				if (vep->remove)
					vep_error(vep,
					    "ESI 1.0 Nested <!--esi"
					    " element in <esi:remove>");
				vep->esicmt_p = vep->esicmt = "-->";
				vep->state = VEP_NEXTTAG;
				vep_mark_skip(vep, p);
				break;
			}
		} else if (vep->state == VEP_CDATA) {
			/*
			 * Easy: just look for the end of CDATA
			 */
			vep->until_p = vep->until = "]]>";
			vep->until_s = VEP_NEXTTAG;
			vep->state = VEP_UNTIL;
		} else if (vep->state == VEP_ESITAG) {
			vep->in_esi_tag = 1;
			vep_mark_skip(vep, p);
			vep->match = vep_match_esi;
			vep->state = VEP_MATCH;
		} else if (vep->state == VEP_ESIINCLUDE) {
			if (vep->remove) {
				vep_error(vep,
				    "ESI 1.0 <esi:include> element"
				    " nested in <esi:remove>");
				vep->state = VEP_TAGERROR;
			} else if (vep->endtag) {
				vep_error(vep,
				    "ESI 1.0 </esi:include> illegal end-tag");
				vep->state = VEP_TAGERROR;
			} else {
				vep->dostuff = vep_do_include;
				vep->state = VEP_INTAG;
				vep->attr = vep_match_attr_include;
			}
		} else if (vep->state == VEP_ESIREMOVE) {
			vep->dostuff = vep_do_remove;
			vep->state = VEP_INTAG;
		} else if (vep->state == VEP_ESICOMMENT) {
			if (vep->remove) {
				vep_error(vep,
				    "ESI 1.0 <esi:comment> element"
				    " nested in <esi:remove>");
				vep->state = VEP_TAGERROR;
			} else if (vep->endtag) {
				vep_error(vep,
				    "ESI 1.0 </esi:comment> illegal end-tag");
				vep->state = VEP_TAGERROR;
			} else {
				vep->dostuff = vep_do_comment;
				vep->state = VEP_INTAG;
			}
		} else if (vep->state == VEP_ESIBOGON) {
			vep_error(vep,
			    "ESI 1.0 <esi:bogus> element");
			vep->state = VEP_TAGERROR;

		/******************************************************
		 * SECTION D
		 */

		} else if (vep->state == VEP_INTAG) {
			vep->tag_i = 0;
			while (p < e && vct_islws(*p) && !vep->emptytag) {
				p++;	
				vep->canattr = 1;
			}
			if (p < e && *p == '/' && !vep->emptytag) {
				p++;
				vep->emptytag = 1;
				vep->canattr = 0;
			}
			if (p < e && *p == '>') {
				p++;
				AN(vep->dostuff);
				vep_mark_skip(vep, p);
				vep->dostuff(vep, DO_TAG);
				vep->in_esi_tag = 0;
				vep->state = VEP_NEXTTAG;
			} else if (p < e && vep->emptytag) {
				vep_error(vep,
				    "XML 1.0 '>' does not follow '/' in tag");
				vep->state = VEP_TAGERROR;
			} else if (p < e && vep->canattr &&
			    vct_isxmlnamestart(*p)) {
				vep->state = VEP_ATTR;
			} else if (p < e) {
				vep_error(vep,
				    "XML 1.0 Illegal attribute tart char");
				vep->state = VEP_TAGERROR;
			}
		} else if (vep->state == VEP_TAGERROR) {
			while (p < e && *p != '>')
				p++;
			if (p < e) {
				p++;
				vep_mark_skip(vep, p);
				vep->in_esi_tag = 0;
				vep->state = VEP_NEXTTAG;
			}

		/******************************************************
		 * SECTION E
		 */

		} else if (vep->state == VEP_ATTR) {
			AZ(vep->attr_delim);
			if (vep->attr == NULL) {
				p++;
				AZ(vep->attr_vsb);
				vep->state = VEP_SKIPATTR;
			} else {
				vep->match = vep->attr;
				vep->state = VEP_MATCH;
			}
		} else if (vep->state == VEP_SKIPATTR) {
			while (p < e && vct_isxmlname(*p))
				p++;
			if (p < e && *p == '=') {
				p++;
				vep->state = VEP_ATTRDELIM;
			} else if (p < e && *p == '>') {
				vep->state = VEP_INTAG;
			} else if (p < e && *p == '/') {
				vep->state = VEP_INTAG;
			} else if (p < e && vct_issp(*p)) {
				vep->state = VEP_INTAG;
			} else if (p < e) {
				vep_error(vep,
				    "XML 1.0 Illegal attr char");
				vep->state = VEP_TAGERROR;
			}
		} else if (vep->state == VEP_ATTRGETVAL) {
			vep->attr_vsb = vsb_newauto();
			vep->state = VEP_ATTRDELIM;
		} else if (vep->state == VEP_ATTRDELIM) {
			AZ(vep->attr_delim);
			if (*p == '"' || *p == '\'') {
				vep->attr_delim = *p++;
				vep->state = VEP_ATTRVAL;
			} else if (!vct_issp(*p)) {
				vep->attr_delim = ' ';
				vep->state = VEP_ATTRVAL;
			} else {
				vep_error(vep,
				    "XML 1.0 Illegal attribute delimiter");
				vep->state = VEP_TAGERROR;
			}
			
		} else if (vep->state == VEP_ATTRVAL) {
			while (p < e && *p != '>' && *p != vep->attr_delim &&
			   (vep->attr_delim != ' ' || !vct_issp(*p))) {
				if (vep->attr_vsb != NULL)
					vsb_bcat(vep->attr_vsb, p, 1);
				p++;
			}
			if (p < e && *p == '>') {
				vep_error(vep,
				    "XML 1.0 Missing end attribute delimiter");
				vep->state = VEP_TAGERROR;
				vep->attr_delim = 0;
				if (vep->attr_vsb != NULL) {
					vsb_finish(vep->attr_vsb);
					vsb_delete(vep->attr_vsb);
					vep->attr_vsb = NULL;
				}
			} else if (p < e) {
				vep->attr_delim = 0;
				p++;
				if (vep->attr_vsb != NULL) {
					vsb_finish(vep->attr_vsb);
					AN(vep->dostuff);
					vep->dostuff(vep, DO_ATTR);
					vep->attr_vsb = NULL;
				}
				vep->state = VEP_INTAG;
			}
	

		/******************************************************
		 * Utility Section
		 */

		} else if (vep->state == VEP_MATCH) {
			/*
			 * Match against a table
			 */
			vm = vep_match(vep, p, e);
			vep->match_hit = vm;
			if (vm != NULL) {
				if (vm->match != NULL)
					p += strlen(vm->match);
				vep->state = *vm->state;
				vep->match = NULL;
				vep->tag_i = 0;
			} else {
				memcpy(vep->tag, p, e - p);
				vep->tag_i = e - p;
				vep->state = VEP_MATCHBUF;
				p = e;
			}
		} else if (vep->state == VEP_MATCHBUF) {
			/*
			 * Match against a table while split over input
			 * sections.
			 */
			do {
				if (*p == '>') {
					for (vm = vep->match;
					    vm->match != NULL; vm++)
						continue;
					AZ(vm->match);
				} else {
					vep->tag[vep->tag_i++] = *p++;
					vm = vep_match(vep,
					    vep->tag, vep->tag + vep->tag_i);
					if (vm && vm->match == NULL) {
						vep->tag_i--;
						p--;
					}
				}
			} while (vm == NULL && p < e);
			vep->match_hit = vm;
			if (vm == NULL) {
				assert(p == e);
			} else {
				vep->state = *vm->state;
				vep->match = NULL;
			}
		} else if (vep->state == VEP_UNTIL) {
			/*
			 * Skip until we see magic string
			 */
			while (p < e) {
				if (*p++ != *vep->until_p++) {
					vep->until_p = vep->until;
				} else if (*vep->until_p == '\0') {
					vep->state = vep->until_s;
					break;
				}
			if (p == e && !vep->remove)
				vep_mark_verbatim(vep, p);
			}
		} else {
			Debug("*** Unknown state %s\n", vep->state);
			INCOMPL();
		}
	}
	/*
	 * We must always mark up the storage we got, try to do so
	 * in the most efficient way, in particular with respect to
	 * minimizing and limiting use of pending.
	 */
	if (p == vep->ver_p) 
		;
	else if (vep->in_esi_tag)
		vep_mark_skip(vep, p);
	else if (vep->remove)
		vep_mark_skip(vep, p);
	else
		vep_mark_pending(vep, p);
}

/*---------------------------------------------------------------------
 * We receive a ungzip'ed object, and want to store it ungzip'ed.
 */

static int __match_proto__()
vfp_esi_bytes_uu(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	struct vep_state *vep;
	ssize_t l, w;
	struct storage *st;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vep = sp->wrk->vep;
	CHECK_OBJ_NOTNULL(vep, VEP_MAGIC);

	while (bytes > 0) {
		if (sp->wrk->storage == NULL) {
			l = params->fetch_chunksize * 1024LL;
			sp->wrk->storage = STV_alloc(sp, l);
		}
		if (sp->wrk->storage == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		st = sp->wrk->storage;
		l = st->space - st->len;
		if (l > bytes)
			l = bytes;
		w = HTC_Read(htc, st->ptr + st->len, l);
		if (w <= 0)
			return (w);
		if (vep->hack_p == NULL)
			vep->hack_p = (const char *)st->ptr + st->len;
		vep->ver_p = (const char *)st->ptr + st->len;
		if (params->esi_syntax & 0x8) {
			ssize_t d;
			for (l = 0; l < w; l += d)  {
				d = (random() & 3) + 1;
				if (l + d >= w)
					d = 1;
				vep_parse(vep,
				    (const char *)st->ptr + st->len + l, d);
			}
		} else
			vep_parse(vep, (const char *)st->ptr + st->len, w);
		st->len += w;
		sp->obj->len += w;
		if (st->len == st->space) {
			VTAILQ_INSERT_TAIL(&sp->obj->store,
			    sp->wrk->storage, list);
			sp->wrk->storage = NULL;
			st = NULL;
		}
		bytes -= w;
	}
	return (1);
}

/*---------------------------------------------------------------------*/

static void __match_proto__()
vfp_esi_begin(struct sess *sp, size_t estimate)
{
	struct vep_state *vep;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->wrk->vep);
	/* XXX: snapshot WS ? We'll need the space */
	vep = (void*)WS_Alloc(sp->wrk->ws, sizeof *vep);
	AN(vep);

	Debug("BEGIN %p\n", vep);

	memset(vep, 0, sizeof *vep);
	vep->magic = VEP_MAGIC;
	vep->sp = sp;
	vep->bytes = vfp_esi_bytes_uu;
	vep->state = VEP_START;
	vep->vsb = vsb_newauto();
	AN(vep->vsb);
	vep->crc = crc32(0L, Z_NULL, 0);
	vep->crcp = crc32(0L, Z_NULL, 0);

	sp->wrk->vep = vep;
	(void)estimate;
}

static int __match_proto__()
vfp_esi_bytes(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	struct vep_state *vep;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vep = sp->wrk->vep;
	Debug("BYTES %jd\n", (intmax_t)bytes);
	CHECK_OBJ_NOTNULL(vep, VEP_MAGIC);
	AN(vep->bytes);
	return (vep->bytes(sp, htc, bytes));
}

static int __match_proto__()
vfp_esi_end(struct sess *sp)
{
	struct storage *st;
	struct vep_state *vep;
	ssize_t l;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vep = sp->wrk->vep;
	sp->wrk->vep = NULL;
	Debug("ENDING %p\n", vep);
	CHECK_OBJ_NOTNULL(vep, VEP_MAGIC);

	l = sp->obj->len - vep->o_total;
	assert(l >= 0);
	if (vep->o_pending)
		vep_mark_common(vep, vep->ver_p, vep->last_mark);
	if (vep->o_wait > 0)
		vep_emit_common(vep, &vep->o_wait, vep->last_mark);

	vsb_finish(vep->vsb);
	l = vsb_len(vep->vsb);
	if (vep->state != VEP_NOTXML && l > 0) {
		Debug("ESI %d <%s>\n", (int)l, vsb_data(vep->vsb));

		/* XXX: This is a huge waste of storage... */
		sp->obj->esidata = STV_alloc(sp, l);
		AN(sp->obj->esidata);
		memcpy(sp->obj->esidata->ptr, vsb_data(vep->vsb), l);
		sp->obj->esidata->len = l;
	}
	vsb_delete(vep->vsb);

	st = sp->wrk->storage;
	sp->wrk->storage = NULL;
	if (st == NULL)
		return (0);

	if (st->len == 0) {
		STV_free(st);
		return (0);
	}
	if (st->len < st->space)
		STV_trim(st, st->len);
	VTAILQ_INSERT_TAIL(&sp->obj->store, st, list);
	sp->wrk->vep = NULL;
	return (0);
}

struct vfp vfp_esi = {
        .begin  =       vfp_esi_begin,
        .bytes  =       vfp_esi_bytes,
        .end    =       vfp_esi_end,
};

#endif /* OLD_ESI */

#if 0

digraph xml {
	rankdir="LR"
	size="7,10"
#################################################################
# SECTION A
#

START		[shape=ellipse]
TESTXML		[shape=ellipse]
NOTXML		[shape=ellipse]
NEXTTAGa	[shape=hexagon, label="NEXTTAG"]
STARTTAGa	[shape=hexagon, label="STARTTAG"]
START		-> TESTXML
START		-> NEXTTAGa	[style=dotted, label="syntax:1"]
TESTXML		-> TESTXML	[label="lws"]
TESTXML		-> NOTXML
TESTXML		-> STARTTAGa	[label="'<'"]

#################################################################
# SECTION B

NOTMYTAG	[shape=ellipse]
NEXTTAG		[shape=ellipse]
NOTMYTAG	-> NEXTTAG	[style=dotted, label="syntax:2"]
STARTTAGb	[shape=hexagon, label="STARTTAG"]
NOTMYTAG	-> NEXTTAG	[label="'>'"]
NOTMYTAG	-> NOTMYTAG	[label="*"]
NEXTTAG		-> NEXTTAG	[label="'-->'"]
NEXTTAG		-> NEXTTAG	[label="*"]
NEXTTAG		-> STARTTAGb	[label="'<'"]

#################################################################
# SECTION C

STARTTAG	[shape=ellipse]
COMMENT		[shape=ellipse]
CDATA		[shape=ellipse]
ESITAG		[shape=ellipse]
ESIETAG		[shape=ellipse]
ESIINCLUDE	[shape=ellipse]
ESIREMOVE	[shape=ellipse]
ESICOMMENT	[shape=ellipse]
ESIBOGON	[shape=ellipse]
INTAGc		[shape=hexagon, label="INTAG"]
NOTMYTAGc	[shape=hexagon, label="NOTMYTAG"]
NEXTTAGc	[shape=hexagon, label="NEXTTAG"]
TAGERRORc	[shape=hexagon, label="TAGERROR"]
C1		[shape=circle,label=""]
STARTTAG	-> COMMENT	[label="'<!--'"]
STARTTAG	-> ESITAG	[label="'<esi'"]
STARTTAG	-> CDATA	[label="'<![CDATA['"]
STARTTAG	-> NOTMYTAGc	[label="'*'"]
COMMENT		-> NEXTTAGc	[label="'esi'"]
COMMENT		-> C1		[label="*"]
C1		-> C1		[label="*"]
C1		-> NEXTTAGc	[label="-->"]
CDATA		-> CDATA	[label="*"]
CDATA		-> NEXTTAGc	[label="]]>"]
ESITAG		-> ESIINCLUDE	[label="'include'"]
ESITAG		-> ESIREMOVE	[label="'remove'"]
ESITAG		-> ESICOMMENT	[label="'comment'"]
ESITAG		-> ESIBOGON	[label="*"]
ESICOMMENT	-> INTAGc
ESICOMMENT	-> TAGERRORc
ESICOMMENT	-> TAGERRORc	[style=dotted, label="nested\nin\nremove"]
ESIREMOVE	-> INTAGc
ESIREMOVE	-> TAGERRORc
ESIINCLUDE	-> INTAGc
ESIINCLUDE	-> TAGERRORc
ESIINCLUDE	-> TAGERRORc	[style=dotted, label="nested\nin\nremove"]
ESIBOGON	-> TAGERRORc

#################################################################
# SECTION D

INTAG		[shape=ellipse]
TAGERROR	[shape=ellipse]
NEXTTAGd	[shape=hexagon, label="NEXTTAG"]
ATTRd		[shape=hexagon, label="ATTR"]
D1		[shape=circle, label=""]
D2		[shape=circle, label=""]
INTAG		-> D1		[label="lws"]
D1		-> D2		[label="/"]
INTAG		-> D2		[label="/"]
INTAG		-> NEXTTAGd	[label=">"]
D1		-> NEXTTAGd	[label=">"]
D2		-> NEXTTAGd	[label=">"]
D1		-> ATTRd	[label="XMLstartchar"]
D1		-> TAGERROR	[label="*"]
D2		-> TAGERROR	[label="*"]
TAGERROR	-> TAGERROR	[label="*"]
TAGERROR	-> NEXTTAGd	[label="'>'"]

#################################################################
# SECTION E

ATTR		[shape=ellipse]
SKIPATTR	[shape=ellipse]
ATTRGETVAL	[shape=ellipse]
ATTRDELIM	[shape=ellipse]
ATTRVAL		[shape=ellipse]
TAGERRORe	[shape=hexagon, label="TAGERROR"]
INTAGe		[shape=hexagon, label="INTAG"]
ATTR		-> SKIPATTR	[label="*"]
ATTR		-> ATTRGETVAL	[label="wanted attr"]
SKIPATTR	-> SKIPATTR	[label="XMLname"]
SKIPATTR	-> ATTRDELIM	[label="'='"]
SKIPATTR	-> TAGERRORe	[label="*"]
ATTRGETVAL	-> ATTRDELIM
ATTRDELIM	-> ATTRVAL	[label="\""]
ATTRDELIM	-> ATTRVAL	[label="\'"]
ATTRDELIM	-> ATTRVAL	[label="*"]
ATTRDELIM	-> TAGERRORe	[label="lws"]
ATTRVAL		-> TAGERRORe	[label="'>'"]
ATTRVAL		-> INTAGe	[label="delim"]
ATTRVAL		-> ATTRVAL	[label="*"]

}

#endif
