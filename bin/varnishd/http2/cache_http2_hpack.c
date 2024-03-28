/*-
 * Copyright (c) 2016-2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include "cache/cache_varnishd.h"

#include <ctype.h>
#include <stdio.h>

#include "http2/cache_http2.h"
#include "vct.h"

static void
h2h_assert_ready(struct h2h_decode *d)
{

	CHECK_OBJ_NOTNULL(d, H2H_DECODE_MAGIC);
	AN(d->out);
	assert(d->namelen >= 2); /* 2 chars from the ": " that we added */
	assert(d->namelen <= d->out_u);
	assert(d->out[d->namelen - 2] == ':');
	assert(d->out[d->namelen - 1] == ' ');
}

// rfc9113,l,2493,2528
static h2_error
h2h_checkhdr(struct vsl_log *vsl, txt nm, txt val)
{
	const char *p;
	int l;
	enum {
		FLD_NAME_FIRST,
		FLD_NAME,
		FLD_VALUE_FIRST,
		FLD_VALUE
	} state;

	if (Tlen(nm) == 0) {
		VSLb(vsl, SLT_BogoHeader, "Empty name");
		return (H2SE_PROTOCOL_ERROR);
	}

	// VSLb(vsl, SLT_Debug, "CHDR [%.*s] [%.*s]",
	//     (int)Tlen(nm), nm.b, (int)Tlen(val), val.b);

	l = vmin_t(int, Tlen(nm) + 2 + Tlen(val), 20);
	state = FLD_NAME_FIRST;
	for (p = nm.b; p < nm.e; p++) {
		switch(state) {
		case FLD_NAME_FIRST:
			state = FLD_NAME;
			if (*p == ':')
				break;
			/* FALLTHROUGH */
		case FLD_NAME:
			if (isupper(*p)) {
				VSLb(vsl, SLT_BogoHeader,
				    "Illegal field header name (upper-case): %.*s",
				    l, nm.b);
				return (H2SE_PROTOCOL_ERROR);
			}
			if (!vct_istchar(*p) || *p == ':') {
				VSLb(vsl, SLT_BogoHeader,
				    "Illegal field header name (non-token): %.*s",
				    l, nm.b);
				return (H2SE_PROTOCOL_ERROR);
			}
			break;
		default:
			WRONG("http2 field name validation state");
		}
	}

	state = FLD_VALUE_FIRST;
	for (p = val.b; p < val.e; p++) {
		switch(state) {
		case FLD_VALUE_FIRST:
			if (vct_issp(*p)) {
				VSLb(vsl, SLT_BogoHeader,
				    "Illegal field value start %.*s", l, nm.b);
				return (H2SE_PROTOCOL_ERROR);
			}
			state = FLD_VALUE;
			/* FALLTHROUGH */
		case FLD_VALUE:
			if (!vct_ishdrval(*p)) {
				VSLb(vsl, SLT_BogoHeader,
				    "Illegal field value %.*s", l, nm.b);
				return (H2SE_PROTOCOL_ERROR);
			}
			break;
		default:
			WRONG("http2 field value validation state");
		}
	}
	if (state == FLD_VALUE && vct_issp(val.e[-1])) {
		VSLb(vsl, SLT_BogoHeader,
		    "Illegal field value (end) %.*s", l, nm.b);
		return (H2SE_PROTOCOL_ERROR);
	}
	return (0);
}

