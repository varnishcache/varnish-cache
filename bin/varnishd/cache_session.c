/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "cache.h"
#include "cache_backend.h"
#include "cache_waiter.h"

/*--------------------------------------------------------------------*/

struct sessmem {
	unsigned		magic;
#define SESSMEM_MAGIC		0x555859c5

	struct sesspool		*pool;
	struct sess		sess;
	unsigned		workspace;
	void			*wsp;
	struct http		*http[2];
	VTAILQ_ENTRY(sessmem)	list;
	struct sockaddr_storage	sockaddr[2];
};

struct sesspool {
	unsigned		magic;
#define SESSPOOL_MAGIC		0xd916e202
	VTAILQ_HEAD(,sessmem)	freelist;
	struct lock		mtx;
	unsigned		nsess;
	unsigned		maxsess;
};

static struct sesspool *sesspool;

/*--------------------------------------------------------------------*/

static struct lock		stat_mtx;

/*--------------------------------------------------------------------*/

void
SES_Charge(struct sess *sp)
{
	struct acct *a = &sp->wrk->acct_tmp;

#define ACCT(foo)				\
	sp->wrk->stats.s_##foo += a->foo;	\
	sp->acct_req.foo += a->foo;		\
	sp->acct_ses.foo += a->foo;		\
	a->foo = 0;
#include "acct_fields.h"
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
	nws = params->sess_workspace;
	nhttp = (uint16_t)params->http_max_hdr;

	hl = HTTP_estimate(nhttp);
	l = sizeof *sm + nws + 2 * hl;
	p = malloc(l);
	if (p == NULL)
		return (NULL);
	q = p + l;

	/* XXX Stats */
	Lck_Lock(&stat_mtx);
	VSC_C_main->n_sess_mem++;
	Lck_Unlock(&stat_mtx);

	/* Don't waste time zeroing the workspace */
	memset(p, 0, l - nws);

	sm = (void*)p;
	p += sizeof *sm;
	sm->magic = SESSMEM_MAGIC;
	sm->workspace = nws;
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
	sp->sockaddr = (void*)(&sm->sockaddr[0]);
	sp->sockaddrlen = sizeof(sm->sockaddr[0]);
	sp->mysockaddr = (void*)(&sm->sockaddr[1]);
	sp->mysockaddrlen = sizeof(sm->sockaddr[1]);
	sp->sockaddr->ss_family = sp->mysockaddr->ss_family = PF_UNSPEC;
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
SES_New(struct sesspool *pp)
{
	struct sessmem *sm;
	struct sess *sp;
	int do_alloc = 0;

	if (pp == NULL)
		pp = sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);

	Lck_Lock(&pp->mtx);
	sm = VTAILQ_FIRST(&pp->freelist);
	if (sm != NULL) {
		VTAILQ_REMOVE(&pp->freelist, sm, list);
	} else if (pp->nsess < pp->maxsess) {
		pp->nsess++;
		do_alloc = 1;
	}
	Lck_Unlock(&pp->mtx);
	if (do_alloc) {
		sm = ses_sm_alloc();
		if (sm != NULL) {
			sm->pool = pp;
			ses_setup(sm);
		}
	}
	if (sm == NULL)
		return (NULL);
	sp = &sm->sess;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	/* XXX Stats */
	VSC_C_main->n_sess++;		/* XXX: locking  ? */
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
	return (sp);
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
		if (Pool_QueueSession(sp))
			VSC_C_main->client_drop_late++;
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
	VSL(SLT_SessionClose, sp->id, "%s", reason);
	i = close(sp->fd);
	assert(i == 0 || errno != EBADF); /* XXX EINVAL seen */
	sp->fd = -1;
}

/*--------------------------------------------------------------------
 * (Close &) Recycle a session.  If the workspace has changed, deleted it,
 * otherwise wash it, and put it up for adoption.
 */

void
SES_Delete(struct sess *sp, const char *reason)
{
	struct acct *b = &sp->acct_ses;
	struct sessmem *sm;
	static char noaddr[] = "-";
	struct sesspool *pp;


	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sm = sp->mem;
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);
	pp = sm->pool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);

	if (reason != NULL)
		SES_Close(sp, reason);
	assert(sp->fd < 0);

	AZ(sp->obj);
	AZ(sp->vcl);
	VSC_C_main->n_sess--;			/* XXX: locking ? */
	assert(!isnan(b->first));
	assert(!isnan(sp->t_end));
	if (sp->addr == NULL)
		sp->addr = noaddr;
	if (sp->port == NULL)
		sp->port = noaddr;
	VSL(SLT_StatSess, sp->id, "%s %s %.0f %ju %ju %ju %ju %ju %ju %ju",
	    sp->addr, sp->port, sp->t_end - b->first,
	    b->sess, b->req, b->pipe, b->pass,
	    b->fetch, b->hdrbytes, b->bodybytes);

	if (sm->workspace != params->sess_workspace) {
		Lck_Lock(&stat_mtx);
		VSC_C_main->n_sess_mem--;
		Lck_Unlock(&stat_mtx);
		free(sm);
		Lck_Lock(&pp->mtx);
		sesspool->nsess--;
		Lck_Unlock(&pp->mtx);
	} else {
		/* Clean and prepare for reuse */
		ses_setup(sm);
		Lck_Lock(&pp->mtx);
		VTAILQ_INSERT_HEAD(&sesspool->freelist, sm, list);
		Lck_Unlock(&pp->mtx);
	}
}

/*--------------------------------------------------------------------*/

void
SES_Init()
{

	ALLOC_OBJ(sesspool, SESSPOOL_MAGIC);
	VTAILQ_INIT(&sesspool->freelist);
	Lck_New(&sesspool->mtx, lck_sessmem);
	sesspool->maxsess = params->max_sess;

	Lck_New(&stat_mtx, lck_stat);
}
