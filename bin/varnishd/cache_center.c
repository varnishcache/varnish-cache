/*
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
 *
 */

/*
DOT digraph vcl_center {
DOT	page="8.2,11.7"
DOT	size="6.3,9.7"
DOT	margin="1.0"
DOT start [
DOT	shape=hexagon
DOT	label="Start"
DOT ]
DOT start -> RECV
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shmlog.h"
#include "vcl.h"
#include "cache.h"


/*--------------------------------------------------------------------
 * We have a refcounted object on the session, now deliver it.
 *
DOT subgraph cluster_deliver {
DOT 	deliver [
DOT		shape=ellipse
DOT		label="Build & send header"
DOT	]
DOT	DELIVER -> deliver [style=bold]
DOT	deliver2 [
DOT		shape=ellipse
DOT		label="Send object"
DOT	]
DOT	deliver -> deliver2 [style=bold]
DOT }
DOT deliver2 -> DONE [style=bold]
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
	struct timespec te;

	assert(sp->obj == NULL);
	assert(sp->vbc == NULL);
	if (sp->fd >= 0 && sp->doclose != NULL)
		vca_close_session(sp, sp->doclose);
	VCL_Rel(sp->vcl);
	sp->vcl = NULL;

	clock_gettime(CLOCK_REALTIME, &te);
	dh = cnt_dt(&sp->t_open, &sp->t_req);
	dp = cnt_dt(&sp->t_req, &sp->t_resp);
	da = cnt_dt(&sp->t_resp, &te);
	VSL(SLT_ReqEnd, sp->id, "%u %ld.%09ld %.9f %.9f %.9f",
	    sp->xid, (long)sp->t_req.tv_sec, (long)sp->t_req.tv_nsec,
	    dh, dp, da);

	SES_Charge(sp);
	vca_return_session(sp);
	return (1);
}


/*--------------------------------------------------------------------
 * Emit an error
 *
DOT subgraph cluster_error {
DOT	error [
DOT		shape=ellipse
DOT		label="Issue HTTP error"
DOT	]
DOT	ERROR -> error
DOT }
DOT error -> DONE
 */

static int cnt_error(struct sess *sp) { (void)sp; INCOMPL(); }


/*--------------------------------------------------------------------
 * We have fetched the headers from the backend, ask the VCL code what
 * to do next, then head off in that direction.
 *
DOT subgraph cluster_fetch {
DOT	fetch [
DOT		shape=ellipse
DOT		label="find obj.ttl\nobj.cacheable"
DOT	]
DOT	FETCH -> fetch [style=bold]
DOT	vcl_fetch [
DOT		shape=box
DOT		label="vcl_fetch()"
DOT	]
DOT	fetch -> vcl_fetch [style=bold]
DOT	fetch_lookup [
DOT		shape=ellipse
DOT		label="obj.cacheable=false\nunbusy obj\ndiscard body\n"
DOT	]
DOT	vcl_fetch -> fetch_lookup [label="lookup", style=dotted, weight=0]
DOT	fetch_pass [
DOT		shape=ellipse
DOT		label="obj.cacheable=false\nunbusy obj"
DOT	]
DOT	vcl_fetch -> fetch_pass [label="pass"]
DOT	fetch_ipass [
DOT		shape=ellipse
DOT		label="obj.cacheable=true\nobj.pass=true\nunbusy obj"
DOT	]
DOT	vcl_fetch -> fetch_ipass [label="insert_pass"]
DOT	fetch_insert [
DOT		shape=ellipse
DOT		label="rcv body\nobj.cacheable=true\nunbusy"
DOT	]
DOT	vcl_fetch -> fetch_insert [label="insert", style=bold]
DOT	fetch_error [
DOT		shape=ellipse
DOT		label="disc body\nobj.cacheable=false\nunbusy"
DOT	]
DOT	vcl_fetch -> fetch_error [label="error"]
DOT }
DOT fetch_lookup -> LOOKUP [style=dotted, weight=0]
DOT fetch_pass -> PASSBODY 
DOT fetch_ipass -> PASSBODY 
DOT fetch_insert -> DELIVER [style=bold]
DOT fetch_error -> ERROR
 */