static h2_error
h2h_addhdr(struct http *hp, struct h2h_decode *d)
{
	/* XXX: This might belong in cache/cache_http.c */
	txt hdr, nm, val;
	int disallow_empty;
	const char *p;
	unsigned n, has_dup;
	h2_error err;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	h2h_assert_ready(d);

	/* Assume hdr is by default a regular header from what we decoded. */
	hdr.b = d->out;
	hdr.e = hdr.b + d->out_u;
	n = hp->nhd;

	/* nm and val are separated by ": " */
	nm.b = hdr.b;
	nm.e = nm.b + d->namelen - 2;
	val.b = nm.e + 2;
	val.e = hdr.e;

	err = h2h_checkhdr(hp->vsl, nm, val);
	if (err != NULL)
		return (err);

	disallow_empty = 0;
	has_dup = 0;

	if (Tlen(hdr) > UINT_MAX) {	/* XXX: cache_param max header size */
		VSLb(hp->vsl, SLT_BogoHeader, "Header too large: %.20s", hdr.b);
		return (H2SE_ENHANCE_YOUR_CALM);
	}

	/* Match H/2 pseudo headers */
	/* XXX: Should probably have some include tbl for pseudo-headers */
	if (!Tstrcmp(nm, ":method")) {
		hdr.b = val.b;
		n = HTTP_HDR_METHOD;
		disallow_empty = 1;

		/* Check HTTP token */
		for (p = hdr.b; p < hdr.e; p++) {
			if (!vct_istchar(*p))
				return (H2SE_PROTOCOL_ERROR);
		}
	} else if (!Tstrcmp(nm, ":path")) {
		hdr.b = val.b;
		n = HTTP_HDR_URL;
		disallow_empty = 1;

		// rfc9113,l,2693,2705
		if (Tlen(val) > 0 && val.b[0] != '/' && Tstrcmp(val, "*")) {
			VSLb(hp->vsl, SLT_BogoHeader,
			    "Illegal :path pseudo-header %.*s",
			    (int)Tlen(val), val.b);
			return (H2SE_PROTOCOL_ERROR);
		}

		/* Path cannot contain LWS or CTL */
		for (p = hdr.b; p < hdr.e; p++) {
			if (vct_islws(*p) || vct_isctl(*p))
				return (H2SE_PROTOCOL_ERROR);
		}
	} else if (!Tstrcmp(nm, ":scheme")) {
		/* XXX: What to do about this one? (typically
		   "http" or "https"). For now set it as a normal
		   header, stripping the first ':'. */
		hdr.b++;
		has_dup = d->has_scheme;
		d->has_scheme = 1;
		disallow_empty = 1;

		/* Check HTTP token */
		for (p = val.b; p < val.e; p++) {
			if (!vct_istchar(*p))
				return (H2SE_PROTOCOL_ERROR);
		}
	} else if (!Tstrcmp(nm, ":authority")) {
		/* NB: we inject "host" in place of "rity" for
		 * the ":authority" pseudo-header.
		 */
		memcpy(d->out + 6, "host", 4);
		hdr.b += 6;
		nm = Tstr(":authority"); /* preserve original */
		has_dup = d->has_authority;
		d->has_authority = 1;
	} else if (nm.b[0] == ':') {
		VSLb(hp->vsl, SLT_BogoHeader,
		    "Unknown pseudo-header: %.*s",
		    vmin_t(int, Tlen(hdr), 20), hdr.b);
		return (H2SE_PROTOCOL_ERROR);	// rfc7540,l,2990,2992
	}

	if (disallow_empty && Tlen(val) == 0) {
		VSLb(hp->vsl, SLT_BogoHeader,
		    "Empty pseudo-header %.*s",
		    (int)Tlen(nm), nm.b);
		return (H2SE_PROTOCOL_ERROR);
	}

	if (n >= HTTP_HDR_FIRST) {
		/* Check for space in struct http */
		if (n >= hp->shd) {
			VSLb(hp->vsl, SLT_LostHeader,
			    "Too many headers: %.*s",
			    vmin_t(int, Tlen(hdr), 20), hdr.b);
			return (H2SE_ENHANCE_YOUR_CALM);
		}
		hp->nhd++;
		AZ(hp->hd[n].b);
	}

	if (has_dup || hp->hd[n].b != NULL) {
		assert(nm.b[0] == ':');
		VSLb(hp->vsl, SLT_BogoHeader,
		    "Duplicate pseudo-header %.*s",
		    (int)Tlen(nm), nm.b);
		return (H2SE_PROTOCOL_ERROR);	// rfc7540,l,3158,3162
	}

	hp->hd[n] = hdr;
	return (0);
}

