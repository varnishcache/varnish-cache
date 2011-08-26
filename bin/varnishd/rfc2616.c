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

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include "cache.h"
#include "vrt.h"

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
 * Our "clockless cache" model is syntehsized from the bits of RFC2616
 * that talks about how a cache should react to a clockless origin server,
 * and more or less uses the inverse logic for the opposite relationship.
 *
 */

void
RFC2616_Ttl(const struct sess *sp)
{
	unsigned max_age, age;
	double h_date, h_expires;
	char *p;
	const struct http *hp;

	hp = sp->wrk->beresp;

	assert(sp->wrk->exp.entered != 0.0 && !isnan(sp->wrk->exp.entered));
	/* If all else fails, cache using default ttl */
	sp->wrk->exp.ttl = params->default_ttl;

	max_age = age = 0;
	h_expires = 0;
	h_date = 0;

	/*
	 * Initial cacheability determination per [RFC2616, 13.4]
	 * We do not support ranges yet, so 206 is out.
	 */

	if (http_GetHdr(hp, H_Age, &p)) {
		age = strtoul(p, NULL, 0);
		sp->wrk->exp.age = age;
	}
	if (http_GetHdr(hp, H_Expires, &p))
		h_expires = TIM_parse(p);

	if (http_GetHdr(hp, H_Date, &p))
		h_date = TIM_parse(p);

	switch (sp->err_code) {
	default:
		sp->wrk->exp.ttl = -1.;
		break;
	case 200: /* OK */
	case 203: /* Non-Authoritative Information */
	case 300: /* Multiple Choices */
	case 301: /* Moved Permanently */
	case 302: /* Moved Temporarily */
	case 307: /* Temporary Redirect */
	case 410: /* Gone */
	case 404: /* Not Found */
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

			if (age > max_age)
				sp->wrk->exp.ttl = 0;
			else
				sp->wrk->exp.ttl = max_age - age;
			break;
		}

		/* No expire header, fall back to default */
		if (h_expires == 0)
			break;


		/* If backend told us it is expired already, don't cache. */
		if (h_expires < h_date) {
			sp->wrk->exp.ttl = 0;
			break;
		}

		if (h_date == 0 ||
		    fabs(h_date - sp->wrk->exp.entered) < params->clock_skew) {
			/*
			 * If we have no Date: header or if it is
			 * sufficiently close to our clock we will
			 * trust Expires: relative to our own clock.
			 */
			if (h_expires < sp->wrk->exp.entered)
				sp->wrk->exp.ttl = 0;
			else
				sp->wrk->exp.ttl = h_expires -
				    sp->wrk->exp.entered;
			break;
		} else {
			/*
			 * But even if the clocks are out of whack we can still
			 * derive a relative time from the two headers.
			 * (the negative ttl case is caught above)
			 */
			sp->wrk->exp.ttl = (int)(h_expires - h_date);
		}

	}

	/* calculated TTL, Our time, Date, Expires, max-age, age */
	WSP(sp, SLT_TTL,
	    "%u RFC %.0f %.0f %.0f %.0f %.0f %.0f %.0f %u",
	    sp->xid, sp->wrk->exp.ttl, -1., -1., sp->wrk->exp.entered,
	    sp->wrk->exp.age, h_date, h_expires, max_age);
}

/*--------------------------------------------------------------------
 * Body existence, fetch method and close policy.
 */