static int
cnt_fetch(struct sess *sp)
{

	assert(sp->vbc != NULL);
	RFC2616_cache_policy(sp, sp->vbc->http);

	VCL_fetch_method(sp);

	if (sp->handling == VCL_RET_LOOKUP)
		INCOMPL();
	if (sp->handling == VCL_RET_PASS) {
		sp->obj->cacheable = 0;
		HSH_Unbusy(sp->obj);
		HSH_Deref(sp->obj);
		sp->obj = NULL;
		sp->step = STP_PASSBODY;
		return (0);
	}
	if (sp->handling == VCL_RET_INSERT_PASS) {
		sp->obj->pass = 1;
		sp->obj->cacheable = 1;
		HSH_Unbusy(sp->obj);
		/* Don't HSH_Deref(sp->obj); we need the ref for storage */
		sp->obj = NULL;
		sp->step = STP_PASSBODY;
		return (0);
	}
	if (sp->handling == VCL_RET_INSERT) {
		sp->obj->cacheable = 1;
		assert(sp->vbc != NULL);
		FetchBody(sp);
		assert(sp->vbc == NULL);
		HSH_Ref(sp->obj); /* get another, STP_DELIVER will deref */
		HSH_Unbusy(sp->obj);
		sp->wrk->acct.fetch++;
		sp->step = STP_DELIVER;
		return (0);
	}
	if (sp->handling == VCL_RET_ERROR)
		INCOMPL();
	INCOMPL();
}


/*--------------------------------------------------------------------
 * We had a cache hit.  Ask VCL, then march off as instructed.
 *
DOT subgraph cluster_hit {
DOT	hit [
DOT		shape=box
DOT		label="vcl_hit()"
DOT	]
DOT	HIT -> hit [style=bold]
DOT	hit2 [
DOT		shape=diamond
DOT		label="obj.pass ?"
DOT	]
DOT	hit -> hit2 [label=deliver, style=bold]
DOT	hit_lookup [
DOT		shape=ellipse
DOT		label="unbusy"
DOT	]
DOT	hit -> hit_lookup [label="lookup", style=dotted, weight=0]
DOT	hit_error [
DOT		shape=ellipse
DOT		label="unbusy"
DOT	]
DOT	hit -> hit_error [label="error", weight=0]
DOT	hit_pass [
DOT		shape=ellipse
DOT		label="unbusy"
DOT	]
DOT	hit -> hit_pass [label=pass]
DOT	hit2 -> hit_pass
DOT }
DOT hit_error -> ERROR
DOT hit_pass -> PASS
DOT hit_lookup -> LOOKUP [style=dotted, weight=0]
DOT hit2 -> DELIVER [style=bold]
 */

static int
cnt_hit(struct sess *sp)
{

	VCL_hit_method(sp);

	if (sp->handling == VCL_RET_DELIVER && sp->obj->pass)
		sp->handling = VCL_RET_PASS;

	if (sp->handling == VCL_RET_DELIVER) {
		sp->step = STP_DELIVER;
		return (0);
	}
	if (sp->handling == VCL_RET_PASS) {
		HSH_Deref(sp->obj);
		sp->obj = NULL;
		sp->step = STP_PASS;
		return (0);
	}

	if (sp->handling == VCL_RET_ERROR)
		INCOMPL();

	if (sp->handling == VCL_RET_LOOKUP)
		INCOMPL();

	INCOMPL();
}


/*--------------------------------------------------------------------
 * Look up request in hash table
 *
 * LOOKUP consists of two substates so that we can reenter if we
 * encounter a busy object.
 *
DOT subgraph cluster_lookup {
DOT	lookup [
DOT		shape=ellipse
DOT		label="find obj in cache"
DOT	]
DOT	LOOKUP -> lookup [style=bold]
DOT	lookup3 [
DOT		shape=ellipse
DOT		label="Insert new busy object"
DOT	]
DOT	lookup -> lookup3 [style=bold]
DOT }
DOT lookup -> HIT [label="hit", style=bold]
DOT lookup3 -> MISS [label="miss", style=bold]
 */

