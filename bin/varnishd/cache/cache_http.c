/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2017 Varnish Software AS
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
 * HTTP request storage and manipulation
 */

#include "config.h"

#include "cache_varnishd.h"
#include <stdio.h>
#include <stdlib.h>

#include "common/heritage.h"

#include "vct.h"
#include "vend.h"
#include "vnum.h"
#include "vtim.h"

#define BODYSTATUS(U, l, n, a, k)				\
	const struct body_status BS_##U[1] = {{			\
		.name = #l,					\
		.nbr = n,					\
		.avail = a,					\
		.length_known = k				\
	}};
#include "tbl/body_status.h"


#define HTTPH(a, b, c) char b[] = "*" a ":";
#include "tbl/http_headers.h"

const char H__Status[]	= "\010:status:";
const char H__Proto[]	= "\007:proto:";
const char H__Reason[]	= "\010:reason:";

static char * via_hdr;

/*--------------------------------------------------------------------
 * Perfect hash to rapidly recognize headers from tbl/http_headers.h
 * which have non-zero flags.
 *
 * A suitable algorithm can be found with `gperf`:
 *
 *	tr '" ,' '   ' < include/tbl/http_headers.h |
 *		awk '$1 == "H(" {print $2}' |
 *		gperf --ignore-case
 *
 */

#define GPERF_MIN_WORD_LENGTH 2
#define GPERF_MAX_WORD_LENGTH 19
#define GPERF_MAX_HASH_VALUE 79

static const unsigned char http_asso_values[256] = {
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80,  0, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80,  5, 80, 20,  0,  0,
	5, 10,  5,  5, 80,  0, 15,  0, 20, 80,
	40, 80,  0, 35, 10, 20, 55, 45,  0,  0,
	80, 80, 80, 80, 80, 80, 80,  5, 80, 20,
	0,  0,  5, 10,  5,  5, 80,  0, 15,  0,
	20, 80, 40, 80,  0, 35, 10, 20, 55, 45,
	0,  0, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 80
};

static struct http_hdrflg {
	char		*hdr;
	unsigned	flag;
} http_hdrflg[GPERF_MAX_HASH_VALUE + 1] = {
	{ NULL }, { NULL }, { NULL }, { NULL },
	{ H_Date },
	{ H_Range },
	{ NULL },
	{ H_Referer },
	{ H_Age },
	{ H_From },
	{ H_Keep_Alive },
	{ H_Retry_After },
	{ H_TE },
	{ H_If_Range },
	{ H_ETag },
	{ H_X_Forwarded_For },
	{ H_Expect },
	{ H_Trailer },
	{ H_If_Match },
	{ H_Host },
	{ H_Accept_Language },
	{ H_Accept },
	{ H_If_Modified_Since },
	{ H_If_None_Match },
	{ H_If_Unmodified_Since },
	{ NULL },
	{ H_Cookie },
	{ H_Upgrade },
	{ H_Last_Modified },
	{ H_Accept_Charset },
	{ H_Accept_Encoding },
	{ H_Content_MD5 },
	{ H_Content_Type },
	{ H_Content_Range },
	{ NULL }, { NULL },
	{ H_Content_Language },
	{ H_Transfer_Encoding },
	{ H_Authorization },
	{ H_Content_Length },
	{ H_User_Agent },
	{ H_Server },
	{ H_Expires },
	{ H_Location },
	{ NULL },
	{ H_Set_Cookie },
	{ H_Content_Encoding },
	{ H_Max_Forwards },
	{ H_Cache_Control },
	{ NULL },
	{ H_Connection },
	{ H_Pragma },
	{ NULL },
	{ H_Accept_Ranges },
	{ H_HTTP2_Settings },
	{ H_Allow },
	{ H_Content_Location },
	{ NULL },
	{ H_Proxy_Authenticate },
	{ H_Vary },
	{ NULL },
	{ H_WWW_Authenticate },
	{ H_Warning },
	{ H_Via },
	{ NULL }, { NULL }, { NULL }, { NULL },
	{ NULL }, { NULL }, { NULL }, { NULL },
	{ NULL }, { NULL }, { NULL }, { NULL },
	{ NULL }, { NULL }, { NULL },
	{ H_Proxy_Authorization }
};

static struct http_hdrflg *
http_hdr_flags(const char *b, const char *e)
{
	unsigned u;
	struct http_hdrflg *retval;

	if (b == NULL || e == NULL)
		return (NULL);
	assert(b <= e);
	u = (unsigned)(e - b);
	assert(b + u == e);
	if (u < GPERF_MIN_WORD_LENGTH || u > GPERF_MAX_WORD_LENGTH)
		return (NULL);
	u += http_asso_values[(uint8_t)(e[-1])] +
	     http_asso_values[(uint8_t)(b[0])];
	if (u > GPERF_MAX_HASH_VALUE)
		return (NULL);
	retval = &http_hdrflg[u];
	if (retval->hdr == NULL)
		return (NULL);
	if (!http_hdr_at(retval->hdr + 1, b, e - b))
		return (NULL);
	return (retval);
}

/*--------------------------------------------------------------------*/

static void
http_init_hdr(char *hdr, int flg)
{
	struct http_hdrflg *f;

	hdr[0] = strlen(hdr + 1);
	f = http_hdr_flags(hdr + 1, hdr + hdr[0]);
	AN(f);
	assert(f->hdr == hdr);
	f->flag = flg;
}

