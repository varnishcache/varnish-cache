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
 * This is a little bit of a mixed bag, containing both memory management
 * and various state-change functions.
 *
 * The overall picture is complicated by the fact that requests can
 * disembark their worker-threads if they hit a busy object, then come
 * back later in a different worker thread to continue.
 * XXX: I wonder if that complexity pays of any more ?
 *
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "waiter/waiter.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

/*--------------------------------------------------------------------*/

struct sesspool {
	unsigned		magic;
#define SESSPOOL_MAGIC		0xd916e202
	struct pool		*pool;
	struct mempool		*mpl_req;
	struct mempool		*mpl_sess;
};

/*--------------------------------------------------------------------
 * Get a new session, preferably by recycling an already ready one
 *
 * Layout is:
 *	struct sess
 *	workspace
 */

static struct sess *
ses_new(struct sesspool *pp)
{
	struct sess *sp;
	unsigned sz;
	char *p, *e;

	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	sp = MPL_Get(pp->mpl_sess, &sz);
	sp->magic = SESS_MAGIC;
	sp->sesspool = pp;

	e = (char*)sp + sz;
	p = (char*)(sp + 1);
	p = (void*)PRNDUP(p);
	assert(p < e);
	WS_Init(sp->ws, "ses", p, e - p);
	sp->addrs = (void*)WS_Alloc(sp->ws, vsa_suckaddr_len * 2);

	sp->t_open = NAN;
	sp->t_idle = NAN;
	Lck_New(&sp->mtx, lck_sess);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp);
}

/*--------------------------------------------------------------------
 * Process new/existing request on this session.
 */

static void
ses_req_pool_task(struct worker *wrk, void *arg)
{
	struct req *req;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);

	THR_SetRequest(req);
	AZ(wrk->aws->r);
	wrk->lastused = NAN;
	HTTP1_Session(wrk, req);
	WS_Assert(wrk->aws);
	AZ(wrk->wrw);
	if (DO_DEBUG(DBG_VCLREL) && wrk->vcl != NULL)
		VCL_Rel(&wrk->vcl);
	THR_SetRequest(NULL);
}

/*--------------------------------------------------------------------
 * Allocate a request + vxid, call ses_req_pool_task()
 */

static void
ses_sess_pool_task(struct worker *wrk, void *arg)
{
	struct req *req;
	struct sess *sp;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(sp, arg, SESS_MAGIC);

	req = SES_GetReq(wrk, sp);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(req->vsl->wid & VSL_CLIENTMARKER);
	VSLb(req->vsl, SLT_Begin, "req %u rxreq", sp->vxid & VSL_IDENTMASK);
	VSL(SLT_Link, sp->vxid, "req %u rxreq", req->vsl->wid & VSL_IDENTMASK);

	sp->sess_step = S_STP_NEWREQ;
	ses_req_pool_task(wrk, req);
}

/*--------------------------------------------------------------------
 * VSL log the endpoints of the TCP connection.
 *
 * We use VSL() to get the sessions vxid and to make sure tha this
 * VSL comes before anything else for this session.
 *
 * This is a separate procedure only to isolate the two stack buffers.
 *
 */

static void
ses_vsl_socket(struct sess *sp, const char *lsockname)
{
	struct sockaddr_storage ss;
	socklen_t sl;
	char laddr[ADDR_BUFSIZE];
	char lport[PORT_BUFSIZE];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(lsockname);

	AN(sp->addrs);
	sl = sizeof ss;
	AZ(getsockname(sp->fd, (void*)&ss, &sl));
	AN(VSA_Build(sess_local_addr(sp), &ss, sl));
	assert(VSA_Sane(sess_local_addr(sp)));

	VTCP_name(sess_remote_addr(sp), laddr, sizeof laddr,
	    lport, sizeof lport);
	sp->client_addr_str = WS_Copy(sp->ws, laddr, -1);
	sp->client_port_str = WS_Copy(sp->ws, lport, -1);
	VTCP_name(sess_local_addr(sp), laddr, sizeof laddr,
	    lport, sizeof lport);
	VSL(SLT_Begin, sp->vxid, "sess 0 HTTP/1");
	VSL(SLT_SessOpen, sp->vxid, "%s %s %s %s %s %.6f %d",
	    sp->client_addr_str, sp->client_port_str, lsockname, laddr, lport,
	    sp->t_open, sp->fd);
}

/*--------------------------------------------------------------------
 * The pool-task for a newly accepted session
 *
 * Called from assigned worker thread
 */

