/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 * HTTP request storage and manipulation
 */

#include "config.h"

#include <stdio.h>
#include <stddef.h>

#include "cache.h"

#include "vend.h"
#include "vct.h"

#define HTTPH(a, b, c) char b[] = "*" a ":";
#include "tbl/http_headers.h"
#undef HTTPH

/*--------------------------------------------------------------------
 * These two functions are in an incestous relationship with the
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
		AN(hp->vsl->wid & (VSL_CLIENTMARKER|VSL_BACKENDMARKER));
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
		AN(hp->vsl->wid & (VSL_CLIENTMARKER|VSL_BACKENDMARKER));
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

	VSC_C_main->losthdr++;
	VSLb(hp->vsl, SLT_Error, "out of workspace (%s)", hp->ws->id);
	WS_MarkOverflow(hp->ws);
}

/*--------------------------------------------------------------------*/
/* List of canonical HTTP response code names from RFC2616 */

static struct http_msg {
	unsigned	nbr;
	const char	*txt;
} http_msg[] = {
#define HTTP_RESP(n, t)	{ n, t},
#include "tbl/http_response.h"
	{ 0, NULL }
};

const char *
http_Status2Reason(unsigned status)
{
	struct http_msg *mp;

	status %= 1000;
	assert(status >= 100);
	for (mp = http_msg; mp->nbr != 0 && mp->nbr <= status; mp++)
		if (mp->nbr == status)
			return (mp->txt);
	return ("Unknown HTTP Status");
}

/*--------------------------------------------------------------------*/

unsigned
HTTP_estimate(unsigned nhttp)
{

	/* XXX: We trust the structs to size-aligned as necessary */
	return (PRNDUP(sizeof (struct http) + sizeof(txt) * nhttp + nhttp));
}

struct http *
HTTP_create(void *p, uint16_t nhttp)
{
	struct http *hp;

	hp = p;
	hp->magic = HTTP_MAGIC;
	hp->hd = (void*)(hp + 1);
	hp->shd = nhttp;
	hp->hdf = (void*)(hp->hd + nhttp);
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

/*--------------------------------------------------------------------*/

void
HTTP_Copy(struct http *to, const struct http * const fm)
{

	assert(fm->nhd <= to->shd);
	memcpy(&to->nhd, &fm->nhd, sizeof *to - offsetof(struct http, nhd));
	memcpy(to->hd, fm->hd, fm->nhd * sizeof *to->hd);
	memcpy(to->hdf, fm->hdf, fm->nhd * sizeof *to->hdf);
}

/*--------------------------------------------------------------------*/

void
http_SetH(const struct http *to, unsigned n, const char *fm)
{

	assert(n < to->shd);
	AN(fm);
	to->hd[n].b = TRUST_ME(fm);
	to->hd[n].e = strchr(to->hd[n].b, '\0');
	to->hdf[n] = 0;
	http_VSLH(to, n);
}

/*--------------------------------------------------------------------*/

static void
http_PutField(const struct http *to, int field, const char *string)
{
	char *p;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	p = WS_Copy(to->ws, string, -1);
	if (p == NULL) {
		http_fail(to);
		VSLb(to->vsl, SLT_LostHeader, "%s", string);
		return;
	}
	to->hd[field].b = p;
	to->hd[field].e = strchr(p, '\0');
	to->hdf[field] = 0;
	http_VSLH(to, field);
}

/*--------------------------------------------------------------------*/

int
http_IsHdr(const txt *hh, const char *hdr)
{
	unsigned l;

	Tcheck(*hh);
	AN(hdr);
	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	return (!strncasecmp(hdr, hh->b, l));
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
		if (strncasecmp(hdr, hp->hd[u].b, l))
			continue;
		return (u);
	}
	return (0);
}

/*--------------------------------------------------------------------
 */

void
http_MarkHeader(const struct http *hp, const char *hdr, unsigned hdrlen,
    uint8_t flag)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	u = http_findhdr(hp, hdrlen, hdr);
	if (u == 0)
		return;
	hp->hdf[u] |= flag;
}

/*--------------------------------------------------------------------
 * This function collapses multiple headerlines of the same name.
 * The lines are joined with a comma, according to [rfc2616, 4.2bot, p32]
 */

