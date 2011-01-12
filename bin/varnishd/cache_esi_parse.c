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

#include "cache.h"
#include "cache_esi.h"
#include "vend.h"
#include "vct.h"
#include "stevedore.h"

#include <stdio.h>

#ifndef OLD_ESI

struct vep_match {
	const char	*match;
	const char	**state;
};

struct vep_state {
	unsigned		magic;
#define VEP_MAGIC		0x55cb9b82
	vfp_bytes_f		*bytes;
	struct vsb		*vsb;

	/* parser state */
	const char		*state;

	unsigned		remove;
	unsigned		skip;

	unsigned		o_verbatim;
	unsigned		o_skip;

	const char		*ver_p;

	const char		*until;
	const char		*until_p;
	const char		*until_s;

	struct vep_match	*match;
	int			match_l;

	char			tag[10];
	int			tag_i;
	
};

/*---------------------------------------------------------------------*/

static const char *VEP_START =		"[Start]";
static const char *VEP_NOTXML =	 	"[NotXml]";
static const char *VEP_STARTTAG = 	"[StartTag]";
static const char *VEP_ENDTAG = 	"[EndTag]";
static const char *VEP_MATCHBUF = 	"[MatchBuf]";
static const char *VEP_NEXTTAG = 	"[NxtTag]";
static const char *VEP_NOTMYTAG =	"[NotMyTag]";
static const char *VEP_ESICMT =		"[EsiComment]";
static const char *VEP_CMT =		"[Comment]";
static const char *VEP_CDATA =		"[CDATA]";
static const char *VEP_ESITAG =		"[ESITag]";
static const char *VEP_ESIETAG =	"[ESIEndTag]";
static const char *VEP_UNTIL =		"[Until]";
static const char *VEP_ESIREMOVE =	"[ESI:Remove]";
static const char *VEP_ESI_REMOVE =	"[ESI:/Remove]";
static const char *VEP_ESIINCLUDE =	"[ESI:Include]";
static const char *VEP_ESICOMMENT =	"[ESI:Comment]";
static const char *VEP_MATCH =		"[Match]";
static const char *VEP_XXX =		"[XXX]";

/*---------------------------------------------------------------------*/

static struct vep_match vep_match_tbl[] = {
	{ "<!--esi",	&VEP_ESICMT },
	{ "<!--",	&VEP_CMT },
	{ "</esi:",	&VEP_ESIETAG },
	{ "<esi:",	&VEP_ESITAG },
	{ "<![CDATA[",	&VEP_CDATA },
	{ NULL,		&VEP_NOTMYTAG }
};

static const int vep_match_tbl_len =
    sizeof vep_match_tbl / sizeof vep_match_tbl[0];

static struct vep_match vep_match_esi[] = {
	{ "include",	&VEP_ESIINCLUDE },
	{ "remove",	&VEP_ESIREMOVE },
	{ "comment",	&VEP_ESICOMMENT },
	{ NULL,		&VEP_XXX }
};

static const int vep_match_esi_len =
    sizeof vep_match_esi / sizeof vep_match_esi[0];

static struct vep_match vep_match_esie[] = {
	{ "remove",	&VEP_ESI_REMOVE },
	{ NULL,		&VEP_XXX }
};

static const int vep_match_esie_len =
    sizeof vep_match_esie / sizeof vep_match_esie[0];

/*---------------------------------------------------------------------
 * return match or NULL if more input needed.
 */