void
HTTP_Init(void)
{
	struct vsb *vsb;

#define HTTPH(a, b, c) http_init_hdr(b, c);
#include "tbl/http_headers.h"

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "1.1 %s (Varnish/" PACKAGE_BRANCH ")",
	    heritage.identity);
	AZ(VSB_finish(vsb));
	REPLACE(via_hdr, VSB_data(vsb));
	VSB_destroy(&vsb);
}

/*--------------------------------------------------------------------
 * These two functions are in an incestuous relationship with the
 * order of macros in include/tbl/vsl_tags_http.h
 *
 * The http->logtag is the SLT_*Method enum, and we add to that, to
 * get the SLT_ to use.
 */

static void
http_VSLH(const struct http *hp, unsigned hdr)
{
	int i;

	if (hp->vsl != NULL) {
		assert(VXID_TAG(hp->vsl->wid));
		i = hdr;
		if (i > HTTP_HDR_FIRST)
			i = HTTP_HDR_FIRST;
		i += hp->logtag;
		VSLbt(hp->vsl, (enum VSL_tag_e)i, hp->hd[hdr]);
	}
}

static void
http_VSLH_del(const struct http *hp, unsigned hdr)
{
	int i;

	if (hp->vsl != NULL) {
		/* We don't support unsetting stuff in the first line */
		assert (hdr >= HTTP_HDR_FIRST);
		assert(VXID_TAG(hp->vsl->wid));
		i = (HTTP_HDR_UNSET - HTTP_HDR_METHOD);
		i += hp->logtag;
		VSLbt(hp->vsl, (enum VSL_tag_e)i, hp->hd[hdr]);
	}
}

/*--------------------------------------------------------------------*/

void
http_VSL_log(const struct http *hp)
{
	unsigned u;

	for (u = 0; u < hp->nhd; u++)
		if (hp->hd[u].b != NULL)
			http_VSLH(hp, u);
}

/*--------------------------------------------------------------------*/

static void
http_fail(const struct http *hp)
{
	char id[WS_ID_SIZE];

	VSC_C_main->losthdr++;
	WS_Id(hp->ws, id);
	VSLb(hp->vsl, SLT_Error, "out of workspace (%s)", id);
	WS_MarkOverflow(hp->ws);
}

/*--------------------------------------------------------------------
 * List of canonical HTTP response code names from RFC2616
 */

static struct http_msg {
	unsigned	nbr;
	const char	*status;
	const char	*txt;
} http_msg[] = {
#define HTTP_RESP(n, t)	{ n, #n, t},
#include "tbl/http_response.h"
	{ 0, "0", NULL }
};

const char *
http_Status2Reason(unsigned status, const char **sstr)
{
	struct http_msg *mp;

	status %= 1000;
	assert(status >= 100);
	for (mp = http_msg; mp->nbr != 0 && mp->nbr <= status; mp++)
		if (mp->nbr == status) {
			if (sstr)
				*sstr = mp->status;
			return (mp->txt);
		}
	return ("Unknown HTTP Status");
}

/*--------------------------------------------------------------------*/

unsigned
HTTP_estimate(unsigned nhttp)
{

	/* XXX: We trust the structs to size-aligned as necessary */
	return (PRNDUP(sizeof(struct http) + sizeof(txt) * nhttp + nhttp));
}

struct http *
HTTP_create(void *p, uint16_t nhttp, unsigned len)
{
	struct http *hp;

	hp = p;
	hp->magic = HTTP_MAGIC;
	hp->hd = (void*)(hp + 1);
	hp->shd = nhttp;
	hp->hdf = (void*)(hp->hd + nhttp);
	assert((unsigned char*)p + len == hp->hdf + PRNDUP(nhttp));
	return (hp);
}

/*--------------------------------------------------------------------*/

void
HTTP_Setup(struct http *hp, struct ws *ws, struct vsl_log *vsl,
    enum VSL_tag_e  whence)
{
	http_Teardown(hp);
	hp->nhd = HTTP_HDR_FIRST;
	hp->logtag = whence;
	hp->ws = ws;
	hp->vsl = vsl;
}

/*--------------------------------------------------------------------
 * http_Teardown() is a safety feature, we use it to zap all http
 * structs once we're done with them, to minimize the risk that
 * old stale pointers exist to no longer valid stuff.
 */

void
http_Teardown(struct http *hp)
{

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	AN(hp->shd);
	memset(&hp->nhd, 0, sizeof *hp - offsetof(struct http, nhd));
	memset(hp->hd, 0, sizeof *hp->hd * hp->shd);
	memset(hp->hdf, 0, sizeof *hp->hdf * hp->shd);
}

/*--------------------------------------------------------------------
 * Duplicate the http content into another http
 * We cannot just memcpy the struct because the hd & hdf are private
 * storage to the struct http.
 */

void
HTTP_Dup(struct http *to, const struct http * fm)
{

	assert(fm->nhd <= to->shd);
	memcpy(to->hd, fm->hd, fm->nhd * sizeof *to->hd);
	memcpy(to->hdf, fm->hdf, fm->nhd * sizeof *to->hdf);
	to->nhd = fm->nhd;
	to->logtag = fm->logtag;
	to->status = fm->status;
	to->protover = fm->protover;
}


/*--------------------------------------------------------------------
 * Clone the entire http structure, including vsl & ws
 */

