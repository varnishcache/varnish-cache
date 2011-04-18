/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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
DOT acceptor [
DOT	shape=hexagon
DOT	label="Request received"
DOT ]
DOT ERROR [shape=plaintext]
DOT RESTART [shape=plaintext]
DOT acceptor -> start [style=bold,color=green]
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

#include "vcl.h"
#include "cli_priv.h"
#include "cache.h"
#include "hash_slinger.h"
#include "stevedore.h"
#include "vsha256.h"

static unsigned xids;

/*--------------------------------------------------------------------
 * WAIT
 * Wait (briefly) until we have a full request in our htc.
 */

static int
cnt_wait(struct sess *sp)
{
	int i;
	struct pollfd pfd[1];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->vcl);
	AZ(sp->obj);
	assert(sp->xid == 0);

	i = HTC_Complete(sp->htc);
	if (i == 0 && params->session_linger > 0) {
		pfd[0].fd = sp->fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		i = poll(pfd, 1, params->session_linger);
		if (i)
			i = HTC_Rx(sp->htc);
	}
	if (i == 0) {
		WSL(sp->wrk, SLT_Debug, sp->fd, "herding");
		sp->wrk->stats.sess_herd++;
		SES_Charge(sp);
		sp->wrk = NULL;
		vca_return_session(sp);
		return (1);
	}
	if (i == 1) {
		sp->step = STP_START;
		return (0);
	}
	if (i == -2) {
		vca_close_session(sp, "overflow");
		return (0);
	}
	if (i == -1 && Tlen(sp->htc->rxbuf) == 0 &&
	    (errno == 0 || errno == ECONNRESET))
		vca_close_session(sp, "EOF");
	else
		vca_close_session(sp, "error");
	sp->step = STP_DONE;
	return (0);
}

/*--------------------------------------------------------------------
 * We have a refcounted object on the session, now deliver it.
 *
DOT subgraph xcluster_prepresp {
DOT	prepresp [
DOT		shape=ellipse
DOT		label="Filter obj.->resp."
DOT	]
DOT	vcl_deliver [
DOT		shape=record
DOT		label="vcl_deliver()|resp."
DOT	]
DOT	prepresp -> vcl_deliver [style=bold,color=green]
DOT	prepresp -> vcl_deliver [style=bold,color=cyan]
DOT	prepresp -> vcl_deliver [style=bold,color=red]
DOT	prepresp -> vcl_deliver [style=bold,color=blue,]
DOT	vcl_deliver -> deliver [style=bold,color=green,label=deliver]
DOT	vcl_deliver -> deliver [style=bold,color=red]
DOT	vcl_deliver -> deliver [style=bold,color=blue]
DOT     vcl_deliver -> errdeliver [label="error"]
DOT     errdeliver [label="ERROR",shape=plaintext]
DOT     vcl_deliver -> rstdeliver [label="restart",color=purple]
DOT     rstdeliver [label="RESTART",shape=plaintext]
DOT     vcl_deliver -> streambody [style=bold,color=cyan,label="deliver"]
DOT }
 *
 */

static int
cnt_prepresp(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vcl, VCL_CONF_MAGIC);

	sp->wrk->res_mode = 0;

	if (!sp->wrk->do_stream)
		sp->wrk->res_mode |= RES_LEN;

	if (!sp->disable_esi && sp->obj->esidata != NULL) {
		/* In ESI mode, we don't know the aggregate length */
		sp->wrk->res_mode &= ~RES_LEN;
		sp->wrk->res_mode |= RES_ESI;
	}

	if (sp->esi_level > 0) {
		sp->wrk->res_mode &= ~RES_LEN;
		sp->wrk->res_mode |= RES_ESI_CHILD;
	}

	if (params->http_gzip_support && sp->obj->gziped &&
	    !RFC2616_Req_Gzip(sp)) {
		/*
		 * We don't know what it uncompresses to
		 * XXX: we could cache that
		 */
		sp->wrk->res_mode &= ~RES_LEN;
		sp->wrk->res_mode |= RES_GUNZIP;
	}

	if (!(sp->wrk->res_mode & (RES_LEN|RES_CHUNKED|RES_EOF))) {
		if (sp->obj->len == 0 && !sp->wrk->do_stream)
			/*
			 * If the object is empty, neither ESI nor GUNZIP
			 * can make it any different size
			 */
			sp->wrk->res_mode |= RES_LEN;
		else if (!sp->wantbody) {
			/* Nothing */
		} else if (sp->http->protover >= 1.1) {
			sp->wrk->res_mode |= RES_CHUNKED;
		} else {
			sp->wrk->res_mode |= RES_EOF;
			sp->doclose = "EOF mode";
		}
	}

	sp->t_resp = TIM_real();
	if (sp->obj->objcore != NULL) {
		if ((sp->t_resp - sp->obj->last_lru) > params->lru_timeout &&
		    EXP_Touch(sp->obj->objcore))
			sp->obj->last_lru = sp->t_resp;
		sp->obj->last_use = sp->t_resp;	/* XXX: locking ? */
	}
	http_Setup(sp->wrk->resp, sp->wrk->ws);
	RES_BuildHttp(sp);
	VCL_deliver_method(sp);
	switch (sp->handling) {
	case VCL_RET_DELIVER:
		break;
	case VCL_RET_RESTART:
		if (sp->restarts >= params->max_restarts)
			break;
		if (sp->wrk->do_stream) {
			VDI_CloseFd(sp);
			HSH_Drop(sp);
		} else {
			(void)HSH_Deref(sp->wrk, NULL, &sp->obj);
		}
		AZ(sp->obj);
		sp->restarts++;
		sp->director = NULL;
		http_Setup(sp->wrk->bereq, NULL);
		http_Setup(sp->wrk->beresp, NULL);
		http_Setup(sp->wrk->resp, NULL);
		sp->step = STP_RECV;
		return (0);
	default:
		WRONG("Illegal action in vcl_deliver{}");
	}
	if (sp->wrk->do_stream)
		sp->step = STP_STREAMBODY;
	else
		sp->step = STP_DELIVER;
	return (0);
}

