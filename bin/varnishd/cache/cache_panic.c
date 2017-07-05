/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
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

#include <execinfo.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "vtim.h"

#include "cache.h"
#include "cache_transport.h"

#include "cache_filter.h"
#include "common/heritage.h"

#include "vrt.h"
#include "cache_director.h"
#include "storage/storage.h"
#include "vcli_serve.h"

/*
 * The panic string is constructed in memory, then copied to the
 * shared memory.
 *
 * It can be extracted post-mortem from a core dump using gdb:
 *
 * (gdb) printf "%s", panicstr
 */

static struct vsb pan_vsb_storage, *pan_vsb;
static pthread_mutex_t panicstr_mtx;

static void pan_sess(struct vsb *, const struct sess *);

/*--------------------------------------------------------------------*/

const char *
body_status_2str(enum body_status e)
{
	switch (e) {
#define BODYSTATUS(U,l)	case BS_##U: return (#l);
#include "tbl/body_status.h"
	default:
		return ("?");
	}
}

/*--------------------------------------------------------------------*/

static const char *
reqbody_status_2str(enum req_body_state_e e)
{
	switch (e) {
#define REQ_BODY(U) case REQ_BODY_##U: return("R_BODY_" #U);
#include "tbl/req_body.h"
	default:
		return("?");
	}
}

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

const char *
sess_close_2str(enum sess_close sc, int want_desc)
{
	switch (sc) {
	case SC_NULL:		return(want_desc ? "(null)": "NULL");
#define SESS_CLOSE(nm, s, err, desc)			\
	case SC_##nm: return(want_desc ? desc : #nm);
#include "tbl/sess_close.h"

	default:		return(want_desc ? "(invalid)" : "INVALID");
	}
}

/*--------------------------------------------------------------------*/

#define N_ALREADY 64
static const void *already_list[N_ALREADY];
static int already_idx;

int
PAN_already(struct vsb *vsb, const void *ptr)
{
	int i;

	if (ptr == NULL) {
		VSB_printf(vsb, "},\n");
		return (1);
	}
	for (i = 0; i < already_idx; i++) {
		if (already_list[i] == ptr) {
			VSB_printf(vsb, "  [Already dumped, see above]\n");
			VSB_printf(vsb, "},\n");
			return (1);
		}
	}
	if (already_idx < N_ALREADY)
		already_list[already_idx++] = ptr;
	return (0);
}

/*--------------------------------------------------------------------*/