void
HTTP_Clone(struct http *to, const struct http * const fm)
{

	HTTP_Dup(to, fm);
	to->vsl = fm->vsl;
	to->ws = fm->ws;
}

/*--------------------------------------------------------------------*/

void
http_Proto(struct http *to)
{
	const char *fm;

	fm = to->hd[HTTP_HDR_PROTO].b;

	if (fm != NULL &&
	    (fm[0] == 'H' || fm[0] == 'h') &&
	    (fm[1] == 'T' || fm[1] == 't') &&
	    (fm[2] == 'T' || fm[2] == 't') &&
	    (fm[3] == 'P' || fm[3] == 'p') &&
	    fm[4] == '/' &&
	    vct_isdigit(fm[5]) &&
	    fm[6] == '.' &&
	    vct_isdigit(fm[7]) &&
	    fm[8] == '\0') {
		to->protover = 10 * (fm[5] - '0') + (fm[7] - '0');
	} else {
		to->protover = 0;
	}
}

/*--------------------------------------------------------------------*/

void
http_SetH(struct http *to, unsigned n, const char *header)
{

	assert(n < to->nhd);
	AN(header);
	to->hd[n].b = TRUST_ME(header);
	to->hd[n].e = strchr(to->hd[n].b, '\0');
	to->hdf[n] = 0;
	http_VSLH(to, n);
	if (n == HTTP_HDR_PROTO)
		http_Proto(to);
}

/*--------------------------------------------------------------------*/

static void
http_PutField(struct http *to, int field, const char *string)
{
	const char *p;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	p = WS_Copy(to->ws, string, -1);
	if (p == NULL) {
		http_fail(to);
		VSLbs(to->vsl, SLT_LostHeader, TOSTRAND(string));
		return;
	}
	http_SetH(to, field, p);
}

/*--------------------------------------------------------------------*/

int
http_IsHdr(const txt *hh, hdr_t hdr)
{
	unsigned l;

	Tcheck(*hh);
	AN(hdr);
	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	return (http_hdr_at(hdr, hh->b, l));
}

/*--------------------------------------------------------------------*/

static unsigned
http_findhdr(const struct http *hp, unsigned l, const char *hdr)
{
	unsigned u;

	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		Tcheck(hp->hd[u]);
		if (hp->hd[u].e < hp->hd[u].b + l + 1)
			continue;
		if (hp->hd[u].b[l] != ':')
			continue;
		if (!http_hdr_at(hdr, hp->hd[u].b, l))
			continue;
		return (u);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Count how many instances we have of this header
 */

unsigned
http_CountHdr(const struct http *hp, hdr_t hdr)
{
	unsigned retval = 0;
	unsigned u;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		Tcheck(hp->hd[u]);
		if (http_IsHdr(&hp->hd[u], hdr))
			retval++;
	}
	return (retval);
}

/*--------------------------------------------------------------------
 * This function collapses multiple header lines of the same name.
 * The lines are joined with a comma, according to [rfc2616, 4.2bot, p32]
 */

void
http_CollectHdr(struct http *hp, hdr_t hdr)
{

	http_CollectHdrSep(hp, hdr, NULL);
}

/*--------------------------------------------------------------------
 * You may prefer to collapse header fields using a different separator.
 * For Cookie headers, the separator is "; " for example. That's probably
 * the only example too.
 */

void
http_CollectHdrSep(struct http *hp, hdr_t hdr, const char *sep)
{
	unsigned u, l, lsep, ml, f, x, d;
	char *b = NULL, *e = NULL;
	const char *v;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (WS_Overflowed(hp->ws))
		return;

	if (sep == NULL || *sep == '\0')
		sep = ", ";
	lsep = strlen(sep);

	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	f = http_findhdr(hp, l - 1, hdr + 1);
	if (f == 0)
		return;

	for (d = u = f + 1; u < hp->nhd; u++) {
		Tcheck(hp->hd[u]);
		if (!http_IsHdr(&hp->hd[u], hdr)) {
			if (d != u) {
				hp->hd[d] = hp->hd[u];
				hp->hdf[d] = hp->hdf[u];
			}
			d++;
			continue;
		}
		if (b == NULL) {
			/* Found second header, start our collection */
			ml = WS_ReserveAll(hp->ws);
			b = WS_Reservation(hp->ws);
			e = b + ml;
			x = Tlen(hp->hd[f]);
			if (b + x >= e) {
				http_fail(hp);
				VSLbs(hp->vsl, SLT_LostHeader,
				    TOSTRAND(hdr + 1));
				WS_Release(hp->ws, 0);
				return;
			}
			memcpy(b, hp->hd[f].b, x);
			b += x;
		}

		AN(b);
		AN(e);

		/* Append the Nth header we found */
		x = Tlen(hp->hd[u]) - l;

		v = hp->hd[u].b + *hdr;
		while (vct_issp(*v)) {
			v++;
			x--;
		}

		if (b + lsep + x >= e) {
			http_fail(hp);
			VSLbs(hp->vsl, SLT_LostHeader, TOSTRAND(hdr + 1));
			WS_Release(hp->ws, 0);
			return;
		}
		memcpy(b, sep, lsep);
		b += lsep;
		memcpy(b, v, x);
		b += x;
	}
	if (b == NULL)
		return;
	hp->nhd = (uint16_t)d;
	AN(e);
	*b = '\0';
	hp->hd[f].b = WS_Reservation(hp->ws);
	hp->hd[f].e = b;
	WS_ReleaseP(hp->ws, b + 1);
}

