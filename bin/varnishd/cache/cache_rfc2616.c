/*-
 * Copyright (c) 2006 Verdens Gang AS
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
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"

#include "vtim.h"

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

void
RFC2616_Ttl(struct busyobj *bo, double now, double *t_origin,
    float *ttl, float *grace, float *keep)
{
	unsigned max_age, age;
	double h_date, h_expires;
	const char *p;
	const struct http *hp;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
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
		/*
		 * We deliberately run with partial results, rather than
		 * reject the Age: header outright.  This will be future
		 * compatible with fractional seconds.
		 */
		age = strtoul(p, NULL, 10);
		*t_origin -= age;
	}

	if (http_GetHdr(hp, H_Expires, &p))
		h_expires = VTIM_parse(p);

	if (http_GetHdr(hp, H_Date, &p))
		h_date = VTIM_parse(p);

	switch (http_GetStatus(hp)) {
	default:
		*ttl = -1.;
		break;
	case 302: /* Moved Temporarily */
	case 307: /* Temporary Redirect */
		/*
		 * https://tools.ietf.org/html/rfc7231#section-6.1
		 *
		 * Do not apply the default ttl, only set a ttl if Cache-Control
		 * or Expires are present. Uncacheable otherwise.
		 */
		*ttl = -1.;
		/* FALL-THROUGH */
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

			if (*p == '-')
				max_age = 0;
			else
				max_age = strtoul(p, NULL, 0);

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

	}

	/*
	 * RFC5861 outlines a way to control the use of stale responses.
	 * We use this to initialize the grace period.
	 */
	if (*ttl >= 0 && http_GetHdrField(hp, H_Cache_Control,
	    "stale-while-revalidate", &p) && p != NULL) {

		if (*p == '-')
			*grace = 0;
		else
			*grace = strtoul(p, NULL, 0);
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

static inline int
rfc2616_strong_compare(const char *p, const char *e)
{
	if ((p[0] == 'W' && p[1] == '/') ||
	    (e[0] == 'W' && e[1] == '/'))
		return (0);
	return (strcmp(p, e) == 0);
}

static inline int
rfc2616_weak_compare(const char *p, const char *e)
{
	if (p[0] == 'W' && p[1] == '/')
		p += 2;
	if (e[0] == 'W' && e[1] == '/')
		e += 2;
	return (strcmp(p, e) == 0);
}

int
RFC2616_Do_Cond(const struct req *req)
{
	const char *p, *e;
	double ims, lm;

	/*
	 * We MUST ignore If-Modified-Since if we have an If-None-Match
	 * header [RFC7232 3.3 p16].
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
		if (!ims || ims > req->t_req)	/* [RFC7232 3.3 p16] */
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

void
RFC2616_Response_Body(const struct worker *wrk, const struct busyobj *bo)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	/*
	 * Figure out how the fetch is supposed to happen, before the
	 * headers are adultered by VCL
	 */
	if (!strcasecmp(http_GetMethod(bo->bereq), "head")) {
		/*
		 * A HEAD request can never have a body in the reply,
		 * no matter what the headers might say.
		 * [RFC7231 4.3.2 p25]
		 */
		wrk->stats->fetch_head++;
		bo->htc->body_status = BS_NONE;
	} else if (http_GetStatus(bo->beresp) <= 199) {
		/*
		 * 1xx responses never have a body.
		 * [RFC7230 3.3.2 p31]
		 * ... but we should never see them.
		 */
		wrk->stats->fetch_1xx++;
		bo->htc->body_status = BS_ERROR;
	} else if (http_IsStatus(bo->beresp, 204)) {
		/*
		 * 204 is "No Content", obviously don't expect a body.
		 * [RFC7230 3.3.1 p29 and 3.3.2 p31]
		 */
		wrk->stats->fetch_204++;
		if ((http_GetHdr(bo->beresp, H_Content_Length, NULL) &&
		    bo->htc->content_length != 0) ||
		    http_GetHdr(bo->beresp, H_Transfer_Encoding, NULL))
			bo->htc->body_status = BS_ERROR;
		else
			bo->htc->body_status = BS_NONE;
	} else if (http_IsStatus(bo->beresp, 304)) {
		/*
		 * 304 is "Not Modified" it has no body.
		 * [RFC7230 3.3 p28]
		 */
		wrk->stats->fetch_304++;
		bo->htc->body_status = BS_NONE;
	} else if (bo->htc->body_status == BS_CHUNKED) {
		wrk->stats->fetch_chunked++;
	} else if (bo->htc->body_status == BS_LENGTH) {
		assert(bo->htc->content_length > 0);
		wrk->stats->fetch_length++;
	} else if (bo->htc->body_status == BS_EOF) {
		wrk->stats->fetch_eof++;
	} else if (bo->htc->body_status == BS_ERROR) {
		wrk->stats->fetch_bad++;
	} else if (bo->htc->body_status == BS_NONE) {
		wrk->stats->fetch_none++;
	} else {
		WRONG("wrong bodystatus");
	}
}

