/*-
 * Copyright (c) 2012-2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_filter.h"

#include "vgz.h"
#include "vsha256.h"
#include "vtim.h"
#include "vcc_debug_if.h"

#include "vmod_debug.h"

/**********************************************************************/

static enum vfp_status v_matchproto_(vfp_pull_f)
xyzzy_vfp_rot13_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	enum vfp_status vp;
	char *q;
	ssize_t l;

	(void)vfe;
	vp = VFP_Suck(vc, p, lp);
	if (vp == VFP_ERROR)
		return (vp);
	q = p;
	for (l = 0; l < *lp; l++, q++) {
		if (*q >= 'A' && *q <= 'Z')
			*q = (((*q - 'A') + 13) % 26) + 'A';
		if (*q >= 'a' && *q <= 'z')
			*q = (((*q - 'a') + 13) % 26) + 'a';
	}
	return (vp);
}

static const struct vfp xyzzy_vfp_rot13 = {
	.name = "rot13",
	.pull = xyzzy_vfp_rot13_pull,
};

/**********************************************************************/

// deliberately fragmenting the stream to make testing more interesting
#define ROT13_BUFSZ 8

static int v_matchproto_(vdp_init_f)
xyzzy_vdp_rot13_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);

	AN(priv);

	*priv = malloc(ROT13_BUFSZ);
	if (*priv == NULL)
		return (-1);

	return (0);
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_vdp_rot13_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	char *q;
	const char *pp;
	int i, j, retval = 0;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	AN(priv);
	AN(*priv);
	if (len <= 0)
		return (VDP_bytes(vdc, act, ptr, len));
	AN(ptr);
	if (act != VDP_END)
		act = VDP_FLUSH;
	q = *priv;
	pp = ptr;

	for (i = 0, j = 0; j < len; i++, j++) {
		if (pp[j] >= 'A' && pp[j] <= 'Z')
			q[i] = (((pp[j] - 'A') + 13) % 26) + 'A';
		else if (pp[j] >= 'a' && pp[j] <= 'z')
			q[i] = (((pp[j] - 'a') + 13) % 26) + 'a';
		else
			q[i] = pp[j];
		if (i == ROT13_BUFSZ - 1 && j < len - 1) {
			retval = VDP_bytes(vdc, VDP_FLUSH, q, ROT13_BUFSZ);
			if (retval != 0)
				return (retval);
			i = -1;
		}
	}
	if (i >= 0)
		retval = VDP_bytes(vdc, act, q, i);
	return (retval);
}

static int v_matchproto_(vdp_fini_f)
xyzzy_vdp_rot13_fini(struct vdp_ctx *vdc, void **priv)
{
	(void)vdc;
	AN(priv);
	free(*priv);
	*priv = NULL;
	return (0);
}

static const struct vdp xyzzy_vdp_rot13 = {
	.name  = "rot13",
	.init  = xyzzy_vdp_rot13_init,
	.bytes = xyzzy_vdp_rot13_bytes,
	.fini  = xyzzy_vdp_rot13_fini,
};

VCL_VOID v_matchproto_(td_debug_rot104)
xyzzy_rot104(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	// This should fail
	AN(VRT_AddFilter(ctx, &xyzzy_vfp_rot13, &xyzzy_vdp_rot13));
}

/**********************************************************************
 * vdp debug_chunked: force http1 chunked encoding by removing the
 * Content-Length header
 *
 * this happens in a VDP because cnt_transmit() runs after VCL and
 * restores it
 */

static int v_matchproto_(vdp_init_f)
xyzzy_vdp_chunked_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	http_Unset(vdc->hp, H_Content_Length);
	*vdc->clen = -1;

	return (1);
}

static const struct vdp xyzzy_vdp_chunked = {
	.name  = "debug.chunked",
	.init  = xyzzy_vdp_chunked_init,
};

