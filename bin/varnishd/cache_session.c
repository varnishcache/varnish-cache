/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * Session and Client management.
 *
 * The srcaddr structures are kept around only as a convenience feature to
 * make it possible to track down offenders and misconfigured caches.
 * As such it is pure overhead and we do not want to spend too much time
 * on maintaining it.
 *
 * We identify srcaddrs instead of full addr+port because the desired level
 * of granularity is "whois is abuse@ or tech-c@ in the RIPE database.
 *
 * XXX: The two-list session management is actually not a good idea
 * XXX: come to think of it, because we want the sessions reused in
 * XXX: Most Recently Used order.
 * XXX: Another and maybe more interesting option would be to cache
 * XXX: free sessions in the worker threads and postpone session
 * XXX: allocation until then.  This does not quite implment MRU order
 * XXX: but it does save some locking, although not that much because
 * XXX: we still have to do the source-addr lookup.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "shmlog.h"
#include "cache.h"
#include "cache_backend.h"

/*--------------------------------------------------------------------*/

struct sessmem {
	unsigned		magic;
#define SESSMEM_MAGIC		0x555859c5

	struct sess		sess;
	struct http		http[2];
	unsigned		workspace;
	VTAILQ_ENTRY(sessmem)	list;
	struct sockaddr_storage	sockaddr[2];
};

static VTAILQ_HEAD(,sessmem)	ses_free_mem[2] = {
    VTAILQ_HEAD_INITIALIZER(ses_free_mem[0]),
    VTAILQ_HEAD_INITIALIZER(ses_free_mem[1]),
};

static unsigned ses_qp;
static struct lock		ses_mem_mtx;

/*--------------------------------------------------------------------*/

struct srcaddr {
	unsigned		magic;
#define SRCADDR_MAGIC		0x375111db

	unsigned		hash;
	VTAILQ_ENTRY(srcaddr)	list;
	struct srcaddrhead	*sah;

	char			addr[TCP_ADDRBUFSIZE];
	unsigned		nref;

	/* How long to keep entry around.  Inherits timescale from t_open */
	double			ttl;

	struct acct		acct;
};

static struct srcaddrhead {
	unsigned		magic;
#define SRCADDRHEAD_MAGIC	0x38231a8b
	VTAILQ_HEAD(,srcaddr)	head;
	struct lock		mtx;
} *srchash;

static unsigned			nsrchash;
static struct lock 		stat_mtx;

/*--------------------------------------------------------------------
 * Assign a srcaddr to this session.
 *
 * Each hash bucket is sorted in least recently used order and if we
 * need to make a new entry we recycle the first expired entry we find.
 * If we find more expired entries during our search, we delete them.
 */

void
SES_RefSrcAddr(struct sess *sp)
{
	unsigned u, v;
	struct srcaddr *c, *c2, *c3;
	struct srcaddrhead *ch;
	double now;

	if (params->srcaddr_ttl == 0) {
		sp->srcaddr = NULL;
		return;
	}
	AZ(sp->srcaddr);
	u = crc32_l(sp->addr, strlen(sp->addr));
	v = u % nsrchash;
	ch = &srchash[v];
	CHECK_OBJ(ch, SRCADDRHEAD_MAGIC);
	now = sp->t_open;
	if (sp->wrk->srcaddr == NULL) {
		sp->wrk->srcaddr = calloc(sizeof *sp->wrk->srcaddr, 1);
		XXXAN(sp->wrk->srcaddr);
	}

	Lck_Lock(&ch->mtx);
	c3 = NULL;
	VTAILQ_FOREACH_SAFE(c, &ch->head, list, c2) {
		if (c->hash == u && !strcmp(c->addr, sp->addr)) {
			if (c->nref == 0)
				VSL_stats->n_srcaddr_act++;
			c->nref++;
			c->ttl = now + params->srcaddr_ttl;
			sp->srcaddr = c;
			VTAILQ_REMOVE(&ch->head, c, list);
			VTAILQ_INSERT_TAIL(&ch->head, c, list);
			if (c3 != NULL) {
				VTAILQ_REMOVE(&ch->head, c3, list);
				VSL_stats->n_srcaddr--;
			}
			Lck_Unlock(&ch->mtx);
			if (c3 != NULL)
				free(c3);
			return;
		}
		if (c->nref > 0 || c->ttl > now)
			continue;
		if (c3 == NULL)
			c3 = c;
	}
	if (c3 == NULL) {
		c3 = sp->wrk->srcaddr;
		sp->wrk->srcaddr = NULL;
		VSL_stats->n_srcaddr++;
	} else
		VTAILQ_REMOVE(&ch->head, c3, list);
	AN(c3);
	memset(c3, 0, sizeof *c3);
	c3->magic = SRCADDR_MAGIC;
	strcpy(c3->addr, sp->addr);
	c3->hash = u;
	c3->acct.first = now;
	c3->ttl = now + params->srcaddr_ttl;
	c3->nref = 1;
	c3->sah = ch;
	VSL_stats->n_srcaddr_act++;
	VTAILQ_INSERT_TAIL(&ch->head, c3, list);
	sp->srcaddr = c3;
	Lck_Unlock(&ch->mtx);
}

