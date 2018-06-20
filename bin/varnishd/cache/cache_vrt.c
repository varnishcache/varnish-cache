/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Runtime support for compiled VCL programs
 */

#include "config.h"

#include "cache_varnishd.h"

#include "cache_objhead.h"
#include "vav.h"
#include "vcl.h"
#include "vrt_obj.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

#include "common/heritage.h"
#include "common/vsmw.h"

const void * const vrt_magic_string_end = &vrt_magic_string_end;
const void * const vrt_magic_string_unset = &vrt_magic_string_unset;

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_synth(VRT_CTX, VCL_INT code, VCL_STRING reason)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (code < 100 || code > 65535)
		code = 503;
	ctx->req->err_code = (uint16_t)code;
	ctx->req->err_reason = reason ? reason
	    : http_Status2Reason(ctx->req->err_code % 1000, NULL);
}

/*--------------------------------------------------------------------*/

void
VRT_acl_log(VRT_CTX, const char *msg)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(msg);
	if (ctx->vsl != NULL)
		VSLb(ctx->vsl, SLT_VCL_acl, "%s", msg);
	else
		VSL(SLT_VCL_acl, 0, "%s", msg);
}

int
VRT_acl_match(VRT_CTX, VCL_ACL acl, VCL_IP ip)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(acl, VRT_ACL_MAGIC);
	assert(VSA_Sane(ip));
	return (acl->match(ctx, ip));
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_hit_for_pass(VRT_CTX, VCL_DURATION d)
{
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->bo == NULL) {
		VSLb(ctx->vsl, SLT_Error,
		    "Note: Ignoring DURATION argument to return(pass);");
		return;
	}
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	oc = ctx->bo->fetch_objcore;
	oc->ttl = d;
	oc->grace = 0.0;
	oc->keep = 0.0;
	VSLb(ctx->vsl, SLT_TTL, "HFP %.0f %.0f %.0f %.0f uncacheable",
	    oc->ttl, oc->grace, oc->keep, oc->t_origin);
}

/*--------------------------------------------------------------------*/

VCL_HTTP
VRT_selecthttp(VRT_CTX, enum gethdr_e where)
{
	struct http *hp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	switch (where) {
	case HDR_REQ:
		hp = ctx->http_req;
		break;
	case HDR_REQ_TOP:
		hp = ctx->http_req_top;
		break;
	case HDR_BEREQ:
		hp = ctx->http_bereq;
		break;
	case HDR_BERESP:
		hp = ctx->http_beresp;
		break;
	case HDR_RESP:
		hp = ctx->http_resp;
		break;
	default:
		WRONG("VRT_selecthttp 'where' invalid");
	}
	return (hp);
}

/*--------------------------------------------------------------------*/

VCL_STRING
VRT_GetHdr(VRT_CTX, const struct gethdr_s *hs)
{
	const char *p;
	struct http *hp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (hs->where == HDR_OBJ) {
		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(ctx->req->objcore, OBJCORE_MAGIC);
		return(HTTP_GetHdrPack(ctx->req->wrk, ctx->req->objcore,
		    hs->what));
	}
	hp = VRT_selecthttp(ctx, hs->where);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (!http_GetHdr(hp, hs->what, &p))
		return (NULL);
	return (p);
}

/*--------------------------------------------------------------------
 * Build STRANDS from what is essentially a STRING_LIST
 */

VCL_STRANDS
VRT_BundleStrands(int n, struct strands *s, char const **d, const char *f, ...)
{
	va_list ap;

	s->n = n;
	s->p = d;
	*d++ = f;
	va_start(ap, f);
	while(--n)
		*d++ = va_arg(ap, const char *);
	assert(va_arg(ap, const char *) == vrt_magic_string_end);
	va_end(ap);
	return (s);
}

/*--------------------------------------------------------------------
 * Compare two STRANDS
 */

int
VRT_CompareStrands(VCL_STRANDS a, VCL_STRANDS b)
{
	const char *pa = NULL, *pb = NULL;
	int na = 0, nb = 0;

	while (1) {
		if (pa != NULL && *pa == '\0')
			pa = NULL;
		if (pb != NULL && *pb == '\0')
			pb = NULL;
		if (pa == NULL && na < a->n)
			pa = a->p[na++];
		else if (pb == NULL && nb < b->n)
			pb = b->p[nb++];
		else if (pa == NULL && pb == NULL)
			return (0);
		else if (pa == NULL)
			return (-1);
		else if (pb == NULL)
			return (1);
		else if (*pa == '\0')
			pa = NULL;
		else if (*pb == '\0')
			pb = NULL;
		else if (*pa != *pb)
			return (*pa - *pb);
		else {
			pa++;
			pb++;
		}
	}
}