/**********************************************************************
 * pedantic tests of the VDP API:
 * - assert that we see a VDP_END
 * - assert that _fini gets called before the task ends
 *
 * note:
 * we could lookup our own vdpe in _fini and check for vdpe->end == VDP_END
 * yet that would cross the API
 */

enum vdp_state_e {
	VDPS_NULL = 0,
	VDPS_INIT,	// _init called
	VDPS_BYTES,	// _bytes called act != VDP_END
	VDPS_END,	// _bytes called act == VDP_END
	VDPS_FINI	// _fini called
};

struct vdp_state_s {
	unsigned		magic;
#define VDP_STATE_MAGIC	0x57c8d309
	enum vdp_state_e	state;
};

static void v_matchproto_(vmod_priv_fini_f)
priv_pedantic_fini(VRT_CTX, void *priv)
{
	struct vdp_state_s *vdps;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ_NOTNULL(vdps, priv, VDP_STATE_MAGIC);

	assert(vdps->state == VDPS_FINI);
}

static const struct vmod_priv_methods priv_pedantic_methods[1] = {{
	.magic = VMOD_PRIV_METHODS_MAGIC,
	.type = "debug_vdp_pedantic",
	.fini = priv_pedantic_fini
}};

static int v_matchproto_(vdp_init_f)
xyzzy_pedantic_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{
	struct vdp_state_s *vdps;
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	WS_TASK_ALLOC_OBJ(ctx, vdps, VDP_STATE_MAGIC);
	if (vdps == NULL)
		return (-1);
	assert(vdps->state == VDPS_NULL);

	p = VRT_priv_task(ctx, (void *)vdc);
	if (p == NULL)
		return (-1);
	p->priv = vdps;
	p->methods = priv_pedantic_methods;

	*priv = vdps;

	vdps->state = VDPS_INIT;

	return (0);
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_pedantic_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct vdp_state_s *vdps;

	CAST_OBJ_NOTNULL(vdps, *priv, VDP_STATE_MAGIC);
	assert(vdps->state >= VDPS_INIT);
	assert(vdps->state < VDPS_END);

	if (act == VDP_END)
		vdps->state = VDPS_END;
	else
		vdps->state = VDPS_BYTES;

	return (VDP_bytes(vdc, act, ptr, len));
}

static int v_matchproto_(vdp_fini_f)
xyzzy_pedantic_fini(struct vdp_ctx *vdc, void **priv)
{
	struct vdp_state_s *vdps;

	(void) vdc;
	AN(priv);
	if (*priv == NULL)
		return (0);
	TAKE_OBJ_NOTNULL(vdps, priv, VDP_STATE_MAGIC);
	assert(vdps->state == VDPS_INIT || vdps->state == VDPS_END);
	vdps->state = VDPS_FINI;

	return (0);
}

static const struct vdp xyzzy_vdp_pedantic = {
	.name  = "debug.pedantic",
	.init  = xyzzy_pedantic_init,
	.bytes = xyzzy_pedantic_bytes,
	.fini  = xyzzy_pedantic_fini,
};

/**********************************************************************
 *
 * this trivial copy/paste/edit filter (of rot13) was specifically made for
 * someone who added a DBG_SLOW_BEREQ debug flag. It should actually be turned
 * in a proper "bandwidth control" filter, but that exceeds an evening's work,
 * so it's kept for later
 */

static enum vfp_status v_matchproto_(vfp_pull_f)
xyzzy_vfp_slow_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{

	(void)vfe;
	VTIM_sleep(1.0);
	return (VFP_Suck(vc, p, lp));
}

static const struct vfp xyzzy_vfp_slow = {
	.name = "debug.slow",
	.pull = xyzzy_vfp_slow_pull,
};

/**********************************************************************/

static int v_matchproto_(vdp_bytes_f)
xyzzy_vdp_slow_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{

	(void)priv;
	VTIM_sleep(1.0);
	return (VDP_bytes(vdc, act, ptr, len));
}

