/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Session management
 *
 * This is a little bit of a mixed back, containing both memory management
 * and various state-change functions.
 *
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "waiter/waiter.h"

/*--------------------------------------------------------------------*/

struct sessmem {
	unsigned		magic;
#define SESSMEM_MAGIC		0x555859c5

	struct sesspool		*pool;

	unsigned		workspace;
	uint16_t		nhttp;
	void			*wsp;
	struct http		*http[2];
	VTAILQ_ENTRY(sessmem)	list;

	struct sess		sess;
};

struct sesspool {
	unsigned		magic;
#define SESSPOOL_MAGIC		0xd916e202
	struct pool		*pool;
	VTAILQ_HEAD(,sessmem)	freelist;
	struct lock		mtx;
	unsigned		nsess;
	unsigned		dly_free_cnt;
	unsigned		req_size;
	struct mempool		*mpl_req;
};

/*--------------------------------------------------------------------
 * Charge statistics from worker to request and session.
 */

void
SES_Charge(struct sess *sp)
{
	struct acct *a = &sp->wrk->acct_tmp;

	sp->req_bodybytes += a->bodybytes;

#define ACCT(foo)				\
	sp->wrk->stats.s_##foo += a->foo;	\
	sp->acct_ses.foo += a->foo;		\
	a->foo = 0;
#include "tbl/acct_fields.h"
#undef ACCT
}

/*--------------------------------------------------------------------
 * This function allocates a session + assorted peripheral data
 * structures in one single malloc operation.
 */

static struct sessmem *
ses_sm_alloc(void)
{
	struct sessmem *sm;
	unsigned char *p, *q;
	unsigned nws;
	uint16_t nhttp;
	unsigned l, hl;

	/*
	 * It is not necessary to lock these, but we need to
	 * cache them locally, to make sure we get a consistent
	 * view of the value.
	 */
	nws = cache_param->sess_workspace;
	nhttp = (uint16_t)cache_param->http_max_hdr;

	hl = HTTP_estimate(nhttp);
	l = sizeof *sm + nws + 2 * hl;
	VSC_C_main->sessmem_size = l;
	p = malloc(l);
	if (p == NULL)
		return (NULL);
	q = p + l;

	/* Don't waste time zeroing the workspace */
	memset(p, 0, l - nws);

	sm = (void*)p;
	p += sizeof *sm;

	sm->magic = SESSMEM_MAGIC;
	sm->workspace = nws;
	sm->nhttp = nhttp;

	sm->http[0] = HTTP_create(p, nhttp);
	p += hl;

	sm->http[1] = HTTP_create(p, nhttp);
	p += hl;

	sm->wsp = p;
	p += nws;

	assert(p == q);

	return (sm);
}

/*--------------------------------------------------------------------
 * This prepares a session for use, based on its sessmem structure.
 */

static void
ses_setup(struct sessmem *sm)
{
	struct sess *sp;

	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);
	sp = &sm->sess;
	memset(sp, 0, sizeof *sp);

	/* We assume that the sess has been zeroed by the time we get here */
	AZ(sp->magic);

	sp->magic = SESS_MAGIC;
	sp->mem = sm;
	sp->sockaddrlen = sizeof(sp->sockaddr);
	sp->mysockaddrlen = sizeof(sp->mysockaddr);
	sp->sockaddr.ss_family = sp->mysockaddr.ss_family = PF_UNSPEC;
	sp->t_open = NAN;
	sp->t_req = NAN;
	sp->t_resp = NAN;
	sp->t_end = NAN;
	EXP_Clr(&sp->exp);

	WS_Init(sp->ws, "sess", sm->wsp, sm->workspace);
	sp->http = sm->http[0];
	sp->http0 = sm->http[1];
}

/*--------------------------------------------------------------------
 * Get a new session, preferably by recycling an already ready one
 */

struct sess *
SES_New(struct worker *wrk, struct sesspool *pp)
{
	struct sessmem *sm;
	struct sess *sp;
	int do_alloc;

	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);

	do_alloc = 0;
	Lck_Lock(&pp->mtx);
	sm = VTAILQ_FIRST(&pp->freelist);
	if (sm != NULL) {
		VTAILQ_REMOVE(&pp->freelist, sm, list);
	} else if (pp->nsess < cache_param->max_sess) {
		pp->nsess++;
		do_alloc = 1;
	}
	wrk->stats.sessmem_free += pp->dly_free_cnt;
	pp->dly_free_cnt = 0;
	Lck_Unlock(&pp->mtx);
	if (do_alloc) {
		sm = ses_sm_alloc();
		if (sm != NULL) {
			wrk->stats.sessmem_alloc++;
			sm->pool = pp;
			ses_setup(sm);
		} else {
			wrk->stats.sessmem_fail++;
		}
	} else if (sm == NULL) {
		wrk->stats.sessmem_limit++;
	}
	if (sm == NULL)
		return (NULL);
	sp = &sm->sess;
	sp->req = MPL_Get(pp->mpl_req, NULL);
	AN(sp->req);
	sp->req->magic = REQ_MAGIC;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp);
}

/*--------------------------------------------------------------------
 * Allocate a session for use by background threads.
 */

struct sess *
SES_Alloc(void)
{
	struct sess *sp;
	struct sessmem *sm;

	sm = ses_sm_alloc();
	AN(sm);
	ses_setup(sm);
	sp = &sm->sess;
	sp->sockaddrlen = 0;
	/* XXX: sp->req ? */
	return (sp);
}

