/*-
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Do Vary processing.
 *
 * When we insert an object into the cache which has a Vary: header,
 * we encode a vary matching string containing the headers mentioned
 * and their value.
 *
 * When we match an object in the cache, we check the present request
 * against the vary matching string.
 *
 * The only kind of header-munging we do is leading & trailing space
 * removal.  All the potential "q=foo" gymnastics is not worth the
 * effort.
 *
 * The vary matching string has the following format:
 *
 * Sequence of: {
 *	<msb>			\   Length of header contents.
 *	<lsb>			/
 *	<length of header + 1>	\
 *	<header>		 \  Same format as argument to http_GetHdr()
 *	':'			 /
 *	'\0'			/
 *      <header>		>   Only present if length != 0xffff
 * }
 *	0xff,			\   Length field
 *	0xff,			/
 *      '\0'			>   Terminator
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"

#include "vct.h"
#include "vend.h"

static unsigned VRY_Validate(const uint8_t *vary);

/**********************************************************************
 * Create a Vary matching string from a Vary header
 *
 * Return value:
 * <0: Parse error
 *  0: No Vary header on object
 * >0: Length of Vary matching string in *psb
 */

int
VRY_Create(struct busyobj *bo, struct vsb **psb)
{
	const char *v, *p, *q, *h, *e;
	struct vsb *sb, *sbh;
	unsigned l;
	int error = 0;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(bo->beresp, HTTP_MAGIC);
	AN(psb);
	AZ(*psb);

	/* No Vary: header, no worries */
	if (!http_GetHdr(bo->beresp, H_Vary, &v))
		return (0);

	/* For vary matching string */
	sb = VSB_new_auto();
	AN(sb);

	/* For header matching strings */
	sbh = VSB_new_auto();
	AN(sbh);

	for (p = v; *p; p++) {

		/* Find next header-name */
		if (vct_issp(*p))
			continue;
		for (q = p; *q && !vct_issp(*q) && *q != ','; q++)
			continue;

		if (q - p > INT8_MAX) {
			VSLb(bo->vsl, SLT_Error,
			    "Vary header name length exceeded");
			error = 1;
			break;
		}

		/* Build a header-matching string out of it */
		VSB_clear(sbh);
		AZ(VSB_printf(sbh, "%c%.*s:%c",
		    (char)(1 + (q - p)), (int)(q - p), p, 0));
		AZ(VSB_finish(sbh));

		if (http_GetHdr(bo->bereq, VSB_data(sbh), &h)) {
			AZ(vct_issp(*h));
			/* Trim trailing space */
			e = strchr(h, '\0');
			while (e > h && vct_issp(e[-1]))
				e--;
			/* Encode two byte length and contents */
			l = e - h;
			if (l > 0xffff - 1) {
				VSLb(bo->vsl, SLT_Error,
				    "Vary header maximum length exceeded");
				error = 1;
				break;
			}
		} else {
			e = h;
			l = 0xffff;
		}
		AZ(VSB_printf(sb, "%c%c", (int)(l >> 8), (int)(l & 0xff)));
		/* Append to vary matching string */
		AZ(VSB_bcat(sb, VSB_data(sbh), VSB_len(sbh)));
		if (e != h)
			AZ(VSB_bcat(sb, h, e - h));

		while (vct_issp(*q))
			q++;
		if (*q == '\0')
			break;
		if (*q != ',') {
			VSLb(bo->vsl, SLT_Error, "Malformed Vary header");
			error = 1;
			break;
		}
		p = q;
	}

	if (error) {
		VSB_destroy(&sbh);
		VSB_destroy(&sb);
		return (-1);
	}

	/* Terminate vary matching string */
	VSB_printf(sb, "%c%c%c", 0xff, 0xff, 0);

	VSB_destroy(&sbh);
	AZ(VSB_finish(sb));
	*psb = sb;
	return (VSB_len(sb));
}

/*
 * Find length of a vary entry
 */
static unsigned
VRY_Len(const uint8_t *p)
{
	unsigned l = vbe16dec(p);

	return (2 + p[2] + 2 + (l == 0xffff ? 0 : l));
}

/*
 * Compare two vary entries
 */
static int
vry_cmp(const uint8_t *v1, const uint8_t *v2)
{
	unsigned retval = 0;

	if (!memcmp(v1, v2, VRY_Len(v1))) {
		/* Same same */
		retval = 0;
	} else if (memcmp(v1 + 2, v2 + 2, v1[2] + 2)) {
		/* Different header */
		retval = 1;
	} else if (cache_param->http_gzip_support &&
	    http_hdr_eq(H_Accept_Encoding, (const char*) v1 + 2)) {
		/*
		 * If we do gzip processing, we do not vary on Accept-Encoding,
		 * because we want everybody to get the gzipped object, and
		 * varnish will gunzip as necessary.  We implement the skip at
		 * check time, rather than create time, so that object in
		 * persistent storage can be used with either setting of
		 * http_gzip_support.
		 */
		retval = 0;
	} else {
		/* Same header, different content */
		retval = 2;
	}
	return (retval);
}

