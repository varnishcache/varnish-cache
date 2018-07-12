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
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "cache/cache.h"

#include "vsa.h"
#include "vcc_if.h"

struct xyzzy_debug_dyn {
	unsigned		magic;
#define VMOD_DEBUG_DYN_MAGIC	0x9b77ccbd
	pthread_mutex_t		mtx;
	char			*vcl_name;
	VCL_BACKEND		dir;
};

struct xyzzy_debug_dyn_uds {
	unsigned		magic;
#define VMOD_DEBUG_UDS_MAGIC	0x6c7370e6
	pthread_mutex_t		mtx;
	char			*vcl_name;
	VCL_BACKEND		dir;
};

static void
dyn_dir_init(VRT_CTX, struct xyzzy_debug_dyn *dyn,
     VCL_STRING addr, VCL_STRING port, VCL_PROBE probe)
{
	struct addrinfo hints, *res = NULL;
	struct suckaddr *sa;
	VCL_BACKEND dir, dir2;
	struct vrt_backend vrt;

	CHECK_OBJ_NOTNULL(dyn, VMOD_DEBUG_DYN_MAGIC);
	XXXAN(addr);
	XXXAN(port);

	INIT_OBJ(&vrt, VRT_BACKEND_MAGIC);
	vrt.port = port;
	vrt.vcl_name = dyn->vcl_name;
	vrt.hosthdr = addr;
	vrt.probe = probe;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	AZ(getaddrinfo(addr, port, &hints, &res));
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
xyzzy_dyn__init(VRT_CTX, struct xyzzy_debug_dyn **dynp,
    const char *vcl_name, VCL_STRING addr, VCL_STRING port, VCL_PROBE probe)
{
	struct xyzzy_debug_dyn *dyn;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(dynp);
	AZ(*dynp);
	AN(vcl_name);

	if (*addr == '\0' || *port == '\0') {
		AN(ctx->handling);
		AZ(*ctx->handling);
		VRT_fail(ctx, "Missing dynamic backend address or port");
		return;
	}

	ALLOC_OBJ(dyn, VMOD_DEBUG_DYN_MAGIC);
	AN(dyn);
	REPLACE(dyn->vcl_name, vcl_name);

	AZ(pthread_mutex_init(&dyn->mtx, NULL));

	dyn_dir_init(ctx, dyn, addr, port, probe);
	XXXAN(dyn->dir);
	*dynp = dyn;
}

VCL_VOID
xyzzy_dyn__fini(struct xyzzy_debug_dyn **dynp)
{
	struct xyzzy_debug_dyn *dyn;

	AN(dynp);
	if (*dynp == NULL)
		return; /* failed initialization */

	CAST_OBJ_NOTNULL(dyn, *dynp, VMOD_DEBUG_DYN_MAGIC);
	/* at this point all backends will be deleted by the vcl */
	free(dyn->vcl_name);
	AZ(pthread_mutex_destroy(&dyn->mtx));
	FREE_OBJ(dyn);
	*dynp = NULL;
}

VCL_BACKEND v_matchproto_()
xyzzy_dyn_backend(VRT_CTX, struct xyzzy_debug_dyn *dyn)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_DEBUG_DYN_MAGIC);
	AN(dyn->dir);
	return (dyn->dir);
}

VCL_VOID
xyzzy_dyn_refresh(VRT_CTX, struct xyzzy_debug_dyn *dyn,
    VCL_STRING addr, VCL_STRING port, VCL_PROBE probe)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(dyn, VMOD_DEBUG_DYN_MAGIC);
	dyn_dir_init(ctx, dyn, addr, port, probe);
}

static int
dyn_uds_init(VRT_CTX, struct xyzzy_debug_dyn_uds *uds, VCL_STRING path)
{
	VCL_BACKEND dir, dir2;
	struct vrt_backend vrt;
	struct stat st;

	if (path == NULL) {
		VRT_fail(ctx, "path is NULL");
		return (-1);
	}
	if (*path != '/') {
		VRT_fail(ctx, "path must be an absolute path: %s", path);
		return (-1);
	}
	errno = 0;
	if (stat(path, &st) != 0) {
		VRT_fail(ctx, "Cannot stat path %s: %s", path, strerror(errno));
		return (-1);
	}
	if (!S_ISSOCK(st.st_mode)) {
		VRT_fail(ctx, "%s is not a socket", path);
		return (-1);
	}

	INIT_OBJ(&vrt, VRT_BACKEND_MAGIC);
	vrt.path = path;
	vrt.vcl_name = uds->vcl_name;
	vrt.hosthdr = "localhost";
	vrt.ipv4_suckaddr = NULL;
	vrt.ipv6_suckaddr = NULL;

	if ((dir = VRT_new_backend(ctx, &vrt)) == NULL)
		return (-1);

	AZ(pthread_mutex_lock(&uds->mtx));
	dir2 = uds->dir;
	uds->dir = dir;
	AZ(pthread_mutex_unlock(&uds->mtx));

	if (dir2 != NULL)
		VRT_delete_backend(ctx, &dir2);
	return (0);
}

VCL_VOID v_matchproto_(td_debug_dyn_uds__init)
xyzzy_dyn_uds__init(VRT_CTX, struct xyzzy_debug_dyn_uds **udsp,
		    const char *vcl_name, VCL_STRING path)
{
	struct xyzzy_debug_dyn_uds *uds;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(udsp);
	AZ(*udsp);
	AN(vcl_name);

	ALLOC_OBJ(uds, VMOD_DEBUG_UDS_MAGIC);
	AN(uds);
	REPLACE(uds->vcl_name, vcl_name);
	AZ(pthread_mutex_init(&uds->mtx, NULL));

	if (dyn_uds_init(ctx, uds, path) != 0) {
		free(uds->vcl_name);
		AZ(pthread_mutex_destroy(&uds->mtx));
		FREE_OBJ(uds);
		return;
	}
	*udsp = uds;
}

VCL_VOID v_matchproto_(td_debug_dyn_uds__fini)
xyzzy_dyn_uds__fini(struct xyzzy_debug_dyn_uds **udsp)
{
	struct xyzzy_debug_dyn_uds *uds;

	if (udsp == NULL || *udsp == NULL)
		return;
	CHECK_OBJ(*udsp, VMOD_DEBUG_UDS_MAGIC);
	uds = *udsp;
	free(uds->vcl_name);
	AZ(pthread_mutex_destroy(&uds->mtx));
	FREE_OBJ(uds);
	*udsp = NULL;
}

VCL_BACKEND v_matchproto_(td_debug_dyn_uds_backend)
xyzzy_dyn_uds_backend(VRT_CTX, struct xyzzy_debug_dyn_uds *uds)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(uds, VMOD_DEBUG_UDS_MAGIC);
	AN(uds->dir);
	return (uds->dir);
}

VCL_VOID v_matchproto_(td_debug_dyn_uds_refresh)
xyzzy_dyn_uds_refresh(VRT_CTX, struct xyzzy_debug_dyn_uds *uds, VCL_STRING path)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(uds, VMOD_DEBUG_UDS_MAGIC);
	(void) dyn_uds_init(ctx, uds, path);
}