static const struct vdp xyzzy_vdp_slow = {
	.name  = "debug.slow",
	.bytes = xyzzy_vdp_slow_bytes
};

/*
 * check VDPs:
 *
 * test that the stream of bytes has a certain checksum or length and either log
 * or panic
 *
 * The sha256 and crc32 variants are basically identical, but the amount of
 * code does not justify generalizing. (slink)
 */

enum vdp_chk_mode_e {
	//lint -esym(749, vdp_chk_mode_e::VDP_CHK_INVAL) deliberately not referenced
	VDP_CHK_INVAL = 0,
	VDP_CHK_LOG,
	VDP_CHK_PANIC,
	VDP_CHK_PANIC_UNLESS_ERROR
};

struct vdp_chksha256_cfg_s {
	unsigned			magic;
#define VDP_CHKSHA256_CFG_MAGIC		0x624f5b32
	enum vdp_chk_mode_e		mode;
	unsigned char			expected[VSHA256_DIGEST_LENGTH];
};

struct vdp_chkcrc32_cfg_s {
	unsigned			magic;
#define VDP_CHKCRC32_CFG_MAGIC		0x5a7a835c
	enum vdp_chk_mode_e		mode;
	uint32_t			expected;
};

struct vdp_chklen_cfg_s {
	unsigned			magic;
#define VDP_CHKLEN_CFG_MAGIC		0x08cf3426
	enum vdp_chk_mode_e		mode;
	size_t				expected;
};

struct vdp_chksha256_s {
	unsigned			magic;
#define VDP_CHKSHA256_MAGIC		0x6856e913
	unsigned			called;
	size_t				bytes;
	struct VSHA256Context		cx[1];
	struct vdp_chksha256_cfg_s	*cfg;
};

struct vdp_chkcrc32_s {
	unsigned			magic;
#define VDP_CHKCRC32_MAGIC		0x15c03d3c
	unsigned			called;
	size_t				bytes;
	uint32_t			crc;
	struct vdp_chkcrc32_cfg_s	*cfg;
};

struct vdp_chklen_s {
	unsigned			magic;
#define VDP_CHKLEN_MAGIC		0x029811f5
	unsigned			called;
	size_t				bytes;
	struct vdp_chklen_cfg_s		*cfg;
};

static const void * const chksha256_priv_id = &chksha256_priv_id;
static const void * const chkcrc32_priv_id = &chkcrc32_priv_id;
static const void * const chklen_priv_id = &chklen_priv_id;

static int v_matchproto_(vdp_init_f)
xyzzy_chksha256_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{
	struct vdp_chksha256_s *vdps;
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	WS_TASK_ALLOC_OBJ(ctx, vdps, VDP_CHKSHA256_MAGIC);
	if (vdps == NULL)
		return (-1);
	VSHA256_Init(vdps->cx);

	p = VRT_priv_task_get(ctx, chksha256_priv_id);
	if (p == NULL)
		return (-1);

	assert(p->len == sizeof(struct vdp_chksha256_cfg_s));
	CAST_OBJ_NOTNULL(vdps->cfg, p->priv, VDP_CHKSHA256_CFG_MAGIC);
	*priv = vdps;

	return (0);
}

static int v_matchproto_(vdp_init_f)
xyzzy_chkcrc32_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{
	struct vdp_chkcrc32_s *vdps;
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	WS_TASK_ALLOC_OBJ(ctx, vdps, VDP_CHKCRC32_MAGIC);
	if (vdps == NULL)
		return (-1);
	vdps->crc = crc32(0L, Z_NULL, 0);

	p = VRT_priv_task_get(ctx, chkcrc32_priv_id);
	if (p == NULL)
		return (-1);

	assert(p->len == sizeof(struct vdp_chkcrc32_cfg_s));
	CAST_OBJ_NOTNULL(vdps->cfg, p->priv, VDP_CHKCRC32_CFG_MAGIC);
	*priv = vdps;

	return (0);
}

