/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "cache_backend.h"
#include "vcli_priv.h"
#include "vct.h"
#include "vtcp.h"

static unsigned fetchfrag;

/*--------------------------------------------------------------------
 * We want to issue the first error we encounter on fetching and
 * supress the rest.  This function does that.
 *
 * Other code is allowed to look at wrk->busyobj->fetch_failed to bail out
 *
 * For convenience, always return -1
 */

int
FetchError2(struct busyobj *bo, const char *error, const char *more)
{

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	if (!bo->fetch_failed) {
		if (more == NULL)
			VSLB(bo, SLT_FetchError, "%s", error);
		else
			VSLB(bo, SLT_FetchError, "%s: %s", error, more);
	}
	bo->fetch_failed = 1;
	return (-1);
}

int
FetchError(struct busyobj *bo, const char *error)
{
	return(FetchError2(bo, error, NULL));
}

/*--------------------------------------------------------------------
 * VFP_NOP
 *
 * This fetch-processor does nothing but store the object.
 * It also documents the API
 */

/*--------------------------------------------------------------------
 * VFP_BEGIN
 *
 * Called to set up stuff.
 *
 * 'estimate' is the estimate of the number of bytes we expect to receive,
 * as seen on the socket, or zero if unknown.
 */
static void __match_proto__()
vfp_nop_begin(struct worker *wrk, size_t estimate)
{

	if (estimate > 0)
		(void)FetchStorage(wrk, estimate);
}

/*--------------------------------------------------------------------
 * VFP_BYTES
 *
 * Process (up to) 'bytes' from the socket.
 *
 * Return -1 on error, issue FetchError()
 *	will not be called again, once error happens.
 * Return 0 on EOF on socket even if bytes not reached.
 * Return 1 when 'bytes' have been processed.
 */

static int __match_proto__()
vfp_nop_bytes(struct worker *wrk, struct http_conn *htc, ssize_t bytes)
{
	ssize_t l, wl;
	struct storage *st;

	AZ(wrk->busyobj->fetch_failed);
	while (bytes > 0) {
		st = FetchStorage(wrk, 0);
		if (st == NULL)
			return(-1);
		l = st->space - st->len;
		if (l > bytes)
			l = bytes;
		wl = HTC_Read(htc, st->ptr + st->len, l);
		if (wl <= 0)
			return (wl);
		st->len += wl;
		wrk->busyobj->fetch_obj->len += wl;
		bytes -= wl;
	}
	return (1);
}

/*--------------------------------------------------------------------
 * VFP_END
 *
 * Finish & cleanup
 *
 * Return -1 for error
 * Return 0 for OK
 */

static int __match_proto__()
vfp_nop_end(struct worker *wrk)
{
	struct storage *st;

	st = VTAILQ_LAST(&wrk->busyobj->fetch_obj->store, storagehead);
	if (st == NULL)
		return (0);

	if (st->len == 0) {
		VTAILQ_REMOVE(&wrk->busyobj->fetch_obj->store, st, list);
		STV_free(st);
		return (0);
	}
	if (st->len < st->space)
		STV_trim(st, st->len);
	return (0);
}

static struct vfp vfp_nop = {
	.begin	=	vfp_nop_begin,
	.bytes	=	vfp_nop_bytes,
	.end	=	vfp_nop_end,
};

/*--------------------------------------------------------------------
 * Fetch Storage to put object into.
 *
 */

struct storage *
FetchStorage(struct worker *wrk, ssize_t sz)
{
	ssize_t l;
	struct storage *st;
	struct object *obj;

	obj = wrk->busyobj->fetch_obj;
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	st = VTAILQ_LAST(&obj->store, storagehead);
	if (st != NULL && st->len < st->space)
		return (st);

	l = fetchfrag;
	if (l == 0)
		l = sz;
	if (l == 0)
		l = cache_param->fetch_chunksize;
	st = STV_alloc(wrk, l);
	if (st == NULL) {
		(void)FetchError(wrk->busyobj, "Could not get storage");
		return (NULL);
	}
	AZ(st->len);
	VTAILQ_INSERT_TAIL(&obj->store, st, list);
	return (st);
}

/*--------------------------------------------------------------------
 * Convert a string to a size_t safely
 */