/*--------------------------------------------------------------------*/

int
http_GetHdr(const struct http *hp, hdr_t hdr, const char **ptr)
{
	unsigned u, l;
	const char *p;

	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	u = http_findhdr(hp, l - 1, hdr);
	if (u == 0) {
		if (ptr != NULL)
			*ptr = NULL;
		return (0);
	}
	if (ptr != NULL) {
		p = hp->hd[u].b + l;
		while (vct_issp(*p))
			p++;
		*ptr = p;
	}
	return (1);
}

/*-----------------------------------------------------------------------------
 * Split source string at any of the separators, return pointer to first
 * and last+1 char of substrings, with whitespace trimmed at both ends.
 * If sep being an empty string is shorthand for VCT::SP
 * If stop is NULL, src is NUL terminated.
 */

static int
http_split(const char **src, const char *stop, const char *sep,
    const char **b, const char **e)
{
	const char *p, *q;

	AN(src);
	AN(*src);
	AN(sep);
	AN(b);
	AN(e);

	if (stop == NULL)
		stop = strchr(*src, '\0');

	for (p = *src; p < stop && (vct_issp(*p) || strchr(sep, *p)); p++)
		continue;

	if (p >= stop) {
		*b = NULL;
		*e = NULL;
		return (0);
	}

	*b = p;
	if (*sep == '\0') {
		for (q = p + 1; q < stop && !vct_issp(*q); q++)
			continue;
		*e = q;
		*src = q;
		return (1);
	}
	for (q = p + 1; q < stop && !strchr(sep, *q); q++)
		continue;
	*src = q;
	while (q > p && vct_issp(q[-1]))
		q--;
	*e = q;
	return (1);
}

/*-----------------------------------------------------------------------------
 * Comparison rule for tokens:
 *	if target string starts with '"', we use memcmp() and expect closing
 *	double quote as well
 *	otherwise we use http_tok_at()
 *
 * On match we increment *bp past the token name.
 */

static int
http_istoken(const char **bp, const char *e, const char *token)
{
	int fl = strlen(token);
	const char *b;

	AN(bp);
	AN(e);
	AN(token);

	b = *bp;

	if (b + fl + 2 <= e && *b == '"' &&
	    !memcmp(b + 1, token, fl) && b[fl + 1] == '"') {
		*bp += fl + 2;
		return (1);
	}
	if (b + fl <= e && http_tok_at(b, token, fl) &&
	    (b + fl == e || !vct_istchar(b[fl]))) {
		*bp += fl;
		return (1);
	}
	return (0);
}

/*-----------------------------------------------------------------------------
 * Find a given data element (token) in a header according to RFC2616's #rule
 * (section 2.1, p15)
 *
 * On case sensitivity:
 *
 * Section 4.2 (Messages Headers) defines field (header) name as case
 * insensitive, but the field (header) value/content may be case-sensitive.
 *
 * http_GetHdrToken looks up a token in a header value and the rfc does not say
 * explicitly if tokens are to be compared with or without respect to case.
 *
 * But all examples and specific statements regarding tokens follow the rule
 * that unquoted tokens are to be matched case-insensitively and quoted tokens
 * case-sensitively.
 *
 * The optional pb and pe arguments will point to the token content start and
 * end+1, white space trimmed on both sides.
 */

int
http_GetHdrToken(const struct http *hp, hdr_t hdr,
    const char *token, const char **pb, const char **pe)
{
	const char *h, *b, *e;

	if (pb != NULL)
		*pb = NULL;
	if (pe != NULL)
		*pe = NULL;
	if (!http_GetHdr(hp, hdr, &h))
		return (0);
	AN(h);

	while (http_split(&h, NULL, ",", &b, &e))
		if (http_istoken(&b, e, token))
			break;
	if (b == NULL)
		return (0);
	if (pb != NULL) {
		for (; vct_islws(*b); b++)
			continue;
		if (b == e) {
			b = NULL;
			e = NULL;
		}
		*pb = b;
		if (pe != NULL)
			*pe = e;
	}
	return (1);
}

/*--------------------------------------------------------------------
 * Find a given header field's quality value (qvalue).
 */

double
http_GetHdrQ(const struct http *hp, hdr_t hdr, const char *field)
{
	const char *hb, *he, *b, *e;
	int i;
	double a, f;

	i = http_GetHdrToken(hp, hdr, field, &hb, &he);
	if (!i)
		return (0.);

	if (hb == NULL)
		return (1.);
	while (http_split(&hb, he, ";", &b, &e)) {
		if (*b != 'q')
			continue;
		for (b++; b < e && vct_issp(*b); b++)
			continue;
		if (b == e || *b != '=')
			continue;
		break;
	}
	if (b == NULL)
		return (1.);
	for (b++; b < e && vct_issp(*b); b++)
		continue;
	if (b == e || (*b != '.' && !vct_isdigit(*b)))
		return (0.);
	a = 0;
	while (b < e && vct_isdigit(*b)) {
		a *= 10.;
		a += *b - '0';
		b++;
	}
	if (b == e || *b++ != '.')
		return (a);
	f = .1;
	while (b < e && vct_isdigit(*b)) {
		a += f * (*b - '0');
		f *= .1;
		b++;
	}
	return (a);
}