/*--------------------------------------------------------------------
 * Collapse a STRING_LIST in the space provided, or return NULL
 */

char *
VRT_StringList(char *d, unsigned dl, const char *p, va_list ap)
{
	char *b, *e;
	unsigned x;

	b = d;
	e = b + dl;
	while (p != vrt_magic_string_end && b < e) {
		if (p != NULL && *p != '\0') {
			x = strlen(p);
			if (b + x < e)
				memcpy(b, p, x);
			b += x;
		}
		p = va_arg(ap, const char *);
	}
	if (b >= e)
		return (NULL);
	*b++ = '\0';
	return (b);
}

/*--------------------------------------------------------------------
 * Copy and merge a STRING_LIST into a workspace.
 */

const char *
VRT_String(struct ws *ws, const char *h, const char *p, va_list ap)
{
	char *b, *e;
	const char *q;
	unsigned u, x;
	va_list aq;

	u = WS_Reserve(ws, 0);
	e = b = ws->f;
	e += u;

	va_copy(aq, ap);
	do
		q = va_arg(aq, const char *);
	while (q == NULL || (q != vrt_magic_string_end && *q == '\0'));

	if (h != NULL && p == NULL && q == vrt_magic_string_end &&
	    WS_Inside(ws, h, NULL)) {
		va_end(aq);
		WS_Release(ws, 0);
		return (h);
	}

	if (h == NULL && p != NULL && q == vrt_magic_string_end &&
	    WS_Inside(ws, p, NULL)) {
		va_end(aq);
		WS_Release(ws, 0);
		return (p);
	}

	if (h == NULL && p == NULL) {
		if (q == vrt_magic_string_end) {
			va_end(aq);
			WS_Release(ws, 0);
			return ("");
		}
		do
			p = va_arg(aq, const char *);
		while (p == NULL || (p != vrt_magic_string_end && *p == '\0'));
		if (p == vrt_magic_string_end && WS_Inside(ws, q, NULL)) {
			va_end(aq);
			WS_Release(ws, 0);
			return (q);
		}
		p = NULL;
		va_end(aq);
	}

	if (h != NULL) {
		x = strlen(h);
		if (b + x < e)
			memcpy(b, h, x);
		b += x;
		if (b < e)
			*b = ' ';
		b++;
	}
	b = VRT_StringList(b, e > b ? e - b : 0, p, ap);
	if (b == NULL || b == e) {
		WS_MarkOverflow(ws);
		WS_Release(ws, 0);
		return (NULL);
	}
	e = b;
	b = ws->f;
	WS_Release(ws, e - b);
	return (b);
}

/*--------------------------------------------------------------------
 * Copy and merge a STRING_LIST on the current workspace
 */

VCL_STRING
VRT_CollectString(VRT_CTX, const char *p, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	va_start(ap, p);
	b = VRT_String(ctx->ws, NULL, p, ap);
	va_end(ap);
	if (b == NULL)
		VRT_fail(ctx, "Workspace overflow");
	return (b);
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_SetHdr(VRT_CTX , const struct gethdr_s *hs,
    const char *p, ...)
{
	struct http *hp;
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(hs);
	AN(hs->what);
	hp = VRT_selecthttp(ctx, hs->where);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	va_start(ap, p);
	if (p == vrt_magic_string_unset) {
		http_Unset(hp, hs->what);
	} else {
		b = VRT_String(hp->ws, hs->what + 1, p, ap);
		if (b == NULL) {
			VSLb(ctx->vsl, SLT_LostHeader, "%s", hs->what + 1);
		} else {
			http_Unset(hp, hs->what);
			http_SetHeader(hp, b);
		}
	}
	va_end(ap);
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_handling(VRT_CTX, unsigned hand)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->handling == NULL)
		return;
	assert(hand > 0);
	assert(hand < VCL_RET_MAX);
	// XXX:NOTYET assert(*ctx->handling == 0);
	*ctx->handling = hand;
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_fail(VRT_CTX, const char *fmt, ...)
{
	va_list ap;

	assert(ctx->vsl != NULL || ctx->msg != NULL);
	AZ(strchr(fmt, '\n'));
	va_start(ap, fmt);
	if (ctx->vsl != NULL)
		VSLbv(ctx->vsl, SLT_VCL_Error, fmt, ap);
	else {
		VSB_vprintf(ctx->msg, fmt, ap);
		VSB_putc(ctx->msg, '\n');
	}
	va_end(ap);
	VRT_handling(ctx, VCL_RET_FAIL);
}

/*--------------------------------------------------------------------
 * Feed data into the hash calculation
 */

VCL_VOID
VRT_hashdata(VRT_CTX, const char *str, ...)
{
	va_list ap;
	const char *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	AN(ctx->specific);
	HSH_AddString(ctx->req, ctx->specific, str);
	va_start(ap, str);
	while (1) {
		p = va_arg(ap, const char *);
		if (p == vrt_magic_string_end)
			break;
		HSH_AddString(ctx->req, ctx->specific, p);
	}
	va_end(ap);
	/*
	 * Add a 'field-separator' to make it more difficult to
	 * manipulate the hash.
	 */
	HSH_AddString(ctx->req, ctx->specific, NULL);
}

/*--------------------------------------------------------------------*/

VCL_TIME
VRT_r_now(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (ctx->now);
}

/*--------------------------------------------------------------------*/

VCL_STRING v_matchproto_()
VRT_IP_string(VRT_CTX, VCL_IP ip)
{
	char *p;
	unsigned len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ip == NULL)
		return (NULL);
	len = WS_Reserve(ctx->ws, 0);
	if (len == 0) {
		WS_Release(ctx->ws, 0);
		return (NULL);
	}
	p = ctx->ws->f;
	VTCP_name(ip, p, len, NULL, 0);
	WS_Release(ctx->ws, strlen(p) + 1);
	return (p);
}

