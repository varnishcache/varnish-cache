/*-
 * Copyright (c) 2012 Varnish Software AS
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

#include "cache/cache.h"

#include "vrt.h"
#include "vcc_if.h"

void __match_proto__(td_debug_panic)
vmod_panic(struct req *req, const char *str, ...)
{
	va_list ap;
	char *b;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	va_start(ap, str);
	b = VRT_String(req->http->ws, "PANIC: ", str, ap);
	va_end(ap);
	VAS_Fail("VCL", "", 0, b, 0, 2);
}

const char * __match_proto__(td_debug_author)
vmod_author(struct req *req, const char *id)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
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

void __match_proto__(td_debug_test_priv_call)
vmod_test_priv_call(struct req *req, struct vmod_priv *priv)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup("BAR");
		priv->free = free;
	} else {
		assert(!strcmp(priv->priv, "BAR"));
	}
}

void __match_proto__(td_debug_test_priv_vcl)
vmod_test_priv_vcl(struct req *req, struct vmod_priv *priv)
{

        CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
        assert(!strcmp(priv->priv, "FOO"));
}