/*--------------------------------------------------------------------
 * Find a given header field's value.
 */

int
http_GetHdrField(const struct http *hp, hdr_t hdr,
    const char *field, const char **ptr)
{
	const char *h;
	int i;

	if (ptr != NULL)
		*ptr = NULL;

	h = NULL;
	i = http_GetHdrToken(hp, hdr, field, &h, NULL);
	if (!i)
		return (i);

	if (ptr != NULL && h != NULL) {
		/* Skip whitespace, looking for '=' */
		while (*h && vct_issp(*h))
			h++;
		if (*h == '=') {
			h++;
			while (*h && vct_issp(*h))
				h++;
			*ptr = h;
		}
	}
	return (i);
}

/*--------------------------------------------------------------------*/

ssize_t
http_GetContentLength(const struct http *hp)
{
	ssize_t cl;
	const char *b;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	if (!http_GetHdr(hp, H_Content_Length, &b))
		return (-1);
	cl = VNUM_uint(b, NULL, &b);
	if (cl < 0)
		return (-2);
	while (vct_islws(*b))
		b++;
	if (*b != '\0')
		return (-2);
	return (cl);
}

ssize_t
http_GetContentRange(const struct http *hp, ssize_t *lo, ssize_t *hi)
{
	ssize_t tmp, cl;
	const char *b, *t;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	if (lo == NULL)
		lo = &tmp;
	if (hi == NULL)
		hi = &tmp;

	*lo = *hi = -1;

	if (!http_GetHdr(hp, H_Content_Range, &b))
		return (-1);

	t = strchr(b, ' ');
	if (t == NULL)
		return (-2);		// Missing space after range unit

	if (!http_range_at(b, bytes, t - b))
		return (-1);		// Unknown range unit, ignore
	b = t + 1;

	if (*b == '*') {		// Content-Range: bytes */123
		*lo = *hi = -1;
		b++;
	} else {			// Content-Range: bytes 1-2/3
		*lo = VNUM_uint(b, NULL, &b);
		if (*lo < 0)
			return (-2);
		if (*b != '-')
			return (-2);
		*hi = VNUM_uint(b + 1, NULL, &b);
		if (*hi < 0)
			return (-2);
	}
	if (*b != '/')
		return (-2);
	if (b[1] == '*') {		// Content-Range: bytes 1-2/*
		cl = -1;
		b += 2;
	} else {
		cl = VNUM_uint(b + 1, NULL, &b);
		if (cl <= 0)
			return (-2);
	}
	while (vct_islws(*b))
		b++;
	if (*b != '\0')
		return (-2);
	if (*lo > *hi)
		return (-2);
	assert(cl >= -1);
	if (*lo >= cl || *hi >= cl)
		return (-2);
	AN(cl);
	return (cl);
}

const char *
http_GetRange(const struct http *hp, ssize_t *lo, ssize_t *hi, ssize_t len)
{
	ssize_t tmp_lo, tmp_hi;
	const char *b, *t;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	if (lo == NULL)
		lo = &tmp_lo;
	if (hi == NULL)
		hi = &tmp_hi;

	*lo = *hi = -1;

	if (!http_GetHdr(hp, H_Range, &b))
		return (NULL);

	t = strchr(b, '=');
	if (t == NULL)
		return ("Missing '='");

	if (!http_range_at(b, bytes, t - b))
		return ("Not Bytes");
	b = t + 1;

	*lo = VNUM_uint(b, NULL, &b);
	if (*lo == -2)
		return ("Low number too big");
	if (*b++ != '-')
		return ("Missing hyphen");

	*hi = VNUM_uint(b, NULL, &b);
	if (*hi == -2)
		return ("High number too big");
	if (*lo == -1 && *hi == -1)
		return ("Neither high nor low");
	if (*lo == -1 && *hi == 0)
		return ("No low, high is zero");
	if (*hi >= 0 && *hi < *lo)
		return ("high smaller than low");

	while (vct_islws(*b))
		b++;
	if (*b != '\0')
		return ("Trailing stuff");

	assert(*lo >= -1);
	assert(*hi >= -1);

	if (len <= 0)
		return (NULL);			// Allow 200 response

	if (*lo < 0) {
		assert(*hi > 0);
		*lo = len - *hi;
		if (*lo < 0)
			*lo = 0;
		*hi = len - 1;
	} else if (len >= 0 && (*hi >= len || *hi < 0)) {
		*hi = len - 1;
	}

	if (*lo >= len)
		return ("low range beyond object");

	return (NULL);
}

/*--------------------------------------------------------------------
 */