VCL_STRING v_matchproto_()
VRT_INT_string(VRT_CTX, VCL_INT num)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (WS_Printf(ctx->ws, "%jd", (intmax_t)num));
}

VCL_STRING v_matchproto_()
VRT_REAL_string(VRT_CTX, VCL_REAL num)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (WS_Printf(ctx->ws, "%.3f", num));
}

VCL_STRING v_matchproto_()
VRT_TIME_string(VRT_CTX, VCL_TIME t)
{
	char *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	p = WS_Alloc(ctx->ws, VTIM_FORMAT_SIZE);
	if (p != NULL)
		VTIM_format(t, p);
	return (p);
}

VCL_STRING v_matchproto_()
VRT_BACKEND_string(VCL_BACKEND d)
{
	if (d == NULL)
		return (NULL);
	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	return (d->vcl_name);
}

VCL_STRING v_matchproto_()
VRT_BOOL_string(VCL_BOOL val)
{

	return (val ? "true" : "false");
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_Rollback(VRT_CTX, VCL_HTTP hp)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (hp == ctx->http_req) {
		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
		Req_Rollback(ctx->req);
	} else if (hp == ctx->http_bereq) {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		// -> VBO_Rollback ?
		VRTPRIV_dynamic_kill(ctx->bo->privs, (uintptr_t)ctx->bo);
		HTTP_Copy(ctx->bo->bereq, ctx->bo->bereq0);
		WS_Reset(ctx->bo->bereq->ws, ctx->bo->ws_bo);
		WS_Reset(ctx->bo->ws, ctx->bo->ws_bo);
	} else
		WRONG("VRT_Rollback 'hp' invalid");
}

/*--------------------------------------------------------------------*/

VCL_VOID
VRT_synth_page(VRT_CTX, const char *str, ...)
{
	va_list ap;
	const char *p;
	struct vsb *vsb;

	CAST_OBJ_NOTNULL(vsb, ctx->specific, VSB_MAGIC);
	va_start(ap, str);
	p = str;
	while (p != vrt_magic_string_end) {
		if (p == NULL)
			p = "(null)";
		if (VSB_cat(vsb, p)) {
			VRT_fail(ctx, "synthetic(): %s",
				 strerror(VSB_error(vsb)));
			break;
		}
		p = va_arg(ap, const char *);
	}
	va_end(ap);
}

/*--------------------------------------------------------------------*/

static struct VSC_main *
vrt_stats(VRT_CTX)
{
	struct worker *wrk;

	if (ctx->req) {
		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
		wrk = ctx->req->wrk;
	} else if (ctx->bo) {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		wrk = ctx->bo->wrk;
	} else {
		// XXX
		return (VSC_C_main);
	}

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	return (wrk->stats);
}

