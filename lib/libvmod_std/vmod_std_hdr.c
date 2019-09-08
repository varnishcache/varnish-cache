/*-
 * Copyright (c) 2011-2016 Varnish Software
 * All rights reserved.
 *
 * Author: Kristian Lyngstol <kristian@bohemians.org>
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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <cache/cache.h>
#include <vcl.h>

//#ifndef VRT_H_INCLUDED
//#  include <vrt.h>
//#endif

//#ifndef VDEF_H_INCLUDED
//#  include <vdef.h>
//#endif

#include "vcc_if.h"

/*
 * This mutex is used to avoid having two threads that initializes the same
 * regex at the same time. While it means that there's a single, global
 * lock for all libvmod-header actions dealing with regular expressions,
 * the contention only applies on the first request that calls that
 * specific function.
 */
pthread_mutex_t header_mutex;

/*
 * Initialize the regex *s on priv, if it hasn't already been done.
 * XXX: We have to recheck the condition after grabbing the lock to avoid a
 * XXX: race condition.
 */
static void
header_init_re(struct vmod_priv *priv, const char *s)
{
	if (priv->priv == NULL) {
		assert(pthread_mutex_lock(&header_mutex) == 0);
		if (priv->priv == NULL) {
			VRT_re_init(&priv->priv, s);
			priv->free = VRT_re_fini;
		}
		pthread_mutex_unlock(&header_mutex);
	}
}

/*
 * Returns true if the *hdr header is the one pointed to by *hh.
 *
 * FIXME: duplication from varnishd.
 */
static int
header_http_IsHdr(const txt *hh, const char *hdr)
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

/*
 * Return true if the hp->hd[u] header matches *hdr and the regex *re
 * matches the content.
 *
 * If re is NULL, content is not tested and as long as it's the right
 * header, a match is returned.
 */
static int
header_http_match(VRT_CTX, const struct http *hp, unsigned u, void *re,
    const char *hdr)
{
	const char *start;
	unsigned l;

	assert(hdr);
	assert(hp);

	Tcheck(hp->hd[u]);
	if (hp->hd[u].b == NULL)
		return (0);

	l = hdr[0];

	if (!header_http_IsHdr(&hp->hd[u], hdr))
		return (0);

	if (re == NULL)
		return (1);

	start = hp->hd[u].b + l;
	while (*start != '\0' && *start == ' ')
		start++;

	if (!*start)
		return (0);
	if (VRT_re_match(ctx, start, re))
		return (1);

	return (0);
}

/*
 * Returns the (first) header named as *hdr that also matches the regular
 * expression *re.
 */
static unsigned
header_http_findhdr(VRT_CTX, const struct http *hp, const char *hdr,
    void *re)
{
        unsigned u;

        for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		if (header_http_match(ctx, hp, u, re, hdr))
			return (u);
        }
        return (0);
}

/*
 * Removes all copies of the header that matches *hdr with content that
 * matches *re. Same as http_Unset(), plus regex.
 */
static void
header_http_Unset(VRT_CTX, struct http *hp, const char *hdr, void *re)
{
	unsigned u, v;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	for (v = u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		if (hp->hd[u].b == NULL)
			continue;
		if (header_http_match(ctx, hp, u, re, hdr))
			continue;
		if (v != u) {
			memcpy(&hp->hd[v], &hp->hd[u], sizeof *hp->hd);
			memcpy(&hp->hdf[v], &hp->hdf[u], sizeof *hp->hdf);
		}
		v++;
	}
	hp->nhd = v;
}

/*
 * Copies all occurrences of *hdr to a destination header *dst_h. Uses
 * vmod_header_append(), so all copies are kept intact.
 *
 * XXX: Not sure I like the idea of iterating a list of headers while
 * XXX: adding to it. It may be correct now, but perhaps not so much in
 * XXX: the future.
 */
static void
header_http_cphdr(VRT_CTX, const struct http *hp, const char *hdr,
    VCL_HEADER dst)
{
        unsigned u;
	const char *p;
	struct strands s; 

        for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		if (!header_http_match(ctx, hp, u, NULL, hdr))
			continue;

		p = hp->hd[u].b + hdr[0];
		while (*p == ' ' || *p == '\t')
			p++;
		s.n = 1;
		s.p = &p;
                vmod_hdr_append(ctx, dst, &s);
        }
}

/*
 * vmod entrypoint. Sets up the header mutex.
 */
int v_matchproto_(vmod_event_f)
vmod_event_function(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{
	(void)ctx;
	(void)priv;

	if (e != VCL_EVENT_LOAD)
		return (0);
	assert(pthread_mutex_init(&header_mutex, NULL) == 0);
	return (0);
}

VCL_VOID v_matchproto_(td_header_append)
vmod_hdr_append(VRT_CTX, VCL_HEADER hdr, VCL_STRANDS s)
{
	struct http *hp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	hp = VRT_selecthttp(ctx, hdr->where);
	http_SetHeader(hp, VRT_StrandsWS(ctx->ws, hdr->what + 1, s));
}

VCL_STRING v_matchproto_(td_header_get)
vmod_hdr_get(VRT_CTX, struct vmod_priv *priv, VCL_HEADER hdr, VCL_STRING s)
{
	struct http *hp;
	unsigned u;
	const char *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	header_init_re(priv, s);

	hp = VRT_selecthttp(ctx, hdr->where);
	u = header_http_findhdr(ctx, hp, hdr->what, priv->priv);
	if (u == 0)
		return (NULL);
	p = hp->hd[u].b + hdr->what[0];
	while (*p == ' ' || *p == '\t')
		p++;
	return (p);
}

VCL_VOID v_matchproto_(td_header_copy)
vmod_hdr_copy(VRT_CTX, VCL_HEADER src, VCL_HEADER dst)
{
	struct http *src_hp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	src_hp = VRT_selecthttp(ctx, src->where);
	header_http_cphdr(ctx, src_hp, src->what, dst);
}

VCL_VOID v_matchproto_(td_header_remove)
vmod_hdr_remove(VRT_CTX, struct vmod_priv *priv, VCL_HEADER hdr, VCL_STRING s)
{
	struct http *hp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	header_init_re(priv, s);

	hp = VRT_selecthttp(ctx, hdr->where);
	header_http_Unset(ctx, hp, hdr->what, priv->priv);
}