stream_close_t
http_DoConnection(struct http *hp, stream_close_t sc_close)
{
	const char *h, *b, *e;
	stream_close_t retval;
	unsigned u, v;
	struct http_hdrflg *f;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	assert(sc_close == SC_REQ_CLOSE || sc_close == SC_RESP_CLOSE);

	if (hp->protover == 10)
		retval = SC_REQ_HTTP10;
	else
		retval = SC_NULL;

	http_CollectHdr(hp, H_Connection);
	if (!http_GetHdr(hp, H_Connection, &h))
		return (retval);
	AN(h);
	while (http_split(&h, NULL, ",", &b, &e)) {
		u = pdiff(b, e);
		if (u == 5 && http_hdr_at(b, "close", u))
			retval = sc_close;
		if (u == 10 && http_hdr_at(b, "keep-alive", u))
			retval = SC_NULL;

		/* Refuse removal of well-known-headers if they would pass. */
		f = http_hdr_flags(b, e);
		if (f != NULL && !(f->flag & HTTPH_R_PASS))
			return (SC_RX_BAD);

		for (v = HTTP_HDR_FIRST; v < hp->nhd; v++) {
			Tcheck(hp->hd[v]);
			if (hp->hd[v].e < hp->hd[v].b + u + 1)
				continue;
			if (hp->hd[v].b[u] != ':')
				continue;
			if (!http_hdr_at(b, hp->hd[v].b, u))
				continue;
			hp->hdf[v] |= HDF_FILTER;
		}
	}
	CHECK_OBJ_NOTNULL(retval, STREAM_CLOSE_MAGIC);
	return (retval);
}

/*--------------------------------------------------------------------*/

int
http_HdrIs(const struct http *hp, hdr_t hdr, const char *val)
{
	const char *p;

	if (!http_GetHdr(hp, hdr, &p))
		return (0);
	AN(p);
	if (http_tok_eq(p, val))
		return (1);
	return (0);
}

/*--------------------------------------------------------------------*/

uint16_t
http_GetStatus(const struct http *hp)
{

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	return (hp->status);
}

int
http_IsStatus(const struct http *hp, int val)
{

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	assert(val >= 100 && val <= 999);
	return (val == (hp->status % 1000));
}

/*--------------------------------------------------------------------
 * Setting the status will also set the Reason appropriately
 */

void
http_SetStatus(struct http *to, uint16_t status, const char *reason)
{
	char buf[4];
	const char *sstr = NULL;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	/*
	 * We allow people to use top digits for internal VCL
	 * signalling, but strip them from the ASCII version.
	 */
	to->status = status;
	status %= 1000;
	assert(status >= 100);

	if (reason == NULL)
		reason = http_Status2Reason(status, &sstr);
	else
		(void)http_Status2Reason(status, &sstr);

	if (sstr) {
		http_SetH(to, HTTP_HDR_STATUS, sstr);
	} else {
		bprintf(buf, "%03d", status);
		http_PutField(to, HTTP_HDR_STATUS, buf);
	}
	http_SetH(to, HTTP_HDR_REASON, reason);
}

/*--------------------------------------------------------------------*/

const char *
http_GetMethod(const struct http *hp)
{

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	Tcheck(hp->hd[HTTP_HDR_METHOD]);
	return (hp->hd[HTTP_HDR_METHOD].b);
}

/*--------------------------------------------------------------------
 * Force a particular header field to a particular value
 */

void
http_ForceField(struct http *to, unsigned n, const char *t)
{
	int i;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	assert(n < HTTP_HDR_FIRST);
	assert(n == HTTP_HDR_METHOD || n == HTTP_HDR_PROTO);
	AN(t);

	/* NB: method names and protocol versions are case-sensitive. */
	if (to->hd[n].b == NULL || strcmp(to->hd[n].b, t)) {
		i = (HTTP_HDR_UNSET - HTTP_HDR_METHOD);
		i += to->logtag;
		/* XXX: this is a dead branch */
		if (n >= HTTP_HDR_FIRST)
			VSLbt(to->vsl, (enum VSL_tag_e)i, to->hd[n]);
		http_SetH(to, n, t);
	}
}

/*--------------------------------------------------------------------*/

void
http_PutResponse(struct http *to, const char *proto, uint16_t status,
    const char *reason)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (proto != NULL)
		http_SetH(to, HTTP_HDR_PROTO, proto);
	http_SetStatus(to, status, reason);
}

/*--------------------------------------------------------------------
 * check if header is filtered by the dynamic marker or the static
 * definitions in http_headers.h
 */

static inline int
http_isfiltered(const struct http *fm, unsigned u, unsigned how)
{
	const char *e;
	const struct http_hdrflg *f;

	if (fm->hdf[u] & HDF_FILTER)
		return (1);
	if (u < HTTP_HDR_FIRST)
		return (0);
	e = strchr(fm->hd[u].b, ':');
	if (e == NULL)
		return (0);
	f = http_hdr_flags(fm->hd[u].b, e);
	return (f != NULL && f->flag & how);
}

int
http_IsFiltered(const struct http *fm, unsigned u, unsigned how)
{

	return (http_isfiltered(fm, u, how));
}

/*--------------------------------------------------------------------
 * Estimate how much workspace we need to Filter this header according
 * to 'how'.
 */

unsigned
http_EstimateWS(const struct http *fm, unsigned how)
{
	unsigned u, l;

	l = 4;
	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	for (u = 0; u < fm->nhd; u++) {
		if (u == HTTP_HDR_METHOD || u == HTTP_HDR_URL)
			continue;
		Tcheck(fm->hd[u]);
		if (http_isfiltered(fm, u, how))
			continue;
		l += Tlen(fm->hd[u]) + 1L;
	}
	return (PRNDUP(l + 1L));
}

/*--------------------------------------------------------------------
 * Encode http struct as byte string.
 *
 * XXX: We could save considerable special-casing below by encoding also
 * XXX: H__Status, H__Reason and H__Proto into the string, but it would
 * XXX: add 26-30 bytes to all encoded objects to save a little code.
 * XXX: It could possibly be a good idea for later HTTP versions.
 */

