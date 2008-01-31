/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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

#include "shmlog.h"
#include "cache.h"
#include "vrt.h"


struct brr {
	unsigned		magic;
#define BRR_MAGIC		0x66f05894
	struct bspec 		*blist;
	int			count;
#if 0
	/* Store a hash of the backend info given in
	 * vcl for comparison when a new vcl file is 
	 * uploaded. Not in use yet.
	 */
	unsigned		hash;
#endif
};

struct bspec {
	unsigned		magic;
#define BSPEC_MAGIC		0x761d69c2
	struct bspec		*next;
	char			*hostname;
	char			*portname;
	struct addrinfo		*addr;
	struct addrinfo		*last_addr;
	double			dnsttl;
	double			dnstime;
	unsigned		dnsseq;
	VTAILQ_HEAD(, vbe_conn)	connlist;
	int			health;
};

/*--------------------------------------------------------------------
 * Try to get a socket connected to one of the addresses on the list.
 * We start from the cached "last good" address and try all items on
 * the list exactly once.
 * If a new DNS lookup is made while we try, we start over and try the
 * new list exactly once.
 */

static int
brr_conn_try_list(const struct sess *sp, struct bspec *bs)
{
	struct addrinfo *ai, *from;
	int s, loops;
	unsigned myseq;

	CHECK_OBJ_NOTNULL(bs, BSPEC_MAGIC);
	if (bs->addr == NULL)
		return (-1);
	AN(bs->last_addr);

	/* Called with lock held */
	myseq = bs->dnsseq;
	loops = 0;
	ai = from = bs->last_addr;
	while (1) {

		/* NB: releases/acquires lock */
		s = VBE_TryConnect(sp, ai);

		if (s >= 0) { 
			/* Update cached "last good" if still valid */
			if (myseq == bs->dnsseq)
				bs->last_addr = ai;
			return (s);
		}

		if (myseq != bs->dnsseq) {
			/* A DNS-lookup happended, try again from start */
			loops = 0;
			from = bs->last_addr;
			ai = from;
		} else {
			/* Try next one */
			ai = ai->ai_next;
			if (ai == NULL) {
				loops++;
				ai = bs->addr;
			}
		}
		if (loops == 1 && ai == from)
			return (-1);
	}
}

/*--------------------------------------------------------------------*/

static const char *
brr_dns_lookup(struct backend *bp, struct bspec *bs)
{
	struct addrinfo *res, hint, *old;
	int error;

	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	CHECK_OBJ_NOTNULL(bs, BSPEC_MAGIC);

	bs->dnstime = TIM_mono();

	/* Let go of lock while we do sleepable stuff */
	UNLOCK(&bp->mtx);

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(bs->hostname,
	    bs->portname == NULL ? "http" : bs->portname,
	    &hint, &res);
	LOCK(&bp->mtx);
	if (error) {
		if (res != NULL)
			freeaddrinfo(res);
		return(gai_strerror(error));
	} 
	bs->dnsseq++;
	old = bs->addr;
	bs->last_addr = res;
	bs->addr = res;
	if (old != NULL)
		freeaddrinfo(old);
	return (NULL);
}

/*--------------------------------------------------------------------*/

