/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
 * All rights reserved.
 *
 * Author: Cecilie Fritzvold <cecilihf@linpro.no>
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


struct ber {
	unsigned		magic;
#define BER_MAGIC		0x645b03f4
	struct brspec 		*blist;
	int			count;
#if 0
	/* Store a hash of the backend info given in
	 * vcl for comparison when a new vcl file is 
	 * uploaded. Not in use yet.
	 */
	unsigned		hash;
#endif
};

struct brspec {
	unsigned		magic;
#define BRSPEC_MAGIC		0x5aa072a7
	struct brspec		*next;
	double			limit;
	char			*hostname;
	char			*portname;
	struct addrinfo		*addr;
	struct addrinfo		*last_addr;
	double			dnsttl;
	double			dnstime;
	unsigned		dnsseq;
	TAILQ_HEAD(, vbe_conn)	connlist;
	int			health;
};

/*--------------------------------------------------------------------*/

static int
ber_conn_try_list(struct sess *sp, struct brspec *bs)
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
	myseq = bs->dnsseq;
	loops = 0;
	from = bs->last_addr;
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
			if (myseq == bs->dnsseq)
				bs->last_addr = ai;
			return (s);
		}
		if (myseq != bs->dnsseq) {
			loops = 0;
			from = bs->last_addr;
			ai = from;
		} else {
			ai = ai->ai_next;
			if (ai == NULL) {
				loops++;
				ai = bs->addr;
			}
		}
	}
	return (-1);
}

/*--------------------------------------------------------------------*/

