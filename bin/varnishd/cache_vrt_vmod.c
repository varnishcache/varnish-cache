/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "vrt.h"
#include "cache.h"

/*--------------------------------------------------------------------
 * Modules stuff
 */

struct vmod {
	char 			*nm;
	char 			*path;
	void 			*hdl;
	const void		*funcs;
	int			funclen;
};

void
VRT_Vmod_Init(void **hdl, void *ptr, int len, const char *nm, const char *path)
{
	struct vmod *v;
	void *x;
	const int *i;
	const char *p;

	v = calloc(sizeof *v, 1);
	AN(v);
	REPLACE(v->nm, nm);
	REPLACE(v->path, path);
	fprintf(stderr, "LOAD MODULE %s (%s)\n", v->nm, v->path);
	v->hdl = dlopen(v->path, RTLD_NOW | RTLD_LOCAL);
	if (v->hdl == NULL)
		fprintf(stderr, "Err: %s\n", dlerror());
	AN(v->hdl);

	x = dlsym(v->hdl, "Vmod_Name");
	AN(x);
	p = x;
	fprintf(stderr, "Loaded name: %p\n", p);
	fprintf(stderr, "Loaded name: %s\n", p);

	x = dlsym(v->hdl, "Vmod_Len");
	AN(x);
	i = x;
	fprintf(stderr, "Loaded len: %p\n", i);
	fprintf(stderr, "Loaded len: %d\n", *i);
	assert(len == *i);

	x = dlsym(v->hdl, "Vmod_Func");
	AN(x);
	fprintf(stderr, "Loaded Funcs at: %p\n", x);
	memcpy(ptr, x, len);

	v->funcs = x;
	v->funclen = *i;

	*hdl = v;
}

void
VRT_Vmod_Fini(void **hdl)
{
	struct vmod *v;

	AN(*hdl);
	v = *hdl;
	fprintf(stderr, "UNLOAD MODULE %s\n", v->nm);
	free(*hdl);
	*hdl = NULL;
}