/*--------------------------------------------------------------------
 * Deliver an already stored object
 *
DOT subgraph xcluster_deliver {
DOT	deliver [
DOT		shape=ellipse
DOT		label="Send body"
DOT	]
DOT }
DOT deliver -> DONE [style=bold,color=green]
DOT deliver -> DONE [style=bold,color=red]
DOT deliver -> DONE [style=bold,color=blue]
 *
 */

static int
cnt_deliver(struct sess *sp)
{

	sp->director = NULL;
	sp->restarts = 0;

	RES_WriteObj(sp);

	assert(WRW_IsReleased(sp->wrk));
	assert(sp->wrk->wrw.ciov == sp->wrk->wrw.siov);
	(void)HSH_Deref(sp->wrk, NULL, &sp->obj);
	http_Setup(sp->wrk->resp, NULL);
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

static int
cnt_done(struct sess *sp)
{
	double dh, dp, da;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_ORNULL(sp->vcl, VCL_CONF_MAGIC);

	AZ(sp->obj);
	AZ(sp->vbc);
	sp->director = NULL;
	sp->restarts = 0;

	if (sp->vcl != NULL && sp->esi_level == 0) {
		if (sp->wrk->vcl != NULL)
			VCL_Rel(&sp->wrk->vcl);
		sp->wrk->vcl = sp->vcl;
		sp->vcl = NULL;
	}

	SES_Charge(sp);

	sp->t_end = TIM_real();
	sp->wrk->lastused = sp->t_end;
	if (sp->xid == 0) {
		sp->t_req = sp->t_end;
		sp->t_resp = sp->t_end;
	} else if (sp->esi_level == 0) {
		dp = sp->t_resp - sp->t_req;
		da = sp->t_end - sp->t_resp;
		dh = sp->t_req - sp->t_open;
		/* XXX: Add StatReq == StatSess */
		WSP(sp, SLT_Length, "%ju", (uintmax_t)sp->acct_req.bodybytes);
		WSL(sp->wrk, SLT_ReqEnd, sp->id, "%u %.9f %.9f %.9f %.9f %.9f",
		    sp->xid, sp->t_req, sp->t_end, dh, dp, da);
	}
	sp->xid = 0;
	sp->t_open = sp->t_end;
	sp->t_resp = NAN;
	WSL_Flush(sp->wrk, 0);

	/* If we did an ESI include, don't mess up our state */
	if (sp->esi_level > 0)
		return (1);

	memset(&sp->acct_req, 0, sizeof sp->acct_req);

	sp->t_req = NAN;
	sp->hash_always_miss = 0;
	sp->hash_ignore_busy = 0;

	if (sp->fd >= 0 && sp->doclose != NULL) {
		/*
		 * This is an orderly close of the connection; ditch nolinger
		 * before we close, to get queued data transmitted.
		 */
		// XXX: not yet (void)TCP_linger(sp->fd, 0);
		vca_close_session(sp, sp->doclose);
	}

	if (sp->fd < 0) {
		sp->wrk->stats.sess_closed++;
		sp->wrk = NULL;
		SES_Delete(sp);
		return (1);
	}

	if (sp->wrk->stats.client_req >= params->wthread_stats_rate)
		WRK_SumStat(sp->wrk);
	/* Reset the workspace to the session-watermark */
	WS_Reset(sp->ws, sp->ws_ses);
	WS_Reset(sp->wrk->ws, NULL);

	i = HTC_Reinit(sp->htc);
	if (i == 1) {
		sp->wrk->stats.sess_pipeline++;
		sp->step = STP_START;
		return (0);
	}
	if (Tlen(sp->htc->rxbuf)) {
		sp->wrk->stats.sess_readahead++;
		sp->step = STP_WAIT;
		return (0);
	}
	if (params->session_linger > 0) {
		sp->wrk->stats.sess_linger++;
		sp->step = STP_WAIT;
		return (0);
	}
	sp->wrk->stats.sess_herd++;
	sp->wrk = NULL;
	vca_return_session(sp);
	return (1);
}

/*--------------------------------------------------------------------
 * Emit an error
 *
DOT subgraph xcluster_error {
DOT	vcl_error [
DOT		shape=record
DOT		label="vcl_error()|resp."
DOT	]
DOT	ERROR -> vcl_error
DOT	vcl_error-> prepresp [label=deliver]
DOT }
 */

static int
cnt_error(struct sess *sp)
{
	struct worker *w;
	struct http *h;
	char date[40];

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	w = sp->wrk;
	if (sp->obj == NULL) {
		HSH_Prealloc(sp);
		/* XXX: 1024 is a pure guess */
		EXP_Clr(&w->exp);
		sp->obj = STV_NewObject(sp, NULL, 1024, &w->exp,
		     params->http_max_hdr);
		sp->obj->xid = sp->xid;
		sp->obj->entered = sp->t_req;
	} else {
		/* XXX: Null the headers ? */
	}
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	h = sp->obj->http;

	if (sp->err_code < 100 || sp->err_code > 999)
		sp->err_code = 501;

	http_PutProtocol(w, sp->fd, h, "HTTP/1.1");
	http_PutStatus(h, sp->err_code);
	TIM_format(TIM_real(), date);
	http_PrintfHeader(w, sp->fd, h, "Date: %s", date);
	http_PrintfHeader(w, sp->fd, h, "Server: Varnish");

	if (sp->err_reason != NULL)
		http_PutResponse(w, sp->fd, h, sp->err_reason);
	else
		http_PutResponse(w, sp->fd, h,
		    http_StatusMessage(sp->err_code));
	VCL_error_method(sp);

	if (sp->handling == VCL_RET_RESTART &&
	    sp->restarts <  params->max_restarts) {
		HSH_Drop(sp);
		sp->director = NULL;
		sp->restarts++;
		sp->step = STP_RECV;
		return (0);
	} else if (sp->handling == VCL_RET_RESTART)
		sp->handling = VCL_RET_DELIVER;


	/* We always close when we take this path */
	sp->doclose = "error";
	sp->wantbody = 1;

	assert(sp->handling == VCL_RET_DELIVER);
	sp->err_code = 0;
	sp->err_reason = NULL;
	http_Setup(sp->wrk->bereq, NULL);
	sp->step = STP_PREPRESP;
	return (0);
}

/*--------------------------------------------------------------------
 * Fetch response headers from the backend
 *
DOT subgraph xcluster_fetch {
DOT	fetch [
DOT		shape=ellipse
DOT		label="fetch hdr\nfrom backend\n(find obj.ttl)"
DOT	]
DOT	vcl_fetch [
DOT		shape=record
DOT		label="vcl_fetch()|req.\nbereq.\nberesp."
DOT	]
DOT	fetch -> vcl_fetch [style=bold,color=blue]
DOT	fetch -> vcl_fetch [style=bold,color=red]
DOT	fetch_pass [
DOT		shape=ellipse
DOT		label="obj.f.pass=true"
DOT	]
DOT	vcl_fetch -> fetch_pass [label="hit_for_pass",style=bold,color=red]
DOT }
DOT fetch_pass -> fetchbody [style=bold,color=red]
DOT vcl_fetch -> fetchbody [label="deliver",style=bold,color=blue]
DOT vcl_fetch -> rstfetch [label="restart",color=purple]
DOT rstfetch [label="RESTART",shape=plaintext]
DOT fetch -> errfetch
DOT vcl_fetch -> errfetch [label="error"]
DOT errfetch [label="ERROR",shape=plaintext]
 */

static int
cnt_fetch(struct sess *sp)
{
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vcl, VCL_CONF_MAGIC);

	AN(sp->director);
	AZ(sp->vbc);
	AZ(sp->wrk->h_content_length);
	AZ(sp->wrk->do_close);
	AZ(sp->wrk->storage_hint);

	http_Setup(sp->wrk->beresp, sp->wrk->ws);

	i = FetchHdr(sp);
	/*
	 * If we recycle a backend connection, there is a finite chance
	 * that the backend closed it before we get a request to it.
	 * Do a single retry in that case.
	 */
	if (i == 1) {
		VSC_main->backend_retry++;
		i = FetchHdr(sp);
	}

	if (i) {
		sp->handling = VCL_RET_ERROR;
		sp->err_code = 503;
	} else {
		/*
		 * These two headers can be spread over multiple actual headers
		 * and we rely on their content outside of VCL, so collect them
		 * into one line here.
		 */
		http_CollectHdr(sp->wrk->beresp, H_Cache_Control);
		http_CollectHdr(sp->wrk->beresp, H_Vary);

		/*
		 * Figure out how the fetch is supposed to happen, before the
		 * headers are adultered by VCL
		 * NB: Also sets other sp->wrk variables 
		 */ 
		sp->wrk->body_status = RFC2616_Body(sp);

		sp->err_code = http_GetStatus(sp->wrk->beresp);

		/*
		 * What does RFC2616 think about TTL ?
		 */
		sp->wrk->entered = TIM_real();
		sp->wrk->age = 0;
		EXP_Clr(&sp->wrk->exp);
		sp->wrk->exp.ttl = RFC2616_Ttl(sp);

		/* pass from vclrecv{} has negative TTL */
		if (sp->objcore == NULL)
			sp->wrk->exp.ttl = -1.;

		sp->wrk->do_esi = 0;

		VCL_fetch_method(sp);

		switch (sp->handling) {
		case VCL_RET_HIT_FOR_PASS:
			if (sp->objcore != NULL)
				sp->objcore->flags |= OC_F_PASS;
			sp->step = STP_FETCHBODY;
			return (0);
		case VCL_RET_DELIVER:
			sp->step = STP_FETCHBODY;
			return (0);
		default:
			break;
		}

		/* We are not going to fetch the body, Close the connection */
		VDI_CloseFd(sp);
	}

	/* Clean up partial fetch */
	AZ(sp->vbc);

	if (sp->objcore != NULL) {
		CHECK_OBJ_NOTNULL(sp->objcore, OBJCORE_MAGIC);
		AZ(HSH_Deref(sp->wrk, sp->objcore, NULL));
		sp->objcore = NULL;
	}
	http_Setup(sp->wrk->bereq, NULL);
	http_Setup(sp->wrk->beresp, NULL);
	sp->wrk->h_content_length = NULL;
	sp->director = NULL;
	sp->wrk->storage_hint = NULL;

	switch (sp->handling) {
	case VCL_RET_RESTART:
		sp->restarts++;
		sp->step = STP_RECV;
		return (0);
	case VCL_RET_ERROR:
		sp->step = STP_ERROR;
		return (0);
	default:
		WRONG("Illegal action in vcl_fetch{}");
	}
}

