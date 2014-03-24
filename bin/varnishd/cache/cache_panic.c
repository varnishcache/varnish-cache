/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#ifndef HAVE_EXECINFO_H
#include "compat/execinfo.h"
#else
#include <execinfo.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "common/heritage.h"

#include "cache_backend.h"
#include "waiter/waiter.h"
#include "vcl.h"

/*
 * The panic string is constructed in memory, then copied to the
 * shared memory.
 *
 * It can be extracted post-mortem from a core dump using gdb:
 *
 * (gdb) printf "%s", panicstr
 */

static struct vsb pan_vsp_storage, *pan_vsp;
static pthread_mutex_t panicstr_mtx = PTHREAD_MUTEX_INITIALIZER;

static void pan_sess(const struct sess *sp);

/*--------------------------------------------------------------------*/

const char *
body_status_2str(enum body_status e)
{
	switch(e) {
#define BODYSTATUS(U,l)	case BS_##U: return (#l);
#include "tbl/body_status.h"
#undef BODYSTATUS
	default:
		return ("?");
	}
}

/*--------------------------------------------------------------------*/

const char *
reqbody_status_2str(enum req_body_state_e e)
{
	switch (e) {
#define REQ_BODY(U) case REQ_BODY_##U: return("R_BODY_" #U);
#include "tbl/req_body.h"
#undef REQ_BODY
	default:
		return("?");
	}
}

/*--------------------------------------------------------------------*/

const char *
sess_close_2str(enum sess_close sc, int want_desc)
{
	switch (sc) {
	case SC_NULL:		return(want_desc ? "(null)": "NULL");
#define SESS_CLOSE(nm, desc)	case SC_##nm: return(want_desc ? desc : #nm);
#include "tbl/sess_close.h"
#undef SESS_CLOSE

	default:		return(want_desc ? "(invalid)" : "INVALID");
	}
}

/*--------------------------------------------------------------------*/

static void
pan_ws(const struct ws *ws, int indent)
{

	VSB_printf(pan_vsp, "%*sws = %p {", indent, "", ws);
	if (!VALID_OBJ(ws, WS_MAGIC)) {
		if (ws != NULL)
			VSB_printf(pan_vsp, " BAD_MAGIC(0x%08x) ", ws->magic);
	} else {
		if (WS_Overflowed(ws))
			VSB_printf(pan_vsp, " OVERFLOW");
		VSB_printf(pan_vsp,
		    "\n%*sid = \"%s\",\n", indent + 2, "", ws->id);
		VSB_printf(pan_vsp,
		    "%*s{s,f,r,e} = {%p", indent + 2, "", ws->s);
		if (ws->f > ws->s)
			VSB_printf(pan_vsp, ",+%ld", (long) (ws->f - ws->s));
		else
			VSB_printf(pan_vsp, ",%p", ws->f);
		if (ws->r > ws->s)
			VSB_printf(pan_vsp, ",+%ld", (long) (ws->r - ws->s));
		else
			VSB_printf(pan_vsp, ",%p", ws->r);
		if (ws->e > ws->s)
			VSB_printf(pan_vsp, ",+%ld", (long) (ws->e - ws->s));
		else
			VSB_printf(pan_vsp, ",%p", ws->e);
	}
	VSB_printf(pan_vsp, "},\n");
	VSB_printf(pan_vsp, "%*s},\n", indent, "" );
}

/*--------------------------------------------------------------------*/

