/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include "shmlog.h"
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

double
RFC2616_Ttl(const struct sess *sp)
{
	int ttl;
	unsigned max_age, age;
	double h_date, h_expires, ttd;
	char *p;
	const struct http *hp;

	hp = sp->wrk->beresp;

	assert(sp->wrk->entered != 0.0 && !isnan(sp->wrk->entered));
	/* If all else fails, cache using default ttl */
	ttl = params->default_ttl;

	max_age = age = 0;
	ttd = 0;
	h_expires = 0;
	h_date = 0;

	do {	/* Allows us to break when we want out */

		/*
		 * First find any relative specification from the backend
		 * These take precedence according to RFC2616, 13.2.4
		 */

		if ((http_GetHdrField(hp, H_Cache_Control, "s-maxage", &p) ||
		    http_GetHdrField(hp, H_Cache_Control, "max-age", &p)) &&
		    p != NULL) {

			max_age = strtoul(p, NULL, 0);
			if (http_GetHdr(hp, H_Age, &p)) {
				age = strtoul(p, NULL, 0);
				sp->wrk->age = age;
			}

			if (age > max_age)
				ttl = 0;
			else
				ttl = max_age - age;
			break;
		}

		/* Next look for absolute specifications from backend */

		if (http_GetHdr(hp, H_Expires, &p))
			h_expires = TIM_parse(p);
		if (h_expires == 0)
			break;

		if (http_GetHdr(hp, H_Date, &p))
			h_date = TIM_parse(p);

		/* If backend told us it is expired already, don't cache. */
		if (h_expires < h_date) {
			ttl = 0;
			break;
		}

		if (h_date == 0 ||
		    (h_date < sp->wrk->entered + params->clock_skew &&
		    h_date + params->clock_skew > sp->wrk->entered)) {
			/*
			 * If we have no Date: header or if it is
			 * sufficiently close to our clock we will
			 * trust Expires: relative to our own clock.
			 */
			if (h_expires < sp->wrk->entered)
				ttl = 0;
			else
				ttd = h_expires;
			break;
		}

		/*
		 * But even if the clocks are out of whack we can still
		 * derive a relative time from the two headers.
		 * (the negative ttl case is caught above)
		 */
		ttl = (int)(h_expires - h_date);

	} while (0);

	if (ttl > 0 && ttd == 0)
		ttd = sp->wrk->entered + ttl;

	/* calculated TTL, Our time, Date, Expires, max-age, age */
	WSP(sp, SLT_TTL, "%u RFC %d %d %d %d %u %u", sp->xid,
	    ttd ? (int)(ttd - sp->wrk->entered) : 0,
	    (int)sp->wrk->entered, (int)h_date,
	    (int)h_expires, max_age, age);

	return (ttd);
}

/*--------------------------------------------------------------------
 * Body existence and fetch method
 * XXX: Missing:  RFC2616 sec. 4.4 in re 1xx, 204 & 304 responses
 */

enum body_status
RFC2616_Body(const struct sess *sp)
{
	struct http *hp;
	char *b;

	hp = sp->wrk->beresp1;

	if (!strcasecmp(http_GetReq(sp->wrk->bereq), "head")) {
		/*
		 * A HEAD request can never have a body in the reply,
		 * no matter what the headers might say.
		 */
		sp->wrk->stats.fetch_head++;
		return (BS_NONE);
	}

	/* If the headers tells us what to do, obey. */

	if (http_GetHdr(hp, H_Content_Length, &b)) {
		sp->wrk->stats.fetch_length++;
		return (BS_LENGTH);
	}

	if (http_HdrIs(hp, H_Transfer_Encoding, "chunked")) {
		 sp->wrk->stats.fetch_chunked++;
		return (BS_CHUNKED);
	}

	if (http_GetHdr(hp, H_Transfer_Encoding, &b)) {
		sp->wrk->stats.fetch_bad++;
		return (BS_ERROR);
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

	if (hp->protover < 1.1) {
		/*
		 * With no Connection header, assume EOF.
		 */
		sp->wrk->stats.fetch_oldhttp++;
		return (BS_EOF);
	}

	/*
	 * XXX: Here it should depends on the status code
	 */
	sp->wrk->stats.fetch_eof++;
	return (BS_EOF);
}