/*--------------------------------------------------------------------
 * Fetch response body from the backend
 *
DOT subgraph xcluster_body {
DOT	fetchbody [
DOT		shape=diamond
DOT		label="stream ?"
DOT	]
DOT	fetchbody2 [
DOT		shape=ellipse
DOT		label="fetch body\nfrom backend\n"
DOT	]
DOT }
DOT fetchbody -> fetchbody2 [label=no,style=bold,color=red]
DOT fetchbody -> fetchbody2 [style=bold,color=blue]
DOT fetchbody -> prepresp [label=yes,style=bold,color=cyan]
DOT fetchbody2 -> prepresp [style=bold,color=red]
DOT fetchbody2 -> prepresp [style=bold,color=blue]
 */


static int
cnt_fetchbody(struct sess *sp)
{
	int i;
	struct http *hp, *hp2;
	char *b;
	unsigned l, nhttp;
	struct vsb *vary = NULL;
	int varyl = 0, pass;

	assert(sp->handling == VCL_RET_HIT_FOR_PASS ||
	    sp->handling == VCL_RET_DELIVER);

	if (sp->objcore == NULL) {
		/* This is a pass from vcl_recv */
		pass = 1;
		/* VCL may have fiddled this, but that doesn't help */
		sp->wrk->exp.ttl = -1.;
	} else if (sp->handling == VCL_RET_HIT_FOR_PASS) {
		/* pass from vcl_fetch{} -> hit-for-pass */
		/* XXX: the bereq was not filtered pass... */
		pass = 1;
	} else {
		/* regular object */
		pass = 0;
	}

	/*
	 * The VCL variables beresp.do_g[un]zip tells us how we want the
	 * object processed before it is stored.
	 *
	 * The backend Content-Encoding header tells us what we are going
	 * to receive, which we classify in the following three classes:
	 *
	 *	"Content-Encoding: gzip"	--> object is gzip'ed.
	 *	no Content-Encoding		--> object is not gzip'ed.
	 *	anything else			--> do nothing wrt gzip
	 *
	 */

	AZ(sp->wrk->vfp);

	/* We do nothing unless the param is set */
	if (!params->http_gzip_support)
		sp->wrk->do_gzip = sp->wrk->do_gunzip = 0;

	sp->wrk->is_gzip =
	    http_HdrIs(sp->wrk->beresp, H_Content_Encoding, "gzip");

	sp->wrk->is_gunzip =
	    !http_GetHdr(sp->wrk->beresp, H_Content_Encoding, NULL);

	/* It can't be both */
	assert(sp->wrk->is_gzip == 0 || sp->wrk->is_gunzip == 0);

	/* We won't gunzip unless it is gzip'ed */
	if (sp->wrk->do_gunzip && !sp->wrk->is_gzip)
		sp->wrk->do_gunzip = 0;

	/* If we do gunzip, remove the C-E header */
	if (sp->wrk->do_gunzip)
		http_Unset(sp->wrk->beresp, H_Content_Encoding);

	/* We wont gzip unless it is ungziped */
	if (sp->wrk->do_gzip && !sp->wrk->is_gunzip)
		sp->wrk->do_gzip = 0;

	/* If we do gzip, add the C-E header */
	if (sp->wrk->do_gzip)
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->beresp,
		    "Content-Encoding: %s", "gzip");

	/* But we can't do both at the same time */
	assert(sp->wrk->do_gzip == 0 || sp->wrk->do_gunzip == 0);

	/* ESI takes precedence and handles gzip/gunzip itself */
	if (sp->wrk->do_esi)
		sp->wrk->vfp = &vfp_esi;
	else if (sp->wrk->do_gunzip)
		sp->wrk->vfp = &vfp_gunzip;
	else if (sp->wrk->do_gzip)
		sp->wrk->vfp = &vfp_gzip;
	else if (sp->wrk->is_gzip)
		sp->wrk->vfp = &vfp_testgzip;

	if (sp->wrk->do_esi)
		sp->wrk->do_stream = 0;
	if (!sp->wantbody)
		sp->wrk->do_stream = 0;

	l = http_EstimateWS(sp->wrk->beresp,
	    pass ? HTTPH_R_PASS : HTTPH_A_INS, &nhttp);

	/* Create Vary instructions */
	if (sp->objcore != NULL) {
		CHECK_OBJ_NOTNULL(sp->objcore, OBJCORE_MAGIC);
		vary = VRY_Create(sp, sp->wrk->beresp);
		if (vary != NULL) {
			varyl = vsb_len(vary);
			assert(varyl > 0);
			l += varyl;
		}
	}

	/*
	 * Space for producing a Content-Length: header including padding
	 * A billion gigabytes is enough for anybody.
	 */
	l += strlen("Content-Length: XxxXxxXxxXxxXxxXxx") + sizeof(void *);

	if (sp->wrk->exp.ttl < params->shortlived || sp->objcore == NULL)
		sp->wrk->storage_hint = TRANSIENT_STORAGE;

	sp->obj = STV_NewObject(sp, sp->wrk->storage_hint, l,
	    &sp->wrk->exp, nhttp);
	if (sp->obj == NULL) {
		/*
		 * Try to salvage the transaction by allocating a
		 * shortlived object on Transient storage.
		 */
		sp->obj = STV_NewObject(sp, TRANSIENT_STORAGE, l,
		    &sp->wrk->exp, nhttp);
		sp->wrk->exp.ttl = params->shortlived;
	}
	if (sp->obj == NULL) {
		HSH_Drop(sp);
		sp->err_code = 503;
		sp->step = STP_ERROR;
		VDI_CloseFd(sp);
		return (0);
	}
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);

	sp->wrk->storage_hint = NULL;

	/* VFP will update as needed */
	sp->obj->gziped = sp->wrk->is_gzip;

	if (vary != NULL) {
		sp->obj->vary =
		    (void *)WS_Alloc(sp->obj->http->ws, varyl);
		AN(sp->obj->vary);
		memcpy(sp->obj->vary, vsb_data(vary), varyl);
		vsb_delete(vary);
	}

	sp->obj->xid = sp->xid;
	sp->obj->response = sp->err_code;
	sp->obj->age = sp->wrk->age;
	sp->obj->entered = sp->wrk->entered;
	WS_Assert(sp->obj->ws_o);

	/* Filter into object */
	hp = sp->wrk->beresp;
	hp2 = sp->obj->http;

	hp2->logtag = HTTP_Obj;
	http_CopyResp(hp2, hp);
	http_FilterFields(sp->wrk, sp->fd, hp2, hp,
	    pass ? HTTPH_R_PASS : HTTPH_A_INS);
	http_CopyHome(sp->wrk, sp->fd, hp2);

	if (http_GetHdr(hp, H_Last_Modified, &b))
		sp->obj->last_modified = TIM_parse(b);
	else
		sp->obj->last_modified = sp->wrk->entered;

	assert(WRW_IsReleased(sp->wrk));

	if (sp->wrk->do_stream) {
		sp->step = STP_PREPRESP;
		return (0);
	}

	/* Use unmodified headers*/
	i = FetchBody(sp);

	sp->wrk->h_content_length = NULL;

	http_Setup(sp->wrk->bereq, NULL);
	http_Setup(sp->wrk->beresp, NULL);
	sp->wrk->vfp = NULL;
	assert(WRW_IsReleased(sp->wrk));
	AZ(sp->vbc);
	AN(sp->director);

	if (i) {
		HSH_Drop(sp);
		AZ(sp->obj);
		sp->err_code = 503;
		sp->step = STP_ERROR;
		return (0);
	}

	if (sp->obj->objcore != NULL) {
		EXP_Insert(sp->obj);
		AN(sp->obj->objcore);
		AN(sp->obj->objcore->ban);
		HSH_Unbusy(sp);
	}
	sp->acct_tmp.fetch++;
	sp->step = STP_PREPRESP;
	return (0);
}