void
HTTP_Encode(const struct http *fm, uint8_t *p0, unsigned l, unsigned how)
{
	unsigned u, w;
	uint16_t n;
	uint8_t *p, *e;

	AN(p0);
	AN(l);
	p = p0;
	e = p + l;
	assert(p + 5 <= e);
	assert(fm->nhd <= fm->shd);
	n = HTTP_HDR_FIRST - 3;
	vbe16enc(p + 2, fm->status);
	p += 4;
	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	for (u = 0; u < fm->nhd; u++) {
		if (u == HTTP_HDR_METHOD || u == HTTP_HDR_URL)
			continue;
		Tcheck(fm->hd[u]);
		if (http_isfiltered(fm, u, how))
			continue;
		http_VSLH(fm, u);
		w = Tlen(fm->hd[u]) + 1L;
		assert(p + w + 1 <= e);
		memcpy(p, fm->hd[u].b, w);
		p += w;
		n++;
	}
	*p++ = '\0';
	assert(p <= e);
	vbe16enc(p0, n + 1);
}

/*--------------------------------------------------------------------
 * Decode byte string into http struct
 */

int
HTTP_Decode(struct http *to, const uint8_t *fm)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	AN(to->vsl);
	AN(fm);
	if (vbe16dec(fm) <= to->shd) {
		to->status = vbe16dec(fm + 2);
		fm += 4;
		for (to->nhd = 0; to->nhd < to->shd; to->nhd++) {
			if (to->nhd == HTTP_HDR_METHOD ||
			    to->nhd == HTTP_HDR_URL) {
				to->hd[to->nhd].b = NULL;
				to->hd[to->nhd].e = NULL;
				continue;
			}
			if (*fm == '\0')
				return (0);
			to->hd[to->nhd].b = (const void*)fm;
			fm = (const void*)strchr((const void*)fm, '\0');
			to->hd[to->nhd].e = (const void*)fm;
			fm++;
			http_VSLH(to, to->nhd);
		}
	}
	VSLb(to->vsl, SLT_Error,
	    "Too many headers to Decode object (%u vs. %u)",
	    vbe16dec(fm), to->shd);
	return (-1);
}

/*--------------------------------------------------------------------*/

uint16_t
HTTP_GetStatusPack(struct worker *wrk, struct objcore *oc)
{
	const char *ptr;
	ptr = ObjGetAttr(wrk, oc, OA_HEADERS, NULL);
	AN(ptr);

	return (vbe16dec(ptr + 2));
}

/*--------------------------------------------------------------------*/

/* Get the first packed header */
int
HTTP_IterHdrPack(struct worker *wrk, struct objcore *oc, const char **p)
{
	const char *ptr;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(p);

	if (*p == NULL) {
		ptr = ObjGetAttr(wrk, oc, OA_HEADERS, NULL);
		AN(ptr);
		ptr += 4;	/* Skip nhd and status */
		ptr = strchr(ptr, '\0') + 1;	/* Skip :proto: */
		ptr = strchr(ptr, '\0') + 1;	/* Skip :status: */
		ptr = strchr(ptr, '\0') + 1;	/* Skip :reason: */
		*p = ptr;
	} else {
		*p = strchr(*p, '\0') + 1;	/* Skip to next header */
	}
	if (**p == '\0')
		return (0);
	return (1);
}

const char *
HTTP_GetHdrPack(struct worker *wrk, struct objcore *oc, hdr_t hdr)
{
	const char *ptr;
	unsigned l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(hdr);

	l = hdr[0];
	assert(l > 0);
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;

	if (hdr[0] == ':') {
		/* Special cases */
		ptr = ObjGetAttr(wrk, oc, OA_HEADERS, NULL);
		AN(ptr);
		ptr += 4;	/* Skip nhd and status */

		/* XXX: should we also have h2_hdr_eq() ? */
		if (!strcmp(hdr, ":proto:"))
			return (ptr);
		ptr = strchr(ptr, '\0') + 1;
		if (!strcmp(hdr, ":status:"))
			return (ptr);
		ptr = strchr(ptr, '\0') + 1;
		if (!strcmp(hdr, ":reason:"))
			return (ptr);
		WRONG("Unknown magic packed header");
	}

	HTTP_FOREACH_PACK(wrk, oc, ptr) {
		if (http_hdr_at(ptr, hdr, l)) {
			ptr += l;
			while (vct_islws(*ptr))
				ptr++;
			return (ptr);
		}
	}

	return (NULL);
}

/*--------------------------------------------------------------------
 * Merge any headers in the oc->OA_HEADER into the struct http if they
 * are not there already.
 */

void
HTTP_Merge(struct worker *wrk, struct objcore *oc, struct http *to)
{
	const char *ptr;
	unsigned u;
	const char *p;
	unsigned nhd_before_merge;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);

	ptr = ObjGetAttr(wrk, oc, OA_HEADERS, NULL);
	AN(ptr);

	to->status = vbe16dec(ptr + 2);
	ptr += 4;

	for (u = 0; u < HTTP_HDR_FIRST; u++) {
		if (u == HTTP_HDR_METHOD || u == HTTP_HDR_URL)
			continue;
		http_SetH(to, u, ptr);
		ptr = strchr(ptr, '\0') + 1;
	}
	nhd_before_merge = to->nhd;
	while (*ptr != '\0') {
		p = strchr(ptr, ':');
		AN(p);
		u = http_findhdr(to, p - ptr, ptr);
		if (u == 0 || u >= nhd_before_merge)
			http_SetHeader(to, ptr);
		ptr = strchr(ptr, '\0') + 1;
	}
}

