/*-
 * Copyright (c) 2018 Varnish Software AS
 * All rights reserved.
 *
 * Author: Federico G. Schwindt <fgsch@lodoss.net>
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "cache/cache.h"
#include "cache/cache_vgz.h"		/* enum vgz_flag */
#include "cache/cache_esi.h"
#include "cache/cache_filter.h"		/* struct vfp_ctx */
#include "common/common_param.h"	/* struct params */

#include "VSC_main.h"
#include "vfil.h"
#include "vsb.h"

int LLVMFuzzerTestOneInput(const uint8_t *, size_t);

struct VSC_main *VSC_C_main;
struct params *cache_param;

void
VSLb(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, ...)
{
	(void)vsl;
	(void)tag;
	(void)fmt;
}

void
VSLb_ts(struct vsl_log *l, const char *event, double first, double *pprev,
    double now)
{
	(void)l;
	(void)event;
	(void)first;
	(void)pprev;
	(void)now;
}

void *
WS_Alloc(struct ws *ws, unsigned bytes)
{
	(void)ws;
	return (calloc(1, bytes));
}

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	struct VSC_main __VSC_C_main;
	struct params __cache_param;
	struct http req = { .magic = HTTP_MAGIC };
	struct http resp = { .magic = HTTP_MAGIC };
	struct vfp_ctx vc = { .magic = VFP_CTX_MAGIC };
	struct vep_state *vep;
	struct vsb *vsb;
	struct worker wrk;
	txt hd[HTTP_HDR_URL + 1];

	if (size < 1)
		return (0);

	AN(data);

	VSC_C_main = &__VSC_C_main;
	cache_param = &__cache_param;

	/* Zero out the esi feature bits for now */
	memset(&__cache_param, 0, sizeof(__cache_param));

	/* Setup req */
	req.hd = hd;
	req.hd[HTTP_HDR_URL].b = "/";

	/* Setup vc */
	vc.wrk = &wrk;
	vc.resp = &resp;

	vep = VEP_Init(&vc, &req, NULL, NULL);
	AN(vep);
	VEP_Parse(vep, (const char *)data, size);
	vsb = VEP_Finish(vep);
	if (vsb != NULL)
		VSB_destroy(&vsb);
	free(vep);

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
		LLVMFuzzerTestOneInput((uint8_t *)buf, len);
		free(buf);
	}
}
#endif
