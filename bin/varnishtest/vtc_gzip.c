/*-
 * Copyright (c) 2008-2019 Varnish Software AS
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vtc.h"
#include "vtc_http.h"
#include "vgz.h"

#define OVERHEAD 64L

#ifdef VGZ_EXTENSIONS
static void
vtc_report_gz_bits(const struct http *hp, z_stream *vz)
{
	vtc_log(hp->vl, 4, "startbit = %ju %ju/%ju",
	    (uintmax_t)vz->start_bit,
	    (uintmax_t)vz->start_bit >> 3, (uintmax_t)vz->start_bit & 7);
	vtc_log(hp->vl, 4, "lastbit = %ju %ju/%ju",
	    (uintmax_t)vz->last_bit,
	    (uintmax_t)vz->last_bit >> 3, (uintmax_t)vz->last_bit & 7);
	vtc_log(hp->vl, 4, "stopbit = %ju %ju/%ju",
	    (uintmax_t)vz->stop_bit,
	    (uintmax_t)vz->stop_bit >> 3, (uintmax_t)vz->stop_bit & 7);
}
#endif

void
vtc_gzip(const struct http *hp, const char *input, char **body, long *bodylen)
{
	unsigned l;
	z_stream vz;
#ifdef VGZ_EXTENSIONS
	int i;
#endif

	memset(&vz, 0, sizeof vz);

	l = strlen(input);
	*body = calloc(1, l + OVERHEAD);
	AN(*body);

	vz.next_in = TRUST_ME(input);
	vz.avail_in = l;

	vz.next_out = TRUST_ME(*body);
	vz.avail_out = l + OVERHEAD;

	assert(Z_OK == deflateInit2(&vz,
	    hp->gziplevel, Z_DEFLATED, 31, 9, Z_DEFAULT_STRATEGY));
	assert(Z_STREAM_END == deflate(&vz, Z_FINISH));
	*bodylen = vz.total_out;
#ifdef VGZ_EXTENSIONS
	i = vz.stop_bit & 7;
	if (hp->gzipresidual >= 0 && hp->gzipresidual != i)
		vtc_log(hp->vl, hp->fatal,
		    "Wrong gzip residual got %d wanted %d",
		    i, hp->gzipresidual);
	vtc_report_gz_bits(hp, &vz);
#endif
	assert(Z_OK == deflateEnd(&vz));
}

void
vtc_gunzip(struct http *hp, char *body, long *bodylen)
{
	z_stream vz;
	char *p;
	unsigned l;
	int i;

	memset(&vz, 0, sizeof vz);

	AN(body);
	if (body[0] != (char)0x1f || body[1] != (char)0x8b)
		vtc_log(hp->vl, hp->fatal,
		    "Gunzip error: body lacks gzip magic");
	vz.next_in = TRUST_ME(body);
	vz.avail_in = *bodylen;

	l = *bodylen * 10;
	p = calloc(1, l);
	AN(p);

	vz.next_out = TRUST_ME(p);
	vz.avail_out = l;

	assert(Z_OK == inflateInit2(&vz, 31));
	i = inflate(&vz, Z_FINISH);
	assert(vz.total_out < l);
	*bodylen = vz.total_out;
	memcpy(body, p, *bodylen);
	free(p);
	vtc_log(hp->vl, 3, "new bodylen %ld", *bodylen);
	vtc_dump(hp->vl, 4, "body", body, *bodylen);
	bprintf(hp->bodylen, "%ld", *bodylen);
#ifdef VGZ_EXTENSIONS
	vtc_report_gz_bits(hp, &vz);
#endif
	if (i != Z_STREAM_END)
		vtc_log(hp->vl, hp->fatal,
		    "Gunzip error = %d (%s) in:%jd out:%jd",
		    i, vz.msg, (intmax_t)vz.total_in, (intmax_t)vz.total_out);
	assert(Z_OK == inflateEnd(&vz));
	body[*bodylen] = '\0';
}