/*--------------------------------------------------------------------*/

static void
ses_relsrcaddr(struct sess *sp)
{
	struct srcaddrhead *ch;

	if (sp->srcaddr == NULL)
		return;
	CHECK_OBJ(sp->srcaddr, SRCADDR_MAGIC);
	ch = sp->srcaddr->sah;
	CHECK_OBJ(ch, SRCADDRHEAD_MAGIC);
	Lck_Lock(&ch->mtx);
	assert(sp->srcaddr->nref > 0);
	sp->srcaddr->nref--;
	if (sp->srcaddr->nref == 0)
		VSL_stats->n_srcaddr_act--;
	sp->srcaddr = NULL;
	Lck_Unlock(&ch->mtx);
}

/*--------------------------------------------------------------------*/

static void
ses_sum_acct(struct acct *sum, const struct acct *inc)
{

#define ACCT(foo)	sum->foo += inc->foo;
#include "acct_fields.h"
#undef ACCT
}

void
SES_Charge(struct sess *sp)
{
	struct acct *a = &sp->wrk->acct;
	struct acct b;

	ses_sum_acct(&sp->acct, a);
	if (sp->srcaddr != NULL) {
		/* XXX: only report once per second ? */
		CHECK_OBJ(sp->srcaddr, SRCADDR_MAGIC);
		Lck_Lock(&sp->srcaddr->sah->mtx);
		ses_sum_acct(&sp->srcaddr->acct, a);
		b = sp->srcaddr->acct;
		Lck_Unlock(&sp->srcaddr->sah->mtx);
		WSL(sp->wrk, SLT_StatAddr, 0,
		    "%s 0 %.0f %ju %ju %ju %ju %ju %ju %ju",
		    sp->srcaddr->addr, sp->t_end - b.first,
		    b.sess, b.req, b.pipe, b.pass,
		    b.fetch, b.hdrbytes, b.bodybytes);
	}
	Lck_Lock(&stat_mtx);
#define ACCT(foo)	VSL_stats->s_##foo += a->foo;
#include "acct_fields.h"
#undef ACCT
	Lck_Unlock(&stat_mtx);
	memset(a, 0, sizeof *a);
}

/*--------------------------------------------------------------------*/