/*--------------------------------------------------------------------
 * Stream the body as we fetch it
DOT subgraph xstreambody {
DOT	streambody [
DOT		shape=ellipse
DOT		label="streaming\nfetch/deliver"
DOT	]
DOT }
DOT streambody -> DONE [style=bold,color=cyan]
 */

static int
cnt_streambody(struct sess *sp)
{
	int i;

	RES_StreamStart(sp);

	/* Use unmodified headers*/
	i = FetchBody(sp);

	sp->wrk->h_content_length = NULL;

	http_Setup(sp->wrk->bereq, NULL);
	http_Setup(sp->wrk->beresp, NULL);
	sp->wrk->vfp = NULL;
	AZ(sp->vbc);
	AN(sp->director);

	if (i) {
		HSH_Drop(sp);
		AZ(sp->obj);
		sp->err_code = 503;
		sp->step = STP_ERROR;
		return (0);
	}

	if (sp->obj->objcore != NULL) {
		EXP_Insert(sp->obj);
		AN(sp->obj->objcore);
		AN(sp->obj->objcore->ban);
		HSH_Unbusy(sp);
	}
	sp->acct_tmp.fetch++;
	sp->director = NULL;
	sp->restarts = 0;

	RES_StreamEnd(sp);

	assert(WRW_IsReleased(sp->wrk));
	assert(sp->wrk->wrw.ciov == sp->wrk->wrw.siov);
	(void)HSH_Deref(sp->wrk, NULL, &sp->obj);
	http_Setup(sp->wrk->resp, NULL);
	sp->step = STP_DONE;
	return (0);
}