static int
brr_conn_try(const struct sess *sp, struct backend *bp, struct bspec *bs)
{
	int s;

	LOCK(&bp->mtx);

	s = brr_conn_try_list(sp, bs);
	if (s >= 0) {
		bp->refcount++;
		UNLOCK(&bp->mtx);
		return (s);
	}

	if (bs->dnstime + bs->dnsttl >= TIM_mono()) {
		UNLOCK(&bp->mtx);
		return (-1);
	}

	(void)brr_dns_lookup(bp, bs);

	/* And try the entire list */
	s = brr_conn_try_list(sp, bs);
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
 * This function is slightly complicated by optimizations on brrmtx.
 */

static struct vbe_conn *
brr_nextfd(const struct sess *sp)
{
	struct vbe_conn *vc;
	struct backend *bp;
	int reuse = 0;
	struct brr *brr;
	struct bspec *bs;
	int min_health = -10;
	int num = 0;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->backend, BACKEND_MAGIC);
	bp = sp->backend;
	CAST_OBJ_NOTNULL(brr, bp->priv, BRR_MAGIC);
	
	do {
		bs = brr->blist = brr->blist->next;
		num++;
		if (num > brr->count) {
			min_health *= 10;
			num = 0;
		}
	} while (bs->health < min_health);
		
	while (1) {
		LOCK(&bp->mtx);
		vc = VTAILQ_FIRST(&bs->connlist);
		if (vc != NULL) {
			bp->refcount++;
			assert(vc->backend == bp);
			assert(vc->fd >= 0);
			VTAILQ_REMOVE(&bs->connlist, vc, list);
		}
		UNLOCK(&bp->mtx);
		if (vc == NULL)
			break;

		if (VBE_CheckFd(vc->fd)) {
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
	vc->fd = brr_conn_try(sp, bp, bs);
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
brr_GetFd(const struct sess *sp)
{
	struct vbe_conn *vc;
	unsigned n;
	for (n = 1; n < 5; n++) {
		vc = brr_nextfd(sp);
		if (vc == NULL) {
			AZ(usleep(100000 * n));
			continue;
		}
		assert(vc->fd >= 0);
		assert(vc->backend == sp->backend);
		WSL(sp->wrk, SLT_BackendXID, vc->fd, "%u", sp->xid);
		WSP(sp, SLT_Backend, "%d %s", vc->fd, sp->backend->vcl_name);
		return (vc);
	}
	return (NULL);
}

/*--------------------------------------------------------------------*/

static void
brr_ClosedFd(struct worker *w, struct vbe_conn *vc)
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
brr_RecycleFd(struct worker *w, struct vbe_conn *vc)
{
	struct bspec *bs;

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(vc->backend, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(bs, vc->priv, BSPEC_MAGIC);

	assert(vc->fd >= 0);
	WSL(w, SLT_BackendReuse, vc->fd, "%s", vc->backend->vcl_name);
	LOCK(&vc->backend->mtx);
	VSL_stats->backend_recycle++;
	VTAILQ_INSERT_HEAD(&bs->connlist, vc, list);
	VBE_DropRefLocked(vc->backend);
}

/*--------------------------------------------------------------------*/

static void
brr_Cleanup(const struct backend *b)
{
	struct brr *brr;
	struct vbe_conn *vbe;
	struct bspec *bs, *bstmp;

	assert(b->refcount == 0);
	CAST_OBJ_NOTNULL(brr, b->priv, BRR_MAGIC);
	
	bs = brr->blist;
	
	do {
		free(bs->portname);
		free(bs->hostname);
		freeaddrinfo(bs->addr);
		while (1) {
			vbe = VTAILQ_FIRST(&bs->connlist);
			if (vbe == NULL)
				break;
			VTAILQ_REMOVE(&bs->connlist, vbe, list);
			if (vbe->fd >= 0)
				AZ(close(vbe->fd));
			free(vbe);
		}
		bstmp = bs;
		bs = bs->next;
		free(bstmp);
	} while (bs != brr->blist);

	free(brr);
}

/*--------------------------------------------------------------------*/

/* Will return the hostname of the first backend in the list */
static const char *
brr_GetHostname(const struct backend *b)
{
	struct brr *brr;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	CAST_OBJ_NOTNULL(brr, b->priv, BRR_MAGIC);
	return (brr->blist->hostname);
}

/*--------------------------------------------------------------------*/

/* This should maybe be divided into two separate functions. One for 
 * increasing/decreasing health, and one for "pulling" the health
 * towards neutral (0) as time passes
 */
static void
brr_UpdateHealth(const struct sess *sp, const struct vbe_conn *vc, int add)
{
	struct bspec *bs, *first;
	struct brr *brr;
	
	if (vc != NULL) {
	
		CAST_OBJ_NOTNULL(bs, vc->priv, BSPEC_MAGIC);
	
		if (bs->health + add >= -10000 || bs->health + add <= 10000)
			bs->health += add;
	} else {
		CAST_OBJ_NOTNULL(brr, sp->backend->priv, BSPEC_MAGIC);
		first = brr->blist;
		bs = first;
		do {
			bs = bs->next;
			bs->health = (int)((double)bs->health / 2);
		} while (bs != first);
	}
}

/*--------------------------------------------------------------------*/

struct backend_method backend_method_round_robin = {
	.name =			"round_robin",
	.getfd =		brr_GetFd,
	.close =		brr_ClosedFd,
	.recycle =		brr_RecycleFd,
	.gethostname =		brr_GetHostname,
	.updatehealth =		brr_UpdateHealth,
	.cleanup =		brr_Cleanup,
};

/*--------------------------------------------------------------------*/

void
VRT_init_round_robin_backend(struct backend **bp, const struct vrt_round_robin_backend *t)
{
	struct backend *b;
	struct brr *brr;
	struct vrt_backend_entry *be;
	struct bspec *bs = NULL;
	struct bspec *bs_prev = NULL;
	struct bspec *bs_first = NULL;
	
	/*
	 * Scan existing backends to see if we can recycle one of them.
	 */
	/* 
	 * XXX: Do this by comparing a hash generated from this new 
	 * XXX: backend with the earlier computed hashes from existing
	 * XXX: backends ? Should the hash be a parameter to this function,
	 * XXX: or computed here? 
	 */	 
	

	b = VBE_NewBackend(&backend_method_round_robin);

	brr = calloc(sizeof *brr, 1);
	XXXAN(brr);
	brr->magic = BRR_MAGIC;
	brr->count = t->count;

	b->priv = brr;

	AN(t->name);
	b->vcl_name = strdup(t->name);
	XXXAN(b->vcl_name);

	be = t->bentry;
	while (be != NULL) {
		bs = calloc(sizeof *bs, 1);
		XXXAN(bs);
		bs->magic = BSPEC_MAGIC;
		AN(be->port);
		bs->portname = strdup(be->port);
		XXXAN(bs->portname);
		
		AN(be->host);
		bs->hostname = strdup(be->host);
		XXXAN(bs->hostname);
		
		bs->dnsttl = 300;
		bs->health = 0;
		
		if (bs_first == NULL)
			bs_first = bs;
			
		bs->next = bs_prev;
		bs_prev = bs;
		be = be->next;
	}
	
	XXXAN(bs_first);
	bs_first->next = bs;
	brr->blist = bs;
	
	*bp = b;
}

