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
 * Stuff for handling the CLI protocol
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id: cli.c 4235 2009-09-11 13:06:15Z phk $")

#include <ctype.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <vqueue.h>
#include <vsb.h>
#include <vlu.h>
#include <cli.h>
#include <cli_priv.h>
#include <cli_common.h>
#include <cli_serve.h>
#include <libvarnish.h>
#include <miniobj.h>
 
struct cls_func {
	unsigned			magic;
#define CLS_FUNC_MAGIC			0x7d280c9b
	VTAILQ_ENTRY(cls_func)		list;
	struct cli_proto		*clp;
};

struct cls_fd {
	unsigned			magic;
#define CLS_FD_MAGIC			0x010dbd1e
	VTAILQ_ENTRY(cls_fd)		list;
	int				fdi, fdo;
	struct vlu			*vlu;
	struct cls			*cls;
	struct cli			*cli, clis;
	cls_cb_f			*closefunc;
	void				*priv;
};

struct cls {
	unsigned			magic;
#define CLS_MAGIC			0x60f044a3
	VTAILQ_HEAD(,cls_fd)		fds;
	unsigned			nfd;
	VTAILQ_HEAD(,cls_func)		funcs;
	cls_cb_f			*before, *after;
	void				*priv;
};

static int
cls_vlu(void *priv, const char *p)
{
	struct cls_fd *cfd;
	struct cls *cs;
	struct cls_func *cfn;

	CAST_OBJ_NOTNULL(cfd, priv, CLS_FD_MAGIC);
	cs = cfd->cls;
	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);

	if (cs->before != NULL)
		cs->before(cs->priv);
	VTAILQ_FOREACH(cfn, &cs->funcs, list) {
		vsb_clear(cfd->cli->sb);
		cfd->cli->result = CLIS_OK;
		cli_dispatch(cfd->cli, cfn->clp, p);
		if (cfd->cli->result != CLIS_UNKNOWN) 
			break;
	}
	if (cs->after != NULL)
		cs->after(cs->priv);
	vsb_finish(cfd->cli->sb);
	AZ(vsb_overflowed(cfd->cli->sb));
	if (cli_writeres(cfd->fdo, cfd->cli) || cfd->cli->result == CLIS_CLOSE)
		return (1);
	return (0);
}

struct cls *
CLS_New(cls_cb_f *before, cls_cb_f *after, void *priv)
{
	struct cls *cs;

	ALLOC_OBJ(cs, CLS_MAGIC);
	AN(cs);
	VTAILQ_INIT(&cs->fds);
	VTAILQ_INIT(&cs->funcs);
	cs->before = before;
	cs->after = after;
	cs->priv = priv;
	return (cs);
}

/* XXX close call back */
int
CLS_AddFd(struct cls *cs, int fdi, int fdo, cls_cb_f *closefunc, void *priv)
{
	struct cls_fd *cfd;

	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	assert(fdi >= 0);
	assert(fdo >= 0);
	ALLOC_OBJ(cfd, CLS_FD_MAGIC);
	AN(cfd);
	cfd->cls = cs;
	cfd->fdi = fdi;
	cfd->fdo = fdo;
	cfd->vlu = VLU_New(cfd, cls_vlu, 65536);	/* XXX */
	cfd->cli = &cfd->clis;
	cfd->cli->sb = vsb_newauto();
	cfd->closefunc = closefunc;
	cfd->priv = priv;
	AN(cfd->cli->sb);
	VTAILQ_INSERT_TAIL(&cs->fds, cfd, list);
	cs->nfd++;
	return (0);
}

static void
cls_close_fd(struct cls *cs, struct cls_fd *cfd)
{
	
	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	CHECK_OBJ_NOTNULL(cfd, CLS_FD_MAGIC);

	VTAILQ_REMOVE(&cs->fds, cfd, list);
	cs->nfd--;
	VLU_Destroy(cfd->vlu);
	vsb_delete(cfd->cli->sb);
	if (cfd->closefunc == NULL) {
		(void)close(cfd->fdi);
		if (cfd->fdo != cfd->fdi)
			(void)close(cfd->fdo);
	} else {
		cfd->closefunc(cfd->priv);
	}
	FREE_OBJ(cfd);
}


int
CLS_AddFunc(struct cls *cs, struct cli_proto *clp)
{
	struct cls_func *cfn;

	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	ALLOC_OBJ(cfn, CLS_FUNC_MAGIC);
	AN(cfn);
	cfn->clp = clp;
	VTAILQ_INSERT_TAIL(&cs->funcs, cfn, list);
	return (0);
}

int
CLS_Poll(struct cls *cs, int timeout)
{
	struct cls_fd *cfd, *cfd2;
	int i, j, k;

	CHECK_OBJ_NOTNULL(cs, CLS_MAGIC);
	if (cs->nfd == 0) {
		errno = 0;
		return (-1);
	}
	assert(cs->nfd > 0);
	{
		struct pollfd pfd[cs->nfd];

		i = 0;
		VTAILQ_FOREACH(cfd, &cs->fds, list) {
			pfd[i].fd = cfd->fdi;
			pfd[i].events = POLLIN;
			pfd[i].revents = 0;
			i++;
		}
		assert(i == cs->nfd);

		j = poll(pfd, cs->nfd, timeout);
		if (j <= 0)
			return (j);
		i = 0;
		VTAILQ_FOREACH_SAFE(cfd, &cs->fds, list, cfd2) {
			assert(pfd[i].fd == cfd->fdi);
			if (pfd[i].revents & POLLHUP)
				k = 1;
			else
				k = VLU_Fd(cfd->fdi, cfd->vlu);
			if (k)
				cls_close_fd(cs, cfd);
			i++;
		}
		assert(i == j);
	}
	return (j);
}