void
http_CollectHdr(struct http *hp, const char *hdr)
{
	unsigned u, l, ml, f, x, d;
	char *b = NULL, *e = NULL;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (WS_Overflowed(hp->ws))
		return;
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
			ml = WS_Reserve(hp->ws, 0);
			b = hp->ws->f;
			e = b + ml;
			x = Tlen(hp->hd[f]);
			if (b + x >= e) {
				http_fail(hp);
				VSLb(hp->vsl, SLT_LostHeader, "%s", hdr + 1);
				WS_Release(hp->ws, 0);
				return;
			}
			memcpy(b, hp->hd[f].b, x);
			b += x;
		}

		AN(b);
		AN(e);

		/* Append the Nth header we found */
		if (b < e)
			*b++ = ',';
		x = Tlen(hp->hd[u]) - l;
		if (b + x >= e) {
			http_fail(hp);
			VSLb(hp->vsl, SLT_LostHeader, "%s", hdr + 1);
			WS_Release(hp->ws, 0);
			return;
		}
		memcpy(b, hp->hd[u].b + *hdr, x);
		b += x;
	}
	if (b == NULL)
		return;
	hp->nhd = (uint16_t)d;
	AN(e);
	*b = '\0';
	hp->hd[f].b = hp->ws->f;
	hp->hd[f].e = b;
	WS_ReleaseP(hp->ws, b + 1);
}

/*--------------------------------------------------------------------*/

