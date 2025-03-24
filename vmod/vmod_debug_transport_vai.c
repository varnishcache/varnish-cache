/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * Copyright 2024 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *	    Nils Goroll <slink@uplex.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 */

#include "config.h"

#include "cache/cache_varnishd.h"

#include "cache/cache_filter.h"
#include "cache/cache_transport.h"
#include "http1/cache_http1.h"

#include "vmod_debug.h"

#define HELLO "hello "

static int v_matchproto_(vdpio_init_f)
vdpio_hello_init(VRT_CTX, struct vdp_ctx *vdc, void **priv, int capacity)
{

	(void)ctx;
	(void)priv;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	AN(vdc->clen);

	if (*vdc->clen < 0)
		return (capacity);

	*vdc->clen += strlen(HELLO);
	http_Unset(vdc->hp, H_Content_Length);
	http_PrintfHeader(vdc->hp, "Content-Length: %zd", *vdc->clen);
	return (capacity);
}

static int v_matchproto_(vdpio_lease_f)
vdpio_hello_lease(struct vdp_ctx *vdc, struct vdp_entry *this,
    struct vscarab *scarab)
{
	int r;

	VSCARAB_CHECK_NOTNULL(scarab);
	if (scarab->used == scarab->capacity)
		return (0);
	//lint -e{446} side effects in initializer - uh?
	VSCARAB_ADD_IOV_NORET(scarab, ((struct iovec)
	    {.iov_base = TRUST_ME(HELLO), .iov_len = strlen(HELLO)}));
	r = vdpio_pull(vdc, this, scarab);

	(void) VDPIO_Close1(vdc, this);

	// return error from pull
	if (r < 0)
		r = 1;
	else
		r += 1;

	return (r);
}

static const struct vdp vdp_hello = {
	.name = "hello",
	.io_init = vdpio_hello_init,
	.io_lease = vdpio_hello_lease
};

static void
dbg_vai_error(struct req *req, struct v1l **v1lp, const char *msg)
{

	(void)req;
	(void)v1lp;
	(void)msg;
	INCOMPL();
}

static void dbg_vai_deliver_finish(struct req *req, struct v1l **v1lp, int err);
static void dbg_vai_deliverobj(struct worker *wrk, void *arg);
static void dbg_vai_lease(struct worker *wrk, void *arg);

static task_func_t *hack_http1_req = NULL;

// copied from cache_http_deliver.c, then split & modified
static enum vtr_deliver_e v_matchproto_(vtr_deliver_f)
dbg_vai_deliver(struct req *req, int sendbody)
{
	struct vrt_ctx ctx[1];
	struct v1l *v1l;
	int cap = 0;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_ORNULL(req->boc, BOC_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);

	if (req->doclose == SC_NULL &&
	    http_HdrIs(req->resp, H_Connection, "close")) {
		req->doclose = SC_RESP_CLOSE;
	} else if (req->doclose != SC_NULL) {
		if (!http_HdrIs(req->resp, H_Connection, "close")) {
			http_Unset(req->resp, H_Connection);
			http_SetHeader(req->resp, "Connection: close");
		}
	} else if (!http_GetHdr(req->resp, H_Connection, NULL))
		http_SetHeader(req->resp, "Connection: keep-alive");

	CHECK_OBJ_NOTNULL(req->wrk, WORKER_MAGIC);

	v1l = V1L_Open(req->ws, &req->sp->fd, req->vsl,
	    req->t_prev + SESS_TMO(req->sp, send_timeout),
	    cache_param->http1_iovs);

	if (v1l == NULL) {
		dbg_vai_error(req, &v1l, "Failure to init v1d "
		    "(workspace_thread overflow)");
		return (VTR_D_DONE);
	}

	// Do not roll back req->ws upon V1L_Close()
	V1L_NoRollback(v1l);

	while (sendbody) {
		if (!http_GetHdr(req->resp, H_Content_Length, NULL)) {
			if (req->http->protover == 11) {
				http_SetHeader(req->resp,
				    "Transfer-Encoding: chunked");
			} else {
				req->doclose = SC_TX_EOF;
			}
		}
		INIT_OBJ(ctx, VRT_CTX_MAGIC);
		VCL_Req2Ctx(ctx, req);
		cap = VDPIO_Upgrade(ctx, req->vdc);
		if (cap <= 0) {
			if (VDP_Push(ctx, req->vdc, req->ws, VDP_v1l, v1l)) {
				dbg_vai_error(req, &v1l, "Failure to push v1d");
				return (VTR_D_DONE);
			}
			break;
		}
		cap = VDPIO_Push(ctx, req->vdc, req->ws, &vdp_hello, NULL);
		if (cap < 1) {
			dbg_vai_error(req, &v1l, "Failure to push hello");
			return (VTR_D_DONE);
		}
		cap = VDPIO_Push(ctx, req->vdc, req->ws, VDP_v1l, v1l);
		if (cap < 1) {
			dbg_vai_error(req, &v1l, "Failure to push v1d (vdpio)");
			return (VTR_D_DONE);
		}
		break;
	}

	if (WS_Overflowed(req->ws)) {
		dbg_vai_error(req, &v1l, "workspace_client overflow");
		return (VTR_D_DONE);
	}

	if (WS_Overflowed(req->sp->ws)) {
		dbg_vai_error(req, &v1l, "workspace_session overflow");
		return (VTR_D_DONE);
	}

	if (WS_Overflowed(req->wrk->aws)) {
		dbg_vai_error(req, &v1l, "workspace_thread overflow");
		return (VTR_D_DONE);
	}

	req->acct.resp_hdrbytes += HTTP1_Write(v1l, req->resp, HTTP1_Resp);

	if (! sendbody) {
		dbg_vai_deliver_finish(req, &v1l, 0);
		return (VTR_D_DONE);
	}

	(void)V1L_Flush(v1l);

	if (hack_http1_req == NULL)
		hack_http1_req = req->task->func;
	AN(hack_http1_req);

	if (cap > 0) {
		VSLb(req->vsl, SLT_Debug, "w=%p scheduling dbg_vai_lease cap %d", req->wrk, cap);
		req->task->func = dbg_vai_lease;
	}
	else {
		VSLb(req->vsl, SLT_Debug, "w=%p scheduling dbg_vai_deliverobj", req->wrk);
		req->task->func = dbg_vai_deliverobj;
	}
	req->task->priv = req;

	req->wrk = NULL;
	req->vdc->wrk = NULL;
	req->transport_priv = v1l;

	AZ(Pool_Task(req->sp->pool, req->task, TASK_QUEUE_RUSH));
	return (VTR_D_DISEMBARK);
}

