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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

#include "vrt.h"
#include "vre.h"

/*--------------------------------------------------------------------*/

char *
vcc_regexp(struct vcc *tl)
{
	char buf[BUFSIZ], *p;
	vre_t *t;
	const char *error;
	int erroroffset;

	Expect(tl, CSTR);
	if (tl->err)
		return (NULL);
	memset(&t, 0, sizeof t);
	t = VRE_compile(tl->t->dec, 0, &error, &erroroffset);
	if (t == NULL) {
		vsb_printf(tl->sb,
		    "Regexp compilation error:\n\n%s\n\n", error);
		vcc_ErrWhere(tl, tl->t);
		return (NULL);
	}
	VRE_free(&t);
	sprintf(buf, "VGC_re_%u", tl->recnt++);
	p = TlAlloc(tl, strlen(buf) + 1);
	strcpy(p, buf);

	Fh(tl, 0, "static void *%s;\n", buf);
	Fi(tl, 0, "\tVRT_re_init(&%s, ",buf);
	EncToken(tl->fi, tl->t);
	Fi(tl, 0, ");\n");
	Ff(tl, 0, "\tVRT_re_fini(%s);\n", buf);
	return (p);
}
