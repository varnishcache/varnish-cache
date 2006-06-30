/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "cache.h"
#include "libvarnish.h"
#include "heritage.h"
/*--------------------------------------------------------------------
 * From RFC2616, 13.2.3 Age Calculations
 *
 * age_value
 *      is the value of Age: header received by the cache with
 *      this response.
 * date_value
 *      is the value of the origin server's Date: header
 * request_time
 *      is the (local) time when the cache made the request
 *      that resulted in this cached response
 * response_time
 *      is the (local) time when the cache received the response
 * now
 *      is the current (local) time
 *
 * apparent_age = max(0, response_time - date_value);
 * corrected_received_age = max(apparent_age, age_value);
 * response_delay = response_time - request_time;
 * corrected_initial_age = corrected_received_age + response_delay;
 * resident_time = now - response_time;
 * current_age   = corrected_initial_age + resident_time;
 *
 */

static time_t
RFC2616_Ttl(struct http *hp, time_t t_req, time_t t_resp)
{
	time_t h_date = 0, h_expires = 0, h_age = 0;
	time_t apparent_age = 0, corrected_received_age;
	time_t response_delay, corrected_initial_age;
	time_t max_age = -1, ttl;
	time_t fudge;
	char *p;

	if (http_GetHdrField(hp, "Cache-Control", "max-age", &p))
		max_age = strtoul(p, NULL, 0);

	if (http_GetHdr(hp, "Date", &p))
		h_date = TIM_parse(p);

	if (h_date + 3600 < t_resp) {
		fudge = t_resp - h_date;
		h_date += fudge;
	} else
		fudge = 0;

	if (h_date < t_resp)
		apparent_age = t_resp - h_date;

	if (http_GetHdr(hp, "Age", &p))
		h_age = strtoul(p, NULL, 0);

	if (h_age > apparent_age)
		corrected_received_age = h_age;
	else
		corrected_received_age = apparent_age;

	response_delay = t_resp - t_req;
	corrected_initial_age = corrected_received_age + response_delay;

	if (http_GetHdr(hp, "Expires", &p)) {
		h_expires = TIM_parse(p) + fudge;
	}

	printf("Date: %d\n", h_date);
	printf("Recv: %d\n", t_resp);
	printf("Expires: %d\n", h_expires);
	printf("Age: %d\n", h_age);
	printf("CIAge: %d\n", corrected_initial_age);
	printf("Max-Age: %d\n", max_age);
	ttl = 0;
	if (max_age >= 0)
		ttl = t_resp + max_age - corrected_initial_age;
	if (h_expires && h_expires < ttl)
		ttl = h_expires;
	if (ttl == 0)
		ttl = t_resp + heritage.default_ttl;
	printf("TTL: %d (%+d)\n", ttl, ttl - t_resp);
	if (ttl < t_resp)
		return (0);

	return (ttl);
}

int
RFC2616_cache_policy(struct sess *sp, struct http *hp)
{
	int body = 0;

	/*
	 * Initial cacheability determination per [RFC2616, 13.4]
	 * We do not support ranges yet, so 206 is out.
	 */
	switch (http_GetStatus(hp)) {
	case 200: /* OK */
		sp->obj->valid = 1;
	case 203: /* Non-Authoritative Information */
	case 300: /* Multiple Choices */
	case 301: /* Moved Permanently */
	case 410: /* Gone */
		sp->obj->cacheable = 1;
		body = 1;
		break;
	default:
		sp->obj->cacheable = 0;
		body = 0;
		break;
	}

	sp->obj->ttl = RFC2616_Ttl(hp, sp->t_req, sp->t_resp);
	if (sp->obj->ttl == 0) {
		sp->obj->cacheable = 0;
	}

	return (body);

}

