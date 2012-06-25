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
SES_Charge(struct worker *wrk, struct req *req)
{
	struct sess *sp;
	struct acct *a;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	a = &wrk->acct_tmp;
	req->req_bodybytes += a->bodybytes;

#define ACCT(foo)				\
	wrk->stats.s_##foo += a->foo;		\
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
	sp->t_rx = NAN;
}

/*--------------------------------------------------------------------
 * Get a new session, preferably by recycling an already ready one
 */

static struct sess *
ses_new(struct sesspool *pp)
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
 * The pool-task function for sessions
 */

static void
ses_pool_task(struct worker *wrk, void *arg)
{
	struct req *req;
	struct sess *sp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	AZ(wrk->aws->r);
	wrk->lastused = NAN;
	THR_SetSession(sp);
	CNT_Session(wrk, req);
	THR_SetSession(NULL);
	WS_Assert(wrk->aws);
	AZ(wrk->wrw);
	if (cache_param->diag_bitmap & 0x00040000) {
		if (wrk->vcl != NULL)
			VCL_Rel(&wrk->vcl);
	}
}

/*--------------------------------------------------------------------
 * The pool-task for a newly accepted session
 */

void
SES_pool_accept_task(struct worker *wrk, void *arg)
{
	struct sesspool *pp;
	struct req *req;
	struct sess *sp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(pp, arg, SESSPOOL_MAGIC);

	/* Turn accepted socket into a session */
	AN(wrk->aws->r);
	sp = ses_new(pp);
	if (sp == NULL) {
		VCA_FailSess(wrk);
		return;
	} 
	VCA_SetupSess(wrk, sp);
	sp->sess_step = S_STP_NEWREQ;
	req = SES_GetReq(sp);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	ses_pool_task(wrk, req);
}

/*--------------------------------------------------------------------
 * Schedule a request back on a work-thread from its sessions pool
 */

int
SES_ScheduleReq(struct req *req)
{
	struct sess *sp;
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	sp->task.func = ses_pool_task;
	sp->task.priv = req;

	if (Pool_Task(pp->pool, &sp->task, POOL_QUEUE_FRONT)) {
		VSC_C_main->client_drop_late++;
		sp->t_idle = VTIM_real();
		AN (req->vcl);
		VCL_Rel(&req->vcl);
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
	struct req *req;
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);
	AZ(sp->req);
	req = SES_GetReq(sp);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp->task.func = ses_pool_task;
	sp->task.priv = req;
	sp->sess_step = S_STP_NEWREQ;
	sp->t_rx = now;
	if (Pool_Task(pp->pool, &sp->task, POOL_QUEUE_FRONT)) {
		VSC_C_main->client_drop_late++;
		sp->t_idle = VTIM_real();
		SES_Delete(sp, "dropped", sp->t_idle);
	}
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
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->req);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	if (reason != NULL)
		SES_Close(sp, reason);
	if (isnan(now))
		now = VTIM_real();
	assert(!isnan(sp->t_open));
	assert(sp->fd < 0);

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

struct req *
SES_GetReq(struct sess *sp)
{
	struct sesspool *pp;
	struct req *req;
	uint16_t nhttp;
	unsigned sz, hl;
	char *p, *e;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	AZ(sp->req);
	req = MPL_Get(pp->mpl_req, &sz);
	AN(req);
	req->magic = REQ_MAGIC;
	sp->req = req;
	req->sp = sp;
	THR_SetRequest(req);

	e = (char*)req + sz;
	p = (char*)(req + 1);
	p = (void*)PRNDUP(p);
	assert(p < e);

	nhttp = (uint16_t)cache_param->http_max_hdr;
	hl = HTTP_estimate(nhttp);

	req->http = HTTP_create(p, nhttp);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	req->http0 = HTTP_create(p, nhttp);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	req->resp = HTTP_create(p, nhttp);
	p += hl;
	p = (void*)PRNDUP(p);
	assert(p < e);

	sz = cache_param->workspace_thread;
	VSL_Setup(req->vsl, p, sz);
	req->vsl->wid = sp->vsl_id;
	p += sz;
	p = (void*)PRNDUP(p);

	assert(p < e);

	WS_Init(req->ws, "req", p, e - p);

	HTC_Init(req->htc, req->ws, sp->fd, req->vsl,
	    cache_param->http_req_size,
	    cache_param->http_req_hdr_len);

	return (req);
}

void
SES_ReleaseReq(struct req *req)
{
	struct sess *sp;
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(sp->req == req);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	MPL_AssertSane(req);
	VSL_Flush(req->vsl, 0);
	req->sp = NULL;
	MPL_Free(pp->mpl_req, req);
	sp->req = NULL;
	THR_SetRequest(NULL);
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
	bprintf(nb, "sess%u", pool_no);
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