/*--------------------------------------------------------------------
 * The very first request
 */
static int
cnt_first(struct sess *sp)
{

	/*
	 * XXX: If we don't have acceptfilters we are somewhat subject
	 * XXX: to DoS'ing here.  One remedy would be to set a shorter
	 * XXX: SO_RCVTIMEO and once we have received something here
	 * XXX: increase it to the normal value.
	 */

	assert(sp->xid == 0);
	assert(sp->restarts == 0);
	VCA_Prep(sp);

	/* Record the session watermark */
	sp->ws_ses = WS_Snapshot(sp->ws);

	/* Receive a HTTP protocol request */
	HTC_Init(sp->htc, sp->ws, sp->fd, params->http_req_size,
	    params->http_req_hdr_len);
	sp->wrk->lastused = sp->t_open;
	sp->acct_tmp.sess++;

	sp->step = STP_WAIT;
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
DOT hit -> rst_hit [label="restart",color=purple]
DOT rst_hit [label="RESTART",shape=plaintext]
DOT hit -> pass [label=pass,style=bold,color=red]
DOT hit -> prepresp [label="deliver",style=bold,color=green]
 */

static int
cnt_hit(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vcl, VCL_CONF_MAGIC);

	assert(!(sp->obj->objcore->flags & OC_F_PASS));

	VCL_hit_method(sp);

	if (sp->handling == VCL_RET_DELIVER) {
		/* Dispose of any body part of the request */
		(void)FetchReqBody(sp);
		AZ(sp->wrk->bereq->ws);
		AZ(sp->wrk->beresp->ws);
		sp->step = STP_PREPRESP;
		return (0);
	}

	/* Drop our object, we won't need it */
	(void)HSH_Deref(sp->wrk, NULL, &sp->obj);
	sp->objcore = NULL;

	switch(sp->handling) {
	case VCL_RET_PASS:
		sp->step = STP_PASS;
		return (0);
	case VCL_RET_ERROR:
		sp->step = STP_ERROR;
		return (0);
	case VCL_RET_RESTART:
		sp->director = NULL;
		sp->restarts++;
		sp->step = STP_RECV;
		return (0);
	default:
		WRONG("Illegal action in vcl_hit{}");
	}
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
DOT		label="obj.f.pass ?"
DOT	]
DOT	hash -> lookup [label="hash",style=bold,color=green]
DOT	lookup -> lookup2 [label="yes",style=bold,color=green]
DOT }
DOT lookup2 -> hit [label="no", style=bold,color=green]
DOT lookup2 -> pass [label="yes",style=bold,color=red]
DOT lookup -> miss [label="no",style=bold,color=blue]
 */

