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
 * Other code is allowed to look at w->busyobj->fetch_failed to bail out
 *
 * For convenience, always return -1
 */

int
FetchError2(struct worker *w, const char *error, const char *more)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	if (!w->busyobj->fetch_failed) {
		if (more == NULL)
			WSLB(w, SLT_FetchError, "%s", error);
		else
			WSLB(w, SLT_FetchError, "%s: %s", error, more);
	}
	w->busyobj->fetch_failed = 1;
	return (-1);
}

int
FetchError(struct worker *w, const char *error)
{
	return(FetchError2(w, error, NULL));
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
vfp_nop_begin(struct worker *w, size_t estimate)
{

	if (estimate > 0)
		(void)FetchStorage(w, estimate);
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
vfp_nop_bytes(struct worker *w, struct http_conn *htc, ssize_t bytes)
{
	ssize_t l, wl;
	struct storage *st;

	AZ(w->busyobj->fetch_failed);
	while (bytes > 0) {
		st = FetchStorage(w, 0);
		if (st == NULL)
			return(-1);
		l = st->space - st->len;
		if (l > bytes)
			l = bytes;
		wl = HTC_Read(w, htc, st->ptr + st->len, l);
		if (wl <= 0)
			return (wl);
		st->len += wl;
		w->busyobj->fetch_obj->len += wl;
		bytes -= wl;
		if (w->busyobj->do_stream)
			RES_StreamPoll(w);
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
vfp_nop_end(struct worker *w)
{
	struct storage *st;

	st = VTAILQ_LAST(&w->busyobj->fetch_obj->store, storagehead);
	if (st == NULL)
		return (0);

	if (st->len == 0) {
		VTAILQ_REMOVE(&w->busyobj->fetch_obj->store, st, list);
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
FetchStorage(struct worker *w, ssize_t sz)
{
	ssize_t l;
	struct storage *st;
	struct object *obj;

	obj = w->busyobj->fetch_obj;
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	st = VTAILQ_LAST(&obj->store, storagehead);
	if (st != NULL && st->len < st->space)
		return (st);

	l = fetchfrag;
	if (l == 0)
		l = sz;
	if (l == 0)
		l = cache_param->fetch_chunksize;
	st = STV_alloc(w, l);
	if (st == NULL) {
		(void)FetchError(w, "Could not get storage");
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
fetch_straight(struct worker *w, struct http_conn *htc, ssize_t cl)
{
	int i;

	assert(w->busyobj->body_status == BS_LENGTH);

	if (cl < 0) {
		return (FetchError(w, "straight length field bogus"));
	} else if (cl == 0)
		return (0);

	i = w->busyobj->vfp->bytes(w, htc, cl);
	if (i <= 0)
		return (FetchError(w, "straight insufficient bytes"));
	return (0);
}

/*--------------------------------------------------------------------
 * Read a chunked HTTP object.
 *
 * XXX: Reading one byte at a time is pretty pessimal.
 */

static int
fetch_chunked(struct worker *w, struct http_conn *htc)
{
	int i;
	char buf[20];		/* XXX: 20 is arbitrary */
	unsigned u;
	ssize_t cl;

	assert(w->busyobj->body_status == BS_CHUNKED);
	do {
		/* Skip leading whitespace */
		do {
			if (HTC_Read(w, htc, buf, 1) <= 0)
				return (-1);
		} while (vct_islws(buf[0]));

		if (!vct_ishex(buf[0]))
			return (FetchError(w,"chunked header non-hex"));

		/* Collect hex digits, skipping leading zeros */
		for (u = 1; u < sizeof buf; u++) {
			do {
				if (HTC_Read(w, htc, buf + u, 1) <= 0)
					return (-1);
			} while (u == 1 && buf[0] == '0' && buf[u] == '0');
			if (!vct_ishex(buf[u]))
				break;
		}

		if (u >= sizeof buf)
			return (FetchError(w,"chunked header too long"));

		/* Skip trailing white space */
		while(vct_islws(buf[u]) && buf[u] != '\n')
			if (HTC_Read(w, htc, buf + u, 1) <= 0)
				return (-1);

		if (buf[u] != '\n')
			return (FetchError(w,"chunked header no NL"));

		buf[u] = '\0';
		cl = fetch_number(buf, 16);
		if (cl < 0)
			return (FetchError(w,"chunked header number syntax"));

		if (cl > 0 && w->busyobj->vfp->bytes(w, htc, cl) <= 0)
			return (-1);

		i = HTC_Read(w, htc, buf, 1);
		if (i <= 0)
			return (-1);
		if (buf[0] == '\r' && HTC_Read(w, htc, buf, 1) <= 0)
			return (-1);
		if (buf[0] != '\n')
			return (FetchError(w,"chunked tail no NL"));
	} while (cl > 0);
	return (0);
}

/*--------------------------------------------------------------------*/

static int
fetch_eof(struct worker *w, struct http_conn *htc)
{
	int i;

	assert(w->busyobj->body_status == BS_EOF);
	i = w->busyobj->vfp->bytes(w, htc, SSIZE_MAX);
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
FetchReqBody(struct sess *sp)
{
	unsigned long content_length;
	char buf[8192];
	char *ptr, *endp;
	int rdcnt;

	if (http_GetHdr(sp->http, H_Content_Length, &ptr)) {

		content_length = strtoul(ptr, &endp, 10);
		/* XXX should check result of conversion */
		while (content_length) {
			if (content_length > sizeof buf)
				rdcnt = sizeof buf;
			else
				rdcnt = content_length;
			rdcnt = HTC_Read(sp->wrk, sp->htc, buf, rdcnt);
			if (rdcnt <= 0)
				return (1);
			content_length -= rdcnt;
			if (!sp->sendbody)
				continue;
			(void)WRW_Write(sp->wrk, buf, rdcnt); /* XXX: stats ? */
			if (WRW_Flush(sp->wrk))
				return (2);
		}
	}
	if (http_GetHdr(sp->http, H_Transfer_Encoding, NULL)) {
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
FetchHdr(struct sess *sp, int need_host_hdr)
{
	struct vbc *vc;
	struct worker *w;
	struct http *hp;
	int retry = -1;
	int i;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	w = sp->wrk;
	CHECK_OBJ_NOTNULL(w->busyobj, BUSYOBJ_MAGIC);
	htc = &w->busyobj->htc;

	AN(sp->director);
	AZ(sp->wrk->obj);

	if (sp->wrk->objcore != NULL) {		/* pass has no objcore */
		CHECK_OBJ_NOTNULL(sp->wrk->objcore, OBJCORE_MAGIC);
		AN(sp->wrk->objcore->flags & OC_F_BUSY);
	}

	hp = w->bereq;

	sp->wrk->vbc = VDI_GetFd(NULL, sp);
	if (sp->wrk->vbc == NULL) {
		WSP(sp, SLT_FetchError, "no backend connection");
		return (-1);
	}
	vc = sp->wrk->vbc;
	if (vc->recycled)
		retry = 1;

	/*
	 * Now that we know our backend, we can set a default Host:
	 * header if one is necessary.  This cannot be done in the VCL
	 * because the backend may be chosen by a director.
	 */
	if (need_host_hdr)
		VDI_AddHostHeader(sp);

	(void)VTCP_blocking(vc->fd);	/* XXX: we should timeout instead */
	WRW_Reserve(w, &vc->fd);
	(void)http_Write(w, vc->vsl_id, hp, 0);	/* XXX: stats ? */

	/* Deal with any message-body the request might have */
	i = FetchReqBody(sp);
	if (WRW_FlushRelease(w) || i > 0) {
		WSP(sp, SLT_FetchError, "backend write error: %d (%s)",
		    errno, strerror(errno));
		VDI_CloseFd(sp->wrk, &sp->wrk->vbc);
		/* XXX: other cleanup ? */
		return (retry);
	}

	/* Checkpoint the vsl.here */
	WSL_Flush(w, 0);

	/* XXX is this the right place? */
	VSC_C_main->backend_req++;

	/* Receive response */

	HTC_Init(htc, w->ws, vc->fd, vc->vsl_id,
	    cache_param->http_resp_size,
	    cache_param->http_resp_hdr_len);

	VTCP_set_read_timeout(vc->fd, vc->first_byte_timeout);

	i = HTC_Rx(htc);

	if (i < 0) {
		WSP(sp, SLT_FetchError, "http first read error: %d %d (%s)",
		    i, errno, strerror(errno));
		VDI_CloseFd(sp->wrk, &sp->wrk->vbc);
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
			VDI_CloseFd(sp->wrk, &sp->wrk->vbc);
			/* XXX: other cleanup ? */
			return (-1);
		}
	}

	hp = w->beresp;

	if (http_DissectResponse(w, htc, hp)) {
		WSP(sp, SLT_FetchError, "http format error");
		VDI_CloseFd(sp->wrk, &sp->wrk->vbc);
		/* XXX: other cleanup ? */
		return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int
FetchBody(struct worker *w, struct object *obj)
{
	int cls;
	struct storage *st;
	int mklen;
	ssize_t cl;
	struct http_conn *htc;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(w->busyobj, BUSYOBJ_MAGIC);
	AZ(w->busyobj->fetch_obj);
	CHECK_OBJ_NOTNULL(w->vbc, VBC_MAGIC);
	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(obj->http, HTTP_MAGIC);

	htc = &w->busyobj->htc;

	if (w->busyobj->vfp == NULL)
		w->busyobj->vfp = &vfp_nop;

	AssertObjCorePassOrBusy(obj->objcore);

	AZ(w->busyobj->vgz_rx);
	AZ(VTAILQ_FIRST(&obj->store));

	w->busyobj->fetch_obj = obj;
	w->busyobj->fetch_failed = 0;

	/* XXX: pick up estimate from objdr ? */
	cl = 0;
	switch (w->busyobj->body_status) {
	case BS_NONE:
		cls = 0;
		mklen = 0;
		break;
	case BS_ZERO:
		cls = 0;
		mklen = 1;
		break;
	case BS_LENGTH:
		cl = fetch_number( w->busyobj->h_content_length, 10);
		w->busyobj->vfp->begin(w, cl > 0 ? cl : 0);
		cls = fetch_straight(w, htc, cl);
		mklen = 1;
		if (w->busyobj->vfp->end(w))
			cls = -1;
		break;
	case BS_CHUNKED:
		w->busyobj->vfp->begin(w, cl);
		cls = fetch_chunked(w, htc);
		mklen = 1;
		if (w->busyobj->vfp->end(w))
			cls = -1;
		break;
	case BS_EOF:
		w->busyobj->vfp->begin(w, cl);
		cls = fetch_eof(w, htc);
		mklen = 1;
		if (w->busyobj->vfp->end(w))
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
	AZ(w->busyobj->vgz_rx);

	/*
	 * It is OK for ->end to just leave the last storage segment
	 * sitting on w->storage, we will always call vfp_nop_end()
	 * to get it trimmed or thrown out if empty.
	 */
	AZ(vfp_nop_end(w));

	w->busyobj->fetch_obj = NULL;

	WSLB(w, SLT_Fetch_Body, "%u(%s) cls %d mklen %u",
	    w->busyobj->body_status, body_status(w->busyobj->body_status),
	    cls, mklen);

	if (w->busyobj->body_status == BS_ERROR) {
		VDI_CloseFd(w, &w->vbc);
		return (__LINE__);
	}

	if (cls < 0) {
		w->stats.fetch_failed++;
		/* XXX: Wouldn't this store automatically be released ? */
		while (!VTAILQ_EMPTY(&obj->store)) {
			st = VTAILQ_FIRST(&obj->store);
			VTAILQ_REMOVE(&obj->store, st, list);
			STV_free(st);
		}
		VDI_CloseFd(w, &w->vbc);
		obj->len = 0;
		return (__LINE__);
	}
	AZ(w->busyobj->fetch_failed);

	if (cls == 0 && w->busyobj->should_close)
		cls = 1;

	WSLB(w, SLT_Length, "%u", obj->len);

	{
	/* Sanity check fetch methods accounting */
		ssize_t uu;

		uu = 0;
		VTAILQ_FOREACH(st, &obj->store, list)
			uu += st->len;
		if (w->busyobj->do_stream)
			/* Streaming might have started freeing stuff */
			assert (uu <= obj->len);

		else
			assert(uu == obj->len);
	}

	if (mklen > 0) {
		http_Unset(obj->http, H_Content_Length);
		http_PrintfHeader(w, w->vbc->vsl_id, obj->http,
		    "Content-Length: %jd", (intmax_t)obj->len);
	}

	if (cls)
		VDI_CloseFd(w, &w->vbc);
	else
		VDI_RecycleFd(w, &w->vbc);

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