static int v_matchproto_(vdp_init_f)
xyzzy_chklen_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{
	struct vdp_chklen_s *vdps;
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	WS_TASK_ALLOC_OBJ(ctx, vdps, VDP_CHKLEN_MAGIC);
	if (vdps == NULL)
		return (-1);
	AZ(vdps->bytes);

	p = VRT_priv_task_get(ctx, chklen_priv_id);
	if (p == NULL)
		return (-1);

	assert(p->len == sizeof(struct vdp_chklen_cfg_s));
	CAST_OBJ_NOTNULL(vdps->cfg, p->priv, VDP_CHKLEN_CFG_MAGIC);
	*priv = vdps;

	return (0);
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_chksha256_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct vdp_chksha256_s *vdps;

	CAST_OBJ_NOTNULL(vdps, *priv, VDP_CHKSHA256_MAGIC);
	if (len != 0)
		VSHA256_Update(vdps->cx, ptr, len);
	vdps->called++;
	vdps->bytes += len;
	return (VDP_bytes(vdc, act, ptr, len));
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_chkcrc32_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct vdp_chkcrc32_s *vdps;

	CAST_OBJ_NOTNULL(vdps, *priv, VDP_CHKCRC32_MAGIC);
	if (len > 0)
		vdps->crc = crc32(vdps->crc, ptr, len);
	vdps->called++;
	vdps->bytes += len;
	return (VDP_bytes(vdc, act, ptr, len));
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_chklen_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	struct vdp_chklen_s *vdps;

	CAST_OBJ_NOTNULL(vdps, *priv, VDP_CHKLEN_MAGIC);
	vdps->called++;
	vdps->bytes += len;
	return (VDP_bytes(vdc, act, ptr, len));
}

static int v_matchproto_(vdp_fini_f)
xyzzy_chksha256_fini(struct vdp_ctx *vdc, void **priv)
{
	unsigned char digest[VSHA256_DIGEST_LENGTH];
	enum vdp_chk_mode_e mode;
	struct vdp_chksha256_s *vdps;
	struct vsb *vsb;
	int r;

	(void) vdc;
	AN(priv);
	if (*priv == NULL)
		return (0);
	TAKE_OBJ_NOTNULL(vdps, priv, VDP_CHKSHA256_MAGIC);

	VSHA256_Final(digest, vdps->cx);
	r = memcmp(digest, vdps->cfg->expected, sizeof digest);
	if (r == 0)
		return (0);

	mode = vdps->cfg->mode;
	if (mode == VDP_CHK_PANIC_UNLESS_ERROR)
		mode = (vdps->called == 0 || vdc->retval != 0) ? VDP_CHK_LOG : VDP_CHK_PANIC;

	if (mode == VDP_CHK_LOG) {
		VSLb(vdc->vsl, SLT_Debug, "sha256 checksum mismatch");

		vsb = VSB_new_auto();
		AN(vsb);
		VSB_quote(vsb, digest, sizeof digest, VSB_QUOTE_HEX);
		AZ(VSB_finish(vsb));
		VSLb(vdc->vsl, SLT_Debug, "got: %s", VSB_data(vsb));

		VSB_clear(vsb);
		VSB_quote(vsb, vdps->cfg->expected, sizeof digest, VSB_QUOTE_HEX);
		AZ(VSB_finish(vsb));
		VSLb(vdc->vsl, SLT_Debug, "exp: %s", VSB_data(vsb));
		VSB_destroy(&vsb);
	}
	else if (mode == VDP_CHK_PANIC)
		WRONG("body checksum");
	else
		WRONG("mode");

	return (0);
}

