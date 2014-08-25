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

#include <fcntl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "cache.h"

#include "vcli_priv.h"
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
	intmax_t		st_dev;
	intmax_t		st_ino;
	void			*hdl;
	const void		*funcs;
	int			funclen;
};

static VTAILQ_HEAD(,vmod)	vmods = VTAILQ_HEAD_INITIALIZER(vmods);

int
VRT_Vmod_Init(void **hdl, void *ptr, int len, const char *nm,
    const char *path, const char *file_id, struct cli *cli)
{
	struct vmod *v;
	const struct vmod_data *d;
	char buf[256];
	void *dlhdl;
	int fd;
	struct stat st;

	ASSERT_CLI();

	fd = open(path, O_RDONLY, 0);
	if (fd < 0) {
		VCLI_Out(cli, "Loading VMOD %s from %s:\n", nm, path);
		VCLI_Out(cli, "open() failed: %s\n", strerror(errno));
		VCLI_Out(cli, "Check child process permissions.\n");
		return (1);
	}

	AZ(fstat(fd, &st));
	VTAILQ_FOREACH(v, &vmods, list)
		if (st.st_dev == v->st_dev &&
		    st.st_ino == v->st_ino &&
		    !strcmp(v->nm, nm))
			break;

	if (v != NULL) {
		AZ(close(fd));
	} else {
		dlhdl = fdlopen(fd, RTLD_NOW | RTLD_LOCAL);

		AZ(close(fd));

		if (dlhdl == NULL) {
			VCLI_Out(cli, "Loading VMOD %s from %s:\n", nm, path);
			VCLI_Out(cli, "fdlopen() failed: %s\n", dlerror());
			VCLI_Out(cli, "Check child process permissions.\n");
			return (1);
		}

		bprintf(buf, "Vmod_%s_Data", nm);

		d = dlsym(dlhdl, buf);
		if (d == NULL ||
		    d->file_id == NULL ||
		    strcmp(d->file_id, file_id)) {
			VCLI_Out(cli, "Loading VMOD %s from %s:\n", nm, path);
			VCLI_Out(cli,
			    "This is no longer the same file seen by"
			    " the VCL-compiler.\n");
			(void)dlclose(dlhdl);
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
			VCLI_Out(cli, "Loading VMOD %s from %s:\n", nm, path);
			VCLI_Out(cli, "VMOD data is mangled.\n");
			(void)dlclose(dlhdl);
			return (1);
		}

		ALLOC_OBJ(v, VMOD_MAGIC);
		AN(v);

		v->st_dev = st.st_dev;
		v->st_ino = st.st_ino;
		v->hdl = dlhdl;

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
	/*
	 * atexit(3) handlers are not called during dlclose(3).  We don't
	 * normally use them, but we do when running GCOV.  This option
	 * enables us to do that.
	 */
	AZ(dlclose(v->hdl));
#endif
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
