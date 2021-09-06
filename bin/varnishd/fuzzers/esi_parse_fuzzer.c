/*-
 * Copyright (c) 2018 Varnish Software AS
 * All rights reserved.
 *
 * Author: Federico G. Schwindt <fgsch@lodoss.net>
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
 *
 * ESI parser fuzzer.
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_vgz.h"		/* enum vgz_flag */
#include "cache/cache_esi.h"
#include "cache/cache_filter.h"		/* struct vfp_ctx */

#include "vfil.h"

int LLVMFuzzerTestOneInput(const uint8_t *, size_t);

struct VSC_main *VSC_C_main;
volatile struct params *cache_param;

int
PAN__DumpStruct(struct vsb *vsb, int block, int track, const void *ptr,
        const char *smagic, unsigned magic, const char *fmt, ...)
{
	(void)vsb;
	(void)block;
	(void)track;
	(void)ptr;
	(void)smagic;
	(void)magic;
	(void)fmt;
	return (0);
}

void
VSL(enum VSL_tag_e tag, uint32_t vxid, const char *fmt, ...)
{
	(void)tag;
	(void)vxid;
	(void)fmt;
}

void
VSLb(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, ...)
{
	(void)vsl;
	(void)tag;
	(void)fmt;
}

void
VSLb_ts(struct vsl_log *l, const char *event, vtim_real first, vtim_real *pprev,
    vtim_real now)
{
	(void)l;
	(void)event;
	(void)first;
	(void)pprev;
	(void)now;
}

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	struct VSC_main __VSC_C_main;
	struct params __cache_param;
	struct http req[1];
	struct http resp[1];
	struct vfp_ctx vc[1];
	struct worker wrk[1];
	struct ws ws[1];
	struct vep_state *vep;
	struct vsb *vsb;
	txt hd[HTTP_HDR_URL + 1];
	char ws_buf[1024];

	if (size < 1)
		return (0);

	AN(data);

	VSC_C_main = &__VSC_C_main;
	cache_param = &__cache_param;

	memset(&__cache_param, 0, sizeof(__cache_param));
#define BSET(b, no) (b)[(no) >> 3] |= (0x80 >> ((no) & 7))
	if (data[0] & 0x8f)
		BSET(__cache_param.feature_bits, FEATURE_ESI_IGNORE_HTTPS);
	if (size > 1 && data[1] & 0x8f)
		BSET(__cache_param.feature_bits, FEATURE_ESI_DISABLE_XML_CHECK);
	if (size > 2 && data[2] & 0x8f)
		BSET(__cache_param.feature_bits, FEATURE_ESI_IGNORE_OTHER_ELEMENTS);
	if (size > 3 && data[3] & 0x8f)
		BSET(__cache_param.feature_bits, FEATURE_ESI_REMOVE_BOM);
#undef BSET

	/* Setup ws */
	WS_Init(ws, "req", ws_buf, sizeof ws_buf);

	/* Setup req */
	INIT_OBJ(req, HTTP_MAGIC);
	req->hd = hd;
	req->hd[HTTP_HDR_URL].b = "/";
	req->ws = ws;

	/* Setup resp */
	INIT_OBJ(resp, HTTP_MAGIC);
	resp->ws = ws;

	/* Setup wrk */
	INIT_OBJ(wrk, WORKER_MAGIC);

	/* Setup vc */
	INIT_OBJ(vc, VFP_CTX_MAGIC);
	vc->wrk = wrk;
	vc->resp = resp;

	vep = VEP_Init(vc, req, NULL, NULL);
	AN(vep);
	VEP_Parse(vep, (const char *)data, size);
	vsb = VEP_Finish(vep);
	if (vsb != NULL)
		VSB_destroy(&vsb);
	WS_Rollback(ws, 0);

	return (0);
}

#if defined(TEST_DRIVER)
int
main(int argc, char **argv)
{
	ssize_t len;
	char *buf;
	int i;

	for (i = 1; i < argc; i++) {
		len = 0;
		buf = VFIL_readfile(NULL, argv[i], &len);
		AN(buf);
		LLVMFuzzerTestOneInput((uint8_t *)buf, len);
		free(buf);
	}
}
#endif