static struct vep_match *
vep_match(struct vep_state *vep, const char *b, const char *e)
{
	struct vep_match *vm;
	const char *q, *r;

	for (vm = vep->match; vm->match; vm++) {
		r = b;
		for (q = vm->match; *q && r < e; q++, r++)
			if (*q != *r)
				break;
		if (*q != '\0' && r == e) {
			if (b != vep->tag) {
				assert(e - b < sizeof vep->tag);
				memcpy(vep->tag, b, e - b);
				vep->tag_i = e - b;
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
vep_emit_len(struct vep_state *vep, ssize_t l, int m8, int m16, int m32)
{
	uint8_t buf[5];

	assert(l > 0);
	if (l < 256) {
		buf[0] = m8;
		buf[1] = (uint8_t)l;
		vsb_bcat(vep->vsb, buf, 2);
	} else if (l < 65536) {
		buf[0] = m16;
		vbe16enc(buf + 1, (uint16_t)l);
		vsb_bcat(vep->vsb, buf, 3);
	} else {
		/* XXX assert < 2^32 */
		buf[0] = m32;
		vbe32enc(buf + 1, (uint32_t)l);
		vsb_bcat(vep->vsb, buf, 5);
	}
} 

static void
vep_emit_skip(struct vep_state *vep)
{
	ssize_t l;

	l = vep->o_skip;
	vep->o_skip = 0;
	assert(l > 0);
	vep_emit_len(vep, l, VEC_S1, VEC_S2, VEC_S4);
} 

static void
vep_emit_verbatim(struct vep_state *vep)
{
	ssize_t l;

	l = vep->o_verbatim;
	vep->o_verbatim = 0;
	assert(l > 0);
	vep_emit_len(vep, l, VEC_V1, VEC_V2, VEC_V4);
	vsb_printf(vep->vsb, "%lx\r\n%c", l, 0);
} 

static void
vep_mark_verbatim(struct vep_state *vep, const char *p)
{
	ssize_t l;

	AN(vep->ver_p);
	l = p - vep->ver_p;
	if (l == 0)
		return;
	if (vep->o_skip > 0) 
		vep_emit_skip(vep);
	AZ(vep->o_skip);
	printf("-->V(%d) [%.*s]\n", (int)l, (int)l, vep->ver_p);
	vep->o_verbatim += l;
	vep->ver_p = p;
} 

static void
vep_mark_skip(struct vep_state *vep, const char *p)
{
	ssize_t l;

	AN(vep->ver_p);
	l = p - vep->ver_p;
	if (l == 0)
		return;
	if (vep->o_verbatim > 0) 
		vep_emit_verbatim(vep);
	AZ(vep->o_verbatim);
	printf("-->S(%d) [%.*s]\n", (int)l, (int)l, vep->ver_p);
	vep->o_skip += l;
	vep->ver_p = p;
} 

static void
vep_emit_literal(struct vep_state *vep, const char *p, const char *e)
{
	ssize_t l;

	if (vep->o_verbatim > 0) 
		vep_emit_verbatim(vep);
	if (vep->o_skip > 0) 
		vep_emit_skip(vep);
	l = e - p;
	printf("---->L(%d) [%.*s]\n", (int)l, (int)l, p);
	vep_emit_len(vep, l, VEC_L1, VEC_L2, VEC_L4);
	vsb_printf(vep->vsb, "%lx\r\n%c", l, 0);
	vsb_bcat(vep->vsb, p, l);
}

/*---------------------------------------------------------------------
 * Parse object for ESI instructions
 */

static void
vep_parse(struct vep_state *vep, const char *b, size_t l)
{
	const char *e, *p;
	struct vep_match *vm;

	CHECK_OBJ_NOTNULL(vep, VEP_MAGIC);
	assert(l > 0);

	e = b + l;

	vep->ver_p = b;
	printf("EP Call %d [%.*s]\n", (int)l, (int)l, b);
	p = b;
	while (p < e) {
		AN(vep->state);
		printf("EP %s [%.*s]\n",
		    vep->state,
		    (int)(e - p), p);
		if (vep->state == VEP_START) {
			/*
			 * Look for the first non-white char, and
			 * abandon if it is not '<' under the assumption
			 * that it is not an ESI file
			 */
			while (p < e && vct_islws(*p))
				p++;
			if (p < e) {
				if (*p == '<')
					vep->state = VEP_STARTTAG;
				else
					vep->state = VEP_NOTXML;
			}
		} else if (vep->state == VEP_NOTXML) {
			p = e;
		} else if (vep->state == VEP_NEXTTAG) {
			/*
			 * Hunt for start of next tag
			 */
			while (p < e && *p != '<') {
#if 0
				if (vep->incmt != NULL &&
				    *p == *vep->incmt_p) {
					if (*++vep->incmt_p == '\0') {
						vep->incmt = NULL;
						vep->incmt = NULL;
					}
				} else
					vep->incmt_p = vep->incmt;
#endif
				p++;
			}
			if (p < e)
				vep->state = VEP_STARTTAG;
		} else if (vep->state == VEP_STARTTAG) {
			/*
			 * Start of tag, set up match table
			 */
			assert(*p == '<');
			if (!vep->remove)
				vep_mark_verbatim(vep, p);
			vep->match = vep_match_tbl;
			vep->match_l = vep_match_tbl_len;
			vep->state = VEP_MATCH;
			vep->skip = 1;
		} else if (vep->state == VEP_ENDTAG) {
			while (p < e && *p != '>') 
				p++;
			if (*p == '>') {
				p++;
				vep->state = VEP_NEXTTAG;
			}
			vep_mark_skip(vep, p);
			vep->skip = 0;
		} else if (vep->state == VEP_CDATA) {
			vep->until_p = vep->until = "]]>";
			vep->until_s = VEP_NEXTTAG;
			vep->state = VEP_UNTIL;
		} else if (vep->state == VEP_MATCH) {
			/*
			 * Match against a table
			 */
			vm = vep_match(vep, p, e);
			if (vm != NULL) {
				if (vm->match != NULL)
					p += strlen(vm->match);
				b = p;
				vep->state = *vm->state;
				vep->match = NULL;
				vep->tag_i = 0;
			} else {
				memcpy(vep->tag, p, e - p);
				vep->tag_i = e - p;
				vep->state = VEP_MATCHBUF;
				p = e;
				break;
			}
		} else if (vep->state == VEP_MATCHBUF) {
			/*
			 * Match against a table while split over input
			 * sections.
			 */
			do {
				if (*p == '>') {
					vm = NULL;
				} else {
					vep->tag[vep->tag_i++] = *p++;
					vm = vep_match(vep,
					    vep->tag, vep->tag + vep->tag_i);
				}
			} while (vm == 0 && p < e);
			if (vm == 0) {
				b = e;
				break;
			}
			if (vm->match && vep->tag_i > strlen(vm->match)) {
				/*
				 * not generally safe but
				 * works for the required
				 * case of <--esi and <--
				 */
				p -= vep->tag_i -
				    strlen(vm->match);
				vep->tag_i--;
			}
			if (vm->match == NULL) {
				vep_emit_literal(vep,
				    vep->tag, vep->tag + vep->tag_i);
			}
			b = p;
			vep->state = *vm->state;
			vep->match = NULL;
			vep->tag_i = 0;
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
			}
		} else if (vep->state == VEP_NOTMYTAG) {
			vep->skip = 0;
			while (p < e) {
				if (*p++ == '>') {
					vep->state = VEP_NEXTTAG;
					break;
				}
			}
		} else if (vep->state == VEP_ESITAG) {
			vep->skip = 1;
			vep_mark_skip(vep, p);
			vep->match = vep_match_esi;
			vep->match_l = vep_match_esi_len;
			vep->state = VEP_MATCH;
		} else if (vep->state == VEP_ESIETAG) {
			vep->match = vep_match_esie;
			vep->match_l = vep_match_esie_len;
			vep->state = VEP_MATCH;
		} else if (vep->state == VEP_ESIREMOVE) {
			vep_mark_skip(vep, p);
			vep->remove = 1;
			vep->state = VEP_NEXTTAG;
		} else if (vep->state == VEP_ESI_REMOVE) {
			vep->remove = 0;
			vep->state = VEP_ENDTAG;
		} else {
			printf("*** Unknown state %s\n", vep->state);
			break;
		}
	}
	if (vep->remove || vep->skip)
		vep_mark_skip(vep, p);
	else
		vep_mark_verbatim(vep, p);
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
#if 1
		{
		for (l = 0; l < w; l++) 
			vep_parse(vep, (const char *)st->ptr + st->len + l, 1);
		}
#else
		vep_parse(vep, (const char *)st->ptr + st->len, w);
#endif
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
	vep = (void*)WS_Alloc(sp->wrk->ws, sizeof *vep);
	AN(vep);


	memset(vep, 0, sizeof *vep);
	vep->magic = VEP_MAGIC;
	vep->bytes = vfp_esi_bytes_uu;
	vep->vsb = vsb_newauto();
	vep->state = VEP_START;
	AN(vep->vsb);

	sp->wrk->vep = vep;
	(void)estimate;
}

static int __match_proto__()
vfp_esi_bytes(struct sess *sp, struct http_conn *htc, size_t bytes)
{
	struct vep_state *vep;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vep = sp->wrk->vep;
	CHECK_OBJ_NOTNULL(vep, VEP_MAGIC);
	AN(vep->bytes);
	return (vep->bytes(sp, htc, bytes));
}

static int __match_proto__()
vfp_esi_end(struct sess *sp)
{
	struct storage *st;
	struct vep_state *vep;
	size_t l;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	vep = sp->wrk->vep;
	CHECK_OBJ_NOTNULL(vep, VEP_MAGIC);

	if (vep->o_verbatim)
		vep_emit_verbatim(vep);
	if (vep->o_skip)
		vep_emit_skip(vep);
	vsb_finish(vep->vsb);
	l = vsb_len(vep->vsb);
	if (vep->state != VEP_NOTXML && l != 0) {
		printf("ESI %d <%s>\n", (int)l, vsb_data(vep->vsb));

		/* XXX: This is a huge waste of storage... */
		sp->obj->esidata = STV_alloc(sp, vsb_len(vep->vsb));
		AN(sp->obj->esidata);
		memcpy(sp->obj->esidata->ptr,
		    vsb_data(vep->vsb), vsb_len(vep->vsb));
		sp->obj->esidata->len = vsb_len(vep->vsb);
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
	return (0);
}

struct vfp vfp_esi = {
        .begin  =       vfp_esi_begin,
        .bytes  =       vfp_esi_bytes,
        .end    =       vfp_esi_end,
};

#endif /* OLD_ESI */
