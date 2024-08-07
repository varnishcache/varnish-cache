/*-
 * Copyright (c) 2010-2017 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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

#include <sys/stat.h>

#include <netinet/in.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <fnmatch.h>

#include "cache/cache.h"

#include "vrnd.h"
#include "vtcp.h"
#include "vsa.h"
#include "vtim.h"
#include "vcl.h"

#include "vcc_std_if.h"

VCL_VOID v_matchproto_(td_std_set_ip_tos)
vmod_set_ip_tos(VRT_CTX, VCL_INT tos)
{
	struct suckaddr *sa;
	int fam, itos = tos;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AZ(SES_Get_local_addr(ctx->req->sp, &sa));
	/* Silently ignore for non-IP addresses. */
	if (VSA_Compare(sa, bogo_ip) == 0)
		return;
	fam = VSA_Get_Proto(sa);
	switch (fam) {
	case PF_INET:
		VTCP_Assert(setsockopt(ctx->req->sp->fd,
		    IPPROTO_IP, IP_TOS, &itos, sizeof(itos)));
		break;
	case PF_INET6:
		VTCP_Assert(setsockopt(ctx->req->sp->fd,
		    IPPROTO_IPV6, IPV6_TCLASS, &itos, sizeof(itos)));
		break;
	default:
		INCOMPL();
	}
}

static const char *
vmod_updown(VRT_CTX, int up, VCL_STRANDS s)
{
	unsigned u;
	char *b, *e;
	const char *p;
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	u = WS_ReserveAll(ctx->ws);
	e = b = WS_Reservation(ctx->ws);
	e += u;
	for (i = 0; i < s->n && b < e; i++) {
		p = s->p[i];
		while (p != NULL && *p != '\0' && b < e) {
			if (up)
				*b++ = (char)toupper(*p++);
			else
				*b++ = (char)tolower(*p++);
		}
	}
	if (b < e)
		*b = '\0';
	b++;
	if (b > e) {
		WS_MarkOverflow(ctx->ws);
		WS_Release(ctx->ws, 0);
		return (NULL);
	} else {
		e = b;
		b = WS_Reservation(ctx->ws);
		WS_Release(ctx->ws, e - b);
		return (b);
	}
}

VCL_STRING v_matchproto_(td_std_toupper)
vmod_toupper(VRT_CTX, VCL_STRANDS s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (vmod_updown(ctx, 1, s));
}

VCL_STRING v_matchproto_(td_std_tolower)
vmod_tolower(VRT_CTX, VCL_STRANDS s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (vmod_updown(ctx, 0, s));
}

VCL_REAL v_matchproto_(td_std_random)
vmod_random(VRT_CTX, VCL_REAL lo, VCL_REAL hi)
{
	double a;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	a = VRND_RandomTestableDouble();
	a *= hi - lo;
	a += lo;
	return (a);
}

VCL_VOID v_matchproto_(td_std_log)
vmod_log(VRT_CTX, VCL_STRANDS s)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (ctx->vsl != NULL)
		VSLbs(ctx->vsl, SLT_VCL_Log, s);
	else
		VSLs(SLT_VCL_Log, NO_VXID, s);
}

/* XXX use vsyslog() ? */
VCL_VOID v_matchproto_(td_std_syslog)
vmod_syslog(VRT_CTX, VCL_INT fac, VCL_STRANDS s)
{
	const char *p;
	uintptr_t sn;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	sn = WS_Snapshot(ctx->ws);
	p = VRT_StrandsWS(ctx->ws, NULL, s);
	if (p != NULL)
		syslog((int)fac, "%s", p);
	WS_Reset(ctx->ws, sn);
}

VCL_BOOL v_matchproto_(td_std_file_exists)
vmod_file_exists(VRT_CTX, VCL_STRING file_name)
{
	struct stat st;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (stat(file_name, &st) == 0);
}

VCL_VOID v_matchproto_(td_std_collect)
vmod_collect(VRT_CTX, VCL_HEADER hdr, VCL_STRING sep)
{
	struct http *hp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (hdr == NULL) {
		VRT_fail(ctx, "std.collect(): header argument is NULL");
		return;
	}
	hp = VRT_selecthttp(ctx, hdr->where);
	if (hp == NULL) {
		VRT_fail(ctx, "std.collect(): header argument "
		    "cannot be used here");
		return;
	}
	http_CollectHdrSep(hp, hdr->what, sep);
}

VCL_BOOL v_matchproto_(td_std_healthy)
vmod_healthy(VRT_CTX, VCL_BACKEND be)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_ORNULL(be, DIRECTOR_MAGIC);
	return (VRT_Healthy(ctx, be, NULL));
}

VCL_INT v_matchproto_(td_std_port)
vmod_port(VRT_CTX, VCL_IP ip)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ip == NULL)
		return (0);
	return (VSA_Port(ip));
}

