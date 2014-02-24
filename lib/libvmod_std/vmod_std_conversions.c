/*-
 * Copyright (c) 2010-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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

#include <ctype.h>
#include <math.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "cache/cache.h"

#include "vrt.h"
#include "vsa.h"
#include "vcc_if.h"

VCL_DURATION __match_proto__()
vmod_duration(const struct vrt_ctx *ctx, const char *p, VCL_DURATION d)
{
	char *e;
	double r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (p == NULL)
		return (d);

	while(isspace(*p))
		p++;

	if (*p != '+' && *p != '-' && !isdigit(*p))
		return (d);

	e = NULL;

	r = strtod(p, &e);

	if (!isfinite(r))
		return (d);

	if (e == NULL)
		return (d);

	while(isspace(*e))
		e++;

	/* NB: Keep this list synchronized with VCC */
	switch (*e++) {
	case 's': break;
	case 'm':
		if (*e == 's') {
			r *= 1e-3;
			e++;
		} else
			r *= 60.;
		break;
	case 'h': r *= 60.*60.; break;
	case 'd': r *= 60.*60.*24.; break;
	case 'w': r *= 60.*60.*24.*7.; break;
	default:
		return (d);
	}

	while(isspace(*e))
		e++;

	if (*e != '\0')
		return (d);

	return (r);
}

VCL_INT __match_proto__()
vmod_integer(const struct vrt_ctx *ctx, const char *p, VCL_INT i)
{
	char *e;
	long r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (p == NULL)
		return (i);

	while(isspace(*p))
		p++;

	if (*p != '+' && *p != '-' && !isdigit(*p))
		return (i);

	e = NULL;

	r = strtol(p, &e, 0);

	if (e == NULL)
		return (i);

	if (*e != '\0')
		return (i);

	return (r);
}

VCL_IP
vmod_ip(const struct vrt_ctx *ctx, VCL_STRING s, VCL_IP d)
{
	struct addrinfo hints, *res0 = NULL;
	const struct addrinfo *res;
	int error;
	void *p;
	struct suckaddr *r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(d);
	assert(VSA_Sane(d));

	p = WS_Alloc(ctx->ws, vsa_suckaddr_len);
	AN(p);
	r = NULL;

	if (s != NULL) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		error = getaddrinfo(s, "80", &hints, &res0);
		if (!error) {
			for (res = res0; res != NULL; res = res->ai_next) {
				r = VSA_Build(p, res->ai_addr, res->ai_addrlen);
				if (r != NULL)
					break;
			}
		}
	}
	if (r == NULL) {
		r = p;
		memcpy(r, d, vsa_suckaddr_len);
	}
	if (res0 != NULL)
		freeaddrinfo(res0);
	return (r);
}
