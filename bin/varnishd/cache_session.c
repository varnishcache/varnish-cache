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
 * Session and Client management.
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

#include "svnid.h"
SVNID("$Id$")

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
	unsigned		workspace;
	void			*wsp;
	struct http		*http[2];
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

static struct lock		stat_mtx;

/*--------------------------------------------------------------------*/

void
SES_Charge(struct sess *sp)
{
	struct acct *a = &sp->acct_req;

#define ACCT(foo)	\
	sp->wrk->stats.s_##foo += a->foo;	\
	sp->acct.foo += a->foo;		\
	a->foo = 0;
#include "acct_fields.h"
#undef ACCT
}

/*--------------------------------------------------------------------*/

static struct sess *
ses_setup(struct sessmem *sm, const struct sockaddr *addr, unsigned len)
{
	struct sess *sp;
	unsigned char *p;
	volatile unsigned nws;
	volatile unsigned nhttp;
	unsigned l, hl;

	if (sm == NULL) {
		if (VSL_stats->n_sess_mem >= params->max_sess)
			return (NULL);
		/*
		 * It is not necessary to lock these, but we need to
		 * cache them locally, to make sure we get a consistent
		 * view of the value.
		 */
		nws = params->sess_workspace;
		nhttp = params->http_headers;
		hl = HTTP_estimate(nhttp);
		l = sizeof *sm + nws + 2 * hl;
		p = malloc(l);
		if (p == NULL)
			return (NULL);

		/* Don't waste time zeroing the workspace */
		memset(p, 0, l - nws);

		sm = (void*)p;
		p += sizeof *sm;
		sm->magic = SESSMEM_MAGIC;
		sm->workspace = nws;
		VSL_stats->n_sess_mem++;
		sm->http[0] = HTTP_create(p, nhttp);
		p += hl;
		sm->http[1] = HTTP_create(p, nhttp);
		p += hl;
		sm->wsp = p;
	}
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);
	VSL_stats->n_sess++;
	sp = &sm->sess;
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
	sp->disable_esi = 0;

	assert(len <= sp->sockaddrlen);
	if (addr != NULL) {
		memcpy(sp->sockaddr, addr, len);
		sp->sockaddrlen = len;
	}

	WS_Init(sp->ws, "sess", sm->wsp, sm->workspace);
	sp->http = sm->http[0];
	sp->http0 = sm->http[1];

	SES_ResetBackendTimeouts(sp);

	return (sp);
}

/*--------------------------------------------------------------------
 * Try to recycle an existing session.
 */

struct sess *
SES_New(const struct sockaddr *addr, unsigned len)
{
	struct sessmem *sm;

	assert(pthread_self() == VCA_thread);
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
	if (sm != NULL)
		VTAILQ_REMOVE(&ses_free_mem[ses_qp], sm, list);
	return (ses_setup(sm, addr, len));
}

/*--------------------------------------------------------------------*/

struct sess *
SES_Alloc(const struct sockaddr *addr, unsigned len)
{
	return (ses_setup(NULL, addr, len));
}

/*--------------------------------------------------------------------*/

void
SES_Delete(struct sess *sp)
{
	struct acct *b = &sp->acct;
	struct sessmem *sm;
	// unsigned workspace;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sm = sp->mem;
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);

	AZ(sp->obj);
	AZ(sp->vcl);
	VSL_stats->n_sess--;
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
		/* Clean and prepare for reuse */
		// workspace = sm->workspace;
		// memset(sm, 0, sizeof *sm);
		// sm->magic = SESSMEM_MAGIC;
		// sm->workspace = workspace;

		/* XXX: cache_waiter_epoll.c evaluates data.ptr. If it's
		 * XXX: not nulled, things go wrong during load.
		 * XXX: Should probably find a better method to deal with
		 * XXX: this scenario...
		 */
#if defined(HAVE_EPOLL_CTL)
		sm->sess.ev.data.ptr = NULL;
#endif

		Lck_Lock(&ses_mem_mtx);
		VTAILQ_INSERT_HEAD(&ses_free_mem[1 - ses_qp], sm, list);
		Lck_Unlock(&ses_mem_mtx);
	}
}

/*--------------------------------------------------------------------*/

void
SES_Init()
{

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
