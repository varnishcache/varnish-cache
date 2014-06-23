/*-
 * Copyright (c) 2010-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Naren Venkataraman of Vimeo Inc.
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

#include "vrt.h"

#include "cache/cache.h"

#include "vcc_if.h"

#define QS_MAX_PARAM_COUNT	32
#define QS_EQUALS(a, b)	\
    ((a) == (b) || ((a) == '\0' && (b) == '&') || ((a) == '&' && (b) == '\0'))

static ssize_t
param_compare(const char *s, const char *t)
{
	for (; QS_EQUALS(*s, *t); s++, t++) {
		if (*s == '&' || *s == '\0')
			return (0);
	}
	return (*s - *t);
}

static size_t
param_copy(char *dst, const char *src)
{
	size_t len;
	len = strcspn(src, "&");
	memcpy(dst, src, len);
	return (len);
}

VCL_STRING __match_proto__(td_std_querysort)
vmod_querysort(const struct vrt_ctx *ctx, VCL_STRING url)
{
	char *param, *params[QS_MAX_PARAM_COUNT];
	char *p, *r;
	size_t len;
	int param_count;
	int i, n;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (url == NULL)
		return (NULL);

	p = strchr(url, '?');
	if (p == NULL)
		return (url);

	param_count = 0;
	params[param_count++] = ++p;
	len = p - url;

	while ((p = strchr(p, '&')) != NULL) {
		param = ++p;

		for (i = 0; i < param_count; i++) {
			if (param[0] < params[i][0] ||
			    param_compare(param, params[i]) < 0) {
				for (n = param_count; n > i; n--)
					params[n] = params[n - 1];
				break;
			}
		}
		params[i] = param;
		param_count++;

		if (param_count == QS_MAX_PARAM_COUNT)
			return (url);
	}

	if (param_count == 1)
		return (url);

	r = WS_Alloc(ctx->ws, strchr(param, '\0') - url + 1);
	if (r == NULL)
		return (url);

	p = memcpy(r, url, len);
	p += len;

	for (i = 0; i < param_count - 1; i++) {
		if (params[i][0] != '\0' && params[i][0] != '&')
			break;
	}

	for (; i < param_count - 1; i++) {
		p += param_copy(p, params[i]);
		*p++ = '&';
	}

	p += param_copy(p, params[i]);
	*p = '\0';

	return (r);
}
