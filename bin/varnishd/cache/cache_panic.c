/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "cache_varnishd.h"
#include "cache_transport.h"

#include "cache_filter.h"
#include "common/heritage.h"
#include "waiter/waiter.h"

#include "storage/storage.h"
#include "vcli_serve.h"
#include "vtim.h"
#include "vbt.h"
#include "vcs.h"
#include "vtcp.h"
#include "vsa.h"

/*
 * The panic string is constructed in a VSB, then copied to the
 * shared memory.
 *
 * It can be extracted post-mortem from a core dump using gdb:
 *
 * (gdb) p *(char **)((char *)pan_vsb+8)
 *
 */

static struct vsb pan_vsb_storage, *pan_vsb;
static pthread_mutex_t panicstr_mtx;

static void pan_sess(struct vsb *, const struct sess *);
static void pan_req(struct vsb *, const struct req *);

/*--------------------------------------------------------------------*/

static const char *
boc_state_2str(enum boc_state_e e)
{
	switch (e) {
#define BOC_STATE(U,l) case BOS_##U: return(#l);
#include "tbl/boc_state.h"
	default:
		return ("?");
	}
}

/*--------------------------------------------------------------------*/

static void
pan_stream_close(struct vsb *vsb, stream_close_t sc)
{

	if (sc != NULL && sc->magic == STREAM_CLOSE_MAGIC)
		VSB_printf(vsb, "%s(%s)", sc->name, sc->desc);
	else
		VSB_printf(vsb, "%p", sc);
}

/*--------------------------------------------------------------------*/

static void
pan_storage(struct vsb *vsb, const char *n, const struct stevedore *stv)
{

	if (stv != NULL && stv->magic == STEVEDORE_MAGIC)
		VSB_printf(vsb, "%s = %s(%s,%s),\n",
		    n, stv->name, stv->ident, stv->vclname);
	else
		VSB_printf(vsb, "%s = %p,\n", n, stv);
}

/*--------------------------------------------------------------------*/

#define N_ALREADY 256
static const void *already_list[N_ALREADY];
static int already_idx;

int
PAN__DumpStruct(struct vsb *vsb, int block, int track, const void *ptr,
    const char *smagic, unsigned magic, const char *fmt, ...)
{
	va_list ap;
	const unsigned *uptr;
	int i;

	AN(vsb);
	va_start(ap, fmt);
	VSB_vprintf(vsb, fmt, ap);
	va_end(ap);
	if (ptr == NULL) {
		VSB_cat(vsb, " = NULL\n");
		return (-1);
	}
	VSB_printf(vsb, " = %p {", ptr);
	if (block)
		VSB_putc(vsb, '\n');
	if (track) {
		for (i = 0; i < already_idx; i++) {
			if (already_list[i] == ptr) {
				VSB_cat(vsb, "  [Already dumped, see above]");
				if (block)
					VSB_putc(vsb, '\n');
				VSB_cat(vsb, "},\n");
				return (-2);
			}
		}
		if (already_idx < N_ALREADY)
			already_list[already_idx++] = ptr;
	}
	uptr = ptr;
	if (*uptr != magic) {
		VSB_printf(vsb, "  .magic = 0x%08x", *uptr);
		VSB_printf(vsb, " EXPECTED: %s=0x%08x", smagic, magic);
		if (block)
			VSB_putc(vsb, '\n');
		VSB_cat(vsb, "}\n");
		return (-3);
	}
	if (block)
		VSB_indent(vsb, 2);
	return (0);
}

/*--------------------------------------------------------------------*/

