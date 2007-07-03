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
 * This file contains the central state machine for pushing requests.
 *
 * We cannot just use direct calls because it is possible to kick a
 * request back to the lookup stage (usually after a rewrite).  The
 * state engine also allows us to break the processing up into some
 * logical chunks which improves readability a little bit.
 *
 * Since the states are rather nasty in detail, I have decided to embedd
 * a dot(1) graph in the source code comments.  So to see the big picture,
 * extract the DOT lines and run though dot(1), for instance with the
 * command:
 *	sed -n '/^DOT/s///p' cache_center.c | dot -Tps > /tmp/_.ps
 */

/*
DOT digraph vcl_center {
xDOT	page="8.2,11.5"
DOT	size="7.2,10.5"
DOT	margin="0.5"
DOT	center="1"
DOT start [
DOT	shape=hexagon
DOT	label="Request received"
DOT ]
DOT ERROR [shape=plaintext]
DOT start -> recv [style=bold,color=green,weight=4]
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_CLOCK_GETTIME
#include "compat/clock_gettime.h"
#endif

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

#include "shmlog.h"
#include "vcl.h"
#include "cache.h"

static unsigned xids;

/*--------------------------------------------------------------------
 * AGAIN
 * We come here when we just completed a request and already have
 * received (part of) the next one.  Instead taking the detour
 * around the acceptor and then back to a worker, just stay in this
 * worker and do what it takes.
 */

static int
cnt_again(struct sess *sp)
{
	int i;

	assert(sp->xid == 0);

	do
		i = http_RecvSome(sp->fd, sp->http);
	while (i == -1);
	if (i == 0) {
		sp->step = STP_RECV;
		return (0);
	}
	if (i == 1)
		vca_close_session(sp, "overflow");
	else if (i == 2)
		vca_close_session(sp, "no request");
	else
		INCOMPL();
	sp->step = STP_DONE;
	return (0);
}


/*--------------------------------------------------------------------
 * We have a refcounted object on the session, now deliver it.
 *
DOT subgraph xcluster_deliver {
DOT 	deliver [
DOT		shape=ellipse
DOT		label="Filter obj.->resp."
DOT	]
DOT	vcl_deliver [
DOT		shape=record
DOT		label="vcl_deliver()|req.\nresp."
DOT	]
DOT	deliver2 [
DOT		shape=ellipse
DOT		label="Send hdr + object"
DOT	]
DOT	deliver -> vcl_deliver [style=bold,color=green,weight=4]
DOT	vcl_deliver -> deliver2 [style=bold,color=green,weight=4,label=deliver]
DOT     vcl_deliver -> errdeliver [label="error"]
DOT     errdeliver [label="ERROR",shape=plaintext]
DOT }
DOT deliver2 -> DONE [style=bold,color=green,weight=4]
 */

static int
cnt_deliver(struct sess *sp)
{

	RES_WriteObj(sp);
	HSH_Deref(sp->obj);
	sp->obj = NULL;
	sp->step = STP_DONE;
	return (0);
}


/*--------------------------------------------------------------------
 * This is the final state, figure out if we should close or recycle
 * the client connection
 *
DOT	DONE [
DOT		shape=hexagon
DOT		label="Request completed"
DOT	]
 */

static double
cnt_dt(struct timespec *t1, struct timespec *t2)
{
	double dt;

	dt = (t2->tv_sec - t1->tv_sec);
	dt += (t2->tv_nsec - t1->tv_nsec) * 1e-9;
	return (dt);
}