static ssize_t
fetch_number(const char *nbr, int radix)
{
	uintmax_t cll;
	ssize_t cl;
	char *q;

	if (*nbr == '\0')
		return (-1);
	cll = strtoumax(nbr, &q, radix);
	if (q == NULL || *q != '\0')
		return (-1);

	cl = (ssize_t)cll;
	if((uintmax_t)cl != cll) /* Protect against bogusly large values */
		return (-1);
	return (cl);
}

/*--------------------------------------------------------------------*/

static int
fetch_straight(struct worker *wrk, struct http_conn *htc, ssize_t cl)
{
	int i;

	assert(wrk->busyobj->body_status == BS_LENGTH);

	if (cl < 0) {
		return (FetchError(wrk->busyobj, "straight length field bogus"));
	} else if (cl == 0)
		return (0);

	i = wrk->busyobj->vfp->bytes(wrk, htc, cl);
	if (i <= 0)
		return (FetchError(wrk->busyobj, "straight insufficient bytes"));
	return (0);
}

/*--------------------------------------------------------------------
 * Read a chunked HTTP object.
 *
 * XXX: Reading one byte at a time is pretty pessimal.
 */

static int
fetch_chunked(struct worker *wrk, struct http_conn *htc)
{
	int i;
	char buf[20];		/* XXX: 20 is arbitrary */
	unsigned u;
	ssize_t cl;

	assert(wrk->busyobj->body_status == BS_CHUNKED);
	do {
		/* Skip leading whitespace */
		do {
			if (HTC_Read(htc, buf, 1) <= 0)
				return (-1);
		} while (vct_islws(buf[0]));

		if (!vct_ishex(buf[0]))
			return (FetchError(wrk->busyobj, "chunked header non-hex"));

		/* Collect hex digits, skipping leading zeros */
		for (u = 1; u < sizeof buf; u++) {
			do {
				if (HTC_Read(htc, buf + u, 1) <= 0)
					return (-1);
			} while (u == 1 && buf[0] == '0' && buf[u] == '0');
			if (!vct_ishex(buf[u]))
				break;
		}

		if (u >= sizeof buf)
			return (FetchError(wrk->busyobj,"chunked header too long"));

		/* Skip trailing white space */
		while(vct_islws(buf[u]) && buf[u] != '\n')
			if (HTC_Read(htc, buf + u, 1) <= 0)
				return (-1);

		if (buf[u] != '\n')
			return (FetchError(wrk->busyobj,"chunked header no NL"));

		buf[u] = '\0';
		cl = fetch_number(buf, 16);
		if (cl < 0)
			return (FetchError(wrk->busyobj,"chunked header number syntax"));

		if (cl > 0 && wrk->busyobj->vfp->bytes(wrk, htc, cl) <= 0)
			return (-1);

		i = HTC_Read(htc, buf, 1);
		if (i <= 0)
			return (-1);
		if (buf[0] == '\r' && HTC_Read( htc, buf, 1) <= 0)
			return (-1);
		if (buf[0] != '\n')
			return (FetchError(wrk->busyobj,"chunked tail no NL"));
	} while (cl > 0);
	return (0);
}

/*--------------------------------------------------------------------*/

static int
fetch_eof(struct worker *wrk, struct http_conn *htc)
{
	int i;

	assert(wrk->busyobj->body_status == BS_EOF);
	i = wrk->busyobj->vfp->bytes(wrk, htc, SSIZE_MAX);
	if (i < 0)
		return (-1);
	return (0);
}

/*--------------------------------------------------------------------
 * Fetch any body attached to the incoming request, and either write it
 * to the backend (if we pass) or discard it (anything else).
 * This is mainly a separate function to isolate the stack buffer and
 * to contain the complexity when we start handling chunked encoding.
 */