static void v_matchproto_(task_func_t)
dbg_vai_deliverobj(struct worker *wrk, void *arg)
{
	struct req *req;
	struct v1l *v1l;
	const char *p;
	int err, chunked;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);
	v1l = req->transport_priv;
	req->transport_priv = NULL;
	AN(v1l);

	THR_SetRequest(req);
	VSLb(req->vsl, SLT_Debug, "w=%p enter dbg_vai_deliverobj", wrk);
	AZ(req->wrk);
	CNT_Embark(wrk, req);
	req->vdc->wrk = wrk;	// move to CNT_Embark?

	chunked = http_GetHdr(req->resp, H_Transfer_Encoding, &p) && strcmp(p, "chunked") == 0;
	if (chunked)
		V1L_Chunked(v1l);
	err = VDP_DeliverObj(req->vdc, req->objcore);
	if (!err && chunked)
		V1L_EndChunk(v1l);
	dbg_vai_deliver_finish(req, &v1l, err);

	VSLb(req->vsl, SLT_Debug, "w=%p resuming http1_req", wrk);
	wrk->task->func = hack_http1_req;
	wrk->task->priv = req;
}

/*
 * copied from sml_notfiy
 */
struct dbg_vai_notify {
	unsigned		magic;
#define DBG_VAI_NOTIFY_MAGIC	0xa0154ed5
	unsigned		hasmore;
	pthread_mutex_t		mtx;
	pthread_cond_t		cond;
};

static void
dbg_vai_notify_init(struct dbg_vai_notify *sn)
{

	INIT_OBJ(sn, DBG_VAI_NOTIFY_MAGIC);
	AZ(pthread_mutex_init(&sn->mtx, NULL));
	AZ(pthread_cond_init(&sn->cond, NULL));
}

static void
dbg_vai_notify_fini(struct dbg_vai_notify *sn)
{

	CHECK_OBJ_NOTNULL(sn, DBG_VAI_NOTIFY_MAGIC);
	AZ(pthread_mutex_destroy(&sn->mtx));
	AZ(pthread_cond_destroy(&sn->cond));
}

static void v_matchproto_(vai_notify_cb)
dbg_vai_notify(vai_hdl hdl, void *priv)
{
	struct dbg_vai_notify *sn;

	(void) hdl;
	CAST_OBJ_NOTNULL(sn, priv, DBG_VAI_NOTIFY_MAGIC);
	AZ(pthread_mutex_lock(&sn->mtx));
	sn->hasmore = 1;
	AZ(pthread_cond_signal(&sn->cond));
	AZ(pthread_mutex_unlock(&sn->mtx));

}