static int v_matchproto_(vdp_fini_f)
xyzzy_chkcrc32_fini(struct vdp_ctx *vdc, void **priv)
{
	enum vdp_chk_mode_e mode;
	struct vdp_chkcrc32_s *vdps;

	(void) vdc;
	AN(priv);
	if (*priv == NULL)
		return (0);
	TAKE_OBJ_NOTNULL(vdps, priv, VDP_CHKCRC32_MAGIC);

	if (vdps->crc == vdps->cfg->expected)
		return (0);

	mode = vdps->cfg->mode;
	if (mode == VDP_CHK_PANIC_UNLESS_ERROR)
		mode = (vdps->called == 0 || vdc->retval != 0) ? VDP_CHK_LOG : VDP_CHK_PANIC;

	if (mode == VDP_CHK_LOG) {
		VSLb(vdc->vsl, SLT_Debug, "crc32 checksum mismatch");
		VSLb(vdc->vsl, SLT_Debug, "got: %08x", vdps->crc);
		VSLb(vdc->vsl, SLT_Debug, "exp: %08x", vdps->cfg->expected);
	}
	else if (mode == VDP_CHK_PANIC)
		WRONG("body checksum");
	else
		WRONG("mode");

	return (0);
}

static int v_matchproto_(vdp_fini_f)
xyzzy_chklen_fini(struct vdp_ctx *vdc, void **priv)
{
	enum vdp_chk_mode_e mode;
	struct vdp_chklen_s *vdps;

	(void) vdc;
	AN(priv);
	if (*priv == NULL)
		return (0);
	TAKE_OBJ_NOTNULL(vdps, priv, VDP_CHKLEN_MAGIC);

	if (vdps->bytes == vdps->cfg->expected)
		return (0);

	mode = vdps->cfg->mode;
	if (mode == VDP_CHK_PANIC_UNLESS_ERROR)
		mode = (vdps->called == 0 || vdc->retval != 0) ? VDP_CHK_LOG : VDP_CHK_PANIC;

	if (mode == VDP_CHK_LOG) {
		VSLb(vdc->vsl, SLT_Debug, "length mismatch");
		VSLb(vdc->vsl, SLT_Debug, "got: %zd", vdps->bytes);
		VSLb(vdc->vsl, SLT_Debug, "exp: %zd", vdps->cfg->expected);
	}
	else if (mode == VDP_CHK_PANIC)
		WRONG("body length");
	else
		WRONG("mode");

	return (0);
}

static const struct vdp xyzzy_vdp_chksha256 = {
	.name  = "debug.chksha256",
	.init  = xyzzy_chksha256_init,
	.bytes = xyzzy_chksha256_bytes,
	.fini  = xyzzy_chksha256_fini,
};

static const struct vdp xyzzy_vdp_chkcrc32 = {
	.name  = "debug.chkcrc32",
	.init  = xyzzy_chkcrc32_init,
	.bytes = xyzzy_chkcrc32_bytes,
	.fini  = xyzzy_chkcrc32_fini,
};

static const struct vdp xyzzy_vdp_chklen = {
	.name  = "debug.chklen",
	.init  = xyzzy_chklen_init,
	.bytes = xyzzy_chklen_bytes,
	.fini  = xyzzy_chklen_fini,
};

#define chkcfg(ws, cfg, magic, id, mode_e) do {							\
	struct vmod_priv *p = VRT_priv_task(ctx, id);						\
												\
	XXXAN(p);										\
	if (p->priv == NULL) {									\
		p->priv = WS_Alloc(ws, sizeof *cfg);						\
		p->len = sizeof *cfg;								\
	}											\
	AN(p->priv);										\
	cfg = p->priv;										\
	INIT_OBJ(cfg, magic);									\
	if (mode_e == VENUM(log))								\
		cfg->mode = VDP_CHK_LOG;							\
	else if (mode_e == VENUM(panic))							\
		cfg->mode = VDP_CHK_PANIC;							\
	else if (mode_e == VENUM(panic_unless_error))						\
		cfg->mode = VDP_CHK_PANIC_UNLESS_ERROR;						\
	else											\
		WRONG("mode");									\
} while(0)