static int
cnt_lookup(struct sess *sp)
{

	assert(sp->obj == NULL);
	sp->step = STP_LOOKUP2;
	return (0);
}

static int
cnt_lookup2(struct sess *sp)
{
	struct object *o;

	/*
	 * We don't assign to sp->obj directly because it is used
 	 * to cache state when we encounter a busy object.
	 */
	o = HSH_Lookup(sp);

	/* If we encountered busy-object, disembark worker thread */
	if (o == NULL) {
		VSL(SLT_Debug, sp->fd,
		    "on waiting list on obj %u", sp->obj->xid);
		SES_Charge(sp);
		return (1);
	}

	sp->obj = o;

	/* If we inserted a new object it's a miss */
	if (sp->obj->busy) {
		VSL_stats->cache_miss++;
		sp->step = STP_MISS;
		return (0);
	}

	/* Account separately for pass and cache objects */
	if (sp->obj->pass) {
		VSL_stats->cache_hitpass++;
		VSL(SLT_HitPass, sp->fd, "%u", sp->obj->xid);
	} else {
		VSL_stats->cache_hit++;
		VSL(SLT_Hit, sp->fd, "%u", sp->obj->xid);
	}
	sp->step = STP_HIT;
	return (0);
}


/*--------------------------------------------------------------------
 * We had a miss, ask VCL, proceed as instructed
 *
DOT subgraph cluster_miss {
DOT	miss [
DOT		shape=box
DOT		label="vcl_miss()"
DOT	]
DOT	MISS -> miss [style=bold]
DOT	miss_error [
DOT		shape=ellipse
DOT		label="obj.cacheable=false\nunbusy"
DOT	]
DOT	miss -> miss_error [label="error"]
DOT	miss_pass [
DOT		shape=ellipse
DOT		label="obj.cacheable=false\nunbusy"
DOT	]
DOT	miss -> miss_pass [label="pass"]
DOT	miss_lookup [
DOT		shape=ellipse
DOT		label="obj.cacheable=false\nunbusy"
DOT	]
DOT	miss -> miss_lookup [label="lookup", style=dotted, weight=0]
DOT	miss_fetch [
DOT		shape=ellipse
DOT		label="fetch obj headers\nfrom backend"
DOT	]
DOT	miss -> miss_fetch [label="fetch", style=bold]
DOT }
DOT miss_error -> ERROR
DOT miss_pass -> PASS
DOT miss_fetch -> FETCH [style=bold]
DOT miss_lookup -> LOOKUP [style=dotted, weight=0]
DOT
 */

static int
cnt_miss(struct sess *sp)
{

	VCL_miss_method(sp);
	if (sp->handling == VCL_RET_ERROR)
		INCOMPL();
	if (sp->handling == VCL_RET_PASS) {
		sp->obj->cacheable = 0;
		HSH_Unbusy(sp->obj);
		HSH_Deref(sp->obj);
		sp->obj = NULL;
		sp->step = STP_PASS;
		return (0);
	}
	if (sp->handling == VCL_RET_LOOKUP)
		INCOMPL();
	if (sp->handling == VCL_RET_FETCH) {
		assert(sp->vbc == NULL);
		FetchHeaders(sp);
		sp->step = STP_FETCH;
		assert(sp->vbc != NULL);
		return (0);
	}
	INCOMPL();
}


/*--------------------------------------------------------------------
 * Start pass processing by getting headers from backend, then
 * continue in passbody.
 *
DOT subgraph cluster_pass {
DOT	pass [
DOT		shape=ellipse
DOT		label="send to bke\nrx bkehdr"
DOT	]
DOT	PASS -> pass
DOT }
DOT pass -> PASSBODY
 */

static int
cnt_pass(struct sess *sp)
{

	assert(sp->vbc == NULL);
	PassSession(sp);
	assert(sp->vbc != NULL);
	sp->step = STP_PASSBODY;
	return (0);
}