/**********************************************************************
 * Prepare predictive vary string
 */

void
VRY_Prep(struct req *req)
{
	if (req->waitinglist_gen == 0) {
		AZ(req->vary_b);
		AZ(req->vary_e);
		(void)WS_ReserveAll(req->ws);
	} else {
		AN(WS_Reservation(req->ws));
	}
	req->vary_b = WS_Reservation(req->ws);
	req->vary_e = req->vary_b + WS_ReservationSize(req->ws);
	if (req->vary_b + 2 < req->vary_e)
		req->vary_b[2] = '\0';
}

void
VRY_Clear(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (req->vary_b != NULL)
		free(req->vary_b);
	req->vary_b = NULL;
	AZ(req->vary_e);
}

/**********************************************************************
 * Finish predictive vary processing
 */

void
VRY_Finish(struct req *req, enum vry_finish_flag flg)
{
	uint8_t *p = NULL;
	size_t l;

	if (req->vary_b + 2 >= req->vary_e) {
		req->vary_b = NULL;
		req->vary_e = NULL;
		WS_Release(req->ws, 0);
		WS_MarkOverflow(req->ws);
		return;
	}

	l = VRY_Validate(req->vary_b);
	if (flg == KEEP && l > 3) {
		p = malloc(l);
		if (p != NULL)
			memcpy(p, req->vary_b, l);
	}
	WS_Release(req->ws, 0);
	req->vary_e = NULL;
	req->vary_b = p;
}

/**********************************************************************
 * Match vary strings, and build a new cached string if possible.
 *
 * Return zero if there is certainly no match.
 * Return non-zero if there could be a match or if we couldn't tell.
 */

int
VRY_Match(const struct req *req, const uint8_t *vary)
{
	uint8_t *vsp = req->vary_b;
	const char *h, *e;
	unsigned lh, ln;
	int i, oflo = 0;

	AN(vsp);
	AN(vary);
	while (vary[2]) {
		if (vsp + 2 >= req->vary_e) {
			/*
			 * Too little workspace to find out
			 */
			oflo = 1;
			break;
		}
		i = vry_cmp(vary, vsp);
		if (i == 1) {
			/*
			 * Different header, build a new entry,
			 * then compare again with that new entry.
			 */

			ln = 2 + vary[2] + 2;
			i = http_GetHdr(req->http, (const char*)(vary+2), &h);
			if (i) {
				/* Trim trailing space */
				e = strchr(h, '\0');
				while (e > h && vct_issp(e[-1]))
					e--;
				lh = e - h;
				assert(lh < 0xffff);
				ln += lh;
			} else {
				e = h = NULL;
				lh = 0xffff;
			}

			if (vsp + ln + 3 >= req->vary_e) {
				/*
				 * Not enough space to build new entry
				 * and put terminator behind it.
				 */
				oflo = 1;
				break;
			}

			vbe16enc(vsp, (uint16_t)lh);
			memcpy(vsp + 2, vary + 2, vary[2] + 2);
			if (h != NULL)
				memcpy(vsp + 2 + vsp[2] + 2, h, lh);
			vsp[ln++] = 0xff;
			vsp[ln++] = 0xff;
			vsp[ln++] = 0;
			assert(VRY_Validate(vsp) == ln);

			i = vry_cmp(vary, vsp);
			assert(i == 0 || i == 2);
		}
		if (i == 0) {
			/* Same header, same contents */
			vsp += VRY_Len(vsp);
			vary += VRY_Len(vary);
		} else if (i == 2) {
			/* Same header, different contents, cannot match */
			return (0);
		}
	}
	if (oflo) {
		vsp = req->vary_b;
		if (vsp + 2 < req->vary_e) {
			vsp[0] = 0xff;
			vsp[1] = 0xff;
			vsp[2] = 0;
		}
		return (0);
	} else {
		return (1);
	}
}

/*
 * Check the validity of a Vary string and return its total length
 */

static unsigned
VRY_Validate(const uint8_t *vary)
{
	unsigned l, retval = 0;

	while (vary[2] != 0) {
		assert(strlen((const char*)vary + 3) == vary[2]);
		l = VRY_Len(vary);
		retval += l;
		vary += l;
	}
	return (retval + 3);
}