VCL_VOID v_matchproto_(td_xyzzy_debug_chksha256)
xyzzy_chksha256(VRT_CTX, VCL_BLOB blob, VCL_ENUM mode_e)
{
	struct vdp_chksha256_cfg_s *cfg;
	size_t l;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(blob, VRT_BLOB_MAGIC);
	XXXAN(blob->blob);
	XXXAN(blob->len);

	chkcfg(ctx->ws, cfg, VDP_CHKSHA256_CFG_MAGIC, chksha256_priv_id, mode_e);

	l = blob->len;
	if (l > sizeof cfg->expected)
		l = sizeof cfg->expected;
	memcpy(cfg->expected, blob->blob, l);

}

VCL_VOID v_matchproto_(td_xyzzy_debug_chkcrc32)
xyzzy_chkcrc32(VRT_CTX, VCL_INT expected, VCL_ENUM mode_e)
{
	struct vdp_chkcrc32_cfg_s *cfg;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	chkcfg(ctx->ws, cfg, VDP_CHKCRC32_CFG_MAGIC, chkcrc32_priv_id, mode_e);

	if (expected < 0)
		expected = 0;
	cfg->expected = (uintmax_t)expected % UINT32_MAX;
}

VCL_VOID v_matchproto_(td_xyzzy_debug_chklen)
xyzzy_chklen(VRT_CTX, VCL_BYTES expected, VCL_ENUM mode_e)
{
	struct vdp_chklen_cfg_s *cfg;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	chkcfg(ctx->ws, cfg, VDP_CHKLEN_CFG_MAGIC, chklen_priv_id, mode_e);

	cfg->expected = expected;
}

/**********************************************************************
 * reserve thread_workspace
 */

static int v_matchproto_(vdp_init_f)
xyzzy_awshog_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{
	struct ws *aws;
	unsigned u;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	if (ctx->req != NULL)
		aws = ctx->req->wrk->aws;
	else if (ctx->bo != NULL)
		aws = ctx->bo->wrk->aws;
	else
		WRONG("neither req nor bo");

	u = WS_ReserveAll(aws);
	WS_Release(aws, 0);
	(void) WS_Alloc(aws, u);
	return (1);
}

static const struct vdp xyzzy_vdp_awshog = {
	.name  = "debug.awshog",
	.init  = xyzzy_awshog_init
};

void
debug_add_filters(VRT_CTX)
{
	AZ(VRT_AddFilter(ctx, &xyzzy_vfp_rot13, &xyzzy_vdp_rot13));
	AZ(VRT_AddFilter(ctx, NULL, &xyzzy_vdp_pedantic));
	AZ(VRT_AddFilter(ctx, NULL, &xyzzy_vdp_chunked));
	AZ(VRT_AddFilter(ctx, &xyzzy_vfp_slow, &xyzzy_vdp_slow));
	AZ(VRT_AddFilter(ctx, NULL, &xyzzy_vdp_chksha256));
	AZ(VRT_AddFilter(ctx, NULL, &xyzzy_vdp_chkcrc32));
	AZ(VRT_AddFilter(ctx, NULL, &xyzzy_vdp_chklen));
	AZ(VRT_AddFilter(ctx, NULL, &xyzzy_vdp_awshog));
}

void
debug_remove_filters(VRT_CTX)
{
	VRT_RemoveFilter(ctx, &xyzzy_vfp_slow, &xyzzy_vdp_slow);
	VRT_RemoveFilter(ctx, &xyzzy_vfp_rot13, &xyzzy_vdp_rot13);
	VRT_RemoveFilter(ctx, NULL, &xyzzy_vdp_pedantic);
	VRT_RemoveFilter(ctx, NULL, &xyzzy_vdp_chunked);
	VRT_RemoveFilter(ctx, NULL, &xyzzy_vdp_chksha256);
	VRT_RemoveFilter(ctx, NULL, &xyzzy_vdp_chkcrc32);
	VRT_RemoveFilter(ctx, NULL, &xyzzy_vdp_chklen);
	VRT_RemoveFilter(ctx, NULL, &xyzzy_vdp_awshog);
}