static void
pan_ws(struct vsb *vsb, const struct ws *ws)
{

	VSB_printf(vsb, "ws = %p {\n", ws);
	if (PAN_already(vsb, ws))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, ws, WS_MAGIC);
	if (ws->id[0] != '\0' && (!(ws->id[0] & 0x20)))
		VSB_printf(vsb, "OVERFLOWED ");
	VSB_printf(vsb, "id = \"%s\",\n", ws->id);
	VSB_printf(vsb, "{s, f, r, e} = {%p", ws->s);
	if (ws->f >= ws->s)
		VSB_printf(vsb, ", +%ld", (long) (ws->f - ws->s));
	else
		VSB_printf(vsb, ", %p", ws->f);
	if (ws->r >= ws->s)
		VSB_printf(vsb, ", +%ld", (long) (ws->r - ws->s));
	else
		VSB_printf(vsb, ", %p", ws->r);
	if (ws->e >= ws->s)
		VSB_printf(vsb, ", +%ld", (long) (ws->e - ws->s));
	else
		VSB_printf(vsb, ", %p", ws->e);
	VSB_printf(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_htc(struct vsb *vsb, const struct http_conn *htc)
{

	VSB_printf(vsb, "http_conn = %p {\n", htc);
	if (PAN_already(vsb, htc))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, htc, HTTP_CONN_MAGIC);
	if (htc->rfd != NULL)
		VSB_printf(vsb, "fd = %d (@%p),\n", *htc->rfd, htc->rfd);
	VSB_printf(vsb, "doclose = %s,\n", sess_close_2str(htc->doclose, 0));
	pan_ws(vsb, htc->ws);
	VSB_printf(vsb, "{rxbuf_b, rxbuf_e} = {%p, %p},\n",
	    htc->rxbuf_b, htc->rxbuf_e);
	VSB_printf(vsb, "{pipeline_b, pipeline_e} = {%p, %p},\n",
	    htc->pipeline_b, htc->pipeline_e);
	VSB_printf(vsb, "content_length = %jd,\n",
	    (intmax_t)htc->content_length);
	VSB_printf(vsb, "body_status = %s,\n",
	    body_status_2str(htc->body_status));
	VSB_printf(vsb, "first_byte_timeout = %f,\n",
	    htc->first_byte_timeout);
	VSB_printf(vsb, "between_bytes_timeout = %f,\n",
	    htc->between_bytes_timeout);
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_http(struct vsb *vsb, const char *id, const struct http *h)
{
	int i;

	VSB_printf(vsb, "http[%s] = %p {\n", id, h);
	if (PAN_already(vsb, h))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, h, HTTP_MAGIC);
	pan_ws(vsb, h->ws);
	VSB_printf(vsb, "hdrs {\n");
	VSB_indent(vsb, 2);
	for (i = 0; i < h->nhd; ++i) {
		if (h->hd[i].b == NULL && h->hd[i].e == NULL)
			continue;
		VSB_printf(vsb, "\"%.*s\",\n",
		    (int)(h->hd[i].e - h->hd[i].b), h->hd[i].b);
	}
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/
static void
pan_boc(struct vsb *vsb, const struct boc *boc)
{
	VSB_printf(vsb, "boc = %p {\n", boc);
	if (PAN_already(vsb, boc))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, boc, BOC_MAGIC);
	VSB_printf(vsb, "refcnt = %u,\n", boc->refcount);
	VSB_printf(vsb, "state = %s,\n", boc_state_2str(boc->state));
	VSB_printf(vsb, "vary = %p,\n", boc->vary);
	VSB_printf(vsb, "stevedore_priv = %p,\n", boc->stevedore_priv);
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_objcore(struct vsb *vsb, const char *typ, const struct objcore *oc)
{
	const char *p;

	VSB_printf(vsb, "objcore[%s] = %p {\n", typ, oc);
	if (PAN_already(vsb, oc))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, oc, OBJCORE_MAGIC);
	VSB_printf(vsb, "refcnt = %d,\n", oc->refcnt);
	VSB_printf(vsb, "flags = {");

/*lint -save -esym(438,p) -esym(838,p) -e539 */
	p = "";
#define OC_FLAG(U, l, v) \
	if (oc->flags & v) { VSB_printf(vsb, "%s" #l, p); p = ", "; }
#include "tbl/oc_flags.h"
	VSB_printf(vsb, "},\n");
	VSB_printf(vsb, "exp_flags = {");
	p = "";
#define OC_EXP_FLAG(U, l, v) \
	if (oc->exp_flags & v) { VSB_printf(vsb, "%s" #l, p); p = ", "; }
#include "tbl/oc_exp_flags.h"
/*lint -restore */
	VSB_printf(vsb, "},\n");

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
		VSB_printf(vsb, ")");
		if (oc->stobj->stevedore->panic) {
			VSB_printf(vsb, " {\n");
			VSB_indent(vsb, 2);
			oc->stobj->stevedore->panic(vsb, oc);
			VSB_indent(vsb, -2);
			VSB_printf(vsb, "}");
		}
	}
	VSB_printf(vsb, ",\n");
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_wrk(struct vsb *vsb, const struct worker *wrk)
{
	const char *hand;
	unsigned m, u;
	const char *p;

	VSB_printf(vsb, "worker = %p {\n", wrk);
	if (PAN_already(vsb, wrk))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, wrk, WORKER_MAGIC);
	VSB_printf(vsb, "stack = {0x%jx -> 0x%jx},\n",
	    (uintmax_t)wrk->stack_start, (uintmax_t)wrk->stack_end);
	pan_ws(vsb, wrk->aws);

	m = wrk->cur_method;
	VSB_printf(vsb, "VCL::method = ");
	if (m == 0) {
		VSB_printf(vsb, "none,\n");
		return;
	}
	if (!(m & 1))
		VSB_printf(vsb, "inside ");
	m &= ~1;
	hand = VCL_Method_Name(m);
	if (hand != NULL)
		VSB_printf(vsb, "%s,\n", hand);
	else
		VSB_printf(vsb, "0x%x,\n", m);

	hand = VCL_Return_Name(wrk->handling);
	if (hand != NULL)
		VSB_printf(vsb, "VCL::return = %s,\n", hand);
	else
		VSB_printf(vsb, "VCL::return = 0x%x,\n", wrk->handling);
	VSB_printf(vsb, "VCL::methods = {");
	m = wrk->seen_methods;
	p = "";
	for (u = 1; m ; u <<= 1) {
		if (m & u) {
			VSB_printf(vsb, "%s%s", p, VCL_Method_Name(u));
			m &= ~u;
			p = ", ";
		}
	}
	VSB_printf(vsb, "},\n");
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

static void
pan_busyobj(struct vsb *vsb, const struct busyobj *bo)
{
	struct vfp_entry *vfe;
	const char *p;

	VSB_printf(vsb, "busyobj = %p {\n", bo);
	if (PAN_already(vsb, bo))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, bo, BUSYOBJ_MAGIC);
	pan_ws(vsb, bo->ws);
	VSB_printf(vsb, "retries = %d, ", bo->retries);
	VSB_printf(vsb, "failed = %d, ", bo->vfc->failed);
	VSB_printf(vsb, "flags = {");
	p = "";
/*lint -save -esym(438,p) -e539 */
#define BO_FLAG(l, r, w, d) \
	if (bo->l) { VSB_printf(vsb, "%s" #l, p); p = ", "; }
#include "tbl/bo_flags.h"
/*lint -restore */
	VSB_printf(vsb, "},\n");

	if (VALID_OBJ(bo->htc, HTTP_CONN_MAGIC))
		pan_htc(vsb, bo->htc);

	if (!VTAILQ_EMPTY(&bo->vfc->vfp)) {
		VSB_printf(vsb, "filters =");
		VTAILQ_FOREACH(vfe, &bo->vfc->vfp, list)
			VSB_printf(vsb, " %s=%d",
			    vfe->vfp->name, (int)vfe->closed);
		VSB_printf(vsb, "\n");
	}

	VDI_Panic(bo->director_req, vsb, "director_req");
	if (bo->director_resp == bo->director_req)
		VSB_printf(vsb, "director_resp = director_req,\n");
	else
		VDI_Panic(bo->director_resp, vsb, "director_resp");
	if (bo->bereq != NULL && bo->bereq->ws != NULL)
		pan_http(vsb, "bereq", bo->bereq);
	if (bo->beresp != NULL && bo->beresp->ws != NULL)
		pan_http(vsb, "beresp", bo->beresp);
	if (bo->fetch_objcore)
		pan_objcore(vsb, "fetch", bo->fetch_objcore);
	if (bo->stale_oc)
		pan_objcore(vsb, "ims", bo->stale_oc);
	VCL_Panic(vsb, bo->vcl);
	VMOD_Panic(vsb);
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_req(struct vsb *vsb, const struct req *req)
{
	const char *stp;
	const struct transport *xp;

	VSB_printf(vsb, "req = %p {\n", req);
	if (PAN_already(vsb, req))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, req, REQ_MAGIC);
	xp = req->transport;
	VSB_printf(vsb, "vxid = %u, transport = %s", VXID(req->vsl->wid),
	    xp == NULL ? "NULL" : xp->name);

	if (xp != NULL && xp->req_panic != NULL) {
		VSB_printf(vsb, " {\n");
		VSB_indent(vsb, 2);
		xp->req_panic(vsb, req);
		VSB_indent(vsb, -2);
		VSB_printf(vsb, "}");
	}
	VSB_printf(vsb, "\n");
	switch (req->req_step) {
#define REQ_STEP(l, u, arg) case R_STP_##u: stp = "R_STP_" #u; break;
#include "tbl/steps.h"
		default: stp = NULL;
	}
	if (stp != NULL)
		VSB_printf(vsb, "step = %s,\n", stp);
	else
		VSB_printf(vsb, "step = 0x%x,\n", req->req_step);

	VSB_printf(vsb, "req_body = %s,\n",
	    reqbody_status_2str(req->req_body_status));

	if (req->err_code)
		VSB_printf(vsb,
		    "err_code = %d, err_reason = %s,\n", req->err_code,
		    req->err_reason ? req->err_reason : "(null)");

	VSB_printf(vsb, "restarts = %d, esi_level = %d,\n",
	    req->restarts, req->esi_level);

	if (req->sp != NULL)
		pan_sess(vsb, req->sp);

	if (req->wrk != NULL)
		pan_wrk(vsb, req->wrk);

	pan_ws(vsb, req->ws);
	if (VALID_OBJ(req->htc, HTTP_CONN_MAGIC))
		pan_htc(vsb, req->htc);
	pan_http(vsb, "req", req->http);
	if (req->resp->ws != NULL)
		pan_http(vsb, "resp", req->resp);

	VCL_Panic(vsb, req->vcl);
	VMOD_Panic(vsb);

	if (req->body_oc != NULL)
		pan_objcore(vsb, "BODY", req->body_oc);
	if (req->objcore != NULL)
		pan_objcore(vsb, "REQ", req->objcore);

	VSB_printf(vsb, "flags = {\n");
	VSB_indent(vsb, 2);
#define REQ_FLAG(l, r, w, d) if (req->l) VSB_printf(vsb, #l ",\n");
#include "tbl/req_flags.h"
	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");

	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_sess(struct vsb *vsb, const struct sess *sp)
{
	const char *ci;
	const char *cp;
	const struct transport *xp;

	VSB_printf(vsb, "sp = %p {\n", sp);
	if (PAN_already(vsb, sp))
		return;
	VSB_indent(vsb, 2);
	PAN_CheckMagic(vsb, sp, SESS_MAGIC);
	xp = XPORT_ByNumber(sp->sattr[SA_TRANSPORT]);
	VSB_printf(vsb, "fd = %d, vxid = %u,\n",
	    sp->fd, VXID(sp->vxid));
	VSB_printf(vsb, "t_open = %f,\n", sp->t_open);
	VSB_printf(vsb, "t_idle = %f,\n", sp->t_idle);
	VSB_printf(vsb, "transport = %s",
	    xp == NULL ? "<none>" : xp->name);
	if (xp != NULL && xp->sess_panic != NULL) {
		VSB_printf(vsb, " {\n");
		VSB_indent(vsb, 2);
		xp->sess_panic(vsb, sp);
		VSB_indent(vsb, -2);
		VSB_printf(vsb, "}");
	}
	VSB_printf(vsb, "\n");
	ci = SES_Get_String_Attr(sp, SA_CLIENT_IP);
	cp = SES_Get_String_Attr(sp, SA_CLIENT_PORT);
	VSB_printf(vsb, "client = %s %s,\n", ci, cp);

	pan_privs(vsb, sp->privs);

	VSB_indent(vsb, -2);
	VSB_printf(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

#define BACKTRACE_LEVELS	10

static void
pan_backtrace(struct vsb *vsb)
{
	void *array[BACKTRACE_LEVELS];
	size_t size;
	size_t i;
	char **strings;
	char *p;
	char buf[32];

	size = backtrace (array, BACKTRACE_LEVELS);
	if (size > BACKTRACE_LEVELS) {
		VSB_printf(vsb, "Backtrace not available (ret=%zu)\n", size);
		return;
	}
	VSB_printf(vsb, "Backtrace:\n");
	VSB_indent(vsb, 2);
	for (i = 0; i < size; i++) {
		bprintf(buf, "%p", array[i]);
		VSB_printf(vsb, "%s: ", buf);
		strings = backtrace_symbols(&array[i], 1);
		if (strings == NULL || strings[0] == NULL) {
			VSB_printf(vsb, "(?)");
		} else {
			p = strings[0];
			if (!memcmp(buf, p, strlen(buf))) {
				p += strlen(buf);
				if (*p == ':')
					p++;
				while (*p == ' ')
					p++;
			}
			VSB_printf(vsb, "%s", p);
		}
		VSB_printf (vsb, "\n");
	}
	VSB_indent(vsb, -2);
}

/*--------------------------------------------------------------------*/

static void __attribute__((__noreturn__))
pan_ic(const char *func, const char *file, int line, const char *cond,
    enum vas_e kind)
{
	const char *q;
	struct req *req;
	struct busyobj *bo;
	struct sigaction sa;
	int err = errno;

	AZ(pthread_mutex_lock(&panicstr_mtx)); /* Won't be released,
						  we're going to die
						  anyway */

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
		    "Missing errorhandling code in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
		break;
	case VAS_INCOMPLETE:
		VSB_printf(pan_vsb,
		    "Incomplete code in %s(), %s line %d:\n",
		    func, file, line);
		break;
	default:
	case VAS_ASSERT:
		VSB_printf(pan_vsb,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
		break;
	}
	VSB_printf(pan_vsb, "version = %s, vrt api = %u.%u\n",
	    VCS_version, VRT_MAJOR_VERSION, VRT_MINOR_VERSION);
	VSB_printf(pan_vsb, "ident = %s,%s\n",
	    VSB_data(vident) + 1, Waiter_GetName());
	VSB_printf(pan_vsb, "now = %f (mono), %f (real)\n",
	    VTIM_mono(), VTIM_real());

	pan_backtrace(pan_vsb);

	if (err)
		VSB_printf(pan_vsb, "errno = %d (%s)\n", err, strerror(err));

	q = THR_GetName();
	if (q != NULL)
		VSB_printf(pan_vsb, "thread = (%s)\n", q);

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
	} else {
		VSB_printf(pan_vsb,
		    "Feature short panic supressed details.\n");
	}
	VSB_printf(pan_vsb, "\n");
	VSB_putc(pan_vsb, '\0');	/* NUL termination */

	if (FEATURE(FEATURE_NO_COREDUMP))
		exit(4);
	else
		abort();
}

/*--------------------------------------------------------------------*/

static void __match_proto__(cli_func_t)
ccf_panic(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)av;
	AZ(priv);
	AZ(strcmp("", "You asked for it"));
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

	AZ(pthread_mutex_init(&panicstr_mtx, NULL));
	VAS_Fail = pan_ic;
	pan_vsb = &pan_vsb_storage;
	AN(heritage.panic_str);
	AN(heritage.panic_str_len);
	AN(VSB_new(pan_vsb, heritage.panic_str, heritage.panic_str_len,
	    VSB_FIXEDLEN));
	CLI_AddFuncs(debug_cmds);
}
