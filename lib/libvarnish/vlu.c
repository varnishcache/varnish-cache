/*-
 * Copyright (c) 2005-2008 Poul-Henning Kamp <phk@freebsd.org>
 * All rights reserved.
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
 * Functions for assembling a bytestream into text-lines and calling
 * a function on each.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vas.h"	// XXX Flexelint "not used" - but req'ed for assert()
#include "miniobj.h"
#include "vdef.h"
#include "vsb.h"

#include "vlu.h"

struct vlu {
	unsigned	magic;
#define LINEUP_MAGIC	0x8286661
	struct vsb	*buf;
	void		*priv;
	vlu_f	*func;
};

struct vlu *
VLU_New(void *priv, vlu_f *func)
{
	struct vlu *l;

	ALLOC_OBJ(l, LINEUP_MAGIC);
	if (l != NULL) {
		l->func = func;
		l->priv = priv;
		l->buf = VSB_new_auto();
		if (l->buf == NULL) {
			FREE_OBJ(l);
			l = NULL;
		}
	}
	return (l);
}

void
VLU_Destroy(struct vlu *l)
{

	CHECK_OBJ_NOTNULL(l, LINEUP_MAGIC);
	VSB_destroy(&l->buf);
	FREE_OBJ(l);
}

static int
LineUpProcess(struct vlu *l)
{
	char *p, *q;
	int i;

	VSB_finish(l->buf);
	for (p = VSB_data(l->buf); *p != '\0'; p = q) {
		/* Find first CR or NL */
		for (q = p; *q != '\0'; q++) {
			if (*q == '\n' || *q == '\r')
				break;
		}
		if (*q == '\0')
			break;
		*q++ = '\0';
		i = l->func(l->priv, p);
		if (i != 0)
			return (i);
	}
	if (*p != '\0') {
		q = strchr(p, '\0');
		assert(q != NULL);
		VSB_keepsome(l->buf, p, (q - p));
	} else
		VSB_clear(l->buf);
	return (0);
}

int
VLU_Fd(int fd, struct vlu *l)
{
	int i;
	char buf[BUFSIZ];

	CHECK_OBJ_NOTNULL(l, LINEUP_MAGIC);
	i = read(fd, buf, sizeof(buf));
	if (i <= 0)
		return (-1);
	VSB_bcat(l->buf, buf, i);
	return (LineUpProcess(l));
}