static int
ber_conn_try(struct sess *sp, struct backend *bp, struct brspec *bs)
{
	int s;
	struct addrinfo *res, hint, *old;
	int error;

	LOCK(&bp->mtx);

	s = ber_conn_try_list(sp, bs);
	if (s >= 0) {
		bp->refcount++;
		UNLOCK(&bp->mtx);
		return (s);
	}

	if (bs->dnstime + bs->dnsttl >= TIM_mono()) {
		UNLOCK(&bp->mtx);
		return (-1);
	}

	/* Then do another lookup to catch DNS changes */
	bs->dnstime = TIM_mono();
	UNLOCK(&bp->mtx);

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(bs->hostname,
	    bs->portname == NULL ? "http" : bs->portname,
	    &hint, &res);
	if (error) {
		if (res != NULL)
			freeaddrinfo(res);
		printf("getaddrinfo: %s\n", gai_strerror(error)); /* XXX */
		LOCK(&bp->mtx);
	} else {
		LOCK(&bp->mtx);
		bs->dnsseq++;
		old = bs->addr;
		bs->last_addr = res;
		bs->addr = res;
		if (old != NULL)
			freeaddrinfo(old);
	}

	/* And try the entire list */
	s = ber_conn_try_list(sp, bs);
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
 * Get the next backend in the round-robin list, and connect to this.
 *
 * Try all cached backend connections for this backend, and use the
 * first one that is looks like it is still connected.
 * If that fails to get us a connection, create a new one, reusing a
 * connection from the freelist, if possible.
 *
 * This function is slightly complicated by optimizations on bermtx.
 */

static struct vbe_conn *
ber_nextfd(struct sess *sp)
{
	struct vbe_conn *vc;
	struct pollfd pfd;
	struct backend *bp;
	int reuse = 0;
	struct ber *ber;
	struct brspec *bs;
	double r;
	int min_health = -10;
	int num = 0;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	bp = sp->backend;
	CAST_OBJ_NOTNULL(ber, bp->priv, BER_MAGIC);
	
	r = (double)rand() / ((double)(RAND_MAX)+1.0);
	bs = ber->blist;
	CHECK_OBJ_NOTNULL(bs, BRSPEC_MAGIC);
	while (r > bs->limit) {
		bs = bs->next;
		CHECK_OBJ_NOTNULL(bs, BRSPEC_MAGIC);
	}

	/* If health is low (bad), use round-robin to find
	 * a server with better health (if possible).
	 */
	while (bs->health < min_health) {
		bs = bs->next;
		num++;
		if (num > ber->count) {
			min_health *= 10;
			num = 0;
		}
	}	

	while (1) {
		LOCK(&bp->mtx);
		vc = TAILQ_FIRST(&bs->connlist);
		if (vc != NULL) {
			bp->refcount++;
			assert(vc->backend == bp);
			assert(vc->fd >= 0);
			TAILQ_REMOVE(&bs->connlist, vc, list);
			vc->priv = bs;
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
	vc->fd = ber_conn_try(sp, bp, bs);
	if (vc->fd < 0) {
		VBE_ReleaseConn(vc);
		VSL_stats->backend_fail++;
		return (NULL);
	} 
	vc->backend = bp;
	vc->priv = bs;
	VSL_stats->backend_conn++;
	return (vc);
}

static struct vbe_conn *
ber_GetFd(struct sess *sp)
{
	struct vbe_conn *vc;
	unsigned n;
	for (n = 1; n < 5; n++) {
		vc = ber_nextfd(sp);
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

/*--------------------------------------------------------------------*/

static void
ber_ClosedFd(struct worker *w, struct vbe_conn *vc)
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

/*--------------------------------------------------------------------*/

static void
ber_RecycleFd(struct worker *w, struct vbe_conn *vc)
{
	struct brspec *bs;

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(bs, vc->priv, BRSPEC_MAGIC);

	assert(vc->fd >= 0);
	WSL(w, SLT_BackendReuse, vc->fd, "%s", vc->backend->vcl_name);
	LOCK(&vc->backend->mtx);
	VSL_stats->backend_recycle++;
	TAILQ_INSERT_HEAD(&bs->connlist, vc, list);
	VBE_DropRefLocked(vc->backend);
}

/*--------------------------------------------------------------------*/

static void
ber_Cleanup(struct backend *b)
{
	struct ber *ber;
	struct vbe_conn *vbe;
	struct brspec *bs, *bstmp;

	assert(b->refcount == 0);
	CAST_OBJ_NOTNULL(ber, b->priv, BER_MAGIC);
	
	bs = ber->blist;
	
	do {
		free(bs->portname);
		free(bs->hostname);
		freeaddrinfo(bs->addr);
		while (1) {
			vbe = TAILQ_FIRST(&bs->connlist);
			if (vbe == NULL)
				break;
			TAILQ_REMOVE(&bs->connlist, vbe, list);
			if (vbe->fd >= 0)
				close(vbe->fd);
			free(vbe);
		}
		bstmp = bs;
		bs = bs->next;
		free(bstmp);
	} while (bs != ber->blist);

	free(ber);
}

/*--------------------------------------------------------------------*/

/* Will return the hostname of the first backend in the list */
static const char *
ber_GetHostname(struct backend *b)
{
	struct ber *ber;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(ber, b->priv, BER_MAGIC);
	return (ber->blist->hostname);
}

/*--------------------------------------------------------------------*/

static void
ber_UpdateHealth(struct sess *sp, struct vbe_conn *vc, int add)
{
	struct brspec *bs, *first;
	struct ber *ber;
	
	if (vc != NULL) {
		CAST_OBJ_NOTNULL(bs, vc->priv, BRSPEC_MAGIC);
	
		if (bs->health + add >= -10000 || bs->health + add <= 10000)
			bs->health += add;
	} else {
		CAST_OBJ_NOTNULL(ber, sp->backend->priv, BRSPEC_MAGIC);
		first = ber->blist;
		bs = first;
		do {
			bs = bs->next;
			bs->health = (int)((double)bs->health / 2);
		} while (bs != first);
	}
}

/*--------------------------------------------------------------------*/

static void
ber_Init(void)
{

}

/*--------------------------------------------------------------------*/

struct backend_method backend_method_random = {
	.name =			"random",
	.getfd =		ber_GetFd,
	.close =		ber_ClosedFd,
	.recycle =		ber_RecycleFd,
	.gethostname =		ber_GetHostname,
	.updatehealth =		ber_UpdateHealth,
	.cleanup =		ber_Cleanup,
	.init =			ber_Init
};

/*--------------------------------------------------------------------*/

void
VRT_init_random_backend(struct backend **bp, struct vrt_random_backend *t)
{
	struct backend *b;
	struct ber *ber;
	struct vrt_backend_entry *be;
	struct brspec *bs = NULL;
	struct brspec *bs_prev = NULL;
	struct brspec *bs_first = NULL;
	double limit = 0;
	double default_weight;
	
	/*
	 * Scan existing backends to see if we can recycle one of them.
	 */
	
	/* 
	 * XXX: Do this by comparing a hash generated from this new 
	 * XXX: backend with the earlier computed hashes from existing
	 * XXX: backends ? Should the hash be a parameter to this function,
	 * XXX: or computed here? 
	 */	 
	
	b = VBE_NewBackend(&backend_method_random);

	ber = calloc(sizeof *ber, 1);
	XXXAN(ber);
	ber->magic = BER_MAGIC;
	ber->count = t->count;

	b->priv = ber;

	AN(t->name);
	b->vcl_name = strdup(t->name);
	XXXAN(b->vcl_name);

	default_weight = 1.0 / (double)t->count;

	be = t->bentry;
	while (be != NULL) {
		bs = calloc(sizeof *bs, 1);
		bs->magic = BRSPEC_MAGIC;
		AN(be->port);
		bs->portname = strdup(be->port);
		XXXAN(bs->portname);
		
		AN(be->host);
		bs->hostname = strdup(be->host);
		XXXAN(bs->hostname);
		
		if (!(t->weighted))
			be->weight = default_weight;
		
		limit += be->weight;
		bs->limit = limit;
		
		bs->dnsttl = 300;
		bs->health = 0;
		
		if (bs_first == NULL)
			bs_first = bs;
			
		bs->next = bs_prev;
		bs_prev = bs;
		be = be->next;
	}
	
	bs_first->next = bs;
	ber->blist = bs;
	
	*bp = b;
}