VCL_VOID v_matchproto_(td_std_rollback)
vmod_rollback(VRT_CTX, VCL_HTTP hp)
{
	VRT_Rollback(ctx, hp);
}

VCL_VOID v_matchproto_(td_std_timestamp)
vmod_timestamp(VRT_CTX, VCL_STRING label)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (label == NULL)
		return;
	if (*label == '\0')
		return;
	if (ctx->bo != NULL && ctx->req == NULL) {
		/* Called from backend vcl methods */
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
		VSLb_ts_busyobj(ctx->bo, label, VTIM_real());
	} else if (ctx->req != NULL) {
		/* Called from request vcl methods */
		CHECK_OBJ(ctx->req, REQ_MAGIC);
		VSLb_ts_req(ctx->req, label, VTIM_real());
	}
}

VCL_BOOL v_matchproto_(td_std_cache_req_body)
vmod_cache_req_body(VRT_CTX, VCL_BYTES size)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	size = vmax_t(VCL_BYTES, size, 0);
	if (VRT_CacheReqBody(ctx, (size_t)size) < 0)
		return (0);
	return (1);
}

VCL_STRING v_matchproto_(td_std_strstr)
vmod_strstr(VRT_CTX, VCL_STRING s1, VCL_STRING s2)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (s1 == NULL || s2 == NULL)
		return (NULL);
	return (strstr(s1, s2));
}

VCL_STRING v_matchproto_(td_std_getenv)
vmod_getenv(VRT_CTX, VCL_STRING name)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (name == NULL || *name == '\0')
		return (NULL);
	return (getenv(name));
}

VCL_VOID v_matchproto_(td_std_late_100_continue)
vmod_late_100_continue(VRT_CTX, VCL_BOOL late)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(ctx->method == VCL_MET_RECV);
	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	if (ctx->req->want100cont)
		ctx->req->late100cont = late;
}

VCL_BOOL v_matchproto_(td_std_syntax)
vmod_syntax(VRT_CTX, VCL_REAL r)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(ctx->syntax == 40 || ctx->syntax == 41);
	/*
	 * We need to be careful because non-integer numbers have imprecise
	 * IEE754 representation (4.1 is 0x1.0666666666666p+2 = 4.09999...)
	 * By scaling up and rounding, this is taken care of.
	 */
	return (round(r * 10) <= ctx->syntax);
}

VCL_BOOL v_matchproto_(td_std_fnmatch)
vmod_fnmatch(VRT_CTX, VCL_STRING pattern, VCL_STRING subject,
	     VCL_BOOL pathname, VCL_BOOL noescape, VCL_BOOL period)
{
	int flags = 0;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (pattern == NULL) {
		VRT_fail(ctx, "std.fnmatch(): pattern is NULL");
		return (0);
	}
	if (subject == NULL) {
		VRT_fail(ctx, "std.fnmatch(): subject is NULL");
		return (0);
	}

	if (pathname)
		flags |= FNM_PATHNAME;
	if (noescape)
		flags |= FNM_NOESCAPE;
	if (period)
		flags |= FNM_PERIOD;
	return (fnmatch(pattern, subject, flags) != FNM_NOMATCH);
}

static const void * const priv_task_id_ban = &priv_task_id_ban;

VCL_BOOL v_matchproto_(td_std_ban)
vmod_ban(VRT_CTX, VCL_STRING s)
{
	struct vmod_priv *priv_task;
	VCL_STRING r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	r = VRT_ban_string(ctx, s);
	priv_task = VRT_priv_task_get(ctx, priv_task_id_ban);

	if (r == NULL && priv_task == NULL)
		return (1);

	if (priv_task == NULL)
		priv_task = VRT_priv_task(ctx, priv_task_id_ban);

	if (priv_task == NULL) {
		VRT_fail(ctx, "std.ban(): no priv_task (out of workspace?)");
		return (0);
	}

	/*
	 * TRUST_ME: the ban error is const. We save it in the un-const priv
	 * pointer, but promise to only ever return it as a (const) VCL_STRING
	 */
	priv_task->priv = TRUST_ME(r);

	return (r == NULL);
}

VCL_STRING v_matchproto_(td_std_ban_error)
vmod_ban_error(VRT_CTX)
{
	struct vmod_priv *priv_task;
	VCL_STRING r;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	priv_task = VRT_priv_task_get(ctx, priv_task_id_ban);
	if (priv_task == NULL)
		return ("");

	r = priv_task->priv;
	if (r == NULL)
		r = "";
	return (r);
}

VCL_TIME v_matchproto_(td_std_now)
vmod_now(VRT_CTX)
{

	(void) ctx;
	return (VTIM_real());
}

VCL_DURATION v_matchproto_(td_std_timed_call)
vmod_timed_call(VRT_CTX, VCL_SUB sub)
{
	vtim_mono b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	b = VTIM_mono();
	VRT_call(ctx, sub);
	return (VTIM_mono() - b);
}