static void
dbg_vai_notify_wait(struct dbg_vai_notify *sn)
{

	CHECK_OBJ_NOTNULL(sn, DBG_VAI_NOTIFY_MAGIC);
	AZ(pthread_mutex_lock(&sn->mtx));
	while (sn->hasmore == 0)
		AZ(pthread_cond_wait(&sn->cond, &sn->mtx));
	AN(sn->hasmore);
	sn->hasmore = 0;
	AZ(pthread_mutex_unlock(&sn->mtx));
}

static void
dbg_vai_lease_done(struct worker *wrk, struct req *req)
{
	VSLb(req->vsl, SLT_Debug, "w=%p resuming http1_req", wrk);
	wrk->task->func = hack_http1_req;
	wrk->task->priv = req;
}

static void v_matchproto_(task_func_t)
dbg_vai_lease(struct worker *wrk, void *arg)
{
	struct req *req;
	struct v1l *v1l;
	const char *p;
	unsigned flags = 0;
	int r, cap, err, chunked;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(req, arg, REQ_MAGIC);
	v1l = req->transport_priv;
	req->transport_priv = NULL;
	AN(v1l);

	THR_SetRequest(req);
	VSLb(req->vsl, SLT_Debug, "w=%p enter dbg_vai_lease", wrk);
	AZ(req->wrk);
	CNT_Embark(wrk, req);
	req->vdc->wrk = wrk;	// move to CNT_Embark?

	cap = req->vdc->retval;
	req->vdc->retval = 0;
	assert(cap > 0);

	VSCARAB_LOCAL(scarab, cap);
	VSCARET_LOCAL(scaret, cap);

	chunked = http_GetHdr(req->resp, H_Transfer_Encoding, &p) && strcmp(p, "chunked") == 0;
	if (chunked)
		V1L_Chunked(v1l);

	struct dbg_vai_notify notify;
	dbg_vai_notify_init(&notify);

	if (VDPIO_Init(req->vdc, req->objcore, req->ws, dbg_vai_notify, &notify, scaret)) {
		dbg_vai_notify_fini(&notify);
		dbg_vai_deliver_finish(req, &v1l, 1);
		dbg_vai_lease_done(wrk, req);
		return;
	}

	err = 0;
	do {
		r = vdpio_pull(req->vdc, NULL, scarab);
		flags = scarab->flags; // because vdpio_return_vscarab
		VSLb(req->vsl, SLT_Debug, "%d = vdpio_pull()", r);
		(void)V1L_Flush(v1l);
		vdpio_return_vscarab(req->vdc, scarab);

		if (r == -ENOBUFS || r == -EAGAIN) {
			VDPIO_Return(req->vdc);
			dbg_vai_notify_wait(&notify);
		}
		else if (r < 0) {
			err = r;
			break;
		}
	} while ((flags & VSCARAB_F_END) == 0);

	if (!err && chunked)
		V1L_EndChunk(v1l);
	dbg_vai_deliver_finish(req, &v1l, err);
	VDPIO_Fini(req->vdc);
	dbg_vai_notify_fini(&notify);
	dbg_vai_lease_done(wrk, req);
}

static void
dbg_vai_deliver_finish(struct req *req, struct v1l **v1lp, int err)
{
	stream_close_t sc;
	uint64_t bytes;

	sc = V1L_Close(v1lp, &bytes);

	if (req->vdc->vai_hdl != NULL)
		req->acct.resp_bodybytes += VDPIO_Close(req->vdc, req->objcore, req->boc);
	req->acct.resp_bodybytes += VDP_Close(req->vdc, req->objcore, req->boc);

	if (sc == SC_NULL && err && req->sp->fd >= 0)
		sc = SC_REM_CLOSE;
	if (sc != SC_NULL)
		Req_Fail(req, sc);
}

static struct transport DBG_transport;

void
debug_transport_vai_init(void)
{
	DBG_transport = HTTP1_transport;
	DBG_transport.name = "DBG VAI";
	DBG_transport.deliver = dbg_vai_deliver;
}

void
debug_transport_vai_use(VRT_CTX)
{
	struct req *req;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	req = ctx->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->transport != &HTTP1_transport) {
		VRT_fail(ctx, "Only works on built-in http1 transport");
		return;
	}
	AZ(req->transport_priv);
	req->transport = &DBG_transport;
}
