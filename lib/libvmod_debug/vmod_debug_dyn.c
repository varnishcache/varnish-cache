/*-
 * Copyright (c) 2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi@varnish-software.com>
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

#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <sys/types.h>

#include "vcl.h"
#include "vrt.h"

#include "cache/cache.h"
#include "cache/cache_director.h"
#include "cache/cache_backend.h"

#include "vsa.h"
#include "vcc_if.h"

struct vmod_debug_dyn {
	unsigned		magic;
#define VMOD_DEBUG_DYN_MAGIC	0x9b77ccbd
	pthread_mutex_t		mtx;
	char			*vcl_name;
	struct director		*dir;
};

static void
dyn_dir_init(VRT_CTX, struct vmod_debug_dyn *dyn,
    VCL_STRING addr, VCL_STRING port)
{
	struct addrinfo hints, *res = NULL;
	struct suckaddr *sa;
	struct director *dir, *dir2;
	struct vrt_backend vrt;

	CHECK_OBJ_NOTNULL(dyn, VMOD_DEBUG_DYN_MAGIC);
	XXXAN(addr);
	XXXAN(port);

	INIT_OBJ(&vrt, VRT_BACKEND_MAGIC);
	vrt.port = port;
	vrt.vcl_name = dyn->vcl_name;
	vrt.hosthdr = vrt.ipv4_addr;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	AZ(getaddrinfo(vrt.ipv4_addr, vrt.port, &hints, &res));
	XXXAZ(res->ai_next);

	sa = VSA_Malloc(res->ai_addr, res->ai_addrlen);
	AN(sa);
	if (VSA_Get_Proto(sa) == AF_INET) {
		vrt.ipv4_addr = addr;
		vrt.ipv4_suckaddr = sa;
	} else if (VSA_Get_Proto(sa) == AF_INET6) {
		vrt.ipv6_addr = addr;
		vrt.ipv6_suckaddr = sa;
	} else
		WRONG("Wrong proto family");

	freeaddrinfo(res);

	dir = VRT_new_backend(ctx, &vrt);
	AN(dir);

	/*
	 * NB: A real dynamic backend should not replace the previous
	 * instance if the new one is identical.  We do it here because
	 * the d* tests requires a replacement.
	 */
	AZ(pthread_mutex_lock(&dyn->mtx));
	dir2 = dyn->dir;
	dyn->dir = dir;
	AZ(pthread_mutex_unlock(&dyn->mtx));

	if (dir2 != NULL)
		VRT_delete_backend(ctx, &dir2);

	free(sa);
}

VCL_VOID
vmod_dyn__init(VRT_CTX, struct vmod_debug_dyn **dynp,
    const char *vcl_name, VCL_STRING addr, VCL_STRING port)
{
	struct vmod_debug_dyn *dyn;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(dynp);
	AZ(*dynp);
	AN(vcl_name);

	ALLOC_OBJ(dyn, VMOD_DEBUG_DYN_MAGIC);
	AN(dyn);
	REPLACE(dyn->vcl_name, vcl_name);

	AZ(pthread_mutex_init(&dyn->mtx, NULL));

	dyn_dir_init(ctx, dyn, addr, port);
	XXXAN(dyn->dir);
	*dynp = dyn;
}

VCL_VOID
vmod_dyn__fini(struct vmod_debug_dyn **dynp)
{
	struct vmod_debug_dyn *dyn;

	AN(dynp);
	CAST_OBJ_NOTNULL(dyn, *dynp, VMOD_DEBUG_DYN_MAGIC);
	/* at this point all backends will be deleted by the vcl */
	free(dyn->vcl_name);
	AZ(pthread_mutex_destroy(&dyn->mtx));
	FREE_OBJ(dyn);
	*dynp = NULL;
}

VCL_BACKEND __match_proto__()
vmod_dyn_backend(VRT_CTX, struct vmod_debug_dyn *dyn)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_DEBUG_DYN_MAGIC);
	AN(dyn->dir);
	return (dyn->dir);
}

VCL_VOID
vmod_dyn_refresh(VRT_CTX, struct vmod_debug_dyn *dyn,
    VCL_STRING addr, VCL_STRING port)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_DEBUG_DYN_MAGIC);
	dyn_dir_init(ctx, dyn, addr, port);
}
