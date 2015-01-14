/*-
 * Copyright (c) 2012-2015 Varnish Software AS
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
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include "cache/cache.h"

#include "vrt.h"
#include "vcc_if.h"

VCL_VOID __match_proto__(td_debug_panic)
vmod_panic(VRT_CTX, const char *str, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	va_start(ap, str);
	b = VRT_String(ctx->ws, "PANIC: ", str, ap);
	va_end(ap);
	VAS_Fail("VCL", "", 0, b, 0, VAS_VCL);
}

VCL_STRING __match_proto__(td_debug_author)
vmod_author(VRT_CTX, VCL_ENUM id)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (!strcmp(id, "phk"))
		return ("Poul-Henning");
	if (!strcmp(id, "des"))
		return ("Dag-Erling");
	if (!strcmp(id, "kristian"))
		return ("Kristian");
	if (!strcmp(id, "mithrandir"))
		return ("Tollef");
	WRONG("Illegal VMOD enum");
}

int
init_function(struct vmod_priv *priv, const struct VCL_conf *cfg)
{
	(void)cfg;

	priv->priv = strdup("FOO");
	priv->free = free;
	return (0);
}

VCL_VOID __match_proto__(td_debug_test_priv_call)
vmod_test_priv_call(VRT_CTX, struct vmod_priv *priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup("BAR");
		priv->free = free;
	} else {
		assert(!strcmp(priv->priv, "BAR"));
	}
}

VCL_STRING __match_proto__(td_debug_test_priv_task)
vmod_test_priv_task(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->free = free;
	}
	return (priv->priv);
}

VCL_VOID __match_proto__(td_debug_test_priv_vcl)
vmod_test_priv_vcl(VRT_CTX, struct vmod_priv *priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(!strcmp(priv->priv, "FOO"));
}

VCL_BLOB
vmod_str2blob(VRT_CTX, VCL_STRING s)
{
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	p = (void*)WS_Alloc(ctx->ws, sizeof *p);
	AN(p);
	memset(p, 0, sizeof *p);
	p->len = strlen(s);
	p->priv = WS_Copy(ctx->ws, s, -1);
	return (p);
}

VCL_STRING
vmod_blob2hex(VRT_CTX, VCL_BLOB b)
{
	char *s, *p;
	uint8_t *q;
	int i;

	s = WS_Alloc(ctx->ws, b->len * 2 + 2);
	AN(s);
	p = s;
	q = b->priv;
	for (i = 0; i < b->len; i++) {
		sprintf(p, "%02x", *q);
		p += 2;
		q += 1;
	}
	VRT_priv_fini(b);
	return (s);
}

VCL_BACKEND
vmod_no_backend(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

VCL_VOID __match_proto__(td_debug_rot52)
vmod_rot52(VRT_CTX, VCL_HTTP hp)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	http_PrintfHeader(hp, "Encrypted: ROT52");
}

VCL_STRING
vmod_argtest(VRT_CTX, VCL_STRING one, VCL_REAL two, VCL_STRING three)
{
	char buf[100];

	bprintf(buf, "%s %g %s", one, two, three);
	return WS_Copy(ctx->ws, buf, -1);
}

VCL_INT
vmod_vre_limit(VRT_CTX)
{
	(void)ctx;
	return (cache_param->vre_limits.match);
}
