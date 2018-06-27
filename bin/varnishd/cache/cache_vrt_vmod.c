/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Runtime support for compiled VCL programs
 */

#include "config.h"

#include "cache_varnishd.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "vcli_serve.h"
#include "vmod_abi.h"

/*--------------------------------------------------------------------
 * Modules stuff
 */

struct vmod {
	unsigned		magic;
#define VMOD_MAGIC		0xb750219c

	VTAILQ_ENTRY(vmod)	list;

	int			ref;

	char			*nm;
	char			*path;
	char			*backup;
	char			*file_id;
	void			*hdl;
	const void		*funcs;
	int			funclen;
	const char		*abi;
	unsigned		vrt_major;
	unsigned		vrt_minor;
};

/* the vmods list is owned by the cli thread */
static VTAILQ_HEAD(,vmod)	vmods = VTAILQ_HEAD_INITIALIZER(vmods);

/* protects all (struct vmod).ref */
static pthread_mutex_t		vmod_ref_mtx = PTHREAD_MUTEX_INITIALIZER;

static unsigned
vmod_abi_mismatch(const struct vmod_data *d)
{

	if (d->vrt_major == 0 && d->vrt_minor == 0)
		return (d->abi == NULL || strcmp(d->abi, VMOD_ABI_Version));

	return (d->vrt_major != VRT_MAJOR_VERSION ||
	    d->vrt_minor > VRT_MINOR_VERSION);
}

int
VRT_Vmod_Init(VRT_CTX, struct vmod **hdl, void *ptr, int len, const char *nm,
    const char *path, const char *file_id, const char *backup)
{
	struct vmod *v;
	const struct vmod_data *d;
	char buf[256];

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(ctx->msg);
	AN(hdl);
	AZ(*hdl);

	VTAILQ_FOREACH(v, &vmods, list)
		if (strcmp(v->nm, nm) == 0 &&
		    strcmp(v->path, path) == 0 &&
		    strcmp(v->file_id, file_id) == 0)
			break;

	if (v == NULL) {
		ALLOC_OBJ(v, VMOD_MAGIC);
		AN(v);

		v->hdl = dlopen(backup, RTLD_NOW | RTLD_LOCAL);
		if (v->hdl == NULL) {
			VSB_printf(ctx->msg, "Loading vmod %s from %s (%s):\n",
			    nm, backup, path);
			VSB_printf(ctx->msg, "dlopen() failed: %s\n",
			    dlerror());
			FREE_OBJ(v);
			return (1);
		}

		bprintf(buf, "Vmod_%s_Data", nm);
		d = dlsym(v->hdl, buf);
		if (d == NULL ||
		    d->file_id == NULL ||
		    strcmp(d->file_id, file_id)) {
			VSB_printf(ctx->msg, "Loading vmod %s from %s (%s):\n",
			    nm, backup, path);
			VSB_printf(ctx->msg,
			    "This is no longer the same file seen by"
			    " the VCL-compiler.\n");
			(void)dlclose(v->hdl);
			FREE_OBJ(v);
			return (1);
		}
		if (vmod_abi_mismatch(d) ||
		    d->name == NULL ||
		    strcmp(d->name, nm) ||
		    d->func == NULL ||
		    d->func_len <= 0 ||
		    d->proto == NULL ||
		    d->json == NULL) {
			VSB_printf(ctx->msg, "Loading vmod %s from %s (%s):\n",
			    nm, backup, path);
			VSB_printf(ctx->msg, "VMOD data is mangled.\n");
			(void)dlclose(v->hdl);
			FREE_OBJ(v);
			return (1);
		}

		v->funclen = d->func_len;
		v->funcs = d->func;
		v->abi = d->abi;
		v->vrt_major = d->vrt_major;
		v->vrt_minor = d->vrt_minor;

		REPLACE(v->nm, nm);
		REPLACE(v->path, path);
		REPLACE(v->file_id, file_id);
		REPLACE(v->backup, backup);

		VSC_C_main->vmods++;
		VTAILQ_INSERT_TAIL(&vmods, v, list);
	}

	assert(len == v->funclen);
	memcpy(ptr, v->funcs, v->funclen);
	AZ(pthread_mutex_lock(&vmod_ref_mtx));
	v->ref++;
	AZ(pthread_mutex_unlock(&vmod_ref_mtx));

	*hdl = v;
	return (0);
}

void
VRT_Vmod_Fini(struct vmod **hdl)
{
	ASSERT_CLI();

	(void) VRT_Vmod_Unref(hdl);
}

/* ref a vmod via the handle, return previous number of references */
int
VRT_Vmod_Ref(struct vmod *v)
{
	int ref;

	AZ(pthread_mutex_lock(&vmod_ref_mtx));
	ref = v->ref++;
	AZ(pthread_mutex_unlock(&vmod_ref_mtx));

	/* initial ref only via _Init */
	assert(ref > 0);
	return (ref);
}

/* deref a vmod via the handle, return new number of references */
int
VRT_Vmod_Unref(struct vmod **hdl)
{
	struct vmod *v;
	int ref;

	TAKE_OBJ_NOTNULL(v, hdl, VMOD_MAGIC);

	AZ(pthread_mutex_lock(&vmod_ref_mtx));
	ref = --v->ref;
	AZ(pthread_mutex_unlock(&vmod_ref_mtx));
	assert(ref >= 0);

	if (ref != 0)
		return (ref);

#ifndef DONT_DLCLOSE_VMODS
	/*
	 * atexit(3) handlers are not called during dlclose(3).  We don't
	 * normally use them, but we do when running GCOV.  This option
	 * enables us to do that.
	 */
	AZ(dlclose(v->hdl));
#endif
	free(v->nm);
	free(v->path);
	free(v->file_id);
	free(v->backup);
	VTAILQ_REMOVE(&vmods, v, list);
	VSC_C_main->vmods--;
	FREE_OBJ(v);

	return (ref);
}

void
VMOD_Panic(struct vsb *vsb)
{
	struct vmod *v;

	VSB_printf(vsb, "vmods = {\n");
	VSB_indent(vsb, 2);
	VTAILQ_FOREACH(v, &vmods, list)
		VSB_printf(vsb, "%s = {%s, %u.%u},\n",
		    v->nm, v->abi, v->vrt_major, v->vrt_minor);
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*---------------------------------------------------------------------*/

static void v_matchproto_(cli_func_t)
ccf_debug_vmod(struct cli *cli, const char * const *av, void *priv)
{
	struct vmod *v;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	VTAILQ_FOREACH(v, &vmods, list)
		VCLI_Out(cli, "%5d %s (%s)\n", v->ref, v->nm, v->path);
}

static struct cli_proto vcl_cmds[] = {
	{ CLICMD_DEBUG_VMOD,			"d", ccf_debug_vmod },
	{ NULL }
};

void
VMOD_Init(void)
{

	CLI_AddFuncs(vcl_cmds);
}