struct sess *
SES_New(const struct sockaddr *addr, unsigned len)
{
	struct sessmem *sm;
	struct sess *sp;
	volatile unsigned u;

	/*
	 * One of the two queues is unlocked because only one
	 * thread ever gets here to empty it.
	 */
	assert(ses_qp <= 1);
	sm = VTAILQ_FIRST(&ses_free_mem[ses_qp]);
	if (sm == NULL) {
		/*
		 * If that queue is empty, flip queues holding the lock
		 * and try the new unlocked queue.
		 */
		Lck_Lock(&ses_mem_mtx);
		ses_qp = 1 - ses_qp;
		Lck_Unlock(&ses_mem_mtx);
		sm = VTAILQ_FIRST(&ses_free_mem[ses_qp]);
	}
	if (sm != NULL) {
		VTAILQ_REMOVE(&ses_free_mem[ses_qp], sm, list);
	} else {
		/*
		 * If that fails, alloc new one.
		 *
		 * It is not necessary to lock mem_workspace, but we
		 * need to cache it locally, to make sure we get a
		 * consistent view of it.
		 */
		u = params->sess_workspace;
		sm = malloc(sizeof *sm + u);
		if (sm == NULL)
			return (NULL);
		sm->magic = SESSMEM_MAGIC;
		sm->workspace = u;
		VSL_stats->n_sess_mem++;
	}
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);
	VSL_stats->n_sess++;
	sp = &sm->sess;
	memset(sp, 0, sizeof *sp);
	sp->magic = SESS_MAGIC;
	sp->mem = sm;
	sp->sockaddr = (void*)(&sm->sockaddr[0]);
	sp->sockaddrlen = sizeof(sm->sockaddr[0]);
	sp->mysockaddr = (void*)(&sm->sockaddr[1]);
	sp->mysockaddrlen = sizeof(sm->sockaddr[1]);
	sp->sockaddr->sa_family = sp->mysockaddr->sa_family = PF_UNSPEC;
	sp->t_open = NAN;
	sp->t_req = NAN;
	sp->t_resp = NAN;
	sp->t_end = NAN;
	sp->grace = NAN;

	assert(len <= sp->sockaddrlen);
	if (addr != NULL) {
		memcpy(sp->sockaddr, addr, len);
		sp->sockaddrlen = len;
	}

	WS_Init(sp->ws, "sess", (void *)(sm + 1), sm->workspace);
	sp->http = &sm->http[0];
	sp->http0 = &sm->http[1];

	SES_ResetBackendTimeouts(sp);

	return (sp);
}

void
SES_Delete(struct sess *sp)
{
	struct acct *b = &sp->acct;
	struct sessmem *sm;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sm = sp->mem;
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);

	AZ(sp->obj);
	AZ(sp->vcl);
	VSL_stats->n_sess--;
	ses_relsrcaddr(sp);
	assert(!isnan(b->first));
	assert(!isnan(sp->t_end));
	VSL(SLT_StatSess, sp->id, "%s %s %.0f %ju %ju %ju %ju %ju %ju %ju",
	    sp->addr, sp->port, sp->t_end - b->first,
	    b->sess, b->req, b->pipe, b->pass,
	    b->fetch, b->hdrbytes, b->bodybytes);
	if (sm->workspace != params->sess_workspace) {
		VSL_stats->n_sess_mem--;
		free(sm);
	} else {
		Lck_Lock(&ses_mem_mtx);
		VTAILQ_INSERT_HEAD(&ses_free_mem[1 - ses_qp], sm, list);
		Lck_Unlock(&ses_mem_mtx);
	}
}

/*--------------------------------------------------------------------*/

void
SES_Init()
{
	int i;

	nsrchash = params->srcaddr_hash;
	srchash = calloc(sizeof *srchash, nsrchash);
	XXXAN(srchash);
	for (i = 0; i < nsrchash; i++) {
		srchash[i].magic = SRCADDRHEAD_MAGIC;
		VTAILQ_INIT(&srchash[i].head);
		Lck_New(&srchash[i].mtx);
	}
	Lck_New(&stat_mtx);
	Lck_New(&ses_mem_mtx);
}

void
SES_ResetBackendTimeouts(struct sess *sp)
{
	sp->connect_timeout = params->connect_timeout;
	sp->first_byte_timeout = params->first_byte_timeout;
	sp->between_bytes_timeout = params->between_bytes_timeout;
}

void
SES_InheritBackendTimeouts(struct sess *sp)
{
	struct backend *be;

	AN(sp);
	AN(sp->vbe);
	AN(sp->vbe->backend);

	be = sp->vbe->backend;
	/* 
	 * We only inherit the backend's timeout if the session timeout
	 * has not already been set in the VCL, as the order of precedence
	 * is parameter < backend definition < VCL.
	 */
	if (be->connect_timeout > 1e-3 && 
	    sp->connect_timeout == params->connect_timeout)
		sp->connect_timeout = be->connect_timeout;
	if (be->first_byte_timeout > 1e-3 && 
	    sp->first_byte_timeout == params->first_byte_timeout)
		sp->first_byte_timeout = be->first_byte_timeout;
	if (be->between_bytes_timeout > 1e-3 &&
	    sp->between_bytes_timeout == params->between_bytes_timeout)
		sp->between_bytes_timeout = be->between_bytes_timeout;
}