/*--------------------------------------------------------------------
 * Schedule a session back on a work-thread from its pool
 */

int
SES_Schedule(struct sess *sp)
{
	struct sessmem *sm;
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	AZ(sp->wrk);

	sm = sp->mem;
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);

	pp = sm->pool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);

	AN(pp->pool);

	if (Pool_Schedule(pp->pool, sp)) {
		SES_Delete(sp, "dropped");
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Handle a session (from waiter)
 *
 * Status: see HTC_Rx()
 */

void
SES_Handle(struct sess *sp, int status)
{

	switch (status) {
	case -2:
		SES_Delete(sp, "blast");
		break;
	case -1:
		SES_Delete(sp, "no request");
		break;
	case 1:
		sp->step = STP_START;
		(void)SES_Schedule(sp);
		break;
	default:
		WRONG("Unexpected return from HTC_Rx()");
	}
}

/*--------------------------------------------------------------------
 * Close a sessions connection.
 */

void
SES_Close(struct sess *sp, const char *reason)
{
	int i;

	assert(sp->fd >= 0);
	VSL(SLT_SessionClose, sp->vsl_id, "%s", reason);
	i = close(sp->fd);
	assert(i == 0 || errno != EBADF); /* XXX EINVAL seen */
	sp->fd = -1;
}

/*--------------------------------------------------------------------
 * (Close &) Free or Recycle a session.
 *
 * If the workspace has changed, deleted it, otherwise wash it, and put
 * it up for adoption.
 *
 * XXX: We should also check nhttp
 */

void
SES_Delete(struct sess *sp, const char *reason)
{
	struct acct *b;
	struct sessmem *sm;
	struct worker *wrk;
	struct sesspool *pp;


	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->req, REQ_MAGIC);
	MPL_AssertSane(sp->req);
	sm = sp->mem;
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);
	pp = sm->pool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	wrk = sp->wrk;
	CHECK_OBJ_ORNULL(wrk, WORKER_MAGIC);


	if (reason != NULL)
		SES_Close(sp, reason);
	assert(sp->fd < 0);

	AZ(sp->vcl);
	if (*sp->addr == '\0')
		strcpy(sp->addr, "-");
	if (*sp->port == '\0')
		strcpy(sp->addr, "-");

	b = &sp->acct_ses;
	assert(!isnan(b->first));
	assert(!isnan(sp->t_end));

	VSL(SLT_StatSess, sp->vsl_id, "%s %s %.0f %ju %ju %ju %ju %ju %ju %ju",
	    sp->addr, sp->port, sp->t_end - b->first,
	    b->sess, b->req, b->pipe, b->pass,
	    b->fetch, b->hdrbytes, b->bodybytes);

	CHECK_OBJ_NOTNULL(sp->req, REQ_MAGIC);
	MPL_AssertSane(sp->req);
	MPL_Free(pp->mpl_req, sp->req);
	sp->req = NULL;

	if (sm->workspace != cache_param->sess_workspace ||
	    sm->nhttp != (uint16_t)cache_param->http_max_hdr ||
	    pp->nsess > cache_param->max_sess) {
		free(sm);
		Lck_Lock(&pp->mtx);
		if (wrk != NULL)
			wrk->stats.sessmem_free++;
		else
			pp->dly_free_cnt++;
		pp->nsess--;
		Lck_Unlock(&pp->mtx);
	} else {
		/* Clean and prepare for reuse */
		ses_setup(sm);
		Lck_Lock(&pp->mtx);
		if (wrk != NULL) {
			wrk->stats.sessmem_free += pp->dly_free_cnt;
			pp->dly_free_cnt = 0;
		}
		VTAILQ_INSERT_HEAD(&pp->freelist, sm, list);
		Lck_Unlock(&pp->mtx);
	}
}

/*--------------------------------------------------------------------
 * Create and delete pools
 */

struct sesspool *
SES_NewPool(struct pool *pp, unsigned pool_no)
{
	struct sesspool *sp;
	char nb[8];

	ALLOC_OBJ(sp, SESSPOOL_MAGIC);
	AN(sp);
	sp->pool = pp;
	VTAILQ_INIT(&sp->freelist);
	Lck_New(&sp->mtx, lck_sessmem);
	bprintf(nb, "req%u", pool_no);
	sp->req_size = sizeof (struct req);
	sp->mpl_req = MPL_New(nb, &cache_param->req_pool, &sp->req_size);
	return (sp);
}

void
SES_DeletePool(struct sesspool *sp, struct worker *wrk)
{
	struct sessmem *sm;

	CHECK_OBJ_NOTNULL(sp, SESSPOOL_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	Lck_Lock(&sp->mtx);
	while (!VTAILQ_EMPTY(&sp->freelist)) {
		sm = VTAILQ_FIRST(&sp->freelist);
		CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);
		VTAILQ_REMOVE(&sp->freelist, sm, list);
		FREE_OBJ(sm);
		wrk->stats.sessmem_free++;
		sp->nsess--;
	}
	AZ(sp->nsess);
	Lck_Unlock(&sp->mtx);
	Lck_Delete(&sp->mtx);
	MPL_Destroy(&sp->mpl_req);
	FREE_OBJ(sp);
}
