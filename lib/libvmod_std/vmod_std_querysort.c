/*-
 * Copyright (c) 2010-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Naren Venkataraman of Vimeo Inc.
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

#include "vrt.h"

#include "cache/cache.h"

#include "vcc_if.h"

static int
compa(const void *a, const void *b)
{
	const char * const *pa = a;
	const char * const *pb = b;
	const char *a1, *b1;

	for(a1 = pa[0], b1 = pb[0]; a1 < pa[1] && b1 < pb[1]; a1++, b1++)
		if (*a1 != *b1)
			return (*a1 - *b1);
	return (0);
}

VCL_STRING __match_proto__(td_std_querysort)
vmod_querysort(VRT_CTX, VCL_STRING url)
{
	const char *cq, *cu;
	char *p, *r;
	const char **pp;
	const char **pe;
	int np;
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (url == NULL)
		return (NULL);

	/* Split :query from :url */
	cu = strchr(url, '?');
	if (cu == NULL)
		return (url);

	/* Spot single-param queries */
	cq = strchr(cu, '&');
	if (cq == NULL)
		return (url);

	r = WS_Copy(ctx->ws, url, -1);
	if (r == NULL)
		return (url);

	(void)WS_Reserve(ctx->ws, 0);
	/* We trust cache_ws.c to align sensibly */
	pp = (const char**)(void*)(ctx->ws->f);
	pe = (const char**)(void*)(ctx->ws->e);

	if (pp + 4 > pe) {
		WS_Release(ctx->ws, 0);
		WS_MarkOverflow(ctx->ws);
		return (url);
	}

	/* Collect params as pointer pairs */
	np = 0;
	pp[np++] = 1 + cu;
	for (cq = 1 + cu; *cq != '\0'; cq++) {
		if (*cq == '&') {
			if (pp + 3 > pe) {
				WS_Release(ctx->ws, 0);
				WS_MarkOverflow(ctx->ws);
				return (url);
			}
			pp[np++] = cq;
			/* Skip trivially empty params */
			while(cq[1] == '&')
				cq++;
			pp[np++] = cq + 1;
		}
	}
	pp[np++] = cq;
	assert(!(np & 1));

	qsort(pp, np / 2, sizeof(*pp) * 2, compa);

	/* Emit sorted params */
	p = 1 + r + (cu - url);
	cq = "";
	for (i = 0; i < np; i += 2) {
		/* Ignore any edge-case zero length params */
		if (pp[i + 1] == pp[i])
			continue;
		assert(pp[i + 1] > pp[i]);
		if (*cq)
			*p++ = *cq;
		memcpy(p, pp[i], pp[i + 1] - pp[i]);
		p += pp[i + 1] - pp[i];
		cq = "&";
	}
	*p = '\0';

	WS_Release(ctx->ws, 0);
	return (r);
}
