/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 * $Id$
 *
 *
 * XXX: When we switch VCL we can have vbe_conn's dangling from
 * XXX: the backends no longer used.  When the VCL's refcount
 * XXX: drops to zero we should zap them.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include "shmlog.h"
#include "cache.h"
#include "vrt.h"

struct bes {
	unsigned		magic;
#define BES_MAGIC		0x015e17ac
	char			*hostname;
	char			*portname;
	struct addrinfo		*addr;
	struct addrinfo		*last_addr;
	double			dnsttl;
	double			dnstime;
	unsigned		dnsseq;
	TAILQ_HEAD(, vbe_conn)	connlist;
};

/*--------------------------------------------------------------------*/


static int
bes_conn_try_list(struct sess *sp, struct bes *bes)
{
	struct addrinfo *ai, *from;
	struct sockaddr_storage ss;
	int fam, sockt, proto;
	socklen_t alen;
	int s, loops;
	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];
	unsigned myseq;

	/* Called with lock held */
	myseq = bes->dnsseq;
	loops = 0;
	from = bes->last_addr;
	for (ai = from; ai != NULL && (loops != 1 || ai != from);) {
		fam = ai->ai_family;
		sockt = ai->ai_socktype;
		proto = ai->ai_protocol;
		alen = ai->ai_addrlen;
		assert(alen <= sizeof ss);
		memcpy(&ss, ai->ai_addr, alen);
		UNLOCK(&sp->backend->mtx);
		s = socket(fam, sockt, proto);
		if (s >= 0 && connect(s, (void *)&ss, alen)) {
			AZ(close(s));
			s = -1;
		}
		if (s >= 0) {
			TCP_myname(s, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
			TCP_name((void*)&ss, alen,
			    abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
			WSL(sp->wrk, SLT_BackendOpen, s, "%s %s %s %s %s",
			    sp->backend->vcl_name, abuf1, pbuf1, abuf2, pbuf2);
		}
		LOCK(&sp->backend->mtx);
		if (s >= 0) {
			if (myseq == bes->dnsseq)
				bes->last_addr = ai;
			return (s);
		}
		if (myseq != bes->dnsseq) {
			loops = 0;
			from = bes->last_addr;
			ai = from;
		} else {
			ai = ai->ai_next;
			if (ai == NULL) {
				loops++;
				ai = bes->addr;
			}
		}
	}
	return (-1);
}

/*--------------------------------------------------------------------*/

static int
bes_conn_try(struct sess *sp, struct backend *bp)
{
	int s;
	struct bes *bes;
	struct addrinfo *res, hint, *old;
	int error;

	CAST_OBJ_NOTNULL(bes, bp->priv, BES_MAGIC);

	LOCK(&bp->mtx);

	s = bes_conn_try_list(sp, bes);
	if (s >= 0) {
		bp->refcount++;
		UNLOCK(&bp->mtx);
		return (s);
	}

	if (bes->dnstime + bes->dnsttl >= TIM_mono()) {
		UNLOCK(&bp->mtx);
		return (-1);
	}

	/* Then do another lookup to catch DNS changes */
	bes->dnstime = TIM_mono();
	UNLOCK(&bp->mtx);

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(bes->hostname,
	    bes->portname == NULL ? "http" : bes->portname,
	    &hint, &res);
	if (error) {
		if (res != NULL)
			freeaddrinfo(res);
		printf("getaddrinfo: %s\n", gai_strerror(error)); /* XXX */
		LOCK(&bp->mtx);
	} else {
		LOCK(&bp->mtx);
		bes->dnsseq++;
		old = bes->addr;
		bes->last_addr = res;
		bes->addr = res;
		if (old != NULL)
			freeaddrinfo(old);
	}

	/* And try the entire list */
	s = bes_conn_try_list(sp, bes);
	if (s >= 0) {
		bp->refcount++;
		UNLOCK(&bp->mtx);
		return (s);
	}

	UNLOCK(&bp->mtx);
	return (-1);
}

/* Get a backend connection ------------------------------------------
 *
 * Try all cached backend connections for this backend, and use the
 * first one that is looks like it is still connected.
 * If that fails to get us a connection, create a new one, reusing a
 * connection from the freelist, if possible.
 *
 * This function is slightly complicated by optimizations on besmtx.
 */

static struct vbe_conn *
bes_nextfd(struct sess *sp)
{
	struct vbe_conn *vc;
	struct pollfd pfd;
	struct backend *bp;
	int reuse = 0;
	struct bes *bes;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	bp = sp->backend;
	CAST_OBJ_NOTNULL(bes, bp->priv, BES_MAGIC);
	while (1) {
		LOCK(&bp->mtx);
		vc = TAILQ_FIRST(&bes->connlist);
		if (vc != NULL) {
			bp->refcount++;
			assert(vc->backend == bp);
			assert(vc->fd >= 0);
			TAILQ_REMOVE(&bes->connlist, vc, list);
		}
		UNLOCK(&bp->mtx);
		if (vc == NULL)
			break;

		/* Test the connection for remote close before we use it */
		pfd.fd = vc->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (!poll(&pfd, 1, 0)) {
			/* XXX locking of stats */
			VSL_stats->backend_reuse += reuse;
			VSL_stats->backend_conn++;
			return (vc);
		}
		VBE_ClosedFd(sp->wrk, vc);
	}

	vc = VBE_NewConn();
	assert(vc->fd == -1);
	AZ(vc->backend);
	vc->fd = bes_conn_try(sp, bp);
	if (vc->fd < 0) {
		VBE_ReleaseConn(vc);
		VSL_stats->backend_fail++;
		return (NULL);
	} 
	vc->backend = bp;
	VSL_stats->backend_conn++;
	return (vc);
}

/*--------------------------------------------------------------------*/

static struct vbe_conn *
bes_GetFd(struct sess *sp)
{
	struct vbe_conn *vc;
	unsigned n;
	for (n = 1; n < 5; n++) {
		vc = bes_nextfd(sp);
		if (vc == NULL) {
			usleep(100000 * n);
			continue;
		}
		assert(vc->fd >= 0);
		assert(vc->backend == sp->backend);
		WSL(sp->wrk, SLT_BackendXID, vc->fd, "%u", sp->xid);
		WSL(sp->wrk, SLT_Backend, sp->fd, "%d %s", vc->fd,
		    sp->backend->vcl_name);
		return (vc);
	}
	return (NULL);
}

/* Close a connection ------------------------------------------------*/

static void
bes_ClosedFd(struct worker *w, struct vbe_conn *vc)
{

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	assert(vc->fd >= 0);
	WSL(w, SLT_BackendClose, vc->fd, "%s", vc->backend->vcl_name);
	AZ(close(vc->fd));
	vc->fd = -1;
	VBE_DropRef(vc->backend);
	vc->backend = NULL;
	VBE_ReleaseConn(vc);
}

/* Recycle a connection ----------------------------------------------*/

static void
bes_RecycleFd(struct worker *w, struct vbe_conn *vc)
{
	struct bes *bes;

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(bes, vc->backend->priv, BES_MAGIC);

	assert(vc->fd >= 0);
	WSL(w, SLT_BackendReuse, vc->fd, "%s", vc->backend->vcl_name);
	LOCK(&vc->backend->mtx);
	VSL_stats->backend_recycle++;
	TAILQ_INSERT_HEAD(&bes->connlist, vc, list);
	VBE_DropRefLocked(vc->backend);
}

/*--------------------------------------------------------------------*/

static void
bes_Cleanup(struct backend *b)
{
	struct bes *bes;
	struct vbe_conn *vbe;

	assert(b->refcount == 0);
	CAST_OBJ_NOTNULL(bes, b->priv, BES_MAGIC);
	free(bes->portname);
	free(bes->hostname);
	freeaddrinfo(bes->addr);
	while (1) {
		vbe = TAILQ_FIRST(&bes->connlist);
		if (vbe == NULL)
			break;
		TAILQ_REMOVE(&bes->connlist, vbe, list);
		if (vbe->fd >= 0)
			close(vbe->fd);
		FREE_OBJ(vbe);
	}
	FREE_OBJ(bes);
}

/*--------------------------------------------------------------------*/

static const char *
bes_GetHostname(struct backend *b)
{
	struct bes *bes;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(bes, b->priv, BES_MAGIC);
	return (bes->hostname);
}

/*--------------------------------------------------------------------*/

static void
bes_UpdateHealth(struct sess *sp, struct vbe_conn *vc, int a)
{
	(void)sp;
	(void)vc;
	(void)a;
	
	/* 
	 * Not of any use for simple backend. The global health
	 * parameter of the backend should be enough.
	 */
}

/*--------------------------------------------------------------------*/

static void
bes_Init(void)
{

}

/*--------------------------------------------------------------------*/

struct backend_method backend_method_simple = {
	.name =			"simple",
	.getfd =		bes_GetFd,
	.close =		bes_ClosedFd,
	.recycle =		bes_RecycleFd,
	.gethostname =		bes_GetHostname,
	.updatehealth =		bes_UpdateHealth,
	.cleanup =		bes_Cleanup,
	.init =			bes_Init
};

/*--------------------------------------------------------------------*/

void
VRT_init_simple_backend(struct backend **bp, struct vrt_simple_backend *t)
{
	struct backend *b;
	struct bes *bes;
	
	/*
	 * Scan existing backends to see if we can recycle one of them.
	 */
	TAILQ_FOREACH(b, &backendlist, list) {
		CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
		if (b->method != &backend_method_simple)
			continue;
		if (strcmp(b->vcl_name, t->name))
			continue;
		CAST_OBJ_NOTNULL(bes, b->priv, BES_MAGIC);
		if (strcmp(bes->portname, t->port))
			continue;
		if (strcmp(bes->hostname, t->host))
			continue;
		b->refcount++;
		*bp = b;
		return;
	}

	b = VBE_NewBackend(&backend_method_simple);

	bes = calloc(sizeof *bes, 1);
	XXXAN(bes);
	bes->magic = BES_MAGIC;

	b->priv = bes;

	bes->dnsttl = 300;

	AN(t->name);
	REPLACE(b->vcl_name, t->name);

	AN(t->port);
	REPLACE(bes->portname, t->port);

	AN(t->host);
	REPLACE(bes->hostname, t->host);

	*bp = b;
}
