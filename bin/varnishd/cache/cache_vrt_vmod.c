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

#include "cache.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vcli_serve.h"
#include "vrt.h"

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
	void			*hdl;
	const void		*funcs;
	int			funclen;
};

static VTAILQ_HEAD(,vmod)	vmods = VTAILQ_HEAD_INITIALIZER(vmods);

static int
vrt_vmod_backup_copy(VRT_CTX, const char *nm, const char *fm, const char *to)
{
	int fi, fo;
	int ret = 0;
	ssize_t sz;
	char buf[BUFSIZ];

	fo = open(to, O_WRONLY | O_CREAT | O_EXCL, 0744);
	if (fo < 0 && errno == EEXIST)
		return (0);
	if (fo < 0) {
		VSB_printf(ctx->msg, "Creating copy of vmod %s: %s\n",
		    nm, strerror(errno));
		return (1);
	}
	fi = open(fm, O_RDONLY);
	if (fi < 0) {
		VSB_printf(ctx->msg, "Opening vmod %s from %s: %s\n",
		    nm, fm, strerror(errno));
		AZ(unlink(to));
		AZ(close(fo));
		return (1);
	}
	while (1) {
		sz = read(fi, buf, sizeof buf);
		if (sz == 0)
			break;
		if (sz < 0 || sz != write(fo, buf, sz)) {
			VSB_printf(ctx->msg, "Copying vmod %s: %s\n",
			    nm, strerror(errno));
			AZ(unlink(to));
			ret = 1;
			break;
		}
	}
	AZ(close(fi));
	AZ(close(fo));
	return(ret);
}

int
VRT_Vmod_Init(VRT_CTX, struct vmod **hdl, void *ptr, int len, const char *nm,
    const char *path, const char *file_id, const char *backup)
{
	struct vmod *v;
	const struct vmod_data *d;
	char buf[256];
	void *dlhdl;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(ctx->msg);
	AN(hdl);
	AZ(*hdl);

	/*
	 * We make a backup copy of the VMOD shlib in our working directory
	 * and dlopen that, so that we can still restart the VCL's we have
	 * already compiled when people updated their VMOD package.
	 */
	if (vrt_vmod_backup_copy(ctx, nm, path, backup))
		return (1);

	dlhdl = dlopen(backup, RTLD_NOW | RTLD_LOCAL);
	if (dlhdl == NULL) {
		VSB_printf(ctx->msg, "Loading vmod %s from %s:\n", nm, backup);
		VSB_printf(ctx->msg, "dlopen() failed: %s\n", dlerror());
		return (1);
	}

	VTAILQ_FOREACH(v, &vmods, list)
		if (v->hdl == dlhdl)
			break;
	if (v == NULL) {
		ALLOC_OBJ(v, VMOD_MAGIC);
		AN(v);
		REPLACE(v->backup, backup);

		v->hdl = dlhdl;

		bprintf(buf, "Vmod_%s_Data", nm);
		d = dlsym(v->hdl, buf);
		if (d == NULL ||
		    d->file_id == NULL ||
		    strcmp(d->file_id, file_id)) {
			VSB_printf(ctx->msg,
			    "Loading vmod %s from %s:\n", nm, path);
			VSB_printf(ctx->msg,
			    "This is no longer the same file seen by"
			    " the VCL-compiler.\n");
			(void)dlclose(v->hdl);
			FREE_OBJ(v);
			return (1);
		}
		if (d->vrt_major != VRT_MAJOR_VERSION ||
		    d->vrt_minor > VRT_MINOR_VERSION ||
		    d->name == NULL ||
		    strcmp(d->name, nm) ||
		    d->func == NULL ||
		    d->func_len <= 0 ||
		    d->proto == NULL ||
		    d->spec == NULL ||
		    d->abi == NULL) {
			VSB_printf(ctx->msg,
			    "Loading VMOD %s from %s:\n", nm, path);
			VSB_printf(ctx->msg, "VMOD data is mangled.\n");
			(void)dlclose(v->hdl);
			FREE_OBJ(v);
			return (1);
		}

		v->funclen = d->func_len;
		v->funcs = d->func;

		REPLACE(v->nm, nm);
		REPLACE(v->path, path);

		VSC_C_main->vmods++;
		VTAILQ_INSERT_TAIL(&vmods, v, list);
	}

	assert(len == v->funclen);
	memcpy(ptr, v->funcs, v->funclen);
	v->ref++;

	*hdl = v;
	return (0);
}

void
VRT_Vmod_Fini(struct vmod **hdl)
{
	struct vmod *v;

	ASSERT_CLI();

	TAKE_OBJ_NOTNULL(v, hdl, VMOD_MAGIC);

#ifndef DONT_DLCLOSE_VMODS
	/*
	 * atexit(3) handlers are not called during dlclose(3).  We don't
	 * normally use them, but we do when running GCOV.  This option
	 * enables us to do that.
	 */
	AZ(dlclose(v->hdl));
#endif
	if (--v->ref != 0)
		return;
	free(v->nm);
	free(v->path);
	AZ(unlink(v->backup));
	free(v->backup);
	VTAILQ_REMOVE(&vmods, v, list);
	VSC_C_main->vmods--;
	FREE_OBJ(v);
}

/*---------------------------------------------------------------------*/

static void
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