static int
cnt_lookup(struct sess *sp)
{
	struct objcore *oc;
	struct object *o;
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vcl, VCL_CONF_MAGIC);


	oc = HSH_Lookup(sp, &oh);

	if (oc == NULL) {
		/*
		 * We lost the session to a busy object, disembark the
		 * worker thread.   The hash code to restart the session,
		 * still in STP_LOOKUP, later when the busy object isn't.
		 * NB:  Do not access sp any more !
		 */
		return (1);
	}

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	/* If we inserted a new object it's a miss */
	if (oc->flags & OC_F_BUSY) {
		sp->wrk->stats.cache_miss++;

		sp->objcore = oc;
		sp->step = STP_MISS;
		return (0);
	}

	o = oc_getobj(sp->wrk, oc);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	sp->obj = o;

	if (oc->flags & OC_F_PASS) {
		sp->wrk->stats.cache_hitpass++;
		WSP(sp, SLT_HitPass, "%u", sp->obj->xid);
		(void)HSH_Deref(sp->wrk, NULL, &sp->obj);
		sp->objcore = NULL;
		sp->step = STP_PASS;
		return (0);
	}

	sp->wrk->stats.cache_hit++;
	WSP(sp, SLT_Hit, "%u", sp->obj->xid);
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
DOT	miss -> vcl_miss [style=bold,color=blue]
DOT }
DOT vcl_miss -> rst_miss [label="restart",color=purple]
DOT rst_miss [label="RESTART",shape=plaintext]
DOT vcl_miss -> err_miss [label="error"]
DOT err_miss [label="ERROR",shape=plaintext]
DOT vcl_miss -> fetch [label="fetch",style=bold,color=blue]
DOT vcl_miss -> pass [label="pass",style=bold,color=red]
DOT
 */

static int
cnt_miss(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vcl, VCL_CONF_MAGIC);

	AZ(sp->obj);
	AN(sp->objcore);
	WS_Reset(sp->wrk->ws, NULL);
	http_Setup(sp->wrk->bereq, sp->wrk->ws);
	http_FilterHeader(sp, HTTPH_R_FETCH);
	http_ForceGet(sp->wrk->bereq);
	if (params->http_gzip_support) {
		/*
		 * We always ask the backend for gzip, even if the
		 * client doesn't grok it.  We will uncompress for
		 * the minority of clients which don't.
		 */
		http_Unset(sp->wrk->bereq, H_Accept_Encoding);
		http_PrintfHeader(sp->wrk, sp->fd, sp->wrk->bereq,
		    "Accept-Encoding: gzip");
	}
	sp->wrk->connect_timeout = 0;
	sp->wrk->first_byte_timeout = 0;
	sp->wrk->between_bytes_timeout = 0;
	VCL_miss_method(sp);
	switch(sp->handling) {
	case VCL_RET_ERROR:
		AZ(HSH_Deref(sp->wrk, sp->objcore, NULL));
		sp->objcore = NULL;
		sp->step = STP_ERROR;
		return (0);
	case VCL_RET_PASS:
		AZ(HSH_Deref(sp->wrk, sp->objcore, NULL));
		sp->objcore = NULL;
		sp->step = STP_PASS;
		return (0);
	case VCL_RET_FETCH:
		sp->step = STP_FETCH;
		return (0);
	case VCL_RET_RESTART:
		AZ(HSH_Deref(sp->wrk, sp->objcore, NULL));
		sp->objcore = NULL;
		INCOMPL();
	default:
		WRONG("Illegal action in vcl_miss{}");
	}
}

/*--------------------------------------------------------------------
 * Start pass processing by getting headers from backend, then
 * continue in passbody.
 *
DOT subgraph xcluster_pass {
DOT	pass [
DOT		shape=ellipse
DOT		label="deref obj."
DOT	]
DOT	pass2 [
DOT		shape=ellipse
DOT		label="filter req.->bereq."
DOT	]
DOT	vcl_pass [
DOT		shape=record
DOT		label="vcl_pass()|req.\nbereq."
DOT	]
DOT	pass_do [
DOT		shape=ellipse
DOT		label="create anon object\n"
DOT	]
DOT	pass -> pass2 [style=bold, color=red]
DOT	pass2 -> vcl_pass [style=bold, color=red]
DOT	vcl_pass -> pass_do [label="pass"] [style=bold, color=red]
DOT }
DOT pass_do -> fetch [style=bold, color=red]
DOT vcl_pass -> rst_pass [label="restart",color=purple]
DOT rst_pass [label="RESTART",shape=plaintext]
DOT vcl_pass -> err_pass [label="error"]
DOT err_pass [label="ERROR",shape=plaintext]
 */

