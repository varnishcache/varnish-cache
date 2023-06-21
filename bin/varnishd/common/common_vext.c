/*-
 * Copyright (c) 2022 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * Loadable extensions
 */

#include "config.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"
#include "vav.h"
#include "vqueue.h"
#include "vrnd.h"
#include "vsb.h"

#include "heritage.h"

struct vext {
	unsigned		magic;
#define VEXT_MAGIC		0xd5063ef6
	VTAILQ_ENTRY(vext)	list;

	char			**argv;
	int			fd;
	struct vsb		*vsb;
	void			*dlptr;
};

static VTAILQ_HEAD(,vext) vext_list =
    VTAILQ_HEAD_INITIALIZER(vext_list);

void
vext_argument(const char *arg)
{
	struct vext *vp;

	fprintf(stderr, "EEE <%s>\n", arg);
	ALLOC_OBJ(vp, VEXT_MAGIC);
	AN(vp);
	vp->argv = VAV_Parse(arg, NULL, ARGV_COMMA);
	AN(vp->argv);
	if (vp->argv[0] != NULL)
		ARGV_ERR("\tParse failure in argument: %s\n\t%s\n",
		    arg, vp->argv[0]);
	VTAILQ_INSERT_TAIL(&vext_list, vp, list);
	fprintf(stderr, "eee <%s>\n", vp->argv[1]);
	vp->fd = open(vp->argv[1], O_RDONLY);
	if (vp->fd < 0)
		ARGV_ERR("\tCannot open %s\n\t%s\n",
		    vp->argv[1], strerror(errno));
}

void
vext_iter(vext_iter_f *func, void *priv)
{
	struct vext *vp;

	VTAILQ_FOREACH(vp, &vext_list, list)
		func(VSB_data(vp->vsb), priv);
}

void
vext_copyin(struct vsb *vident)
{
	struct vext *vp;
	const char *p;
	int i, fdo;
	unsigned u;
	char buf[BUFSIZ];
	ssize_t sz, szw;

	VTAILQ_FOREACH(vp, &vext_list, list) {
		if (vp->vsb == NULL) {
			vp->vsb = VSB_new_auto();
			AN(vp->vsb);
		}
		VSB_clear(vp->vsb);
		p = strrchr(vp->argv[1], '/');
		if (p != NULL)
			p++;
		else
			p = vp->argv[0];
		VSB_printf(vident, ",-E%s", p);
		VSB_printf(vp->vsb, "vext_cache/%s,", p);
		for (i = 0; i < 8; i++) {
			AZ(VRND_RandomCrypto(&u, sizeof u));
			u %= 26;
			VSB_printf(vp->vsb, "%c", 'a' + (char)u);
		}
		VSB_cat(vp->vsb, ".so");
		AZ(VSB_finish(vp->vsb));
		fprintf(stderr, "ee2 %s\n", VSB_data(vp->vsb));
		fdo = open(VSB_data(vp->vsb), O_WRONLY|O_CREAT|O_EXCL, 0755);
		xxxassert(fdo >= 0);
		AZ(lseek(vp->fd, 0, SEEK_SET));
		do {
			sz = read(vp->fd, buf, sizeof buf);
			if (sz > 0) {
				szw = write(fdo, buf, sz);
				xxxassert(szw == sz);
			}
		} while (sz > 0);
		closefd(&fdo);
		closefd(&vp->fd);
	}
}

void
vext_load(void)
{
	struct vext *vp;

	VTAILQ_FOREACH(vp, &vext_list, list) {
		vp->dlptr = dlopen(
		    VSB_data(vp->vsb),
		    RTLD_NOW | RTLD_GLOBAL
		);
		if (vp->dlptr == NULL) {
			XXXAN(vp->dlptr);
		}
		fprintf(stderr, "Loaded -E %s\n", VSB_data(vp->vsb));
	}
}

void
vext_cleanup(int do_unlink)
{
	struct vext *vp;

	VTAILQ_FOREACH(vp, &vext_list, list) {
		if (vp->vsb != NULL && VSB_len(vp->vsb) > 0) {
			if (do_unlink)
				XXXAZ(unlink(VSB_data(vp->vsb)));
			VSB_clear(vp->vsb);
		}
	}
}
