/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"

#include "vtim.h"
#include "vct.h"

/*--------------------------------------------------------------------
 * TTL and Age calculation in Varnish
 *
 * RFC2616 has a lot to say about how caches should calculate the TTL
 * and expiry times of objects, but it sort of misses the case that
 * applies to Varnish:  the server-side cache.
 *
 * A normal cache, shared or single-client, has no symbiotic relationship
 * with the server, and therefore must take a very defensive attitude
 * if the Data/Expiry/Age/max-age data does not make sense.  Overall
 * the policy described in section 13 of RFC 2616 results in no caching
 * happening on the first little sign of trouble.
 *
 * Varnish on the other hand tries to offload as many transactions from
 * the backend as possible, and therefore just passing through everything
 * if there is a clock-skew between backend and Varnish is not a workable
 * choice.
 *
 * Varnish implements a policy which is RFC2616 compliant when there
 * is no clockskew, and falls as gracefully as possible otherwise.
 * Our "clockless cache" model is synthesized from the bits of RFC2616
 * that talks about how a cache should react to a clockless origin server,
 * and more or less uses the inverse logic for the opposite relationship.
 *
 */

static inline unsigned
rfc2616_time(const char *p)
{
	char *ep;
	unsigned long val;
	if (*p == '-')
		return (0);
	val = strtoul(p, &ep, 10);
	if (val > UINT_MAX)
		return (UINT_MAX);
	while (vct_issp(*ep))
		ep++;
	/* We accept ',' as an end character because we may be parsing a
	 * multi-element Cache-Control part. We accept '.' to be future
	 * compatible with fractional seconds. */
	if (*ep == '\0' || *ep == ',' || *ep == '.')
		return (val);
	return (0);
}

void
RFC2616_Ttl(struct busyobj *bo, vtim_real now, vtim_real *t_origin,
    float *ttl, float *grace, float *keep)
{
	unsigned max_age, age;
	vtim_real h_date, h_expires;
	const char *p;
	const struct http *hp;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->fetch_objcore, OBJCORE_MAGIC);
	assert(now != 0.0 && !isnan(now));
	AN(t_origin);
	AN(ttl);
	AN(grace);
	AN(keep);

	*t_origin = now;
	*ttl = cache_param->default_ttl;
	*grace = cache_param->default_grace;
	*keep = cache_param->default_keep;

	hp = bo->beresp;

	max_age = age = 0;
	h_expires = 0;
	h_date = 0;

	/*
	 * Initial cacheability determination per [RFC2616, 13.4]
	 * We do not support ranges to the backend yet, so 206 is out.
	 */

	if (http_GetHdr(hp, H_Age, &p)) {
		age = rfc2616_time(p);
		*t_origin -= age;
	}

	if (bo->fetch_objcore->flags & OC_F_PRIVATE) {
		/* Pass object. Halt the processing here, keeping only the
		 * parsed value of t_origin, as that will be needed to
		 * synthesize a correct Age header in delivery. The
		 * SLT_TTL log tag at the end of this function is
		 * deliberately skipped to avoid confusion when reading
		 * the log.*/
		*ttl = -1;
		*grace = 0;
		*keep = 0;
		return;
	}

	if (http_GetHdr(hp, H_Expires, &p))
		h_expires = VTIM_parse(p);

	if (http_GetHdr(hp, H_Date, &p))
		h_date = VTIM_parse(p);

	switch (http_GetStatus(hp)) {
	case 302: /* Moved Temporarily */
	case 307: /* Temporary Redirect */
		/*
		 * https://tools.ietf.org/html/rfc7231#section-6.1
		 *
		 * Do not apply the default ttl, only set a ttl if Cache-Control
		 * or Expires are present. Uncacheable otherwise.
		 */
		*ttl = -1.;
		/* FALLTHROUGH */
	case 200: /* OK */
	case 203: /* Non-Authoritative Information */
	case 204: /* No Content */
	case 300: /* Multiple Choices */
	case 301: /* Moved Permanently */
	case 304: /* Not Modified - handled like 200 */
	case 404: /* Not Found */
	case 410: /* Gone */
	case 414: /* Request-URI Too Large */
		/*
		 * First find any relative specification from the backend
		 * These take precedence according to RFC2616, 13.2.4
		 */

		if ((http_GetHdrField(hp, H_Cache_Control, "s-maxage", &p) ||
		    http_GetHdrField(hp, H_Cache_Control, "max-age", &p)) &&
		    p != NULL) {
			max_age = rfc2616_time(p);
			*ttl = max_age;
			break;
		}

		/* No expire header, fall back to default */
		if (h_expires == 0)
			break;


		/* If backend told us it is expired already, don't cache. */
		if (h_expires < h_date) {
			*ttl = 0;
			break;
		}

		if (h_date == 0 ||
		    fabs(h_date - now) < cache_param->clock_skew) {
			/*
			 * If we have no Date: header or if it is
			 * sufficiently close to our clock we will
			 * trust Expires: relative to our own clock.
			 */
			if (h_expires < now)
				*ttl = 0;
			else
				*ttl = h_expires - now;
			break;
		} else {
			/*
			 * But even if the clocks are out of whack we can still
			 * derive a relative time from the two headers.
			 * (the negative ttl case is caught above)
			 */
			*ttl = (int)(h_expires - h_date);
		}
		break;
	default:
		*ttl = -1.;
		break;
	}

	/*
	 * RFC5861 outlines a way to control the use of stale responses.
	 * We use this to initialize the grace period.
	 */
	if (*ttl >= 0 && http_GetHdrField(hp, H_Cache_Control,
	    "stale-while-revalidate", &p) && p != NULL) {
		*grace = rfc2616_time(p);
	}

	VSLb(bo->vsl, SLT_TTL,
	    "RFC %.0f %.0f %.0f %.0f %.0f %.0f %.0f %u %s",
	    *ttl, *grace, *keep, now,
	    *t_origin, h_date, h_expires, max_age,
	    bo->uncacheable ? "uncacheable" : "cacheable");
}