static int
cnt_done(struct sess *sp)
{
	double dh, dp, da;

	AZ(sp->obj);
	sp->backend = NULL;
	if (sp->vcl != NULL) {
		if (sp->wrk->vcl != NULL)
			VCL_Rel(&sp->wrk->vcl);
		sp->wrk->vcl = sp->vcl;
		sp->vcl = NULL;
	}

	clock_gettime(CLOCK_REALTIME, &sp->t_end);
	sp->wrk->idle = sp->t_end.tv_sec;
	if (sp->xid == 0) {
		sp->t_req = sp->t_end;
		sp->t_resp = sp->t_end;
	}
	dp = cnt_dt(&sp->t_req, &sp->t_resp);
	da = cnt_dt(&sp->t_resp, &sp->t_end);
	dh = cnt_dt(&sp->t_open, &sp->t_req);
	WSL(sp->wrk, SLT_ReqEnd, sp->id, "%u %ld.%09ld %ld.%09ld %.9f %.9f %.9f",
	    sp->xid,
	    (long)sp->t_req.tv_sec, (long)sp->t_req.tv_nsec,
	    (long)sp->t_end.tv_sec, (long)sp->t_end.tv_nsec,
	    dh, dp, da);

	sp->xid = 0;
	sp->t_open = sp->t_end;
	SES_Charge(sp);
	WSL_Flush(sp->wrk);
	if (sp->fd >= 0 && sp->doclose != NULL)
		vca_close_session(sp, sp->doclose);
	if (sp->fd < 0) {
		VSL_stats->sess_closed++;
		sp->wrk = NULL;
		vca_return_session(sp);
		return (1);
	}

	if (http_RecvPrepAgain(sp->http)) {
		VSL_stats->sess_pipeline++;
		sp->step = STP_RECV;
		return (0);
	}
	if (sp->http->pl_s < sp->http->pl_e) {
		VSL_stats->sess_readahead++;
		sp->step = STP_AGAIN;
		return (0);
	}
	VSL_stats->sess_herd++;
	sp->wrk = NULL;
	vca_return_session(sp);
	return (1);
}


/*--------------------------------------------------------------------
 * Emit an error
 *
DOT subgraph xcluster_error {
DOT	error [
DOT		shape=ellipse
DOT		label="Issue HTTP error"
DOT	]
DOT	ERROR -> error
DOT }
DOT error -> DONE
 */

static int
cnt_error(struct sess *sp)
{

	RES_Error(sp, sp->err_code, sp->err_reason);
	sp->err_code = 0;
	sp->err_reason = NULL;
	sp->step = STP_DONE;
	return (0);
}


/*--------------------------------------------------------------------
 * We have fetched the headers from the backend, ask the VCL code what
 * to do next, then head off in that direction.
 *
DOT subgraph xcluster_fetch {
DOT	fetch [
DOT		shape=ellipse
DOT		label="fetch from backend\n(find obj.ttl)"
DOT	]
DOT	vcl_fetch [
DOT		shape=record
DOT		label="vcl_fetch()|req.\nobj."
DOT	]
DOT	fetch -> vcl_fetch [style=bold,color=blue,weight=2]
DOT	fetch_pass [
DOT		shape=ellipse
DOT		label="obj.pass=true"
DOT	]
DOT	vcl_fetch -> fetch_pass [label="pass"]
DOT }
DOT fetch_pass -> deliver
DOT vcl_fetch -> deliver [label="insert",style=bold,color=blue,weight=2]
DOT vcl_fetch -> errfetch [label="error"]
DOT errfetch [label="ERROR",shape=plaintext]
 */

static int
cnt_fetch(struct sess *sp)
{
	struct bereq *bereq;
	struct http *hp;
	char *b;
	int i;

	bereq = vbe_new_bereq();
	AN(bereq);
	hp = bereq->http;
	hp->logtag = HTTP_Tx;

	http_GetReq(sp->wrk, sp->fd, hp, sp->http);
	http_FilterHeader(sp->wrk, sp->fd, hp, sp->http, HTTPH_R_FETCH);
	http_PrintfHeader(sp->wrk, sp->fd, hp, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->wrk, sp->fd, hp,
	    "X-Forwarded-for: %s", sp->addr);
	if (!http_GetHdr(hp, H_Host, &b)) {
		http_PrintfHeader(sp->wrk, sp->fd, hp, "Host: %s",
		    sp->backend->hostname);
	}
	sp->bereq = bereq;

	i = Fetch(sp);
	vbe_free_bereq(sp->bereq);
	sp->bereq = NULL;

	if (i) {
		SYN_ErrorPage(sp, 503, "Error talking to backend", 30);
	} else {
		RFC2616_cache_policy(sp, &sp->obj->http);	/* XXX -> VCL */

		VCL_fetch_method(sp);

		if (sp->handling == VCL_RET_ERROR)
			INCOMPL();

		if (sp->handling == VCL_RET_PASS)
			sp->obj->pass = 1;
	}

	sp->obj->cacheable = 1;
	if (sp->obj->objhead != NULL) {
		VRY_Create(sp);
		HSH_Ref(sp->obj); /* get another, STP_DELIVER will deref */
		HSH_Unbusy(sp->obj);
	}
	sp->wrk->acct.fetch++;
	sp->step = STP_DELIVER;
	return (0);
}

/*--------------------------------------------------------------------
 * The very first request
 */