static void
pan_vbc(const struct vbc *vbc)
{

	struct backend *be;

	be = vbc->backend;

	VSB_printf(pan_vsp, "  backend = %p fd = %d {\n", be, vbc->fd);
	VSB_printf(pan_vsp, "    display_name = \"%s\",\n", be->display_name);
	VSB_printf(pan_vsp, "  },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_storage(const struct storage *st)
{
	int i, j;

#define MAX_BYTES (4*16)
#define show(ch) (((ch) > 31 && (ch) < 127) ? (ch) : '.')

	VSB_printf(pan_vsp, "      %u {\n", st->len);
	for (i = 0; i < MAX_BYTES && i < st->len; i += 16) {
		VSB_printf(pan_vsp, "        ");
		for (j = 0; j < 16; ++j) {
			if (i + j < st->len)
				VSB_printf(pan_vsp, "%02x ", st->ptr[i + j]);
			else
				VSB_printf(pan_vsp, "   ");
		}
		VSB_printf(pan_vsp, "|");
		for (j = 0; j < 16; ++j)
			if (i + j < st->len)
				VSB_printf(pan_vsp,
				    "%c", show(st->ptr[i + j]));
		VSB_printf(pan_vsp, "|\n");
	}
	if (st->len > MAX_BYTES)
		VSB_printf(pan_vsp,
		    "        [%u more]\n", st->len - MAX_BYTES);
	VSB_printf(pan_vsp, "      },\n");

#undef show
#undef MAX_BYTES
}

/*--------------------------------------------------------------------*/

static void
pan_http(const char *id, const struct http *h, int indent)
{
	int i;

	VSB_printf(pan_vsp, "%*shttp[%s] = {\n", indent, "", id);
	VSB_printf(pan_vsp, "%*sws = %p[%s]\n", indent + 2, "",
	    h->ws, h->ws ? h->ws->id : "");
	for (i = 0; i < h->nhd; ++i) {
		if (h->hd[i].b == NULL && h->hd[i].e == NULL)
			continue;
		VSB_printf(pan_vsp, "%*s\"%.*s\",\n", indent + 4, "",
		    (int)(h->hd[i].e - h->hd[i].b),
		    h->hd[i].b);
	}
	VSB_printf(pan_vsp, "%*s},\n", indent, "");
}


/*--------------------------------------------------------------------*/

static void
pan_object(const char *typ, const struct object *o)
{
	const struct storage *st;

	VSB_printf(pan_vsp, "  obj (%s) = %p {\n", typ, o);
	VSB_printf(pan_vsp, "    vxid = %u,\n", o->vxid);
	pan_http("obj", o->http, 4);
	VSB_printf(pan_vsp, "    len = %jd,\n", (intmax_t)o->len);
	VSB_printf(pan_vsp, "    store = {\n");
	VTAILQ_FOREACH(st, &o->store, list)
		pan_storage(st);
	VSB_printf(pan_vsp, "    },\n");
	VSB_printf(pan_vsp, "  },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_objcore(const char *typ, const struct objcore *oc)
{

	VSB_printf(pan_vsp, "  objcore (%s) = %p {\n", typ, oc);
	VSB_printf(pan_vsp, "    refcnt = %d\n", oc->refcnt);
	VSB_printf(pan_vsp, "    flags = 0x%x\n", oc->flags);
	VSB_printf(pan_vsp, "    objhead = %p\n", oc->objhead);
	VSB_printf(pan_vsp, "  }\n");
}

/*--------------------------------------------------------------------*/

static void
pan_vcl(const struct VCL_conf *vcl)
{
	int i;

	VSB_printf(pan_vsp, "  vcl = {\n");
	VSB_printf(pan_vsp, "    srcname = {\n");
	for (i = 0; i < vcl->nsrc; ++i)
		VSB_printf(pan_vsp, "      \"%s\",\n", vcl->srcname[i]);
	VSB_printf(pan_vsp, "    },\n");
	VSB_printf(pan_vsp, "  },\n");
}


/*--------------------------------------------------------------------*/

static void
pan_wrk(const struct worker *wrk)
{
	const char *hand;

	VSB_printf(pan_vsp, "  worker = %p {\n", wrk);
	pan_ws(wrk->aws, 4);

	hand = VCL_Method_Name(wrk->cur_method);
	if (hand != NULL)
		VSB_printf(pan_vsp, "  VCL::method = %s,\n", hand);
	else
		VSB_printf(pan_vsp, "  VCL::method = 0x%x,\n", wrk->cur_method);
	hand = VCL_Return_Name(wrk->handling);
	if (hand != NULL)
		VSB_printf(pan_vsp, "  VCL::return = %s,\n", hand);
	else
		VSB_printf(pan_vsp, "  VCL::return = 0x%x,\n", wrk->handling);
	VSB_printf(pan_vsp, "  },\n");
}

static void
pan_busyobj(const struct busyobj *bo)
{

	VSB_printf(pan_vsp, "  busyobj = %p {\n", bo);
	pan_ws(bo->ws, 4);
	VSB_printf(pan_vsp, "  refcnt = %u\n", bo->refcount);
	VSB_printf(pan_vsp, "  retries = %d\n", bo->retries);
	VSB_printf(pan_vsp, "  failed = %d\n", bo->failed);
	VSB_printf(pan_vsp, "  state = %d\n", (int)bo->state);
#define BO_FLAG(l, r, w, d) if(bo->l) VSB_printf(pan_vsp, "    is_" #l "\n");
#include "tbl/bo_flags.h"
#undef BO_FLAG

	VSB_printf(pan_vsp, "    bodystatus = %d (%s),\n",
	    bo->htc.body_status, body_status_2str(bo->htc.body_status));
	VSB_printf(pan_vsp, "    },\n");
	if (VALID_OBJ(bo->vbc, BACKEND_MAGIC))
		pan_vbc(bo->vbc);
	if (bo->bereq->ws != NULL)
		pan_http("bereq", bo->bereq, 4);
	if (bo->beresp->ws != NULL)
		pan_http("beresp", bo->beresp, 4);
	pan_ws(bo->ws_o, 4);
	if (bo->fetch_objcore)
		pan_objcore("FETCH", bo->fetch_objcore);
	if (bo->fetch_obj)
		pan_object("FETCH", bo->fetch_obj);
	if (bo->ims_obj)
		pan_object("IMS", bo->ims_obj);
	VSB_printf(pan_vsp, "  }\n");
}

/*--------------------------------------------------------------------*/

static void
pan_req(const struct req *req)
{
	const char *stp;

	VSB_printf(pan_vsp, "req = %p {\n", req);

	VSB_printf(pan_vsp, "  sp = %p, vxid = %u,", req->sp, req->vsl->wid);

	switch (req->req_step) {
#define REQ_STEP(l, u, arg) case R_STP_##u: stp = "R_STP_" #u; break;
#include "tbl/steps.h"
#undef REQ_STEP
		default: stp = NULL;
	}
	if (stp != NULL)
		VSB_printf(pan_vsp, "  step = %s,\n", stp);
	else
		VSB_printf(pan_vsp, "  step = 0x%x,\n", req->req_step);

	VSB_printf(pan_vsp, "  req_body = %s,\n",
	    reqbody_status_2str(req->req_body_status));

	if (req->err_code)
		VSB_printf(pan_vsp,
		    "  err_code = %d, err_reason = %s,\n", req->err_code,
		    req->err_reason ? req->err_reason : "(null)");

	VSB_printf(pan_vsp, "  restarts = %d, esi_level = %d\n",
	    req->restarts, req->esi_level);

	if (req->sp != NULL)
		pan_sess(req->sp);

	if (req->wrk != NULL)
		pan_wrk(req->wrk);

	pan_ws(req->ws, 2);
	pan_http("req", req->http, 2);
	if (req->resp->ws != NULL)
		pan_http("resp", req->resp, 2);

	if (VALID_OBJ(req->vcl, VCL_CONF_MAGIC))
		pan_vcl(req->vcl);

	if (VALID_OBJ(req->obj, OBJECT_MAGIC)) {
		if (req->obj->objcore->busyobj != NULL)
			pan_busyobj(req->obj->objcore->busyobj);
		pan_object("REQ", req->obj);
	}

	VSB_printf(pan_vsp, "},\n");
}

/*--------------------------------------------------------------------*/

static void
pan_sess(const struct sess *sp)
{
	const char *stp;

	VSB_printf(pan_vsp, "  sp = %p {\n", sp);
	VSB_printf(pan_vsp, "    fd = %d, vxid = %u,\n",
	    sp->fd, sp->vxid & VSL_IDENTMASK);
	VSB_printf(pan_vsp, "    client = %s %s,\n", sp->client_addr_str,
		sp->client_port_str);
	switch (sp->sess_step) {
#define SESS_STEP(l, u) case S_STP_##u: stp = "S_STP_" #u; break;
#include "tbl/steps.h"
#undef SESS_STEP
		default: stp = NULL;
	}
	if (stp != NULL)
		VSB_printf(pan_vsp, "    step = %s,\n", stp);
	else
		VSB_printf(pan_vsp, "    step = 0x%x,\n", sp->sess_step);

	VSB_printf(pan_vsp, "  },\n");
}

/*--------------------------------------------------------------------*/

static void
pan_backtrace(void)
{
	void *array[10];
	size_t size;
	size_t i;

	size = backtrace (array, 10);
	if (size == 0)
		return;
	VSB_printf(pan_vsp, "Backtrace:\n");
	for (i = 0; i < size; i++) {
		VSB_printf (pan_vsp, "  ");
		if (Symbol_Lookup(pan_vsp, array[i]) < 0) {
			char **strings;
			strings = backtrace_symbols(&array[i], 1);
			if (strings != NULL && strings[0] != NULL)
				VSB_printf(pan_vsp,
				     "%p: %s", array[i], strings[0]);
			else
				VSB_printf(pan_vsp, "%p: (?)", array[i]);
		}
		VSB_printf (pan_vsp, "\n");
	}
}

/*--------------------------------------------------------------------*/

static void __attribute__((__noreturn__))
pan_ic(const char *func, const char *file, int line, const char *cond,
    int err, enum vas_e kind)
{
	const char *q;
	struct req *req;
	struct busyobj *bo;

	AZ(pthread_mutex_lock(&panicstr_mtx)); /* Won't be released,
						  we're going to die
						  anyway */
	switch(kind) {
	case VAS_WRONG:
		VSB_printf(pan_vsp,
		    "Wrong turn at %s:%d:\n%s\n", file, line, cond);
		break;
	case VAS_VCL:
		VSB_printf(pan_vsp,
		    "Panic from VCL:\n  %s\n", cond);
		break;
	case VAS_MISSING:
		VSB_printf(pan_vsp,
		    "Missing errorhandling code in %s(), %s line %d:\n"
		    "  Condition(%s) not true.",
		    func, file, line, cond);
		break;
	case VAS_INCOMPLETE:
		VSB_printf(pan_vsp,
		    "Incomplete code in %s(), %s line %d:\n",
		    func, file, line);
		break;
	default:
	case VAS_ASSERT:
		VSB_printf(pan_vsp,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
		break;
	}
	if (err)
		VSB_printf(pan_vsp, "errno = %d (%s)\n", err, strerror(err));

	q = THR_GetName();
	if (q != NULL)
		VSB_printf(pan_vsp, "thread = (%s)\n", q);

	VSB_printf(pan_vsp, "ident = %s,%s\n",
	    VSB_data(vident) + 1, WAIT_GetName());

	pan_backtrace();

	if (!FEATURE(FEATURE_SHORT_PANIC)) {
		req = THR_GetRequest();
		if (req != NULL) {
			pan_req(req);
			VSL_Flush(req->vsl, 0);
		}
		bo = THR_GetBusyobj();
		if (bo != NULL) {
			pan_busyobj(bo);
			VSL_Flush(bo->vsl, 0);
		}
	}
	VSB_printf(pan_vsp, "\n");
	VSB_bcat(pan_vsp, "", 1);	/* NUL termination */

	if (FEATURE(FEATURE_NO_COREDUMP))
		exit(4);
	else
		abort();
}

/*--------------------------------------------------------------------*/

void
PAN_Init(void)
{

	VAS_Fail = pan_ic;
	pan_vsp = &pan_vsp_storage;
	AN(heritage.panic_str);
	AN(heritage.panic_str_len);
	AN(VSB_new(pan_vsp, heritage.panic_str, heritage.panic_str_len,
	    VSB_FIXEDLEN));
}