static int
cnt_pass(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vcl, VCL_CONF_MAGIC);
	AZ(sp->obj);

	WS_Reset(sp->wrk->ws, NULL);
	http_Setup(sp->wrk->bereq, sp->wrk->ws);
	http_FilterHeader(sp, HTTPH_R_PASS);

	sp->wrk->connect_timeout = 0;
	sp->wrk->first_byte_timeout = 0;
	sp->wrk->between_bytes_timeout = 0;
	VCL_pass_method(sp);
	if (sp->handling == VCL_RET_ERROR) {
		sp->step = STP_ERROR;
		return (0);
	}
	assert(sp->handling == VCL_RET_PASS);
	sp->acct_tmp.pass++;
	sp->sendbody = 1;
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
DOT	vcl_pipe -> pipe_do [label="pipe",style=bold,color=orange]
DOT	pipe -> vcl_pipe [style=bold,color=orange]
DOT }
DOT pipe_do -> DONE [style=bold,color=orange]
DOT vcl_pipe -> err_pipe [label="error"]
DOT err_pipe [label="ERROR",shape=plaintext]
 */

static int
cnt_pipe(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vcl, VCL_CONF_MAGIC);

	sp->acct_tmp.pipe++;
	WS_Reset(sp->wrk->ws, NULL);
	http_Setup(sp->wrk->bereq, sp->wrk->ws);
	http_FilterHeader(sp, HTTPH_R_PIPE);

	VCL_pipe_method(sp);

	if (sp->handling == VCL_RET_ERROR)
		INCOMPL();
	assert(sp->handling == VCL_RET_PIPE);

	PipeSession(sp);
	assert(WRW_IsReleased(sp->wrk));
	http_Setup(sp->wrk->bereq, NULL);
	sp->step = STP_DONE;
	return (0);
}

/*--------------------------------------------------------------------
 * RECV
 * We have a complete request, set everything up and start it.
 *
DOT subgraph xcluster_recv {
DOT	recv [
DOT		shape=record
DOT		label="vcl_recv()|req."
DOT	]
DOT }
DOT RESTART -> recv
DOT recv -> pipe [label="pipe",style=bold,color=orange]
DOT recv -> pass2 [label="pass",style=bold,color=red]
DOT recv -> err_recv [label="error"]
DOT err_recv [label="ERROR",shape=plaintext]
DOT recv -> hash [label="lookup",style=bold,color=green]
 */

static int
cnt_recv(struct sess *sp)
{
	unsigned recv_handling;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vcl, VCL_CONF_MAGIC);
	AZ(sp->obj);
	assert(sp->wrk->wrw.ciov == sp->wrk->wrw.siov);

	/* By default we use the first backend */
	AZ(sp->director);
	sp->director = sp->vcl->director[0];
	AN(sp->director);

	sp->disable_esi = 0;
	sp->hash_always_miss = 0;
	sp->hash_ignore_busy = 0;
	sp->client_identity = NULL;

	http_CollectHdr(sp->http, H_Cache_Control);

	VCL_recv_method(sp);
	recv_handling = sp->handling;

	if (sp->restarts >= params->max_restarts) {
		if (sp->err_code == 0)
			sp->err_code = 503;
		sp->step = STP_ERROR;
		return (0);
	}

	/* XXX: do_esi ? */
	sp->wrk->is_gzip = 0;
	sp->wrk->is_gunzip = 0;
	sp->wrk->do_gzip = 0;
	sp->wrk->do_gunzip = 0;
	sp->wrk->do_stream = 0;

	if (params->http_gzip_support &&
	     (recv_handling != VCL_RET_PIPE) &&
	     (recv_handling != VCL_RET_PASS)) {
		if (RFC2616_Req_Gzip(sp)) {
			http_Unset(sp->http, H_Accept_Encoding);
			http_PrintfHeader(sp->wrk, sp->fd, sp->http,
			    "Accept-Encoding: gzip");
		} else {
			http_Unset(sp->http, H_Accept_Encoding);
		}
	}

	SHA256_Init(sp->wrk->sha256ctx);
	VCL_hash_method(sp);
	assert(sp->handling == VCL_RET_HASH);
	SHA256_Final(sp->digest, sp->wrk->sha256ctx);

	if (!strcmp(sp->http->hd[HTTP_HDR_REQ].b, "HEAD"))
		sp->wantbody = 0;
	else
		sp->wantbody = 1;

	sp->sendbody = 0;
	switch(recv_handling) {
	case VCL_RET_LOOKUP:
		/* XXX: discard req body, if any */
		sp->step = STP_LOOKUP;
		return (0);
	case VCL_RET_PIPE:
		if (sp->esi_level > 0) {
			/* XXX: VSL something */
			INCOMPL();
			/* sp->step = STP_DONE; */
			return (1);
		}
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
		WRONG("Illegal action in vcl_recv{}");
	}
}

/*--------------------------------------------------------------------
 * START
 * Handle a request, wherever it came from recv/restart.
 *
DOT start [shape=box,label="Dissect request"]
DOT start -> recv [style=bold,color=green]
 */