void
SES_pool_accept_task(struct worker *wrk, void *arg)
{
	struct sesspool *pp;
	struct sess *sp;
	const char *lsockname;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(pp, arg, SESSPOOL_MAGIC);

	/* Turn accepted socket into a session */
	AN(wrk->aws->r);
	sp = ses_new(pp);
	if (sp == NULL) {
		VCA_FailSess(wrk);
		return;
	}
	wrk->stats.s_sess++;

	sp->t_open = VTIM_real();
	sp->t_idle = sp->t_open;
	sp->vxid = VXID_Get(&wrk->vxid_pool) | VSL_CLIENTMARKER;

	lsockname = VCA_SetupSess(wrk, sp);
	ses_vsl_socket(sp, lsockname);

	ses_sess_pool_task(wrk, sp);
}

/*--------------------------------------------------------------------
 * Schedule a request back on a work-thread from its sessions pool
 *
 * This is used to reschedule requests waiting on busy objects
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

	sp->task.func = ses_req_pool_task;
	sp->task.priv = req;

	if (Pool_Task(pp->pool, &sp->task, POOL_QUEUE_FRONT)) {
		AN (req->vcl);
		VCL_Rel(&req->vcl);
		SES_Delete(sp, SC_OVERLOAD, NAN);
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
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);
	sp->task.func = ses_sess_pool_task;
	sp->task.priv = sp;
	if (Pool_Task(pp->pool, &sp->task, POOL_QUEUE_FRONT))
		SES_Delete(sp, SC_OVERLOAD, now);
}

/*--------------------------------------------------------------------
 * Close a sessions connection.
 * XXX: Technically speaking we should catch a t_end timestamp here
 * XXX: for SES_Delete() to use.
 */

void
SES_Close(struct sess *sp, enum sess_close reason)
{
	int i;

	assert(sp->fd >= 0);
	sp->reason = reason;
	i = close(sp->fd);
	assert(i == 0 || errno != EBADF); /* XXX EINVAL seen */
	sp->fd = -1;
}

/*--------------------------------------------------------------------
 * Report and dismantle a session.
 */

void
SES_Delete(struct sess *sp, enum sess_close reason, double now)
{
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	if (reason != SC_NULL)
		SES_Close(sp, reason);
	assert(sp->fd < 0);

	if (isnan(now))
		now = VTIM_real();
	assert(!isnan(sp->t_open));

	VSL(SLT_SessClose, sp->vxid, "%s %.3f",
	    sess_close_2str(sp->reason, 0), now - sp->t_open);
	VSL(SLT_End, sp->vxid, "%s", "");

	Lck_Delete(&sp->mtx);
	MPL_Free(pp->mpl_sess, sp);
}

/*--------------------------------------------------------------------
 * Alloc/Free a request
 */

struct req *
SES_GetReq(struct worker *wrk, struct sess *sp)
{
	struct sesspool *pp;
	struct req *req;
	uint16_t nhttp;
	unsigned sz, hl;
	char *p, *e;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);

	req = MPL_Get(pp->mpl_req, &sz);
	AN(req);
	req->magic = REQ_MAGIC;
	req->sp = sp;

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

	sz = cache_param->vsl_buffer;
	VSL_Setup(req->vsl, p, sz);
	req->vsl->wid = VXID_Get(&wrk->vxid_pool) | VSL_CLIENTMARKER;
	p += sz;
	p = (void*)PRNDUP(p);

	assert(p < e);

	WS_Init(req->ws, "req", p, e - p);

	req->req_bodybytes = 0;
	req->resp_hdrbytes = 0;
	req->resp_bodybytes = 0;

	req->t_first = NAN;
	req->t_prev = NAN;
	req->t_req = NAN;

	VTAILQ_INIT(&req->body);

	return (req);
}

void
SES_ReleaseReq(struct req *req)
{
	struct sess *sp;
	struct sesspool *pp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	/* Make sure the request counters have all been zeroed */
#define ACCT(foo) \
	AZ(req->acct.foo);
#include "tbl/acct_fields_req.h"
#undef ACCT

	AZ(req->vcl);
	sp = req->sp;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	pp = sp->sesspool;
	CHECK_OBJ_NOTNULL(pp, SESSPOOL_MAGIC);
	AN(pp->pool);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	MPL_AssertSane(req);
	if (req->vsl->wid != 0)
		/* Non-released VXID - assume it was from a req */
		VSLb(req->vsl, SLT_End, "%s", "");
	VSL_Flush(req->vsl, 0);
	req->sp = NULL;
	MPL_Free(pp->mpl_req, req);
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
	pp->mpl_sess = MPL_New(nb, &cache_param->sess_pool,
	    &cache_param->workspace_session);
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