VCL_VOID
VRT_ban_string(VRT_CTX, VCL_STRING str)
{
	char *a1, *a2, *a3;
	char **av;
	struct ban_proto *bp;
	const char *err;
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(ctx->vsl);

	if (str == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error, "ban(): Null argument");
		return;
	}

	bp = BAN_Build();
	if (bp == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error, "ban(): Out of Memory");
		return;
	}
	av = VAV_Parse(str, NULL, ARGV_NOESC);
	AN(av);
	if (av[0] != NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error, "ban(): %s", av[0]);
		VAV_Free(av);
		BAN_Abandon(bp);
		return;
	}
	for (i = 0; ;) {
		a1 = av[++i];
		if (a1 == NULL) {
			VSLb(ctx->vsl, SLT_VCL_Error,
			    "ban(): No ban conditions found.");
			break;
		}
		a2 = av[++i];
		if (a2 == NULL) {
			VSLb(ctx->vsl, SLT_VCL_Error,
			    "ban(): Expected comparison operator.");
			break;
		}
		a3 = av[++i];
		if (a3 == NULL) {
			VSLb(ctx->vsl, SLT_VCL_Error,
			    "ban(): Expected second operand.");
			break;
		}
		err = BAN_AddTest(bp, a1, a2, a3);
		if (err) {
			VSLb(ctx->vsl, SLT_VCL_Error, "ban(): %s", err);
			break;
		}
		if (av[++i] == NULL) {
			err = BAN_Commit(bp, vrt_stats(ctx));
			if (err == NULL)
				bp = NULL;
			else
				VSLb(ctx->vsl, SLT_VCL_Error, "ban(): %s", err);
			break;
		}
		if (strcmp(av[i], "&&")) {
			VSLb(ctx->vsl, SLT_VCL_Error,
			    "ban(): Expected && between conditions,"
			    " found \"%s\"", av[i]);
			break;
		}
	}
	if (bp != NULL)
		BAN_Abandon(bp);
	VAV_Free(av);
}

VCL_BYTES
VRT_CacheReqBody(VRT_CTX, VCL_BYTES maxsize)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (ctx->method != VCL_MET_RECV) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		    "req.body can only be cached in vcl_recv{}");
		return (-1);
	}
	return (VRB_Cache(ctx->req, maxsize));
}

/*--------------------------------------------------------------------
 * purges
 */

VCL_INT
VRT_purge(VRT_CTX, VCL_DURATION ttl, VCL_DURATION grace, VCL_DURATION keep)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if ((ctx->method & (VCL_MET_HIT|VCL_MET_MISS)) == 0) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		    "purge can only happen in vcl_hit{} or vcl_miss{}");
		VRT_handling(ctx, VCL_RET_FAIL);
		return (0);
	}

	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->wrk, WORKER_MAGIC);
	return (HSH_Purge(ctx->req->wrk, ctx->req->objcore->objhead,
	    ctx->req->t_req, ttl, grace, keep));
}

/*--------------------------------------------------------------------
 */

struct vsmw_cluster * v_matchproto_()
VRT_VSM_Cluster_New(VRT_CTX, size_t sz)
{
	struct vsmw_cluster *vc;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(sz > 0);
	AN(vsc_lock);
	AN(vsc_unlock);
	AN(heritage.proc_vsmw);
	vsc_lock();
	vc = VSMW_NewCluster(heritage.proc_vsmw, sz, "VSC_cluster");
	vsc_unlock();
	return (vc);
}

void v_matchproto_()
VRT_VSM_Cluster_Destroy(VRT_CTX, struct vsmw_cluster **vsmcp)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(vsmcp);
	VSMW_DestroyCluster(heritage.proc_vsmw, vsmcp);
}

/*--------------------------------------------------------------------
 * Simple stuff
 */

int
VRT_strcmp(const char *s1, const char *s2)
{
	if (s1 == NULL || s2 == NULL)
		return(1);
	return (strcmp(s1, s2));
}

void
VRT_memmove(void *dst, const void *src, unsigned len)
{

	(void)memmove(dst, src, len);
}

VCL_BOOL
VRT_ipcmp(VCL_IP sua1, VCL_IP sua2)
{
	if (sua1 == NULL || sua2 == NULL)
		return(1);
	return (VSA_Compare_IP(sua1, sua2));
}

VCL_BLOB
VRT_blob(VRT_CTX, const char *err, const void *src, size_t len)
{
	struct vmod_priv *p;
	void *d;

	p = (void *)WS_Alloc(ctx->ws, sizeof *p);
	d = WS_Copy(ctx->ws, src, len);
	if (p == NULL || d == NULL) {
		VRT_fail(ctx, "Workspace overflow (%s)", err);
		return (NULL);
	}
	memset(p, 0, sizeof *p);
	p->len = len;
	p->priv = d;
	return (p);
}