static int
cnt_start(struct sess *sp)
{
	int done;
	char *p;
	const char *r = "HTTP/1.1 100 Continue\r\n\r\n";

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AZ(sp->restarts);
	AZ(sp->obj);
	AZ(sp->vcl);

	/* Update stats of various sorts */
	sp->wrk->stats.client_req++;
	sp->t_req = TIM_real();
	sp->wrk->lastused = sp->t_req;
	sp->acct_tmp.req++;

	/* Assign XID and log */
	sp->xid = ++xids;				/* XXX not locked */
	WSP(sp, SLT_ReqStart, "%s %s %u", sp->addr, sp->port,  sp->xid);

	/* Borrow VCL reference from worker thread */
	VCL_Refresh(&sp->wrk->vcl);
	sp->vcl = sp->wrk->vcl;
	sp->wrk->vcl = NULL;

	http_Setup(sp->http, sp->ws);
	done = http_DissectRequest(sp);

	/* If we could not even parse the request, just close */
	if (done < 0) {
		sp->step = STP_DONE;
		vca_close_session(sp, "junk");
		return (0);
	}

	/* Catch request snapshot */
	sp->ws_req = WS_Snapshot(sp->ws);

	/* Catch original request, before modification */
	HTTP_Copy(sp->http0, sp->http);

	if (done != 0) {
		sp->err_code = done;
		sp->step = STP_ERROR;
		return (0);
	}

	sp->doclose = http_DoConnection(sp->http);

	/* XXX: Handle TRACE & OPTIONS of Max-Forwards = 0 */

	/*
	 * Handle Expect headers
	 */
	if (http_GetHdr(sp->http, H_Expect, &p)) {
		if (strcasecmp(p, "100-continue")) {
			sp->err_code = 417;
			sp->step = STP_ERROR;
			return (0);
		}

		/* XXX: Don't bother with write failures for now */
		(void)write(sp->fd, r, strlen(r));
		/* XXX: When we do ESI includes, this is not removed
		 * XXX: because we use http0 as our basis.  Believed
		 * XXX: safe, but potentially confusing.
		 */
		http_Unset(sp->http, H_Expect);
	}

	sp->step = STP_RECV;
	return (0);
}

/*--------------------------------------------------------------------
 * Central state engine dispatcher.
 *
 * Kick the session around until it has had enough.
 *
 */

static void
cnt_diag(struct sess *sp, const char *state)
{
	if (sp->wrk != NULL) {
		WSL(sp->wrk, SLT_Debug, sp->id,
		    "thr %p STP_%s sp %p obj %p vcl %p",
		    pthread_self(), state, sp, sp->obj, sp->vcl);
		WSL_Flush(sp->wrk, 0);
	} else {
		VSL(SLT_Debug, sp->id,
		    "thr %p STP_%s sp %p obj %p vcl %p",
		    pthread_self(), state, sp, sp->obj, sp->vcl);
	}
}

void
CNT_Session(struct sess *sp)
{
	int done;
	struct worker *w;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	w = sp->wrk;
	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

	/*
	 * Possible entrance states
	 */
	assert(
	    sp->step == STP_FIRST ||
	    sp->step == STP_START ||
	    sp->step == STP_LOOKUP ||
	    sp->step == STP_RECV);

	/*
	 * Whenever we come in from the acceptor we need to set blocking
	 * mode, but there is no point in setting it when we come from
	 * ESI or when a parked sessions returns.
	 * It would be simpler to do this in the acceptor, but we'd rather
	 * do the syscall in the worker thread.
	 */
	if (sp->step == STP_FIRST || sp->step == STP_START)
		(void)TCP_blocking(sp->fd);

	/*
	 * NB: Once done is set, we can no longer touch sp!
	 */
	for (done = 0; !done; ) {
		assert(sp->wrk == w);
		/*
		 * This is a good place to be paranoid about the various
		 * pointers still pointing to the things we expect.
		 */
		CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
		CHECK_OBJ_ORNULL(sp->obj, OBJECT_MAGIC);
		CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
		CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
		WS_Assert(w->ws);

		switch (sp->step) {
#define STEP(l,u) \
		    case STP_##u: \
			if (params->diag_bitmap & 0x01) \
				cnt_diag(sp, #u); \
			done = cnt_##l(sp); \
		        break;
#include "steps.h"
#undef STEP
		default:
			WRONG("State engine misfire");
		}
		WS_Assert(w->ws);
		CHECK_OBJ_ORNULL(w->nobjhead, OBJHEAD_MAGIC);
	}
	WSL_Flush(w, 0);
	assert(WRW_IsReleased(w));
}

/*
DOT }
*/

/*--------------------------------------------------------------------
 * Debugging aids
 */

static void
cli_debug_xid(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	if (av[2] != NULL)
		xids = strtoul(av[2], NULL, 0);
	cli_out(cli, "XID is %u", xids);
}

/*
 * Default to seed=1, this is the only seed value POSIXl guarantees will
 * result in a reproducible random number sequence.
 */
static void
cli_debug_srandom(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	unsigned seed = 1;

	if (av[2] != NULL)
		seed = strtoul(av[2], NULL, 0);
	srandom(seed);
	srand48(random());
	cli_out(cli, "Random(3) seeded with %lu", seed);
}

static struct cli_proto debug_cmds[] = {
	{ "debug.xid", "debug.xid",
		"\tExamine or set XID\n", 0, 1, "d", cli_debug_xid },
	{ "debug.srandom", "debug.srandom",
		"\tSeed the random(3) function\n", 0, 1, "d", cli_debug_srandom },
	{ NULL }
};

/*--------------------------------------------------------------------
 *
 */

void
CNT_Init(void)
{

	srandomdev();
	srand48(random());
	xids = random();
	CLI_AddFuncs(debug_cmds);
}