void
h2h_decode_init(const struct h2_sess *h2)
{
	struct h2h_decode *d;

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(h2->new_req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(h2->new_req->http, HTTP_MAGIC);
	AN(h2->decode);
	d = h2->decode;
	INIT_OBJ(d, H2H_DECODE_MAGIC);
	VHD_Init(d->vhd);
	d->out_l = WS_ReserveAll(h2->new_req->http->ws);
	/*
	 * Can't do any work without any buffer
	 * space. Require non-zero size.
	 */
	XXXAN(d->out_l);
	d->out = h2->new_req->http->ws->f;
	d->reset = d->out;
}

/* Possible error returns:
 *
 * H2E_COMPRESSION_ERROR: Lost compression state due to incomplete header
 * block. This is a connection level error.
 *
 * H2E_ENHANCE_YOUR_CALM: Ran out of workspace or http header space. This
 * is a stream level error.
 */
h2_error
h2h_decode_fini(const struct h2_sess *h2)
{
	h2_error ret;
	struct h2h_decode *d;

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	d = h2->decode;
	CHECK_OBJ_NOTNULL(h2->new_req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(d, H2H_DECODE_MAGIC);
	WS_ReleaseP(h2->new_req->http->ws, d->out);
	if (d->vhd_ret != VHD_OK) {
		/* HPACK header block didn't finish at an instruction
		   boundary */
		VSLb(h2->new_req->http->vsl, SLT_BogoHeader,
		    "HPACK compression error/fini (%s)", VHD_Error(d->vhd_ret));
		ret = H2CE_COMPRESSION_ERROR;
	} else if (d->error == NULL && !d->has_scheme) {
		VSLb(h2->vsl, SLT_Debug, "Missing :scheme");
		ret = H2SE_MISSING_SCHEME; //rfc7540,l,3087,3090
	} else
		ret = d->error;
	d->magic = 0;
	return (ret);
}

/* Possible error returns:
 *
 * H2E_COMPRESSION_ERROR: Lost compression state due to invalid header
 * block. This is a connection level error.
 *
 * H2E_PROTOCOL_ERROR: Malformed header or duplicate pseudo-header.
 *		       Violation of field name/value charsets
 */
h2_error
h2h_decode_bytes(struct h2_sess *h2, const uint8_t *in, size_t in_l)
{
	struct http *hp;
	struct h2h_decode *d;
	size_t in_u = 0;

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(h2->new_req, REQ_MAGIC);
	hp = h2->new_req->http;
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(hp->ws, WS_MAGIC);
	AN(hp->ws->r);
	d = h2->decode;
	CHECK_OBJ_NOTNULL(d, H2H_DECODE_MAGIC);

	/* Only H2E_ENHANCE_YOUR_CALM indicates that we should continue
	   processing. Other errors should have been returned and handled
	   by the caller. */
	assert(d->error == 0 || d->error == H2SE_ENHANCE_YOUR_CALM);

	while (1) {
		AN(d->out);
		assert(d->out_u <= d->out_l);
		d->vhd_ret = VHD_Decode(d->vhd, h2->dectbl, in, in_l, &in_u,
		    d->out, d->out_l, &d->out_u);

		if (d->vhd_ret < 0) {
			VSLb(hp->vsl, SLT_BogoHeader,
			    "HPACK compression error (%s)",
			    VHD_Error(d->vhd_ret));
			d->error = H2CE_COMPRESSION_ERROR;
			break;
		} else if (d->vhd_ret == VHD_OK || d->vhd_ret == VHD_MORE) {
			assert(in_u == in_l);
			break;
		}

		if (d->error == H2SE_ENHANCE_YOUR_CALM) {
			d->out_u = 0;
			assert(d->out_u < d->out_l);
			continue;
		}

		switch (d->vhd_ret) {
		case VHD_NAME_SEC:
			/* XXX: header flag for never-indexed header */
		case VHD_NAME:
			assert(d->namelen == 0);
			if (d->out_l - d->out_u < 2) {
				d->error = H2SE_ENHANCE_YOUR_CALM;
				break;
			}
			d->out[d->out_u++] = ':';
			d->out[d->out_u++] = ' ';
			d->namelen = d->out_u;
			break;

		case VHD_VALUE_SEC:
			/* XXX: header flag for never-indexed header */
		case VHD_VALUE:
			assert(d->namelen > 0);
			if (d->out_l - d->out_u < 1) {
				d->error = H2SE_ENHANCE_YOUR_CALM;
				break;
			}
			d->error = h2h_addhdr(hp, d);
			if (d->error)
				break;
			d->out[d->out_u++] = '\0'; /* Zero guard */
			d->out += d->out_u;
			d->out_l -= d->out_u;
			d->out_u = 0;
			d->namelen = 0;
			break;

		case VHD_BUF:
			d->error = H2SE_ENHANCE_YOUR_CALM;
			break;

		default:
			WRONG("Unhandled return value");
			break;
		}

		if (d->error == H2SE_ENHANCE_YOUR_CALM) {
			d->out = d->reset;
			d->out_l = hp->ws->r - d->out;
			d->out_u = 0;
			assert(d->out_u < d->out_l);
		} else if (d->error)
			break;
	}

	if (d->error == H2SE_ENHANCE_YOUR_CALM)
		return (0); /* Stream error, delay reporting until
			       h2h_decode_fini so that we can process the
			       complete header block */
	return (d->error);
}
