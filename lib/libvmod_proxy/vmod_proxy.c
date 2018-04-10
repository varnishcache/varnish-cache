/*-
 * Copyright (c) 2018 GANDI SAS
 * All rights reserved.
 *
 * Author: Emmanuel Hocdet <manu@gandi.net>
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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cache/cache.h"

#include "vend.h"

#include "proxy/cache_proxy.h"

#include "vcc_if.h"


struct pp2_tlv_ssl {
	uint8_t  client;
	uint32_t verify;
}__attribute__((packed));

#define PP2_CLIENT_SSL           0x01
#define PP2_CLIENT_CERT_CONN     0x02
#define PP2_CLIENT_CERT_SESS     0x04

static VCL_BOOL
tlv_ssl_flag(VRT_CTX, int flag)
{
	struct pp2_tlv_ssl *dst;
	int len;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (VPX_tlv(ctx->req, PP2_TYPE_SSL, (void **)&dst, &len))
		return (0);

	return ((dst->client & flag) == flag);
}

VCL_BOOL v_matchproto_(td_proxy_is_ssl)
vmod_is_ssl(VRT_CTX)
{
	return tlv_ssl_flag(ctx, PP2_CLIENT_SSL);
}

VCL_BOOL v_matchproto_(td_proxy_client_has_cert_sess)
vmod_client_has_cert_sess(VRT_CTX)
{
	return tlv_ssl_flag(ctx, PP2_CLIENT_CERT_SESS);
}

VCL_BOOL v_matchproto_(td_proxy_client_has_cert_conn)
vmod_client_has_cert_conn(VRT_CTX)
{
	return tlv_ssl_flag(ctx, PP2_CLIENT_CERT_CONN);
}

/* return come from SSL_get_verify_result */
VCL_INT v_matchproto_(td_proxy_ssl_verify_result)
vmod_ssl_verify_result(VRT_CTX)
{
	struct pp2_tlv_ssl *dst;
	int len;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (VPX_tlv(ctx->req, PP2_TYPE_SSL, (void **)&dst, &len))
		return (0); /* X509_V_OK */

	return (vbe32dec(&dst->verify));
}

static VCL_STRING
tlv_string(VRT_CTX, int tlv)
{
	char *dst, *d;
	int len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (VPX_tlv(ctx->req, tlv, (void **)&dst, &len))
		return (NULL);
	if (!WS_Reserve(ctx->ws, len+1))
		return (NULL);
	d = ctx->ws->f;
	memcpy(d, dst, len);
	d[len] = '\0';
	WS_Release(ctx->ws, len+1);
	return (d);
}

VCL_STRING v_matchproto_(td_proxy_alpn)
vmod_alpn(VRT_CTX)
{
	return tlv_string(ctx, PP2_TYPE_ALPN);
}

VCL_STRING v_matchproto_(td_proxy_authority)
vmod_authority(VRT_CTX)
{
	return tlv_string(ctx, PP2_TYPE_AUTHORITY);
}

VCL_STRING v_matchproto_(td_proxy_ssl_version)
vmod_ssl_version(VRT_CTX)
{
	return tlv_string(ctx, PP2_SUBTYPE_SSL_VERSION);
}

VCL_STRING v_matchproto_(td_proxy_ssl_cipher)
vmod_ssl_cipher(VRT_CTX)
{
	return tlv_string(ctx, PP2_SUBTYPE_SSL_CIPHER);
}

VCL_STRING v_matchproto_(td_proxy_cert_sign)
vmod_cert_sign(VRT_CTX)
{
	return tlv_string(ctx, PP2_SUBTYPE_SSL_SIG_ALG);
}

VCL_STRING v_matchproto_(td_proxy_cert_key)
vmod_cert_key(VRT_CTX)
{
	return tlv_string(ctx, PP2_SUBTYPE_SSL_KEY_ALG);
}

VCL_STRING v_matchproto_(td_proxy_client_cert_cn)
vmod_client_cert_cn(VRT_CTX)
{
	return tlv_string(ctx, PP2_SUBTYPE_SSL_CN);
}