static int
cnt_first(struct sess *sp)
{
	int i;

	/*
	 * XXX: If we don't have acceptfilters we are somewhat subject
	 * XXX: to DoS'ing here.  One remedy would be to set a shorter
	 * XXX: SO_RCVTIMEO and once we have received something here
	 * XXX: increase it to the normal value.
	 */

	assert(sp->xid == 0);
	VCA_Prep(sp);
	sp->wrk->idle = sp->t_open.tv_sec;
	sp->wrk->acct.sess++;
	SES_RefSrcAddr(sp);
	do
		i = http_RecvSome(sp->fd, sp->http);
	while (i == -1);
	if (i == 0) {
		sp->step = STP_RECV;
		return (0);
	}
	if (i == 1)
		vca_close_session(sp, "blast");
	else if (i == 2)
		vca_close_session(sp, "silent");
	else
		INCOMPL();
	sp->step = STP_DONE;
	return (0);
}

/*--------------------------------------------------------------------
 * HIT
 * We had a cache hit.  Ask VCL, then march off as instructed.
 *
DOT subgraph xcluster_hit {
DOT	hit [
DOT		shape=record
DOT		label="vcl_hit()|req.\nobj."
DOT	]
DOT }
DOT hit -> err_hit [label="error"]
DOT err_hit [label="ERROR",shape=plaintext]
DOT hit -> pass [label=pass]
DOT hit -> deliver [label="deliver",style=bold,color=green,weight=4]
 */

static int
cnt_hit(struct sess *sp)
{

	assert(!sp->obj->pass);

	VCL_hit_method(sp);

	if (sp->handling == VCL_RET_DELIVER) {
		sp->step = STP_DELIVER;
		return (0);
	}

	/* Drop our object, we won't need it */
	HSH_Deref(sp->obj);
	sp->obj = NULL;

	if (sp->handling == VCL_RET_PASS) {
		sp->step = STP_PASS;
		return (0);
	}

	if (sp->handling == VCL_RET_ERROR) {
		sp->step = STP_ERROR;
		return (0);
	}


	INCOMPL();
}


/*--------------------------------------------------------------------
 * LOOKUP
 * Hash things together and look object up in hash-table.
 *
 * LOOKUP consists of two substates so that we can reenter if we
 * encounter a busy object.
 *
DOT subgraph xcluster_lookup {
DOT	hash [
DOT		shape=record
DOT		label="vcl_hash()|req."
DOT	]
DOT	lookup [
DOT		shape=diamond
DOT		label="obj in cache ?\ncreate if not"
DOT	]
DOT	lookup2 [
DOT		shape=diamond
DOT		label="obj.pass ?"
DOT	]
DOT	hash -> lookup [label="hash",style=bold,color=green,weight=4]
DOT	lookup -> lookup2 [label="yes",style=bold,color=green,weight=4]
DOT }
DOT lookup2 -> hit [label="no", style=bold,color=green,weight=4]
DOT lookup2 -> pass [label="yes"]
DOT lookup -> miss [label="no",style=bold,color=blue,weight=2]
 */

static int
cnt_lookup(struct sess *sp)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	if (sp->obj == NULL) {
		WS_Reserve(sp->http->ws, 0);
		sp->hash_b = sp->http->ws->f;
		sp->hash_e = sp->hash_b;
		VCL_hash_method(sp);		/* XXX: no-op for now */
		WS_ReleaseP(sp->http->ws, sp->hash_e);
		/* XXX check error */
	}

	o = HSH_Lookup(sp);

	if (o == NULL) {
		/*
		 * We hit a busy object, disembark worker thread and expect
		 * hash code to restart us, still in STP_LOOKUP, later.
		 */
		WSL(sp->wrk, SLT_Debug, sp->fd,
		    "on waiting list on obj %u", sp->obj->xid);
		SES_Charge(sp);
		return (1);
	}

	WS_Return(sp->http->ws, sp->hash_b, sp->hash_e);
	sp->hash_b = sp->hash_e = NULL;

	sp->obj = o;

	/* If we inserted a new object it's a miss */
	if (sp->obj->busy) {
		VSL_stats->cache_miss++;
		sp->step = STP_MISS;
		return (0);
	}

	if (sp->obj->pass) {
		VSL_stats->cache_hitpass++;
		WSL(sp->wrk, SLT_HitPass, sp->fd, "%u", sp->obj->xid);
		HSH_Deref(sp->obj);
		sp->obj = NULL;
		sp->step = STP_PASS;
		return (0);
	} 

	VSL_stats->cache_hit++;
	WSL(sp->wrk, SLT_Hit, sp->fd, "%u", sp->obj->xid);
	sp->step = STP_HIT;
	return (0);
}


