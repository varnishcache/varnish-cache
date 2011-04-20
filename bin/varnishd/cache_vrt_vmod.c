/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "cli_priv.h"
#include "vrt.h"
#include "cache.h"

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

void
VRT_Vmod_Init(void **hdl, void *ptr, int len, const char *nm, const char *path)
{
	struct vmod *v;
	void *x;
	const int *i;

	ASSERT_CLI();

	VTAILQ_FOREACH(v, &vmods, list)
		if (!strcmp(v->nm, nm))
			break;
	if (v == NULL) {
		ALLOC_OBJ(v, VMOD_MAGIC);
		AN(v);

		VTAILQ_INSERT_TAIL(&vmods, v, list);
		VSC_main->vmods++;

		REPLACE(v->nm, nm);
		REPLACE(v->path, path);

		v->hdl = dlopen(v->path, RTLD_NOW | RTLD_LOCAL);
		AN(v->hdl);

		x = dlsym(v->hdl, "Vmod_Name");
		AN(x);
		/* XXX: check that name is correct */

		x = dlsym(v->hdl, "Vmod_Len");
		AN(x);
		i = x;
		v->funclen = *i;

		x = dlsym(v->hdl, "Vmod_Func");
		AN(x);
		v->funcs = x;
	}

	assert(len == v->funclen);
	memcpy(ptr, v->funcs, v->funclen);
	v->ref++;

	*hdl = v;
}

void
VRT_Vmod_Fini(void **hdl)
{
	struct vmod *v;

	ASSERT_CLI();

	AN(*hdl);
	CAST_OBJ_NOTNULL(v, *hdl, VMOD_MAGIC);
	*hdl = NULL;
	if (--v->ref != 0)
		return;
#ifndef DONT_DLCLOSE_VMODS
	AZ(dlclose(v->hdl));
#endif
	free(v->nm);
	free(v->path);
	VTAILQ_REMOVE(&vmods, v, list);
	VSC_main->vmods--;
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
		cli_out(cli, "%5d %s (%s)\n", v->ref, v->nm, v->path);
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