int
FetchReqBody(const struct sess *sp, int sendbody)
{
	unsigned long content_length;
	char buf[8192];
	char *ptr, *endp;
	int rdcnt;

	if (sp->req->reqbodydone) {
		AZ(sendbody);
		return (0);
	}

	if (http_GetHdr(sp->req->http, H_Content_Length, &ptr)) {
		sp->req->reqbodydone = 1;

		content_length = strtoul(ptr, &endp, 10);
		/* XXX should check result of conversion */
		while (content_length) {
			if (content_length > sizeof buf)
				rdcnt = sizeof buf;
			else
				rdcnt = content_length;
			rdcnt = HTC_Read(sp->req->htc, buf, rdcnt);
			if (rdcnt <= 0)
				return (1);
			content_length -= rdcnt;
			if (sendbody) {
				/* XXX: stats ? */
				(void)WRW_Write(sp->wrk, buf, rdcnt);
				if (WRW_Flush(sp->wrk))
					return (2);
			}
		}
	}
	if (http_GetHdr(sp->req->http, H_Transfer_Encoding, NULL)) {
		/* XXX: Handle chunked encoding. */
		WSP(sp, SLT_Debug, "Transfer-Encoding in request");
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Send request, and receive the HTTP protocol response, but not the
 * response body.
 *
 * Return value:
 *	-1 failure, not retryable
 *	 0 success
 *	 1 failure which can be retried.
 */

int
FetchHdr(struct sess *sp, int need_host_hdr, int sendbody)
{
	struct vbc *vc;
	struct worker *wrk;
	struct http *hp;
	int retry = -1;
	int i;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	wrk = sp->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->busyobj, BUSYOBJ_MAGIC);
	htc = &wrk->busyobj->htc;

	AN(sp->req->director);
	AZ(sp->req->obj);

	if (sp->req->objcore != NULL) {		/* pass has no objcore */
		CHECK_OBJ_NOTNULL(sp->req->objcore, OBJCORE_MAGIC);
		AN(sp->req->objcore->flags & OC_F_BUSY);
	}

	hp = wrk->busyobj->bereq;

	wrk->busyobj->vbc = VDI_GetFd(NULL, sp);
	if (wrk->busyobj->vbc == NULL) {
		WSP(sp, SLT_FetchError, "no backend connection");
		return (-1);
	}
	vc = wrk->busyobj->vbc;
	if (vc->recycled)
		retry = 1;

	/*
	 * Now that we know our backend, we can set a default Host:
	 * header if one is necessary.  This cannot be done in the VCL
	 * because the backend may be chosen by a director.
	 */
	if (need_host_hdr)
		VDI_AddHostHeader(wrk->busyobj->bereq, vc);

	(void)VTCP_blocking(vc->fd);	/* XXX: we should timeout instead */
	WRW_Reserve(wrk, &vc->fd);
	(void)http_Write(wrk, hp, 0);	/* XXX: stats ? */

	/* Deal with any message-body the request might have */
	i = FetchReqBody(sp, sendbody);
	if (WRW_FlushRelease(wrk) || i > 0) {
		WSP(sp, SLT_FetchError, "backend write error: %d (%s)",
		    errno, strerror(errno));
		VDI_CloseFd(wrk, &wrk->busyobj->vbc);
		/* XXX: other cleanup ? */
		return (retry);
	}

	/* Checkpoint the vsl.here */
	WSL_Flush(wrk->vsl, 0);

	/* XXX is this the right place? */
	VSC_C_main->backend_req++;

	/* Receive response */

	HTC_Init(htc, wrk->busyobj->ws, vc->fd, vc->vsl,
	    cache_param->http_resp_size,
	    cache_param->http_resp_hdr_len);

	VTCP_set_read_timeout(vc->fd, vc->first_byte_timeout);

	i = HTC_Rx(htc);

	if (i < 0) {
		WSP(sp, SLT_FetchError, "http first read error: %d %d (%s)",
		    i, errno, strerror(errno));
		VDI_CloseFd(wrk, &wrk->busyobj->vbc);
		/* XXX: other cleanup ? */
		/* Retryable if we never received anything */
		return (i == -1 ? retry : -1);
	}

	VTCP_set_read_timeout(vc->fd, vc->between_bytes_timeout);

	while (i == 0) {
		i = HTC_Rx(htc);
		if (i < 0) {
			WSP(sp, SLT_FetchError,
			    "http first read error: %d %d (%s)",
			    i, errno, strerror(errno));
			VDI_CloseFd(wrk, &wrk->busyobj->vbc);
			/* XXX: other cleanup ? */
			return (-1);
		}
	}

	hp = wrk->busyobj->beresp;

	if (http_DissectResponse(hp, htc)) {
		WSP(sp, SLT_FetchError, "http format error");
		VDI_CloseFd(wrk, &wrk->busyobj->vbc);
		/* XXX: other cleanup ? */
		return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int
FetchBody(struct worker *wrk, struct object *obj)
{
	int cls;
	struct storage *st;
	int mklen;
	ssize_t cl;
	struct http_conn *htc;
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	bo = wrk->busyobj;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	AZ(bo->fetch_obj);
	CHECK_OBJ_NOTNULL(bo->vbc, VBC_MAGIC);
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(obj->http, HTTP_MAGIC);

	htc = &bo->htc;

	if (bo->vfp == NULL)
		bo->vfp = &vfp_nop;

	AssertObjCorePassOrBusy(obj->objcore);

	AZ(bo->vgz_rx);
	AZ(VTAILQ_FIRST(&obj->store));

	bo->fetch_obj = obj;
	bo->fetch_failed = 0;

	/* XXX: pick up estimate from objdr ? */
	cl = 0;
	switch (bo->body_status) {
	case BS_NONE:
		cls = 0;
		mklen = 0;
		break;
	case BS_ZERO:
		cls = 0;
		mklen = 1;
		break;
	case BS_LENGTH:
		cl = fetch_number(bo->h_content_length, 10);
		bo->vfp->begin(wrk, cl > 0 ? cl : 0);
		cls = fetch_straight(wrk, htc, cl);
		mklen = 1;
		if (bo->vfp->end(wrk))
			cls = -1;
		break;
	case BS_CHUNKED:
		bo->vfp->begin(wrk, cl);
		cls = fetch_chunked(wrk, htc);
		mklen = 1;
		if (bo->vfp->end(wrk))
			cls = -1;
		break;
	case BS_EOF:
		bo->vfp->begin(wrk, cl);
		cls = fetch_eof(wrk, htc);
		mklen = 1;
		if (bo->vfp->end(wrk))
			cls = -1;
		break;
	case BS_ERROR:
		cls = 1;
		mklen = 0;
		break;
	default:
		cls = 0;
		mklen = 0;
		INCOMPL();
	}
	AZ(bo->vgz_rx);

	/*
	 * It is OK for ->end to just leave the last storage segment
	 * sitting on wrk->storage, we will always call vfp_nop_end()
	 * to get it trimmed or thrown out if empty.
	 */
	AZ(vfp_nop_end(wrk));

	bo->fetch_obj = NULL;

	VSLB(bo, SLT_Fetch_Body, "%u(%s) cls %d mklen %d",
	    bo->body_status, body_status(bo->body_status),
	    cls, mklen);

	if (bo->body_status == BS_ERROR) {
		VDI_CloseFd(wrk, &bo->vbc);
		return (__LINE__);
	}

	if (cls < 0) {
		wrk->stats.fetch_failed++;
		VDI_CloseFd(wrk, &bo->vbc);
		obj->len = 0;
		return (__LINE__);
	}
	AZ(bo->fetch_failed);

	if (cls == 0 && bo->should_close)
		cls = 1;

	VSLB(bo, SLT_Length, "%zd", obj->len);

	{
	/* Sanity check fetch methods accounting */
		ssize_t uu;

		uu = 0;
		VTAILQ_FOREACH(st, &obj->store, list)
			uu += st->len;
		if (bo->do_stream)
			/* Streaming might have started freeing stuff */
			assert (uu <= obj->len);

		else
			assert(uu == obj->len);
	}

	if (mklen > 0) {
		http_Unset(obj->http, H_Content_Length);
		http_PrintfHeader(obj->http, "Content-Length: %zd", obj->len);
	}

	if (cls)
		VDI_CloseFd(wrk, &bo->vbc);
	else
		VDI_RecycleFd(wrk, &bo->vbc);

	return (0);
}

/*--------------------------------------------------------------------
 * Debugging aids
 */

static void
debug_fragfetch(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	(void)cli;
	fetchfrag = strtoul(av[2], NULL, 0);
}

static struct cli_proto debug_cmds[] = {
	{ "debug.fragfetch", "debug.fragfetch",
		"\tEnable fetch fragmentation\n", 1, 1, "d", debug_fragfetch },
	{ NULL }
};

/*--------------------------------------------------------------------
 *
 */

void
Fetch_Init(void)
{

	CLI_AddFuncs(debug_cmds);
}