/*--------------------------------------------------------------------
 * We had a miss, ask VCL, proceed as instructed
 *
DOT subgraph xcluster_miss {
DOT	miss [
DOT		shape=ellipse
DOT		label="filter req.->bereq."
DOT	]
DOT	vcl_miss [
DOT		shape=record
DOT		label="vcl_miss()|req.\nbereq."
DOT	]
DOT	miss_ins [
DOT		label="obj.pass=true"
DOT	]
DOT	miss -> vcl_miss [style=bold,color=blue,weight=2]
DOT }
DOT vcl_miss -> err_miss [label="error"]
DOT err_miss [label="ERROR",shape=plaintext]
DOT vcl_miss -> fetch [label="fetch",style=bold,color=blue,weight=2]
DOT miss_ins -> pass
DOT vcl_miss -> miss_ins [label="pass"]
DOT
 */

static int
cnt_miss(struct sess *sp)
{

	VCL_miss_method(sp);
	if (sp->handling == VCL_RET_ERROR) {
		sp->obj->cacheable = 0;
		HSH_Unbusy(sp->obj);
		HSH_Deref(sp->obj);
		sp->obj = NULL;
		sp->step = STP_ERROR;
		return (0);
	}
	if (sp->handling == VCL_RET_PASS) {
		sp->obj->cacheable = 0;
		HSH_Unbusy(sp->obj);
		HSH_Deref(sp->obj);
		sp->obj = NULL;
		sp->step = STP_PASS;
		return (0);
	}
	if (sp->handling == VCL_RET_FETCH) {
		sp->step = STP_FETCH;
		return (0);
	}
	INCOMPL();
}


/*--------------------------------------------------------------------
 * Start pass processing by getting headers from backend, then
 * continue in passbody.
 *
DOT subgraph xcluster_pass {
DOT	pass [
DOT		shape=ellipse
DOT		label="deref obj\nfilter req.->bereq."
DOT	]
DOT	vcl_pass [
DOT		shape=record
DOT		label="vcl_pass()|req.\nbereq."
DOT	]
DOT	pass_do [
DOT		shape=ellipse
DOT		label="create anon object\n"
DOT	]
DOT	pass -> vcl_pass
DOT	vcl_pass -> pass_do [label="pass"]
DOT }
DOT pass_do -> fetch
DOT vcl_pass -> err_pass [label="error"]
DOT err_pass [label="ERROR",shape=plaintext]
 */

static int
cnt_pass(struct sess *sp)
{

	AZ(sp->obj);

	VCL_pass_method(sp);
	if (sp->handling == VCL_RET_ERROR) {
		sp->step = STP_ERROR;
		return (0);
	}
	HSH_Prealloc(sp);
	sp->obj = sp->wrk->nobj;
	sp->wrk->nobj = NULL;
	sp->obj->busy = 1;
	sp->step = STP_FETCH;
	return (0);
}

/*--------------------------------------------------------------------
 * Ship the request header to the backend unchanged, then pipe
 * until one of the ends close the connection.
 *
DOT subgraph xcluster_pipe {
DOT	pipe [
DOT		shape=ellipse
DOT		label="Filter req.->bereq."
DOT	]
DOT	vcl_pipe [
DOT		shape=record
DOT		label="vcl_pipe()|req.\nbereq\."
DOT	]
DOT	pipe_do [
DOT		shape=ellipse
DOT		label="send bereq.\npipe until close"
DOT	]
DOT	vcl_pipe -> pipe_do [label="pipe"]
DOT	pipe -> vcl_pipe 
DOT }
DOT pipe_do -> DONE
DOT vcl_pipe -> err_pipe [label="error"]
DOT err_pipe [label="ERROR",shape=plaintext]
 */

static int
cnt_pipe(struct sess *sp)
{
	struct bereq *bereq;
	struct http *hp;
	char *b;

	sp->wrk->acct.pipe++;

	bereq = vbe_new_bereq();
	XXXAN(bereq);
	hp = bereq->http;
	hp->logtag = HTTP_Tx;

	http_CopyReq(sp->wrk, sp->fd, hp, sp->http);
	http_FilterHeader(sp->wrk, sp->fd, hp, sp->http, HTTPH_R_PIPE);
	http_PrintfHeader(sp->wrk, sp->fd, hp, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->wrk, sp->fd, hp, "X-Forwarded-for: %s", sp->addr);

	/* XXX: does this belong in VCL ? */
	if (!http_GetHdr(hp, H_Host, &b)) {
		http_PrintfHeader(sp->wrk, sp->fd, hp, "Host: %s",
		    sp->backend->hostname);
	}

	VCL_pipe_method(sp);

	if (sp->handling == VCL_RET_ERROR)
		INCOMPL();

	PipeSession(sp, bereq);
	sp->step = STP_DONE;
	return (0);
}


