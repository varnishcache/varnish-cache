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
#include "vsha256.h"
#include "vtim.h"

static unsigned ses_size = sizeof (struct sess);

/*--------------------------------------------------------------------*/

struct sesspool {
	unsigned		magic;
#define SESSPOOL_MAGIC		0xd916e202
	struct pool		*pool;
	struct mempool		*mpl_req;
	struct mempool		*mpl_sess;
};

/*--------------------------------------------------------------------
 * Charge statistics from worker to request and session.
 */

void
SES_Charge(struct sess *sp)
{
	struct acct *a = &sp->wrk->acct_tmp;

	CHECK_OBJ_NOTNULL(sp->req, REQ_MAGIC);
	sp->req->req_bodybytes += a->bodybytes;

#define ACCT(foo)				\
	sp->wrk->stats.s_##foo += a->foo;	\
	sp->acct_ses.foo += a->foo;		\
	a->foo = 0;
#include "tbl/acct_fields.h"
#undef ACCT
}

/*--------------------------------------------------------------------
 * This prepares a session for use, based on its sessmem structure.
 */

static void
ses_setup(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sp->sockaddrlen = sizeof(sp->sockaddr);
	sp->mysockaddrlen = sizeof(sp->mysockaddr);
	sp->sockaddr.ss_family = sp->mysockaddr.ss_family = PF_UNSPEC;
	sp->t_open = NAN;
	sp->t_idle = NAN;
	sp->t_req = NAN;
}

/*--------------------------------------------------------------------
 * Get a new session, preferably by recycling an already ready one
 */

struct sess *
SES_New(struct sesspool *pp)
{
	struct sess *sp;

	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	sp = MPL_Get(pp->mpl_sess, NULL);
	sp->magic = SESS_MAGIC;
	sp->sesspool = pp;
	ses_setup(sp);
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

	ALLOC_OBJ(sp, SESS_MAGIC);
	AN(sp);
	ses_setup(sp);
	/* XXX: sp->req ? */
	return (sp);
}

/*--------------------------------------------------------------------
 * Schedule a session back on a work-thread from its pool
 */

int
SES_Schedule(struct sess *sp)
{
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->wrk);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	if (Pool_Schedule(pp->pool, sp)) {
		VSC_C_main->client_drop_late++;
		sp->t_idle = VTIM_real();
		if (sp->req != NULL && sp->req->vcl != NULL) {
			/*
			 * A session parked on a busy object can come here
			 * after it wakes up.  Loose the VCL reference.
			 */
			VCL_Rel(&sp->req->vcl);
		}
		SES_Delete(sp, "dropped", sp->t_idle);
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Handle a session (from waiter)
 */

void
SES_Handle(struct sess *sp, double now)
{

	sp->step = STP_WAIT;
	sp->t_req = now;
	(void)SES_Schedule(sp);
}

/*--------------------------------------------------------------------
 * Close a sessions connection.
 * XXX: Technically speaking we should catch a t_end timestamp here
 * XXX: for SES_Delete() to use.
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
SES_Delete(struct sess *sp, const char *reason, double now)
{
	struct acct *b;
	struct worker *wrk;
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	wrk = sp->wrk;
	CHECK_OBJ_ORNULL(wrk, WORKER_MAGIC);

	if (reason != NULL)
		SES_Close(sp, reason);
	if (isnan(now))
		now = VTIM_real();
	assert(!isnan(sp->t_open));
	assert(sp->fd < 0);

	if (sp->req != NULL) {
		AZ(sp->req->vcl);
		SES_ReleaseReq(sp);
	}

	if (*sp->addr == '\0')
		strcpy(sp->addr, "-");
	if (*sp->port == '\0')
		strcpy(sp->addr, "-");

	b = &sp->acct_ses;

	VSL(SLT_StatSess, sp->vsl_id, "%s %s %.0f %ju %ju %ju %ju %ju %ju %ju",
	    sp->addr, sp->port,
	    now - sp->t_open,
	    b->sess, b->req, b->pipe, b->pass,
	    b->fetch, b->hdrbytes, b->bodybytes);

	MPL_Free(pp->mpl_sess, sp);

}

/*--------------------------------------------------------------------
 * Alloc/Free sp->req
 */

void
SES_GetReq(struct sess *sp)
{
	struct sesspool *pp;
	uint16_t nhttp;
	unsigned sz, hl;
	char *p;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	AZ(sp->req);
	sp->req = MPL_Get(pp->mpl_req, &sz);
	AN(sp->req);
	sp->req->magic = REQ_MAGIC;

	p = (char*)(sp->req + 1);
	sz -= sizeof *sp->req;

	nhttp = (uint16_t)cache_param->http_max_hdr;
	hl = HTTP_estimate(nhttp);

	xxxassert(sz > 3 * hl + sizeof(struct SHA256Context) + 128);

	sp->req->sha256ctx = (void*)p;
	p += sizeof(struct SHA256Context);

	sp->req->http = HTTP_create(p, nhttp);
	p += hl;		// XXX: align ?
	sz -= hl;

	sp->req->http0 = HTTP_create(p, nhttp);
	p += hl;		// XXX: align ?
	sz -= hl;

	sp->req->resp = HTTP_create(p, nhttp);
	p += hl;		// XXX: align ?
	sz -= hl;

	WS_Init(sp->req->ws, "req", p, sz);
}

void
SES_ReleaseReq(struct sess *sp)
{
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);
	CHECK_OBJ_NOTNULL(sp->req, REQ_MAGIC);
	MPL_AssertSane(sp->req);
	MPL_Free(pp->mpl_req, sp->req);
	sp->req = NULL;
}

/*--------------------------------------------------------------------
 * Create and delete pools
 */

struct sesspool *
SES_NewPool(struct pool *wp, unsigned pool_no)
{
	struct sesspool *pp;
	char nb[8];

	ALLOC_OBJ(pp, SESSPOOL_MAGIC);
	AN(pp);
	pp->pool = wp;
	bprintf(nb, "req%u", pool_no);
	pp->mpl_req = MPL_New(nb, &cache_param->req_pool,
	    &cache_param->workspace_client);
	pp->mpl_sess = MPL_New(nb, &cache_param->sess_pool, &ses_size);
	return (pp);
}

void
SES_DeletePool(struct sesspool *pp)
{

	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	MPL_Destroy(&pp->mpl_sess);
	MPL_Destroy(&pp->mpl_req);
	FREE_OBJ(pp);
}
