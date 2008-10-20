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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 *
 * Functions for assembling a bytestream into text-lines and calling
 * a function on each.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "libvarnish.h"
#include "vlu.h"
#include "miniobj.h"

struct vlu {
	unsigned	magic;
#define LINEUP_MAGIC	0x8286661
	char		*buf;
	unsigned	bufl;
	unsigned	bufp;
	void		*priv;
	vlu_f	*func;
};

struct vlu *
VLU_New(void *priv, vlu_f *func, unsigned bufsize)
{
	struct vlu *l;

	if (bufsize == 0)
		bufsize = BUFSIZ;
	ALLOC_OBJ(l, LINEUP_MAGIC);
	if (l != NULL) {
		l->func = func;
		l->priv = priv;
		l->bufl = bufsize - 1;
		l->buf = malloc(l->bufl + 1);
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
	free(l->buf);
	FREE_OBJ(l);
}

static int
LineUpProcess(struct vlu *l)
{
	char *p, *q;
	int i;

	l->buf[l->bufp] = '\0';
	for (p = l->buf; *p != '\0'; p = q) {
		q = strchr(p, '\n');
		if (q == NULL)
			break;
		*q++ = '\0';
		i = l->func(l->priv, p);
		if (i != 0)
			return (i);
	}
	if (*p != '\0') {
		q = strchr(p, '\0');
		assert(q != NULL);
		l->bufp = (unsigned)(q - p);
		memmove(l->buf, p, l->bufp);
		l->buf[l->bufp] = '\0';
	} else
		l->bufp = 0;
	return (0);
}

int
VLU_Fd(int fd, struct vlu *l)
{
	int i;

	CHECK_OBJ_NOTNULL(l, LINEUP_MAGIC);
	i = read(fd, l->buf + l->bufp, l->bufl - l->bufp);
	if (i <= 0)
		return (-1);
	l->bufp += i;
	return (LineUpProcess(l));
}

int
VLU_File(FILE *f, struct vlu *l)
{
	char *p;

	CHECK_OBJ_NOTNULL(l, LINEUP_MAGIC);
	p = fgets(l->buf + l->bufp, l->bufl - l->bufp, f);
	if (p == NULL)
		return (-1);
	l->bufp = strlen(l->buf);
	return (LineUpProcess(l));
}