/*--------------------------------------------------------------------
 * RECV
 * We have a complete request, get a VCL reference and dispatch it
 * as instructed by vcl_recv{}
 *
DOT subgraph xcluster_recv {
DOT	recv [
DOT		shape=record
DOT		label="vcl_recv()|req."
DOT	]
DOT }
DOT recv -> pipe [label="pipe"]
DOT recv -> pass [label="pass"]
DOT recv -> err_recv [label="error"]
DOT err_recv [label="ERROR",shape=plaintext]
DOT recv -> hash [label="lookup",style=bold,color=green,weight=4]
 */

static int
cnt_recv(struct sess *sp)
{
	int done;

	AZ(sp->vcl);
	AZ(sp->obj);

	/* Update stats of various sorts */
	VSL_stats->client_req++;			/* XXX not locked */
	clock_gettime(CLOCK_REALTIME, &sp->t_req);
	sp->wrk->idle = sp->t_req.tv_sec;
	sp->wrk->acct.req++;

	/* Assign XID and log */
	sp->xid = ++xids;				/* XXX not locked */
	WSL(sp->wrk, SLT_ReqStart, sp->fd,
	    "%s %s %u", sp->addr, sp->port,  sp->xid);

	/* Borrow VCL reference from worker thread */
	VCL_Refresh(&sp->wrk->vcl);
	sp->vcl = sp->wrk->vcl;
	sp->wrk->vcl = NULL;

	done = http_DissectRequest(sp->wrk, sp->http, sp->fd);
	if (done != 0) {
		RES_Error(sp, done, NULL);		/* XXX: STP_ERROR ? */
		sp->step = STP_DONE;
		return (0);
	}

	http_DoConnection(sp);

	/* By default we use the first backend */
	sp->backend = sp->vcl->backend[0];

	/* XXX: Handle TRACE & OPTIONS of Max-Forwards = 0 */

	VCL_recv_method(sp);

	sp->wantbody = (!strcmp(sp->http->hd[HTTP_HDR_REQ].b, "GET") ||
	    !strcmp(sp->http->hd[HTTP_HDR_REQ].b, "POST"));
	switch(sp->handling) {
	case VCL_RET_LOOKUP:
		/* XXX: discard req body, if any */
		sp->step = STP_LOOKUP;
		return (0);
	case VCL_RET_PIPE:
		sp->step = STP_PIPE;
		return (0);
	case VCL_RET_PASS:
		sp->step = STP_PASS;
		return (0);
	case VCL_RET_ERROR:
		/* XXX: discard req body, if any */
		sp->step = STP_ERROR;
		return (0);
	default:
		INCOMPL();
	}
}


/*--------------------------------------------------------------------
 * Central state engine dispatcher.
 *
 * Kick the session around until it has had enough.
 *
 */

void
CNT_Session(struct sess *sp)
{
	int done;
	struct worker *w;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	w = sp->wrk;
	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

	for (done = 0; !done; ) {
		/*
		 * This is a good place to be paranoid about the various
		 * pointers still pointing to the things we expect.
		 */
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		if (sp->obj != NULL)
			CHECK_OBJ(sp->obj, OBJECT_MAGIC);
		CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
		if (w->nobj != NULL)
			CHECK_OBJ(w->nobj, OBJECT_MAGIC);
		if (w->nobjhead != NULL)
			CHECK_OBJ(w->nobjhead, OBJHEAD_MAGIC);

		switch (sp->step) {
#define STEP(l,u) case STP_##u: done = cnt_##l(sp); break;
#include "steps.h"
#undef STEP
		default:	INCOMPL();
		}
		if (w->nobj != NULL)
			CHECK_OBJ(w->nobj, OBJECT_MAGIC);
		if (w->nobjhead != NULL)
			CHECK_OBJ(w->nobjhead, OBJHEAD_MAGIC);
	}
	WSL_Flush(w);
}

/*
DOT }
*/

void
CNT_Init(void)
{

	srandomdev();
	xids = random();
}
