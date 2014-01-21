/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "vcli_priv.h"
#include "vrt.h"
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
	void			*hdl;
	const void		*funcs;
	int			funclen;
};

static VTAILQ_HEAD(,vmod)	vmods = VTAILQ_HEAD_INITIALIZER(vmods);

int
VRT_Vmod_Init(void **hdl, void *ptr, int len, const char *nm,
    const char *path, struct cli *cli)
{
	struct vmod *v;
	void *x, *y, *z, *w;
	char buf[256];
	void *dlhdl;

	ASSERT_CLI();

	dlhdl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (dlhdl == NULL) {
		VCLI_Out(cli, "Loading VMOD %s from %s:\n", nm, path);
		VCLI_Out(cli, "dlopen() failed: %s\n", dlerror());
		VCLI_Out(cli, "Check child process permissions.\n");
		return (1);
	}

	VTAILQ_FOREACH(v, &vmods, list)
		if (v->hdl == dlhdl)
			break;
	if (v == NULL) {
		ALLOC_OBJ(v, VMOD_MAGIC);
		AN(v);

		v->hdl = dlhdl;

		bprintf(buf, "Vmod_%s_Name", nm);
		x = dlsym(v->hdl, buf);
		bprintf(buf, "Vmod_%s_Len", nm);
		y = dlsym(v->hdl, buf);
		bprintf(buf, "Vmod_%s_Func", nm);
		z = dlsym(v->hdl, buf);
		bprintf(buf, "Vmod_%s_ABI", nm);
		w = dlsym(v->hdl, buf);
		if (x == NULL || y == NULL || z == NULL || w == NULL) {
			VCLI_Out(cli, "Loading VMOD %s from %s:\n", nm, path);
			VCLI_Out(cli, "VMOD symbols not found\n");
			VCLI_Out(cli, "Check relative pathnames.\n");
			(void)dlclose(v->hdl);
			FREE_OBJ(v);
			return (1);
		}
		AN(x);
		AN(y);
		AN(z);
		AN(w);
		if (strcmp(x, nm)) {
			VCLI_Out(cli, "Loading VMOD %s from %s:\n", nm, path);
			VCLI_Out(cli, "File contain wrong VMOD (\"%s\")\n",
			    (char *) x);
			VCLI_Out(cli, "Check relative pathnames ?.\n");
			(void)dlclose(v->hdl);
			FREE_OBJ(v);
			return (1);
		}

		if (strcmp(w, VMOD_ABI_Version)) {
			VCLI_Out(cli, "Loading VMOD %s from %s:\n", nm, path);
			VCLI_Out(cli, "VMOD ABI (%s)", (char*)w);
			VCLI_Out(cli, " incompatible with varnish ABI (%s)\n",
			    VMOD_ABI_Version);
			(void)dlclose(v->hdl);
			FREE_OBJ(v);
			return (1);
		}

		// XXX: Check w for ABI version compatibility

		v->funclen = *(const int *)y;
		v->funcs = z;

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
VRT_Vmod_Fini(void **hdl)
{
	struct vmod *v;

	ASSERT_CLI();

	AN(*hdl);
	CAST_OBJ_NOTNULL(v, *hdl, VMOD_MAGIC);
	*hdl = NULL;

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
	{ "debug.vmod", "debug.vmod", "show loaded vmods", 0, 0,
		"d", ccf_debug_vmod },
	{ NULL }
};

void
VMOD_Init(void)
{

	CLI_AddFuncs(vcl_cmds);
}
