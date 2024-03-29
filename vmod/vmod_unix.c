/*-
 * Copyright 2018 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Geoffrey Simmons <geoffrey.simmons@uplex.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"

#include <pwd.h>
#include <grp.h>
#include <string.h>

#include "cache/cache.h"
#include "vcl.h"
#include "common/heritage.h"

#include "vmod_unix_cred_compat.h"
#include "vcc_unix_if.h"

#define FAIL(ctx, msg) \
	VRT_fail((ctx), "vmod unix failure: " msg)

#define ERR(ctx, msg) \
	VSLb((ctx)->vsl, SLT_VCL_Error, "vmod unix error: " msg)

#define VERR(ctx, fmt, ...) \
	VSLb((ctx)->vsl, SLT_VCL_Error, "vmod unix error: " fmt, __VA_ARGS__)

#define ERRNOTUDS(ctx) \
	ERR((ctx), "not listening on a Unix domain socket")

#define FAIL_SUPPORT(ctx) \
	FAIL((ctx), "not supported on this platform")

#define ERRNOCREDS(ctx) \
	VERR((ctx), "could not read peer credentials: %s", strerror(errno))

#define ERRNOMEM(ctx) \
	ERR((ctx), "out of space")

static struct sess *
get_sp(VRT_CTX)
{
	struct sess *sp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (ctx->req) {
		CHECK_OBJ(ctx->req, REQ_MAGIC);
		sp = ctx->req->sp;
	} else {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		sp = ctx->bo->sp;
	}
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->listen_sock, LISTEN_SOCK_MAGIC);
	return (sp);
}

#define NUM_FUNC(func)					\
VCL_INT							\
vmod_##func(VRT_CTX)					\
{							\
	struct sess *sp;				\
	uid_t uid;					\
	gid_t gid;					\
	int ret;					\
							\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);		\
	AZ(ctx->method & VCL_MET_TASK_H);	\
							\
	sp = get_sp(ctx);				\
	if (!sp->listen_sock->uds) {			\
		ERRNOTUDS(ctx);				\
		return (-1);				\
	}						\
							\
	ret = get_ids(sp->fd, &uid, &gid);		\
	if (ret == 0)					\
		return (func);				\
							\
	if (ret == NOT_SUPPORTED)			\
		FAIL_SUPPORT(ctx);			\
	else if (ret == CREDS_FAIL)			\
		ERRNOCREDS(ctx);			\
	return (-1);					\
}

NUM_FUNC(uid)
NUM_FUNC(gid)

#define NAME_FUNC(func, type, get, id, fld)			\
VCL_STRING							\
vmod_##func(VRT_CTX)						\
{								\
	struct type *s;						\
	VCL_INT ret;						\
	VCL_STRING name;					\
								\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);			\
	ret = vmod_##id(ctx);					\
	if (ret == -1)						\
		return (NULL);					\
								\
	errno = 0;						\
	s = get((id##_t) ret);					\
	if (s == NULL) {					\
		ERRNOCREDS(ctx);				\
		return (NULL);					\
	}							\
	if ((name = WS_Copy(ctx->ws, s->fld, -1)) == NULL) {	\
		ERRNOMEM(ctx);					\
		return (NULL);					\
	}							\
	return (name);						\
}

NAME_FUNC(user, passwd, getpwuid, uid, pw_name)
NAME_FUNC(group, group, getgrgid, gid, gr_name)