/*--------------------------------------------------------------------*/

static void
http_linkh(const struct http *to, const struct http *fm, unsigned n)
{

	assert(n < HTTP_HDR_FIRST);
	Tcheck(fm->hd[n]);
	to->hd[n] = fm->hd[n];
	to->hdf[n] = fm->hdf[n];
	http_VSLH(to, n);
}

/*--------------------------------------------------------------------*/

void
http_FilterReq(struct http *to, const struct http *fm, unsigned how)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);

	http_linkh(to, fm, HTTP_HDR_METHOD);
	http_linkh(to, fm, HTTP_HDR_URL);
	http_linkh(to, fm, HTTP_HDR_PROTO);
	to->protover = fm->protover;
	to->status = fm->status;

	to->nhd = HTTP_HDR_FIRST;
	for (u = HTTP_HDR_FIRST; u < fm->nhd; u++) {
		Tcheck(fm->hd[u]);
		if (http_isfiltered(fm, u, how))
			continue;
		assert (to->nhd < to->shd);
		to->hd[to->nhd] = fm->hd[u];
		to->hdf[to->nhd] = 0;
		http_VSLH(to, to->nhd);
		to->nhd++;
	}
}

/*--------------------------------------------------------------------
 * This function copies any header fields which reference foreign
 * storage into our own WS.
 */

void
http_CopyHome(const struct http *hp)
{
	unsigned u, l;
	const char *p;

	for (u = 0; u < hp->nhd; u++) {
		if (hp->hd[u].b == NULL) {
			assert(u < HTTP_HDR_FIRST);
			continue;
		}

		l = Tlen(hp->hd[u]);
		if (WS_Allocated(hp->ws, hp->hd[u].b, l))
			continue;

		p = WS_Copy(hp->ws, hp->hd[u].b, l + 1L);
		if (p == NULL) {
			http_fail(hp);
			VSLbs(hp->vsl, SLT_LostHeader, TOSTRAND(hp->hd[u].b));
			return;
		}
		hp->hd[u].b = p;
		hp->hd[u].e = p + l;
	}
}

/*--------------------------------------------------------------------*/

void
http_SetHeader(struct http *to, const char *header)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (to->nhd >= to->shd) {
		VSLbs(to->vsl, SLT_LostHeader, TOSTRAND(header));
		http_fail(to);
		return;
	}
	http_SetH(to, to->nhd++, header);
}

/*--------------------------------------------------------------------*/

void
http_ForceHeader(struct http *to, hdr_t hdr, const char *val)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (http_HdrIs(to, hdr, val))
		return;
	http_Unset(to, hdr);
	http_PrintfHeader(to, "%s %s", hdr + 1, val);
}

void
http_AppendHeader(struct http *to, hdr_t hdr, const char *val)
{
	const char *old;

	http_CollectHdr(to, hdr);
	if (http_GetHdr(to, hdr, &old)) {
		http_Unset(to, hdr);
		http_PrintfHeader(to, "%s %s, %s", hdr + 1, old, val);
	} else {
		http_PrintfHeader(to, "%s %s", hdr + 1, val);
	}
}

void
http_PrintfHeader(struct http *to, const char *fmt, ...)
{
	va_list ap, ap2;
	struct vsb vsb[1];
	size_t sz;
	char *p;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);

	va_start(ap, fmt);
	va_copy(ap2, ap);

	WS_VSB_new(vsb, to->ws);
	VSB_vprintf(vsb, fmt, ap);
	p = WS_VSB_finish(vsb, to->ws, &sz);

	if (p == NULL || to->nhd >= to->shd) {
		http_fail(to);
		VSLbv(to->vsl, SLT_LostHeader, fmt, ap2);
	} else {
		http_SetH(to, to->nhd++, p);
	}
	va_end(ap);
	va_end(ap2);
}

void
http_TimeHeader(struct http *to, const char *fmt, vtim_real now)
{
	char *p;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (to->nhd >= to->shd) {
		VSLbs(to->vsl, SLT_LostHeader, TOSTRAND(fmt));
		http_fail(to);
		return;
	}
	p = WS_Alloc(to->ws, strlen(fmt) + VTIM_FORMAT_SIZE);
	if (p == NULL) {
		http_fail(to);
		VSLbs(to->vsl, SLT_LostHeader, TOSTRAND(fmt));
		return;
	}
	strcpy(p, fmt);
	VTIM_format(now, strchr(p, '\0'));
	http_SetH(to, to->nhd++, p);
}

const char *
http_ViaHeader(void)
{

	return (via_hdr);
}

/*--------------------------------------------------------------------*/

void
http_Unset(struct http *hp, hdr_t hdr)
{
	uint16_t u, v;

	for (v = u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		Tcheck(hp->hd[u]);
		if (http_IsHdr(&hp->hd[u], hdr)) {
			http_VSLH_del(hp, u);
			continue;
		}
		if (v != u) {
			memcpy(&hp->hd[v], &hp->hd[u], sizeof *hp->hd);
			memcpy(&hp->hdf[v], &hp->hdf[u], sizeof *hp->hdf);
		}
		v++;
	}
	hp->nhd = v;
}