static void
pan_htc(struct vsb *vsb, const struct http_conn *htc)
{

	if (PAN_dump_struct(vsb, htc, HTTP_CONN_MAGIC, "http_conn"))
		return;
	if (htc->rfd != NULL)
		VSB_printf(vsb, "fd = %d (@%p),\n", *htc->rfd, htc->rfd);
	VSB_cat(vsb, "doclose = ");
	pan_stream_close(vsb, htc->doclose);
	VSB_cat(vsb, "\n");
	WS_Panic(vsb, htc->ws);
	VSB_printf(vsb, "{rxbuf_b, rxbuf_e} = {%p, %p},\n",
	    htc->rxbuf_b, htc->rxbuf_e);
	VSB_printf(vsb, "{pipeline_b, pipeline_e} = {%p, %p},\n",
	    htc->pipeline_b, htc->pipeline_e);
	VSB_printf(vsb, "content_length = %jd,\n",
	    (intmax_t)htc->content_length);
	VSB_printf(vsb, "body_status = %s,\n",
	    htc->body_status ? htc->body_status->name : "NULL");
	VSB_printf(vsb, "first_byte_timeout = %f,\n",
	    htc->first_byte_timeout);
	VSB_printf(vsb, "between_bytes_timeout = %f,\n",
	    htc->between_bytes_timeout);
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_http(struct vsb *vsb, const char *id, const struct http *h)
{
	int i;

	if (PAN_dump_struct(vsb, h, HTTP_MAGIC, "http[%s]", id))
		return;
	WS_Panic(vsb, h->ws);
	VSB_cat(vsb, "hdrs {\n");
	VSB_indent(vsb, 2);
	for (i = 0; i < h->nhd; ++i) {
		if (h->hd[i].b == NULL && h->hd[i].e == NULL)
			continue;
		VSB_printf(vsb, "\"%.*s\",\n",
		    (int)(h->hd[i].e - h->hd[i].b), h->hd[i].b);
	}
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_boc(struct vsb *vsb, const struct boc *boc)
{
	if (PAN_dump_struct(vsb, boc, BOC_MAGIC, "boc"))
		return;
	VSB_printf(vsb, "refcnt = %u,\n", boc->refcount);
	VSB_printf(vsb, "state = %s,\n", boc_state_2str(boc->state));
	VSB_printf(vsb, "vary = %p,\n", boc->vary);
	VSB_printf(vsb, "stevedore_priv = %p,\n", boc->stevedore_priv);
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_objcore(struct vsb *vsb, const char *typ, const struct objcore *oc)
{
	const char *p;

	if (PAN_dump_struct(vsb, oc, OBJCORE_MAGIC, "objcore[%s]", typ))
		return;
	VSB_printf(vsb, "refcnt = %d,\n", oc->refcnt);
	VSB_cat(vsb, "flags = {");

/*lint -save -esym(438,p) -esym(838,p) -e539 */
	p = "";
#define OC_FLAG(U, l, v) \
	if (oc->flags & v) { VSB_printf(vsb, "%s" #l, p); p = ", "; }
#include "tbl/oc_flags.h"
	VSB_cat(vsb, "},\n");
	VSB_cat(vsb, "exp_flags = {");
	p = "";
#define OC_EXP_FLAG(U, l, v) \
	if (oc->exp_flags & v) { VSB_printf(vsb, "%s" #l, p); p = ", "; }
#include "tbl/oc_exp_flags.h"
/*lint -restore */
	VSB_cat(vsb, "},\n");

	if (oc->boc != NULL)
		pan_boc(vsb, oc->boc);
	VSB_printf(vsb, "exp = {%f, %f, %f, %f},\n",
	    oc->t_origin, oc->ttl, oc->grace, oc->keep);
	VSB_printf(vsb, "objhead = %p,\n", oc->objhead);
	VSB_printf(vsb, "stevedore = %p", oc->stobj->stevedore);
	if (oc->stobj->stevedore != NULL) {
		VSB_printf(vsb, " (%s", oc->stobj->stevedore->name);
		if (strlen(oc->stobj->stevedore->ident))
			VSB_printf(vsb, " %s", oc->stobj->stevedore->ident);
		VSB_cat(vsb, ")");
		if (oc->stobj->stevedore->panic) {
			VSB_cat(vsb, " {\n");
			VSB_indent(vsb, 2);
			oc->stobj->stevedore->panic(vsb, oc);
			VSB_indent(vsb, -2);
			VSB_cat(vsb, "}");
		}
	}
	VSB_cat(vsb, ",\n");
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_wrk(struct vsb *vsb, const struct worker *wrk)
{
	const char *hand;
	unsigned m, u;
	const char *p;

	if (PAN_dump_struct(vsb, wrk, WORKER_MAGIC, "worker"))
		return;
	WS_Panic(vsb, wrk->aws);

	m = wrk->cur_method;
	VSB_cat(vsb, "VCL::method = ");
	if (m == 0) {
		VSB_cat(vsb, "none,\n");
		return;
	}
	if (!(m & 1))
		VSB_cat(vsb, "inside ");
	m &= ~1;
	hand = VCL_Method_Name(m);
	if (hand != NULL)
		VSB_printf(vsb, "%s,\n", hand);
	else
		VSB_printf(vsb, "0x%x,\n", m);

	VSB_cat(vsb, "VCL::methods = {");
	m = wrk->seen_methods;
	p = "";
	for (u = 1; m ; u <<= 1) {
		if (m & u) {
			VSB_printf(vsb, "%s%s", p, VCL_Method_Name(u));
			m &= ~u;
			p = ", ";
		}
	}
	VSB_cat(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

static void
pan_vfp(struct vsb *vsb, const struct vfp_ctx *vfc)
{
	struct vfp_entry *vfe;

	if (PAN_dump_struct(vsb, vfc, VFP_CTX_MAGIC, "vfc"))
		return;
	VSB_printf(vsb, "failed = %d,\n", vfc->failed);
	VSB_printf(vsb, "req = %p,\n", vfc->req);
	VSB_printf(vsb, "resp = %p,\n", vfc->resp);
	VSB_printf(vsb, "wrk = %p,\n", vfc->wrk);
	VSB_printf(vsb, "oc = %p,\n", vfc->oc);

	if (!VTAILQ_EMPTY(&vfc->vfp)) {
		VSB_cat(vsb, "filters = {\n");
		VSB_indent(vsb, 2);
		VTAILQ_FOREACH(vfe, &vfc->vfp, list) {
			VSB_printf(vsb, "%s = %p {\n", vfe->vfp->name, vfe);
			VSB_indent(vsb, 2);
			VSB_printf(vsb, "priv1 = %p,\n", vfe->priv1);
			VSB_printf(vsb, "priv2 = %zd,\n", vfe->priv2);
			VSB_printf(vsb, "closed = %d\n", vfe->closed);
			VSB_indent(vsb, -2);
			VSB_cat(vsb, "},\n");
		}
		VSB_indent(vsb, -2);
		VSB_cat(vsb, "},\n");
	}

	VSB_printf(vsb, "obj_flags = 0x%x,\n", vfc->obj_flags);
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

static void
pan_busyobj(struct vsb *vsb, const struct busyobj *bo)
{
	const char *p;
	const struct worker *wrk;

	if (PAN_dump_struct(vsb, bo, BUSYOBJ_MAGIC, "busyobj"))
		return;
	VSB_printf(vsb, "end = %p,\n", bo->end);
	VSB_printf(vsb, "retries = %u,\n", bo->retries);

	if (bo->req != NULL)
		pan_req(vsb, bo->req);
	if (bo->sp != NULL)
		pan_sess(vsb, bo->sp);
	wrk = bo->wrk;
	if (wrk != NULL)
		pan_wrk(vsb, wrk);

	if (bo->vfc != NULL)
		pan_vfp(vsb, bo->vfc);
	if (bo->vfp_filter_list != NULL) {
		VSB_printf(vsb, "vfp_filter_list = \"%s\",\n",
		    bo->vfp_filter_list);
	}

	WS_Panic(vsb, bo->ws);
	VSB_printf(vsb, "ws_bo = %p,\n", (void *)bo->ws_bo);

	// bereq0 left out
	if (bo->bereq != NULL && bo->bereq->ws != NULL)
		pan_http(vsb, "bereq", bo->bereq);
	if (bo->beresp != NULL && bo->beresp->ws != NULL)
		pan_http(vsb, "beresp", bo->beresp);
	if (bo->stale_oc)
		pan_objcore(vsb, "stale_oc", bo->stale_oc);
	if (bo->fetch_objcore)
		pan_objcore(vsb, "fetch", bo->fetch_objcore);

	if (VALID_OBJ(bo->htc, HTTP_CONN_MAGIC))
		pan_htc(vsb, bo->htc);

	// fetch_task left out

	VSB_cat(vsb, "flags = {");
	p = "";
/*lint -save -esym(438,p) -e539 */
#define BERESP_FLAG(l, r, w, f, d)				\
	if (bo->l) { VSB_printf(vsb, "%s" #l, p); p = ", "; }
#define BEREQ_FLAG(l, r, w, d) BERESP_FLAG(l, r, w, 0, d)
#include "tbl/bereq_flags.h"
#include "tbl/beresp_flags.h"
/*lint -restore */
	VSB_cat(vsb, "},\n");

	// timeouts/timers/acct/storage left out

	pan_storage(vsb, "storage", bo->storage);
	VDI_Panic(bo->director_req, vsb, "director_req");
	if (bo->director_resp == bo->director_req)
		VSB_cat(vsb, "director_resp = director_req,\n");
	else
		VDI_Panic(bo->director_resp, vsb, "director_resp");
	VCL_Panic(vsb, "vcl", bo->vcl);
	if (wrk != NULL)
		VPI_Panic(vsb, wrk->vpi, bo->vcl);

	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_top(struct vsb *vsb, const struct reqtop *top)
{
	if (PAN_dump_struct(vsb, top, REQTOP_MAGIC, "top"))
		return;
	pan_req(vsb, top->topreq);
	pan_privs(vsb, top->privs);
	VCL_Panic(vsb, "vcl0", top->vcl0);
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_req(struct vsb *vsb, const struct req *req)
{
	const struct transport *xp;
	const struct worker *wrk;

	if (PAN_dump_struct(vsb, req, REQ_MAGIC, "req"))
		return;
	xp = req->transport;
	VSB_printf(vsb, "vxid = %ju, transport = %s", VXID(req->vsl->wid),
	    xp == NULL ? "NULL" : xp->name);

	if (xp != NULL && xp->req_panic != NULL) {
		VSB_cat(vsb, " {\n");
		VSB_indent(vsb, 2);
		xp->req_panic(vsb, req);
		VSB_indent(vsb, -2);
		VSB_cat(vsb, "}");
	}
	VSB_cat(vsb, "\n");
	if (req->req_step == NULL)
		VSB_cat(vsb, "step = R_STP_TRANSPORT\n");
	else
		VSB_printf(vsb, "step = %s\n", req->req_step->name);

	VSB_printf(vsb, "req_body = %s,\n",
	    req->req_body_status ? req->req_body_status->name : "NULL");

	if (req->err_code)
		VSB_printf(vsb,
		    "err_code = %d, err_reason = %s,\n", req->err_code,
		    req->err_reason ? req->err_reason : "(null)");

	VSB_printf(vsb, "restarts = %u, esi_level = %u,\n",
	    req->restarts, req->esi_level);

	VSB_printf(vsb, "vary_b = %p, vary_e = %p,\n",
	    req->vary_b, req->vary_e);

	VSB_printf(vsb, "d_ttl = %f, d_grace = %f,\n",
	    req->d_ttl, req->d_grace);

	pan_storage(vsb, "storage", req->storage);

	VDI_Panic(req->director_hint, vsb, "director_hint");

	if (req->sp != NULL)
		pan_sess(vsb, req->sp);

	wrk = req->wrk;
	if (wrk != NULL)
		pan_wrk(vsb, wrk);

	WS_Panic(vsb, req->ws);
	if (VALID_OBJ(req->htc, HTTP_CONN_MAGIC))
		pan_htc(vsb, req->htc);
	pan_http(vsb, "req", req->http);
	if (req->resp != NULL && req->resp->ws != NULL)
		pan_http(vsb, "resp", req->resp);
	if (req->vdc != NULL)
		VDP_Panic(vsb, req->vdc);

	VCL_Panic(vsb, "vcl", req->vcl);
	if (wrk != NULL)
		VPI_Panic(vsb, wrk->vpi, req->vcl);

	if (req->body_oc != NULL)
		pan_objcore(vsb, "BODY", req->body_oc);
	if (req->objcore != NULL)
		pan_objcore(vsb, "REQ", req->objcore);

	VSB_cat(vsb, "flags = {\n");
	VSB_indent(vsb, 2);
#define REQ_FLAG(l, r, w, d) if (req->l) VSB_printf(vsb, #l ",\n");
#include "tbl/req_flags.h"
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");

	pan_privs(vsb, req->privs);

	if (req->top != NULL)
		pan_top(vsb, req->top);

	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

#define pan_addr(vsb, sp, field) do {					\
		struct suckaddr *sa;					\
		char h[VTCP_ADDRBUFSIZE];				\
		char p[VTCP_PORTBUFSIZE];				\
									\
		(void) SES_Get_##field##_addr((sp), &sa);		\
		if (! VSA_Sane(sa))					\
			break;						\
		VTCP_name(sa, h, sizeof h, p, sizeof p);		\
		VSB_printf((vsb), "%s.ip = %s:%s,\n", #field, h, p);	\
	} while (0)

static void
pan_sess(struct vsb *vsb, const struct sess *sp)
{
	const char *ci;
	const char *cp;
	const struct transport *xp;

	if (PAN_dump_struct(vsb, sp, SESS_MAGIC, "sess"))
		return;
	VSB_printf(vsb, "fd = %d, vxid = %ju,\n",
	    sp->fd, VXID(sp->vxid));
	VSB_printf(vsb, "t_open = %f,\n", sp->t_open);
	VSB_printf(vsb, "t_idle = %f,\n", sp->t_idle);

	if (! VALID_OBJ(sp, SESS_MAGIC)) {
		VSB_indent(vsb, -2);
		VSB_cat(vsb, "},\n");
		return;
	}

	WS_Panic(vsb, sp->ws);
	xp = XPORT_ByNumber(sp->sattr[SA_TRANSPORT]);
	VSB_printf(vsb, "transport = %s",
	    xp == NULL ? "<none>" : xp->name);
	if (xp != NULL && xp->sess_panic != NULL) {
		VSB_cat(vsb, " {\n");
		VSB_indent(vsb, 2);
		xp->sess_panic(vsb, sp);
		VSB_indent(vsb, -2);
		VSB_cat(vsb, "}");
	}
	VSB_cat(vsb, "\n");

	// duplicated below, remove ?
	ci = SES_Get_String_Attr(sp, SA_CLIENT_IP);
	cp = SES_Get_String_Attr(sp, SA_CLIENT_PORT);
	if (VALID_OBJ(sp->listen_sock, LISTEN_SOCK_MAGIC))
		VSB_printf(vsb, "client = %s %s %s,\n", ci, cp,
			   sp->listen_sock->endpoint);
	else
		VSB_printf(vsb, "client = %s %s <unknown>\n", ci, cp);

	if (VALID_OBJ(sp->listen_sock, LISTEN_SOCK_MAGIC)) {
		VSB_printf(vsb, "local.endpoint = %s,\n",
			   sp->listen_sock->endpoint);
		VSB_printf(vsb, "local.socket = %s,\n",
			   sp->listen_sock->name);
	}
	pan_addr(vsb, sp, local);
	pan_addr(vsb, sp, remote);
	pan_addr(vsb, sp, server);
	pan_addr(vsb, sp, client);

	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_backtrace(struct vsb *vsb)
{

	VSB_cat(vsb, "Backtrace:\n");
	VSB_indent(vsb, 2);
	VBT_format(vsb);
	VSB_indent(vsb, -2);
}

#ifdef HAVE_PTHREAD_GETATTR_NP
static void
pan_threadattr(struct vsb *vsb)
{
	pthread_attr_t attr[1];
	size_t sz;
	void *addr;

	if (pthread_getattr_np(pthread_self(), attr) != 0)
		return;

	VSB_cat(vsb, "pthread.attr = {\n");
	VSB_indent(vsb, 2);

	if (pthread_attr_getguardsize(attr, &sz) == 0)
		VSB_printf(vsb, "guard = %zu,\n", sz);
	if (pthread_attr_getstack(attr, &addr, &sz) == 0) {
		VSB_printf(vsb, "stack_bottom = %p,\n", addr);
		VSB_printf(vsb, "stack_top = %p,\n", (char *)addr + sz);
		VSB_printf(vsb, "stack_size = %zu,\n", sz);
	}
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "}\n");
	(void) pthread_attr_destroy(attr);
}
#endif

static void
pan_argv(struct vsb *vsb)
{
	int i;

	VSB_cat(pan_vsb, "argv = {\n");
	VSB_indent(vsb, 2);
	for (i = 0; i < heritage.argc; i++) {
		VSB_printf(vsb, "[%d] = ", i);
		VSB_quote(vsb, heritage.argv[i], -1, VSB_QUOTE_CSTR);
		VSB_cat(vsb, ",\n");
	}
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "}\n");

}
/*--------------------------------------------------------------------*/

static void __attribute__((__noreturn__))
pan_ic(const char *func, const char *file, int line, const char *cond,
    enum vas_e kind)
{
	const char *q;
	struct req *req;
	struct busyobj *bo;
	struct worker *wrk;
	struct sigaction sa;
	int i, err = errno;

	if (pthread_getspecific(panic_key) != NULL) {
		VSB_cat(pan_vsb, "\n\nPANIC REENTRANCY\n\n");
		abort();
	}

	/* If we already panicking in another thread, do nothing */
	do {
		i = pthread_mutex_trylock(&panicstr_mtx);
		if (i != 0)
			sleep (1);
	} while (i != 0);

	assert (VSB_len(pan_vsb) == 0);

	AZ(pthread_setspecific(panic_key, pan_vsb));

	/*
	 * should we trigger a SIGSEGV while handling a panic, our sigsegv
	 * handler would hide the panic, so we need to reset the handler to
	 * default
	 */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_DFL;
	(void)sigaction(SIGSEGV, &sa, NULL);
	/* Set SIGABRT back to default so the final abort() has the
	   desired effect */
	(void)sigaction(SIGABRT, &sa, NULL);

	switch (kind) {
	case VAS_WRONG:
		VSB_printf(pan_vsb,
		    "Wrong turn at %s:%d:\n%s\n", file, line, cond);
		break;
	case VAS_VCL:
		VSB_printf(pan_vsb,
		    "Panic from VCL:\n  %s\n", cond);
		break;
	case VAS_MISSING:
		VSB_printf(pan_vsb,
		    "Missing error handling code in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
		break;
	case VAS_INCOMPLETE:
		VSB_printf(pan_vsb,
		    "Incomplete code in %s(), %s line %d:\n",
		    func, file, line);
		break;
	case VAS_ASSERT:
	default:
		VSB_printf(pan_vsb,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
		break;
	}
	VSB_printf(pan_vsb, "version = %s, vrt api = %u.%u\n",
	    VCS_String("V"), VRT_MAJOR_VERSION, VRT_MINOR_VERSION);
	VSB_printf(pan_vsb, "ident = %s,%s\n",
	    heritage.ident, Waiter_GetName());
	VSB_printf(pan_vsb, "now = %f (mono), %f (real)\n",
	    VTIM_mono(), VTIM_real());

	pan_backtrace(pan_vsb);

	if (err)
		VSB_printf(pan_vsb, "errno = %d (%s)\n", err, VAS_errtxt(err));

	pan_argv(pan_vsb);

	VSB_printf(pan_vsb, "pthread.self = %p\n", TRUST_ME(pthread_self()));

	q = THR_GetName();
	if (q != NULL)
		VSB_printf(pan_vsb, "pthread.name = (%s)\n", q);

#ifdef HAVE_PTHREAD_GETATTR_NP
	pan_threadattr(pan_vsb);
#endif

	if (!FEATURE(FEATURE_SHORT_PANIC)) {
		req = THR_GetRequest();
		VSB_cat(pan_vsb, "thr.");
		pan_req(pan_vsb, req);
		if (req != NULL)
			VSL_Flush(req->vsl, 0);
		bo = THR_GetBusyobj();
		VSB_cat(pan_vsb, "thr.");
		pan_busyobj(pan_vsb, bo);
		if (bo != NULL)
			VSL_Flush(bo->vsl, 0);
		wrk = THR_GetWorker();
		VSB_cat(pan_vsb, "thr.");
		pan_wrk(pan_vsb, wrk);
		VMOD_Panic(pan_vsb);
		pan_pool(pan_vsb);
	} else {
		VSB_cat(pan_vsb, "Feature short panic suppressed details.\n");
	}
	VSB_cat(pan_vsb, "\n");
	VSB_putc(pan_vsb, '\0');	/* NUL termination */

	v_gcov_flush();

	/*
	 * Do a little song and dance for static checkers which
	 * are not smart enough to figure out that calling abort()
	 * with a mutex held is OK and probably very intentional.
	 */
	if (pthread_getspecific(panic_key))	/* ie: always */
		abort();
	PTOK(pthread_mutex_unlock(&panicstr_mtx));
	abort();
}

/*--------------------------------------------------------------------*/

static void v_noreturn_ v_matchproto_(cli_func_t)
ccf_panic(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)av;
	AZ(priv);
	AZ(strcmp("", "You asked for it"));
	/* NOTREACHED */
	abort();
}

/*--------------------------------------------------------------------*/

static struct cli_proto debug_cmds[] = {
	{ CLICMD_DEBUG_PANIC_WORKER,		"d",	ccf_panic },
	{ NULL }
};

/*--------------------------------------------------------------------*/

void
PAN_Init(void)
{

	PTOK(pthread_mutex_init(&panicstr_mtx, &mtxattr_errorcheck));
	VAS_Fail_Func = pan_ic;
	pan_vsb = &pan_vsb_storage;
	AN(heritage.panic_str);
	AN(heritage.panic_str_len);
	AN(VSB_init(pan_vsb, heritage.panic_str, heritage.panic_str_len));
	VSB_cat(pan_vsb, "This is a test\n");
	AZ(VSB_finish(pan_vsb));
	VSB_clear(pan_vsb);
	heritage.panic_str[0] = '\0';
	CLI_AddFuncs(debug_cmds);
}