int
http_GetHdr(const struct http *hp, const char *hdr, char **ptr)
{
	unsigned u, l;
	char *p;

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

/*--------------------------------------------------------------------
 * Find a given data element in a header according to RFC2616's #rule
 * (section 2.1, p15)
 */

int
http_GetHdrData(const struct http *hp, const char *hdr,
    const char *field, char **ptr)
{
	char *h, *e;
	unsigned fl;

	if (ptr != NULL)
		*ptr = NULL;
	if (!http_GetHdr(hp, hdr, &h))
		return (0);
	AN(h);
	e = strchr(h, '\0');
	fl = strlen(field);
	while (h + fl <= e) {
		/* Skip leading whitespace and commas */
		if (vct_islws(*h) || *h == ',') {
			h++;
			continue;
		}
		/* Check for substrings before memcmp() */
		if ((h + fl == e || vct_issepctl(h[fl])) &&
		    !memcmp(h, field, fl)) {
			if (ptr != NULL) {
				h += fl;
				while (vct_islws(*h))
					h++;
				*ptr = h;
			}
			return (1);
		}
		/* Skip until end of header or comma */
		while (*h && *h != ',')
			h++;
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Find a given headerfields Q value.
 */

double
http_GetHdrQ(const struct http *hp, const char *hdr, const char *field)
{
	char *h;
	int i;
	double a, b;

	h = NULL;
	i = http_GetHdrData(hp, hdr, field, &h);
	if (!i)
		return (0.);

	if (h == NULL)
		return (1.);
	/* Skip whitespace, looking for '=' */
	while (*h && vct_issp(*h))
		h++;
	if (*h++ != ';')
		return (1.);
	while (*h && vct_issp(*h))
		h++;
	if (*h++ != 'q')
		return (1.);
	while (*h && vct_issp(*h))
		h++;
	if (*h++ != '=')
		return (1.);
	while (*h && vct_issp(*h))
		h++;
	a = 0.;
	while (vct_isdigit(*h)) {
		a *= 10.;
		a += *h - '0';
		h++;
	}
	if (*h++ != '.')
		return (a);
	b = .1;
	while (vct_isdigit(*h)) {
		a += b * (*h - '0');
		b *= .1;
		h++;
	}
	return (a);
}

/*--------------------------------------------------------------------
 * Find a given headerfields value.
 */

int
http_GetHdrField(const struct http *hp, const char *hdr,
    const char *field, char **ptr)
{
	char *h;
	int i;

	if (ptr != NULL)
		*ptr = NULL;

	h = NULL;
	i = http_GetHdrData(hp, hdr, field, &h);
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

int
http_HdrIs(const struct http *hp, const char *hdr, const char *val)
{
	char *p;

	if (!http_GetHdr(hp, hdr, &p))
		return (0);
	AN(p);
	if (!strcasecmp(p, val))
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
http_SetStatus(struct http *to, uint16_t status)
{
	char buf[4];

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	/*
	 * We allow people to use top digits for internal VCL
	 * signalling, but strip them from the ASCII version.
	 */
	to->status = status;
	status %= 1000;
	assert(status >= 100);
	bprintf(buf, "%03d", status);
	http_PutField(to, HTTP_HDR_STATUS, buf);
	http_SetH(to, HTTP_HDR_REASON, http_Status2Reason(status));
}

/*--------------------------------------------------------------------*/

const char *
http_GetReq(const struct http *hp)
{

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	Tcheck(hp->hd[HTTP_HDR_METHOD]);
	return (hp->hd[HTTP_HDR_METHOD].b);
}

/*--------------------------------------------------------------------
 * Force a particular header field to a particular value
 */

void
http_ForceField(const struct http *to, unsigned n, const char *t)
{
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	assert(n < HTTP_HDR_FIRST);
	AN(t);
	if (to->hd[n].b == NULL || strcmp(to->hd[n].b, t))
		http_SetH(to, n, t);
}

/*--------------------------------------------------------------------*/

void
http_PutResponse(struct http *to, const char *proto, uint16_t status,
    const char *reason)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (proto != NULL)
		http_SetH(to, HTTP_HDR_PROTO, proto);
	http_SetStatus(to, status);
	if (reason == NULL)
		reason = http_Status2Reason(status);
	http_SetH(to, HTTP_HDR_REASON, reason);
}

/*--------------------------------------------------------------------
 * Estimate how much workspace we need to Filter this header according
 * to 'how'.
 */

unsigned
http_EstimateWS(const struct http *fm, unsigned how, uint16_t *nhd)
{
	unsigned u, l;

	l = 4;
	*nhd = 1 + HTTP_HDR_FIRST - 3;
	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	for (u = 0; u < fm->nhd; u++) {
		if (u == HTTP_HDR_METHOD || u == HTTP_HDR_URL)
			continue;
		AN(fm->hd[u].b);
		AN(fm->hd[u].e);
		if (fm->hdf[u] & HDF_FILTER)
			continue;
#define HTTPH(a, b, c) \
		if (((c) & how) && http_IsHdr(&fm->hd[u], (b))) \
			continue;
#include "tbl/http_headers.h"
#undef HTTPH
		l += Tlen(fm->hd[u]) + 1L;
		(*nhd)++;
	}
	return (PRNDUP(l + 1L));
}

/*--------------------------------------------------------------------
 * Encode http struct as byte string.
 */

uint8_t *
HTTP_Encode(const struct http *fm, struct ws *ws, unsigned how)
{
	unsigned u, w;
	uint16_t n;
	uint8_t *p, *e;

	u = WS_Reserve(ws, 0);
	p = (uint8_t*)ws->f;
	e = (uint8_t*)ws->f + u;
	if (p + 5 > e) {
		WS_Release(ws, 0);
		return (NULL);
	}
	assert(fm->nhd < fm->shd);
	n = HTTP_HDR_FIRST - 3;
	vbe16enc(p + 2, fm->status);
	p += 4;
	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	for (u = 0; u < fm->nhd; u++) {
		if (u == HTTP_HDR_METHOD || u == HTTP_HDR_URL)
			continue;
		AN(fm->hd[u].b);
		AN(fm->hd[u].e);
		if (fm->hdf[u] & HDF_FILTER)
			continue;
#define HTTPH(a, b, c) \
		if (((c) & how) && http_IsHdr(&fm->hd[u], (b))) \
			continue;
#include "tbl/http_headers.h"
#undef HTTPH
		w = Tlen(fm->hd[u]) + 1L;
		if (p + w + 1 > e) {
			WS_Release(ws, 0);
			return (NULL);
		}
		memcpy(p, fm->hd[u].b, w);
		p += w;
		n++;
	}
	*p++ = '\0';
	assert(p <= e);
	e = (uint8_t*)ws->f;
	vbe16enc(e, n + 1);
	VSLb(fm->vsl, SLT_Debug, "HTTPENC %zd",  p - (uint8_t*)ws->f);
	WS_ReleaseP(ws, (void*)p);
	return (e);
}

/*--------------------------------------------------------------------
 * Decode byte string into http struct
 *
 * XXX: cannot make fm const because to->hd isn't.
 */

int
HTTP_Decode(struct http *to, uint8_t *fm)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	AN(fm);
	if (vbe16dec(fm) > to->shd)
		return(-1);
	to->status = vbe16dec(fm + 2);
	fm += 4;
	for (to->nhd = 0; to->nhd < to->shd; to->nhd++) {
		if (to->nhd == HTTP_HDR_METHOD || to->nhd == HTTP_HDR_URL) {
			to->hd[to->nhd].b = NULL;
			to->hd[to->nhd].e = NULL;
			continue;
		}
		if (*fm == '\0')
			return (0);
		to->hd[to->nhd].b = (void*)fm;
		fm = (void*)strchr((void*)fm, '\0');
		to->hd[to->nhd].e = (void*)fm;
		fm++;
		if (to->vsl != NULL)
			http_VSLH(to, to->nhd);
	}
	return (-1);
}

/*--------------------------------------------------------------------*/

static void
http_filterfields(struct http *to, const struct http *fm, unsigned how)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	to->nhd = HTTP_HDR_FIRST;
	to->status = fm->status;
	for (u = HTTP_HDR_FIRST; u < fm->nhd; u++) {
		if (fm->hd[u].b == NULL)
			continue;
		if (fm->hdf[u] & HDF_FILTER)
			continue;
		Tcheck(fm->hd[u]);
#define HTTPH(a, b, c) \
		if (((c) & how) && http_IsHdr(&fm->hd[u], (b))) \
			continue;
#include "tbl/http_headers.h"
#undef HTTPH
		assert (to->nhd < to->shd);
		to->hd[to->nhd] = fm->hd[u];
		to->hdf[to->nhd] = 0;
		http_VSLH(to, to->nhd);
		to->nhd++;
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
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);

	http_linkh(to, fm, HTTP_HDR_METHOD);
	http_linkh(to, fm, HTTP_HDR_URL);
	http_linkh(to, fm, HTTP_HDR_PROTO);
	http_filterfields(to, fm, how);
}

/*--------------------------------------------------------------------
 * Merge two HTTP headers the "wrong" way. Used by backend IMS to
 * merge in the headers of the validated object with the headers of
 * the 304 response.
 */

void
http_Merge(const struct http *fm, struct http *to, int not_ce)
{
	unsigned u, v;
	const char *p;

	to->status = fm->status;
	http_linkh(to, fm, HTTP_HDR_PROTO);
	http_linkh(to, fm, HTTP_HDR_STATUS);
	http_linkh(to, fm, HTTP_HDR_REASON);

	for (u = HTTP_HDR_FIRST; u < fm->nhd; u++)
		fm->hdf[u] |= HDF_MARKER;
	if (not_ce) {
		u = http_findhdr(fm,
		    H_Content_Encoding[0] - 1, H_Content_Encoding + 1);
		if (u > 0)
			fm->hdf[u] &= ~HDF_MARKER;
	}
	for (v = HTTP_HDR_FIRST; v < to->nhd; v++) {
		p = strchr(to->hd[v].b, ':');
		AN(p);
		u = http_findhdr(fm, p - to->hd[v].b, to->hd[v].b);
		if (u)
			fm->hdf[u] &= ~HDF_MARKER;
	}
	for (u = HTTP_HDR_FIRST; u < fm->nhd; u++)
		if (fm->hdf[u] & HDF_MARKER)
			http_SetHeader(to, fm->hd[u].b);
}

/*--------------------------------------------------------------------
 * This function copies any header fields which reference foreign
 * storage into our own WS.
 */

void
http_CopyHome(const struct http *hp)
{
	unsigned u, l;
	char *p;

	for (u = 0; u < hp->nhd; u++) {
		if (hp->hd[u].b == NULL)
			continue;
		if (hp->hd[u].b >= hp->ws->s && hp->hd[u].e <= hp->ws->e)
			continue;

		l = Tlen(hp->hd[u]);
		p = WS_Copy(hp->ws, hp->hd[u].b, l + 1L);
		if (p == NULL) {
			http_fail(hp);
			VSLb(hp->vsl, SLT_LostHeader, "%s", hp->hd[u].b);
			return;
		}
		hp->hd[u].b = p;
		hp->hd[u].e = p + l;
	}
}

/*--------------------------------------------------------------------*/

void
http_SetHeader(struct http *to, const char *hdr)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (to->nhd >= to->shd) {
		VSLb(to->vsl, SLT_LostHeader, "%s", hdr);
		http_fail(to);
		return;
	}
	http_SetH(to, to->nhd++, hdr);
}

/*--------------------------------------------------------------------*/

void
http_ForceHeader(struct http *to, const char *hdr, const char *val)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (http_HdrIs(to, hdr, val))
		return;
	http_Unset(to, hdr);
	http_PrintfHeader(to, "%s %s", hdr + 1, val);
}

void
http_PrintfHeader(struct http *to, const char *fmt, ...)
{
	va_list ap;
	unsigned l, n;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	l = WS_Reserve(to->ws, 0);
	va_start(ap, fmt);
	n = vsnprintf(to->ws->f, l, fmt, ap);
	va_end(ap);
	if (n + 1 >= l || to->nhd >= to->shd) {
		http_fail(to);
		VSLb(to->vsl, SLT_LostHeader, "%s", to->ws->f);
		WS_Release(to->ws, 0);
		return;
	}
	to->hd[to->nhd].b = to->ws->f;
	to->hd[to->nhd].e = to->ws->f + n;
	to->hdf[to->nhd] = 0;
	WS_Release(to->ws, n + 1);
	http_VSLH(to, to->nhd);
	to->nhd++;
}

/*--------------------------------------------------------------------*/

void
http_Unset(struct http *hp, const char *hdr)
{
	uint16_t u, v;

	for (v = u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		if (hp->hd[u].b == NULL)
			continue;
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

/*--------------------------------------------------------------------*/

void
HTTP_Init(void)
{

#define HTTPH(a, b, c) b[0] = (char)strlen(b + 1);
#include "tbl/http_headers.h"
#undef HTTPH
}
