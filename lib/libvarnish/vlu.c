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
	int 		telnet;
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
		l->telnet = -1;
	}
	return (l);
}

void
VLU_SetTelnet(struct vlu *l, int fd)
{
	CHECK_OBJ_NOTNULL(l, LINEUP_MAGIC);
	assert(fd >= 0);
	l->telnet = fd;
}

void
VLU_Destroy(struct vlu *l)
{

	CHECK_OBJ_NOTNULL(l, LINEUP_MAGIC);
	free(l->buf);
	FREE_OBJ(l);
}

static int
vlu_dotelnet(struct vlu *l, char *p)
{
	char *e;
	char tno[3];
	int i;

	e = l->buf + l->bufp;
	assert(p >= l->buf && p < e);
	assert(*p == (char)255);

	/* We need at least two characters */
	if (p == e - 1)
		return (1);

	/* And three for will/wont/do/dont */
	if (p[1] >= (char)251 && p[1] <= (char)254 && p == e - 2)
		return (1);

	switch (p[1]) {
	case (char)251:	/* WILL */
	case (char)252:	/* WONT */
		/* Ignore these */
		i = 3;
		break;
	case (char)253:	/* DO */
	case (char)254:	/* DONT */
		/* Return WONT for these */
		memcpy(tno, p, 3);
		tno[1] = (char)252;
		write(l->telnet, tno, 3);
		i = 3;
		break;
	default:
		/* Ignore the rest */
		/* XXX: only p[1] >= 240 ? */
		i = 2;
	}

	/* Remove telnet sequence from buffer */
	memmove(p, p + i, 1 + e - (p + i));
	l->bufp -= i;
	return (0);
}

static int
LineUpProcess(struct vlu *l)
{
	char *p, *q;
	int i;

	l->buf[l->bufp] = '\0';
	for (p = l->buf; *p != '\0'; p = q) {
		/* Find first CR or NL */
		for (q = p; *q != '\0'; q++) {
			while (l->telnet >= 0 && *q == (char)255) 
				if (vlu_dotelnet(l, q))
					return (0);
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