/*--------------------------------------------------------------------
 * We get here when we have the backends headers, send them to client
 * and pass any body the backend may have on as well.
 *
DOT subgraph cluster_passbody {
DOT	passbody [
DOT		shape=ellipse
DOT		label="send hdrs\npass body\n"
DOT	]
DOT	PASSBODY -> passbody
DOT }
DOT passbody -> DONE
 */

static int
cnt_passbody(struct sess *sp)
{

	sp->wrk->acct.pass++;
	assert(sp->vbc != NULL);
	PassBody(sp);
	assert(sp->vbc == NULL);
	sp->step = STP_DONE;
	return (0);
}


/*--------------------------------------------------------------------
 * Ship the request header to the backend unchanged, then pipe
 * until one of the ends close the connection.
 *
DOT subgraph cluster_pipe {
DOT	pipe [
DOT		shape=ellipse
DOT		label="build&send hdr\npipe until close"
DOT	]
DOT	PIPE -> pipe
DOT }
DOT pipe -> DONE
 */

static int
cnt_pipe(struct sess *sp)
{

	sp->wrk->acct.pipe++;
	PipeSession(sp);
	sp->step = STP_DONE;
	return (0);
}


/*--------------------------------------------------------------------
 * Dispatch the request as instructed by VCL
 *
DOT subgraph cluster_recv {
DOT	recv [
DOT		shape=box
DOT		label="vcl_recv()"
DOT	]
DOT	RECV -> recv
DOT	recv_lookup [
DOT		shape=ellipse
DOT		label="discard any body"
DOT	]
DOT	recv -> recv_lookup [label="lookup"]
DOT	recv_error [
DOT		shape=ellipse
DOT		label="discard any body"
DOT	]
DOT	recv -> recv_error [label="error"]
DOT }
DOT recv -> PIPE [label="pipe"]
DOT recv -> PASS [label="pass"]
DOT recv_lookup -> LOOKUP
DOT recv_error -> ERROR
 */

static int
cnt_recv(struct sess *sp)
{
	int done;
	char *b;

	sp->t0 = time(NULL);
	assert(sp->vcl == NULL);
	sp->vcl = VCL_Get();

	assert(sp->obj == NULL);
	assert(sp->vbc == NULL);

	sp->wrk->acct.req++;
	done = http_DissectRequest(sp->http, sp->fd);
	if (done != 0) {
		RES_Error(sp, done, NULL);
		sp->step = STP_DONE;
		return (0);
	}

	if (http_GetHdr(sp->http, H_Connection, &b) && !strcmp(b, "close"))
		sp->doclose = "Connection:";
	else if (strcmp(sp->http->hd[HTTP_HDR_PROTO].b, "HTTP/1.1"))
		sp->doclose = "not HTTP/1.1";

	sp->backend = sp->vcl->backend[0];

	/* XXX: Handle TRACE & OPTIONS of Max-Forwards = 0 */

	/* XXX: determine if request comes with body */

	VCL_recv_method(sp);

	switch(sp->handling) {
	case VCL_RET_LOOKUP:
		/* XXX: discard req body, if any */
		sp->wantbody = !strcmp(sp->http->hd[HTTP_HDR_REQ].b, "GET");
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
 * We grab a VCL reference, and keeps kicking the session around until
 * it has had enough.
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
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		if (sp->obj != NULL)
			CHECK_OBJ(sp->obj, OBJECT_MAGIC);
		CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
		if (w->nobj != NULL)
			CHECK_OBJ(w->nobj, OBJECT_MAGIC);
		if (w->nobjhead != NULL)
			CHECK_OBJ(w->nobjhead, OBJHEAD_MAGIC);
		switch (sp->step) {
#define STEP(l,u) \
		case STP_##u: \
			VSL(SLT_Debug, sp->id, "State " #u); \
			done = cnt_##l(sp); \
			break;
#include "steps.h"
#undef STEP
		default:	INCOMPL();
		}
		if (w->nobj != NULL)
			CHECK_OBJ(w->nobj, OBJECT_MAGIC);
		if (w->nobjhead != NULL)
			CHECK_OBJ(w->nobjhead, OBJHEAD_MAGIC);
	}
}

/*
DOT }
*/
