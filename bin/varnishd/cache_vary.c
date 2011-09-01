/*-
 * Copyright (c) 2006-2011 Varnish Software AS
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
 *      '\0'
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include "cache.h"
#include "vend.h"
#include "vct.h"

struct vsb *
VRY_Create(const struct sess *sp, const struct http *hp)
{
	char *v, *p, *q, *h, *e;
	struct vsb *sb, *sbh;
	int l;

	/* No Vary: header, no worries */
	if (!http_GetHdr(hp, H_Vary, &v))
		return (NULL);

	/* For vary matching string */
	sb = VSB_new_auto();
	AN(sb);

	/* For header matching strings */
	sbh = VSB_new_auto();
	AN(sbh);

	if (*v == ':') {
		WSP(sp, SLT_Error, "Vary header had extra ':', fix backend");
		v++;
	}
	for (p = v; *p; p++) {

		/* Find next header-name */
		if (vct_issp(*p))
			continue;
		for (q = p; *q && !vct_issp(*q) && *q != ','; q++)
			continue;

		/* Build a header-matching string out of it */
		VSB_clear(sbh);
		VSB_printf(sbh, "%c%.*s:%c",
		    (char)(1 + (q - p)), (int)(q - p), p, 0);
		AZ(VSB_finish(sbh));

		if (http_GetHdr(sp->http, VSB_data(sbh), &h)) {
			AZ(vct_issp(*h));
			/* Trim trailing space */
			e = strchr(h, '\0');
			while (e > h && vct_issp(e[-1]))
				e--;
			/* Encode two byte length and contents */
			l = e - h;
			assert(!(l & ~0xffff));
		} else {
			e = h;
			l = 0xffff;
		}
		VSB_printf(sb, "%c%c", (unsigned)l >> 8, l & 0xff);
		/* Append to vary matching string */
		VSB_bcat(sb, VSB_data(sbh), VSB_len(sbh));
		if (e != h)
			VSB_bcat(sb, h, e - h);

		while (vct_issp(*q))
			q++;
		if (*q == '\0')
			break;
		xxxassert(*q == ',');
		p = q;
	}
	/* Terminate vary matching string */
	VSB_printf(sb, "%c%c%c", 0xff, 0xff, 0);

	VSB_delete(sbh);
	AZ(VSB_finish(sb));
	return(sb);
}

/*
 * Find length of a vary entry
 */
static unsigned
vry_len(const uint8_t *p)
{
	unsigned l = vbe16dec(p);

	return (2 + p[2] + 2 + (l == 0xffff ? 0 : l));
}

/*
 * Compare two vary entries
 */
static int
vry_cmp(const uint8_t * const *v1, uint8_t * const *v2)
{
	unsigned retval = 0;

	if (!memcmp(*v1, *v2, vry_len(*v1))) {
		/* Same same */
		retval = 0;
	} else if (memcmp((*v1) + 2, (*v2) + 2, (*v1)[2] + 2)) {
		/* Different header */
		retval = 1;
	} else if (params->http_gzip_support &&
	    !strcasecmp(H_Accept_Encoding, (const char*)((*v1)+2))) {
		/*
		 * If we do gzip processing, we do not vary on Accept-Encoding,
		 * because we want everybody to get the gzip'ed object, and
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

int
VRY_Match(struct sess *sp, const uint8_t *vary)
{
	uint8_t *vsp = sp->vary_b;
	char *h, *e;
	unsigned lh, ln;
	int i, retval = 1, oflo = 0;

	AN(vsp);
	while (vary[2]) {
		i = vry_cmp(&vary, &vsp);
		if (i == 1) {
			/* Build a new entry */

			i = http_GetHdr(sp->http, (const char*)(vary+2), &h);
			if (i) {
				/* Trim trailing space */
				e = strchr(h, '\0');
				while (e > h && vct_issp(e[-1]))
					e--;
				lh = e - h;
				assert(lh < 0xffff);
			} else {
				e = h = NULL;
				lh = 0xffff;
			}

			/* Length of the entire new vary entry */
			ln = 2 + vary[2] + 2 + (lh == 0xffff ? 0 : lh);
			if (vsp + ln >= sp->vary_e) {
				vsp = sp->vary_b;
				oflo = 1;
			}

			/*
			 * We MUST have space for one entry and the end marker
			 * after it, which prevents old junk from confusing us
			 */
			assert(vsp + ln + 2 < sp->vary_e);

			vbe16enc(vsp, (uint16_t)lh);
			memcpy(vsp + 2, vary + 2, vary[2] + 2);
			if (h != NULL && e != NULL) {
				memcpy(vsp + 2 + vsp[2] + 2, h, e - h);
				vsp[2 + vary[2] + 2 + (e - h) + 2] = '\0';
			} else
				vsp[2 + vary[2] + 2 + 2] = '\0';

			i = vry_cmp(&vary, &vsp);
			assert(i != 1);	/* hdr must be the same now */
		}
		if (i != 0)
			retval = 0;
		vsp += vry_len(vsp);
		vary += vry_len(vary);
	}
	if (vsp + 3 > sp->vary_e)
		oflo = 1;

	if (oflo) {
		/* XXX: Should log this */
		vsp = sp->vary_b;
	}
	vsp[0] = 0xff;
	vsp[1] = 0xff;
	vsp[2] = 0;
	if (oflo)
		sp->vary_l = NULL;
	else
		sp->vary_l = vsp + 3;
	return (retval);
}

void
VRY_Validate(const uint8_t *vary)
{

	while (vary[2] != 0) {
		assert(strlen((const char*)vary+3) == vary[2]);
		vary += vry_len(vary);
	}
}