enum body_status
RFC2616_Body(const struct sess *sp)
{
	struct http *hp;
	char *b;

	hp = sp->wrk->beresp;

	if (hp->protover < 11 && !http_HdrIs(hp, H_Connection, "keep-alive"))
		sp->wrk->do_close = 1;
	else if (http_HdrIs(hp, H_Connection, "close"))
		sp->wrk->do_close = 1;
	else
		sp->wrk->do_close = 0;

	if (!strcasecmp(http_GetReq(sp->wrk->bereq), "head")) {
		/*
		 * A HEAD request can never have a body in the reply,
		 * no matter what the headers might say.
		 * [RFC2516 4.3 p33]
		 */
		sp->wrk->stats.fetch_head++;
		return (BS_NONE);
	}

	if (hp->status <= 199) {
		/*
		 * 1xx responses never have a body.
		 * [RFC2616 4.3 p33]
		 */
		sp->wrk->stats.fetch_1xx++;
		return (BS_NONE);
	}

	if (hp->status == 204) {
		/*
		 * 204 is "No Content", obviously don't expect a body.
		 * [RFC2616 10.2.5 p60]
		 */
		sp->wrk->stats.fetch_204++;
		return (BS_NONE);
	}

	if (hp->status == 304) {
		/*
		 * 304 is "Not Modified" it has no body.
		 * [RFC2616 10.3.5 p63]
		 */
		sp->wrk->stats.fetch_304++;
		return (BS_NONE);
	}

	if (http_HdrIs(hp, H_Transfer_Encoding, "chunked")) {
		 sp->wrk->stats.fetch_chunked++;
		return (BS_CHUNKED);
	}

	if (http_GetHdr(hp, H_Transfer_Encoding, &b)) {
		sp->wrk->stats.fetch_bad++;
		return (BS_ERROR);
	}

	if (http_GetHdr(hp, H_Content_Length, &sp->wrk->h_content_length)) {
		sp->wrk->stats.fetch_length++;
		return (BS_LENGTH);
	}

	if (http_HdrIs(hp, H_Connection, "keep-alive")) {
		/*
		 * Keep alive with neither TE=Chunked or C-Len is impossible.
		 * We assume a zero length body.
		 */
		sp->wrk->stats.fetch_zero++;
		return (BS_ZERO);
	}

	if (http_HdrIs(hp, H_Connection, "close")) {
		/*
		 * In this case, it is safe to just read what comes.
		 */
		sp->wrk->stats.fetch_close++;
		return (BS_EOF);
	}

	if (hp->protover < 11) {
		/*
		 * With no Connection header, assume EOF.
		 */
		sp->wrk->stats.fetch_oldhttp++;
		return (BS_EOF);
	}

	/*
	 * Fall back to EOF transfer.
	 */
	sp->wrk->stats.fetch_eof++;
	return (BS_EOF);
}

/*--------------------------------------------------------------------
 * Find out if the request can receive a gzip'ed response
 */

unsigned
RFC2616_Req_Gzip(const struct sess *sp)
{


	/*
	 * "x-gzip" is for http/1.0 backwards compat, final note in 14.3
	 * p104 says to not do q values for x-gzip, so we just test
	 * for its existence.
	 */
	if (http_GetHdrData(sp->http, H_Accept_Encoding, "x-gzip", NULL))
		return (1);

	/*
	 * "gzip" is the real thing, but the 'q' value must be nonzero.
	 * We do not care a hoot if the client prefers some other
	 * compression more than gzip: Varnish only does gzip.
	 */
	if (http_GetHdrQ(sp->http, H_Accept_Encoding, "gzip") > 0.)
		return (1);

	/* Bad client, no gzip. */
	return (0);
}

/*--------------------------------------------------------------------*/

int
RFC2616_Do_Cond(const struct sess *sp)
{
	char *p, *e;
	double ims;
	int do_cond = 0;

	/* RFC 2616 13.3.4 states we need to match both ETag
	   and If-Modified-Since if present*/

	if (http_GetHdr(sp->http, H_If_Modified_Since, &p) ) {
		if (!sp->obj->last_modified)
			return (0);
		ims = TIM_parse(p);
		if (ims > sp->t_req)	/* [RFC2616 14.25] */
			return (0);
		if (sp->obj->last_modified > ims)
			return (0);
		do_cond = 1;
	}

	if (http_GetHdr(sp->http, H_If_None_Match, &p) &&
	    http_GetHdr(sp->obj->http, H_ETag, &e)) {
		if (strcmp(p,e) != 0)
			return (0);
		do_cond = 1;
	}

	return (do_cond);
}
