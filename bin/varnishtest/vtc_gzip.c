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

#ifdef VGZ_EXTENSIONS
static void
vtc_report_gz_bits(struct vtclog *vl, const z_stream *vz)
{
	vtc_log(vl, 4, "startbit = %ju %ju/%ju",
	    (uintmax_t)vz->start_bit,
	    (uintmax_t)vz->start_bit >> 3, (uintmax_t)vz->start_bit & 7);
	vtc_log(vl, 4, "lastbit = %ju %ju/%ju",
	    (uintmax_t)vz->last_bit,
	    (uintmax_t)vz->last_bit >> 3, (uintmax_t)vz->last_bit & 7);
	vtc_log(vl, 4, "stopbit = %ju %ju/%ju",
	    (uintmax_t)vz->stop_bit,
	    (uintmax_t)vz->stop_bit >> 3, (uintmax_t)vz->stop_bit & 7);
}
#endif

static struct vsb *
vtc_gzip_vsb(struct vtclog *vl, int fatal, int gzip_level, const struct vsb *vin, int *residual)
{
	z_stream vz;
	struct vsb *vout;
	int i;
	char buf[BUFSIZ];

	AN(residual);
	memset(&vz, 0, sizeof vz);
	vout = VSB_new_auto();
	AN(vout);

	vz.next_in = (void*)VSB_data(vin);
	vz.avail_in = VSB_len(vin);

	assert(Z_OK == deflateInit2(&vz,
	    gzip_level, Z_DEFLATED, 31, 9, Z_DEFAULT_STRATEGY));

	do {
		vz.next_out = (void*)buf;
		vz.avail_out = sizeof buf;
		i = deflate(&vz, Z_FINISH);
		if (vz.avail_out != sizeof buf)
			VSB_bcat(vout, buf, sizeof buf - vz.avail_out);
	} while (i == Z_OK || i == Z_BUF_ERROR);
	if (i != Z_STREAM_END)
		vtc_log(vl, fatal,
		    "Gzip error = %d (%s) in:%jd out:%jd",
		    i, vz.msg, (intmax_t)vz.total_in, (intmax_t)vz.total_out);
	AZ(VSB_finish(vout));
#ifdef VGZ_EXTENSIONS
	*residual = vz.stop_bit & 7;
	vtc_report_gz_bits(vl, &vz);
#else
	*residual = 0;
#endif
	assert(Z_OK == deflateEnd(&vz));
	return (vout);
}

void
vtc_gzip(struct http *hp, const char *input, char **body, long *bodylen)
{
	struct vsb *vin, *vout;
	int res;

	vin = VSB_new_auto();
	AN(vin);
	VSB_bcat(vin, input, strlen(input));
	AZ(VSB_finish(vin));
	vout = vtc_gzip_vsb(hp->vl, hp->fatal, hp->gziplevel, vin, &res);
	VSB_destroy(&vin);

#ifdef VGZ_EXTENSIONS
	if (hp->gzipresidual >= 0 && hp->gzipresidual != res)
		vtc_log(hp->vl, hp->fatal,
		    "Wrong gzip residual got %d wanted %d",
		    res, hp->gzipresidual);
#endif
	*body = malloc(VSB_len(vout) + 1);
	AN(*body);
	memcpy(*body, VSB_data(vout), VSB_len(vout) + 1);
	*bodylen = VSB_len(vout);
	VSB_destroy(&vout);
	vtc_log(hp->vl, 3, "new bodylen %ld", *bodylen);
	vtc_dump(hp->vl, 4, "body", *body, *bodylen);
	bprintf(hp->bodylen, "%ld", *bodylen);
}

static struct vsb *
vtc_gunzip_vsb(struct vtclog *vl, int fatal, const struct vsb *vin)
{
	z_stream vz;
	struct vsb *vout;
	int i;
	char buf[BUFSIZ];

	memset(&vz, 0, sizeof vz);
	vout = VSB_new_auto();
	AN(vout);

	vz.next_in = (void*)VSB_data(vin);
	vz.avail_in = VSB_len(vin);

	assert(Z_OK == inflateInit2(&vz, 31));

	do {
		vz.next_out = (void*)buf;
		vz.avail_out = sizeof buf;
		i = inflate(&vz, Z_FINISH);
		if (vz.avail_out != sizeof buf)
			VSB_bcat(vout, buf, sizeof buf - vz.avail_out);
	} while (i == Z_OK || i == Z_BUF_ERROR);
	if (i != Z_STREAM_END)
		vtc_log(vl, fatal,
		    "Gunzip error = %d (%s) in:%jd out:%jd",
		    i, vz.msg, (intmax_t)vz.total_in, (intmax_t)vz.total_out);
	AZ(VSB_finish(vout));
#ifdef VGZ_EXTENSIONS
	vtc_report_gz_bits(vl, &vz);
#endif
	assert(Z_OK == inflateEnd(&vz));
	return (vout);
}

void
vtc_gunzip(struct http *hp, char *body, long *bodylen)
{
	struct vsb *vin, *vout;

	AN(body);
	if (body[0] != (char)0x1f || body[1] != (char)0x8b)
		vtc_log(hp->vl, hp->fatal,
		    "Gunzip error: body lacks gzip magic");

	vin = VSB_new_auto();
	AN(vin);
	VSB_bcat(vin, body, *bodylen);
	AZ(VSB_finish(vin));
	vout = vtc_gunzip_vsb(hp->vl, hp->fatal, vin);
	VSB_destroy(&vin);

	memcpy(body, VSB_data(vout), VSB_len(vout) + 1);
	*bodylen = VSB_len(vout);
	VSB_destroy(&vout);
	vtc_log(hp->vl, 3, "new bodylen %ld", *bodylen);
	vtc_dump(hp->vl, 4, "body", body, *bodylen);
	bprintf(hp->bodylen, "%ld", *bodylen);
}
