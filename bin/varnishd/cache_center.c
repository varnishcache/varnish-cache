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
#include <sbuf.h>

#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "vcl.h"
#include "cache.h"

/*--------------------------------------------------------------------
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

static void
cnt_deliver(struct worker *w, struct sess *sp)
{

	vca_write_obj(w, sp);
	sp->step = STP_DONE;
}

/*--------------------------------------------------------------------
DOT	DONE [
DOT		shape=hexagon
DOT		label="Request completed"
DOT	]
 */


static void
cnt_done(struct worker *w, struct sess *sp)
{
	char *b;

	if (http_GetHdr(sp->http, "Connection", &b) &&
	    !strcmp(b, "close")) {
		vca_close_session(sp, "Connection header");
	} else if (http_GetProto(sp->http, &b) &&
	    strcmp(b, "HTTP/1.1")) {
		vca_close_session(sp, "not HTTP/1.1");
	}
}

/*--------------------------------------------------------------------
DOT subgraph cluster_error {
DOT	error [
DOT		shape=ellipse
DOT		label="Issue HTTP error"
DOT	]
DOT	ERROR -> error
DOT }
DOT error -> DONE
 */

static void cnt_error(struct worker *w, struct sess *sp) { (void)w; (void)sp; INCOMPL(); }

/*--------------------------------------------------------------------
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

static void
cnt_fetch(struct worker *w, struct sess *sp)
{

	RFC2616_cache_policy(sp, sp->bkd_http);

	VCL_fetch_method(sp);

	if (sp->handling == VCL_RET_LOOKUP)
		INCOMPL();
	if (sp->handling == VCL_RET_PASS)
		INCOMPL();
	if (sp->handling == VCL_RET_INSERT_PASS)
		INCOMPL();
	if (sp->handling == VCL_RET_INSERT) {
		FetchBody(w, sp);
		sp->step = STP_DELIVER;
		return;
	}
	if (sp->handling == VCL_RET_ERROR)
		INCOMPL();
	INCOMPL();
}

/*--------------------------------------------------------------------
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

static void
cnt_hit(struct worker *w, struct sess *sp)
{

	VCL_hit_method(sp);

	vca_write_obj(w, sp);
	HSH_Deref(sp->obj);
	sp->obj = NULL;
	sp->step = STP_DONE;
}

/*--------------------------------------------------------------------
DOT subgraph cluster_lookup {
DOT	lookup [
DOT		shape=ellipse
DOT		label="find obj in cache"
DOT	]
DOT	LOOKUP -> lookup [style=bold]
DOT	lookup2 [
DOT		shape=ellipse
DOT		label="Insert new busy object"
DOT	]
DOT	lookup -> lookup2 [style=bold]
DOT }
DOT lookup -> HIT [label="hit", style=bold]
DOT lookup2 -> MISS [label="miss", style=bold]
 */

static void
cnt_lookup(struct worker *w, struct sess *sp)
{

	sp->obj = HSH_Lookup(w, sp->http);
	if (sp->obj->busy) {
		VSL_stats->cache_miss++;
		sp->step = STP_MISS;
	} else {
		VSL_stats->cache_hit++;
		VSL(SLT_Hit, sp->fd, "%u", sp->obj->xid);
		sp->step = STP_HIT;
	}
}

/*--------------------------------------------------------------------
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

static void
cnt_miss(struct worker *w, struct sess *sp)
{

	VCL_miss_method(sp);
	if (sp->handling == VCL_RET_ERROR)
		INCOMPL();
	if (sp->handling == VCL_RET_PASS)
		INCOMPL();
	if (sp->handling == VCL_RET_LOOKUP)
		INCOMPL();
	if (sp->handling == VCL_RET_FETCH) {
		FetchHeaders(w, sp);
		sp->step = STP_FETCH;
		return;
	}
	INCOMPL();
}

/*--------------------------------------------------------------------
DOT subgraph cluster_pass {
DOT	pass [
DOT		shape=ellipse
DOT		label="send to bke\nrx bkehdr"
DOT	]
DOT	PASS -> pass
DOT }
DOT pass -> PASSBODY
 */

static void cnt_pass(struct worker *w, struct sess *sp) { (void)w; (void)sp; INCOMPL(); }

/*--------------------------------------------------------------------
DOT subgraph cluster_passbody {
DOT	passbody [
DOT		shape=ellipse
DOT		label="send hdrs\npass body\n"
DOT	]
DOT	PASSBODY -> passbody
DOT }
DOT passbody -> DONE
 */

static void cnt_passbody(struct worker *w, struct sess *sp) { (void)w; (void)sp; INCOMPL(); }

/*--------------------------------------------------------------------
DOT subgraph cluster_pipe {
DOT	pipe [
DOT		shape=ellipse
DOT		label="build&send hdr\npipe until close"
DOT	]
DOT	PIPE -> pipe
DOT }
DOT pipe -> DONE
 */

static void cnt_pipe(struct worker *w, struct sess *sp) { (void)w; (void)sp; INCOMPL(); }

/*--------------------------------------------------------------------
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

static void
cnt_recv(struct worker *w, struct sess *sp)
{
	int done;

	done = http_DissectRequest(sp->http, sp->fd);
	if (done != 0) {
		RES_Error(w, sp, done, NULL);
		sp->step = STP_DONE;
		return;
	}

	sp->backend = sp->vcl->backend[0];

	/* XXX: Handle TRACE & OPTIONS of Max-Forwards = 0 */

	/* XXX: determine if request comes with body */

	VCL_recv_method(sp);

	switch(sp->handling) {
	case VCL_RET_LOOKUP:
		/* XXX: discard req body, if any */
		sp->step = STP_LOOKUP;
		return;
	case VCL_RET_PIPE:
		sp->step = STP_PIPE;
		return;
	case VCL_RET_PASS:
		sp->step = STP_PASS;
		return;
	case VCL_RET_ERROR:
		/* XXX: discard req body, if any */
		sp->step = STP_ERROR;
		return;
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
CNT_Session(struct worker *w, struct sess *sp)
{

	time(&sp->t0);
	AZ(pthread_mutex_lock(&sessmtx));
	sp->vcl = GetVCL();
	AZ(pthread_mutex_unlock(&sessmtx));

	for (sp->step = STP_RECV; sp->step != STP_DONE; ) {
		switch (sp->step) {
#define STEP(l,u) case STP_##u: cnt_##l(w, sp); break;
#include "steps.h"
#undef STEP
		default:	INCOMPL();
		}
	}

	cnt_done(w, sp);	/* The loop doesn't do this */

	AZ(pthread_mutex_lock(&sessmtx));
	RelVCL(sp->vcl);
	AZ(pthread_mutex_unlock(&sessmtx));
	sp->vcl = NULL;

	vca_return_session(sp);
}

/*
DOT }
*/