/*--------------------------------------------------------------------
 * Find out if the request can receive a gzip'ed response
 */

unsigned
RFC2616_Req_Gzip(const struct http *hp)
{


	/*
	 * "x-gzip" is for http/1.0 backwards compat, final note in 14.3
	 * p104 says to not do q values for x-gzip, so we just test
	 * for its existence.
	 */
	if (http_GetHdrToken(hp, H_Accept_Encoding, "x-gzip", NULL, NULL))
		return (1);

	/*
	 * "gzip" is the real thing, but the 'q' value must be nonzero.
	 * We do not care a hoot if the client prefers some other
	 * compression more than gzip: Varnish only does gzip.
	 */
	if (http_GetHdrQ(hp, H_Accept_Encoding, "gzip") > 0.)
		return (1);

	/* Bad client, no gzip. */
	return (0);
}

/*--------------------------------------------------------------------*/

// rfc7232,l,547,548
static inline int
rfc2616_strong_compare(const char *p, const char *e)
{
	if ((p[0] == 'W' && p[1] == '/') ||
	    (e[0] == 'W' && e[1] == '/'))
		return (0);
	/* XXX: should we also have http_etag_cmp() ? */
	return (strcmp(p, e) == 0);
}

// rfc7232,l,550,552
static inline int
rfc2616_weak_compare(const char *p, const char *e)
{
	if (p[0] == 'W' && p[1] == '/')
		p += 2;
	if (e[0] == 'W' && e[1] == '/')
		e += 2;
	/* XXX: should we also have http_etag_cmp() ? */
	return (strcmp(p, e) == 0);
}

int
RFC2616_Do_Cond(const struct req *req)
{
	const char *p, *e;
	vtim_real ims, lm;

	if (!http_IsStatus(req->resp, 200))
		return (0);

	/*
	 * rfc7232,l,861,866
	 * We MUST ignore If-Modified-Since if we have an If-None-Match header
	 */
	if (http_GetHdr(req->http, H_If_None_Match, &p)) {
		if (!http_GetHdr(req->resp, H_ETag, &e))
			return (0);
		if (http_GetHdr(req->http, H_Range, NULL))
			return (rfc2616_strong_compare(p, e));
		else
			return (rfc2616_weak_compare(p, e));
	}

	if (http_GetHdr(req->http, H_If_Modified_Since, &p)) {
		ims = VTIM_parse(p);
		if (!ims || ims > req->t_req)	// rfc7232,l,868,869
			return (0);
		if (http_GetHdr(req->resp, H_Last_Modified, &p)) {
			lm = VTIM_parse(p);
			if (!lm || lm > ims)
				return (0);
			return (1);
		}
		AZ(ObjGetDouble(req->wrk, req->objcore, OA_LASTMODIFIED, &lm));
		if (lm > ims)
			return (0);
		return (1);
	}

	return (0);
}

/*--------------------------------------------------------------------*/

void
RFC2616_Weaken_Etag(struct http *hp)
{
	const char *p;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	if (!http_GetHdr(hp, H_ETag, &p))
		return;
	AN(p);
	if (p[0] == 'W' && p[1] == '/')
		return;
	http_Unset(hp, H_ETag);
	http_PrintfHeader(hp, "ETag: W/%s", p);
}

/*--------------------------------------------------------------------*/

void
RFC2616_Vary_AE(struct http *hp)
{
	const char *vary;

	if (http_GetHdrToken(hp, H_Vary, "Accept-Encoding", NULL, NULL))
		return;
	if (http_GetHdr(hp, H_Vary, &vary)) {
		http_Unset(hp, H_Vary);
		http_PrintfHeader(hp, "Vary: %s, Accept-Encoding", vary);
	} else {
		http_SetHeader(hp, "Vary: Accept-Encoding");
	}
}

/*--------------------------------------------------------------------*/

const char *
RFC2616_Strong_LM(const struct http *hp, struct worker *wrk,
    struct objcore *oc)
{
	const char *p = NULL, *e = NULL;
	vtim_real lm, d;

	CHECK_OBJ_ORNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_ORNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_ORNULL(hp, HTTP_MAGIC);

	if (hp != NULL) {
		(void)http_GetHdr(hp, H_Last_Modified, &p);
		(void)http_GetHdr(hp, H_Date, &e);
	} else if (wrk != NULL && oc != NULL) {
		p = HTTP_GetHdrPack(wrk, oc, H_Last_Modified);
		e = HTTP_GetHdrPack(wrk, oc, H_Date);
	}

	if (p == NULL || e == NULL)
		return (NULL);

	lm = VTIM_parse(p);
	d = VTIM_parse(e);

	/* The cache entry includes a Date value which is at least one second
	 * after the Last-Modified value.
	 * [RFC9110 8.8.2.2-6.2]
	 */
	return ((lm && d && lm + 1 <= d) ? p : NULL);
}
