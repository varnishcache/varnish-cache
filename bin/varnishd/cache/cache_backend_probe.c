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
 *
 * Poll backends for collection of health statistics
 *
 * We co-opt threads from the worker pool for probing the backends,
 * but we want to avoid a potentially messy cleanup operation when we
 * retire the backend, so the thread owns the health information, which
 * the backend references, rather than the other way around.
 *
 */

#include "config.h"

#include <poll.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "cache_varnishd.h"

#include "binary_heap.h"
#include "vcli_serve.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

#include "cache_backend.h"
#include "cache_tcp_pool.h"

/* Default averaging rate, we want something pretty responsive */
#define AVG_RATE			4

struct vbp_target {
	unsigned			magic;
#define VBP_TARGET_MAGIC		0x6b7cb656

	VRT_BACKEND_PROBE_FIELDS()

	struct backend			*backend;
	struct tcp_pool			*tcp_pool;

	char				*req;
	int				req_len;

	char				resp_buf[128];
	unsigned			good;

	/* Collected statistics */
#define BITMAP(n, c, t, b)	uintmax_t	n;
#include "tbl/backend_poll.h"

	double				last;
	double				avg;
	double				rate;

	double				due;
	int				running;
	int				heap_idx;
	struct pool_task		task;
};

static struct lock			vbp_mtx;
static pthread_cond_t			vbp_cond;
static struct binheap			*vbp_heap;

static const unsigned char vbp_proxy_local[] = {
	0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51,
	0x55, 0x49, 0x54, 0x0a, 0x20, 0x00, 0x00, 0x00,
};

/*--------------------------------------------------------------------*/

static void
vbp_delete(struct vbp_target *vt)
{
#define DN(x)	/**/
	VRT_BACKEND_PROBE_HANDLE();
#undef DN
	VTP_Rel(&vt->tcp_pool);
	free(vt->req);
	FREE_OBJ(vt);
}


/*--------------------------------------------------------------------
 * Record pokings...
 */

static void
vbp_start_poke(struct vbp_target *vt)
{
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

#define BITMAP(n, c, t, b) \
	vt->n <<= 1;
#include "tbl/backend_poll.h"

	vt->last = 0;
	vt->resp_buf[0] = '\0';
}

static void
vbp_has_poked(struct vbp_target *vt)
{
	unsigned i, j;
	uint64_t u;

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	/* Calculate exponential average */
	if (vt->happy & 1) {
		if (vt->rate < AVG_RATE)
			vt->rate += 1.0;
		vt->avg += (vt->last - vt->avg) / vt->rate;
	}

	u = vt->happy;
	for (i = j = 0; i < vt->window; i++) {
		if (u & 1)
			j++;
		u >>= 1;
	}
	vt->good = j;
}

void
VBP_Update_Backend(struct vbp_target *vt)
{
	unsigned i = 0;
	char bits[10];
	const char *logmsg;

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	Lck_Lock(&vbp_mtx);
	if (vt->backend == NULL) {
		Lck_Unlock(&vbp_mtx);
		return;
	}

#define BITMAP(n, c, t, b) \
	bits[i++] = (vt->n & 1) ? c : '-';
#include "tbl/backend_poll.h"
	bits[i] = '\0';
	assert(i < sizeof bits);

	if (vt->backend->director == NULL) {
		Lck_Unlock(&vbp_mtx);
		return;
	}

	if (vt->good >= vt->threshold) {
		if (vt->backend->director->sick) {
			logmsg = "Back healthy";
			VRT_SetHealth(vt->backend->director, 1);
		} else {
			logmsg = "Still healthy";
		}
	} else {
		if (vt->backend->director->sick) {
			logmsg = "Still sick";
		} else {
			logmsg = "Went sick";
			VRT_SetHealth(vt->backend->director, 0);
		}
	}
	VSL(SLT_Backend_health, 0, "%s %s %s %u %u %u %.6f %.6f %s",
	    vt->backend->director->vcl_name, logmsg, bits,
	    vt->good, vt->threshold, vt->window,
	    vt->last, vt->avg, vt->resp_buf);
	VBE_SetHappy(vt->backend, vt->happy);

	Lck_Unlock(&vbp_mtx);
}

static void
vbp_reset(struct vbp_target *vt)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);
	vt->avg = 0.0;
	vt->rate = 0.0;
#define BITMAP(n, c, t, b) \
	vt->n = 0;
#include "tbl/backend_poll.h"

	for (u = 0; u < vt->initial; u++) {
		vbp_start_poke(vt);
		vt->happy |= 1;
		vbp_has_poked(vt);
	}
}

/*--------------------------------------------------------------------
 * Poke one backend, once, but possibly at both IPv4 and IPv6 addresses.
 *
 * We do deliberately not use the stuff in cache_backend.c, because we
 * want to measure the backends response without local distractions.
 */

static int
vbp_write(struct vbp_target *vt, int *sock, const void *buf, size_t len)
{
	int i;

	i = write(*sock, buf, len);
	if (i != len) {
		if (i < 0) {
			vt->err_xmit |= 1;
			bprintf(vt->resp_buf, "Write error %d (%s)",
				errno, strerror(errno));
		} else {
			bprintf(vt->resp_buf,
				"Short write (%d/%zu) error %d (%s)",
				i, len, errno, strerror(errno));
		}
		VTCP_close(sock);
		return (-1);
	}
	return (0);
}

static int
vbp_write_proxy_v1(struct vbp_target *vt, int *sock)
{
	char buf[105]; /* maximum size for a TCP6 PROXY line with null char */
	char addr[VTCP_ADDRBUFSIZE];
	char port[VTCP_PORTBUFSIZE];
	struct sockaddr_storage ss;
	struct vsb vsb;
	socklen_t l;

	VTCP_myname(*sock, addr, sizeof addr, port, sizeof port);
	AN(VSB_new(&vsb, buf, sizeof buf, VSB_FIXEDLEN));

	l = sizeof ss;
	AZ(getsockname(*sock, (void *)&ss, &l));
	if (ss.ss_family == AF_INET || ss.ss_family == AF_INET6) {
		VSB_printf(&vsb, "PROXY %s %s %s %s %s\r\n",
		    ss.ss_family == AF_INET ? "TCP4" : "TCP6",
		    addr, addr, port, port);
	} else
		VSB_cat(&vsb, "PROXY UNKNOWN\r\n");
	AZ(VSB_finish(&vsb));

	return (vbp_write(vt, sock, VSB_data(&vsb), VSB_len(&vsb)));
}

static void
vbp_poke(struct vbp_target *vt)
{
	int s, tmo, i, proxy_header, err;
	double t_start, t_now, t_end;
	unsigned rlen, resp;
	char buf[8192], *p;
	struct pollfd pfda[1], *pfd = pfda;
	const struct suckaddr *sa;

	t_start = t_now = VTIM_real();
	t_end = t_start + vt->timeout;

	s = VTP_Open(vt->tcp_pool, t_end - t_now, (const void **)&sa, &err);
	if (s < 0) {
		bprintf(vt->resp_buf, "Open error %d (%s)", err, strerror(err));
		Lck_Lock(&vbp_mtx);
		if (vt->backend)
			VBE_Connect_Error(vt->backend->vsc, err);
		Lck_Unlock(&vbp_mtx);
		return;
	}

	i = VSA_Get_Proto(sa);
	if (VSA_Compare(sa, bogo_ip) == 0)
		vt->good_unix |= 1;
	else if (i == AF_INET)
		vt->good_ipv4 |= 1;
	else if (i == AF_INET6)
		vt->good_ipv6 |= 1;
	else
		WRONG("Wrong probe protocol family");

	t_now = VTIM_real();
	tmo = (int)round((t_end - t_now) * 1e3);
	if (tmo <= 0) {
		bprintf(vt->resp_buf,
			"Open timeout %.3fs exceeded by %.3fs",
			vt->timeout, t_now - t_end);
		VTCP_close(&s);
		return;
	}

	Lck_Lock(&vbp_mtx);
	if (vt->backend != NULL)
		proxy_header = vt->backend->proxy_header;
	else
		proxy_header = -1;
	Lck_Unlock(&vbp_mtx);

	if (proxy_header < 0) {
		bprintf(vt->resp_buf, "%s", "No backend");
		VTCP_close(&s);
		return;
	}

	/* Send the PROXY header */
	assert(proxy_header <= 2);
	if (proxy_header == 1) {
		if (vbp_write_proxy_v1(vt, &s) != 0)
			return;
	} else if (proxy_header == 2 &&
	    vbp_write(vt, &s, vbp_proxy_local, sizeof vbp_proxy_local) != 0)
		return;

	/* Send the request */
	if (vbp_write(vt, &s, vt->req, vt->req_len) != 0)
		return;
	vt->good_xmit |= 1;

	pfd->fd = s;
	rlen = 0;
	while (1) {
		pfd->events = POLLIN;
		pfd->revents = 0;
		tmo = (int)round((t_end - t_now) * 1e3);
		if (tmo > 0)
			i = poll(pfd, 1, tmo);
		if (i == 0) {
			vt->err_recv |= 1;
			bprintf(vt->resp_buf, "Poll error %d (%s)",
				errno, strerror(errno));
			VTCP_close(&s);
			return;
		}
		if (tmo <= 0) {
			bprintf(vt->resp_buf,
				"Poll (read) timeout %.3fs exceeded by %.3fs",
				vt->timeout, t_now - t_end);
			VTCP_close(&s);
			return;
		}
		if (rlen < sizeof vt->resp_buf)
			i = read(s, vt->resp_buf + rlen,
			    sizeof vt->resp_buf - rlen);
		else
			i = read(s, buf, sizeof buf);
		if (i <= 0) {
			if (i < 0)
				bprintf(vt->resp_buf, "Read error %d (%s)",
					errno, strerror(errno));
			break;
		}
		rlen += i;
	}

	VTCP_close(&s);

	if (i < 0) {
		/* errno reported above */
		vt->err_recv |= 1;
		return;
	}

	if (rlen == 0) {
		bprintf(vt->resp_buf, "%s", "Empty response");
		return;
	}

	/* So we have a good receive ... */
	t_now = VTIM_real();
	vt->last = t_now - t_start;
	vt->good_recv |= 1;

	/* Now find out if we like the response */
	vt->resp_buf[sizeof vt->resp_buf - 1] = '\0';
	p = strchr(vt->resp_buf, '\r');
	if (p != NULL)
		*p = '\0';
	p = strchr(vt->resp_buf, '\n');
	if (p != NULL)
		*p = '\0';

	i = sscanf(vt->resp_buf, "HTTP/%*f %u ", &resp);

	if (i == 1 && resp == vt->exp_status)
		vt->happy |= 1;
}

/*--------------------------------------------------------------------
 */

static void v_matchproto_(task_func_t)
vbp_task(struct worker *wrk, void *priv)
{
	struct vbp_target *vt;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CAST_OBJ_NOTNULL(vt, priv, VBP_TARGET_MAGIC);

	AN(vt->running);
	AN(vt->req);
	assert(vt->req_len > 0);

	vbp_start_poke(vt);
	vbp_poke(vt);
	vbp_has_poked(vt);
	VBP_Update_Backend(vt);

	Lck_Lock(&vbp_mtx);
	if (vt->running < 0) {
		assert(vt->heap_idx == BINHEAP_NOIDX);
		vbp_delete(vt);
	} else {
		vt->running = 0;
		if (vt->heap_idx != BINHEAP_NOIDX) {
			vt->due = VTIM_real() + vt->interval;
			binheap_delete(vbp_heap, vt->heap_idx);
			binheap_insert(vbp_heap, vt);
		}
	}
	Lck_Unlock(&vbp_mtx);
}

/*--------------------------------------------------------------------
 */

static void * v_matchproto_(bgthread_t)
vbp_thread(struct worker *wrk, void *priv)
{
	double now, nxt;
	struct vbp_target *vt;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AZ(priv);
	Lck_Lock(&vbp_mtx);
	while (1) {
		now = VTIM_real();
		vt = binheap_root(vbp_heap);
		if (vt == NULL) {
			nxt = 8.192 + now;
			(void)Lck_CondWait(&vbp_cond, &vbp_mtx, nxt);
		} else if (vt->due > now) {
			nxt = vt->due;
			vt = NULL;
			(void)Lck_CondWait(&vbp_cond, &vbp_mtx, nxt);
		} else {
			binheap_delete(vbp_heap, vt->heap_idx);
			vt->due = now + vt->interval;
			if (!vt->running) {
				vt->running = 1;
				vt->task.func = vbp_task;
				vt->task.priv = vt;
				if (Pool_Task_Any(&vt->task, TASK_QUEUE_REQ))
					vt->running = 0;
			}
			binheap_insert(vbp_heap, vt);
		}
	}
	NEEDLESS(Lck_Unlock(&vbp_mtx));
	NEEDLESS(return NULL);
}


/*--------------------------------------------------------------------
 * Cli functions
 */

static void
vbp_bitmap(struct vsb *vsb, char c, uint64_t map, const char *lbl)
{
	int i;
	uint64_t u = (1ULL << 63);

	VSB_printf(vsb, "  ");
	for (i = 0; i < 64; i++) {
		if (map & u)
			VSB_putc(vsb, c);
		else
			VSB_putc(vsb, '-');
		map <<= 1;
	}
	VSB_printf(vsb, " %s\n", lbl);
}

/*lint -e{506} constant value boolean */
/*lint -e{774} constant value boolean */
void
VBP_Status(struct vsb *vsb, const struct backend *be, int details, int json)
{
	struct vbp_target *vt;
	char buf[12];

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	vt = be->probe;
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	if (!details) {
		bprintf(buf, "%u/%u %s", vt->good, vt->window,
		    vt->backend->director->sick ? "bad" : "good");
		if (json)
			VSB_printf(vsb, "\"%s\"", buf);
		else
			VSB_printf(vsb, "%-10s", buf);
		return;
	}

	if (json) {
		VSB_printf(vsb, "{\n");
		VSB_indent(vsb, 2);
#define BITMAP(nn, cc, tt, bb)					\
		VSB_printf(vsb, "\"bits_%c\": %ju,\n", cc, vt->nn);
#include "tbl/backend_poll.h"
		VSB_printf(vsb, "\"good\": %u,\n", vt->good);
		VSB_printf(vsb, "\"threshold\": %u,\n", vt->threshold);
		VSB_printf(vsb, "\"window\": %u", vt->window);
		VSB_indent(vsb, -2);
		VSB_printf(vsb, "\n");
		VSB_printf(vsb, "},\n");
		return;
	}

	VSB_printf(vsb,
	    "\nCurrent states  good: %2u threshold: %2u window: %2u\n",
	    vt->good, vt->threshold, vt->window);
	VSB_printf(vsb,
	    "  Average response time of good probes: %.6f\n", vt->avg);
	VSB_printf(vsb,
	    "  Oldest ======================"
	    "============================ Newest\n");

#define BITMAP(n, c, t, b)					\
		if ((vt->n != 0) || (b))			\
			vbp_bitmap(vsb, (c), vt->n, (t));
#include "tbl/backend_poll.h"
}

/*--------------------------------------------------------------------
 * Build request from probe spec
 */

static void
vbp_build_req(struct vbp_target *vt, const struct vrt_backend_probe *vbp,
    const struct backend *be)
{
	struct vsb *vsb;

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_clear(vsb);
	if (vbp->request != NULL) {
		VSB_cat(vsb, vbp->request);
	} else {
		VSB_printf(vsb, "GET %s HTTP/1.1\r\n",
		    vbp->url != NULL ?  vbp->url : "/");
		if (be->hosthdr != NULL)
			VSB_printf(vsb, "Host: %s\r\n", be->hosthdr);
		VSB_printf(vsb, "Connection: close\r\n");
		VSB_printf(vsb, "\r\n");
	}
	AZ(VSB_finish(vsb));
	vt->req = strdup(VSB_data(vsb));
	AN(vt->req);
	vt->req_len = VSB_len(vsb);
	VSB_destroy(&vsb);
}

/*--------------------------------------------------------------------
 * Sanitize and set defaults
 * XXX: we could make these defaults parameters
 */

static void
vbp_set_defaults(struct vbp_target *vt, const struct vrt_backend_probe *vp)
{

#define DN(x)	do { vt->x = vp->x; } while (0)
	VRT_BACKEND_PROBE_HANDLE();
#undef DN

	if (vt->timeout == 0.0)
		vt->timeout = 2.0;
	if (vt->interval == 0.0)
		vt->interval = 5.0;
	if (vt->window == 0)
		vt->window = 8;
	if (vt->threshold == 0)
		vt->threshold = 3;
	if (vt->exp_status == 0)
		vt->exp_status = 200;

	if (vt->initial == ~0U)
		vt->initial = vt->threshold - 1;

	if (vt->initial > vt->threshold)
		vt->initial = vt->threshold;
}

/*--------------------------------------------------------------------
 */

void
VBP_Control(const struct backend *be, int enable)
{
	struct vbp_target *vt;

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	vt = be->probe;
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	vbp_reset(vt);
	VBP_Update_Backend(vt);

	Lck_Lock(&vbp_mtx);
	if (enable) {
		assert(vt->heap_idx == BINHEAP_NOIDX);
		vt->due = VTIM_real();
		binheap_insert(vbp_heap, vt);
		AZ(pthread_cond_signal(&vbp_cond));
	} else {
		assert(vt->heap_idx != BINHEAP_NOIDX);
		binheap_delete(vbp_heap, vt->heap_idx);
	}
	Lck_Unlock(&vbp_mtx);
}

/*--------------------------------------------------------------------
 * Insert/Remove/Use called from cache_backend.c
 */

void
VBP_Insert(struct backend *b, const struct vrt_backend_probe *vp,
    struct tcp_pool *tp)
{
	struct vbp_target *vt;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	CHECK_OBJ_NOTNULL(vp, VRT_BACKEND_PROBE_MAGIC);

	AZ(b->probe);

	ALLOC_OBJ(vt, VBP_TARGET_MAGIC);
	XXXAN(vt);

	vt->tcp_pool = tp;
	VTP_AddRef(vt->tcp_pool);
	vt->backend = b;
	b->probe = vt;

	vbp_set_defaults(vt, vp);
	vbp_build_req(vt, vp, b);

	vbp_reset(vt);
	VBP_Update_Backend(vt);
}

void
VBP_Remove(struct backend *be)
{
	struct vbp_target *vt;

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	vt = be->probe;
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	Lck_Lock(&vbp_mtx);
	VRT_SetHealth(be->director, 1);
	be->probe = NULL;
	vt->backend = NULL;
	if (vt->running) {
		vt->running = -1;
		vt = NULL;
	}
	Lck_Unlock(&vbp_mtx);
	if (vt != NULL) {
		assert(vt->heap_idx == BINHEAP_NOIDX);
		vbp_delete(vt);
	}
}

/*-------------------------------------------------------------------*/

static int v_matchproto_(binheap_cmp_t)
vbp_cmp(void *priv, const void *a, const void *b)
{
	const struct vbp_target *aa, *bb;

	AZ(priv);
	CAST_OBJ_NOTNULL(aa, a, VBP_TARGET_MAGIC);
	CAST_OBJ_NOTNULL(bb, b, VBP_TARGET_MAGIC);

	if (aa->running && !bb->running)
		return (0);

	return (aa->due < bb->due);
}

static void v_matchproto_(binheap_update_t)
vbp_update(void *priv, void *p, unsigned u)
{
	struct vbp_target *vt;

	AZ(priv);
	CAST_OBJ_NOTNULL(vt, p, VBP_TARGET_MAGIC);
	vt->heap_idx = u;
}

/*-------------------------------------------------------------------*/

void
VBP_Init(void)
{
	pthread_t thr;

	Lck_New(&vbp_mtx, lck_backend);
	vbp_heap = binheap_new(NULL, vbp_cmp, vbp_update);
	AN(vbp_heap);
	AZ(pthread_cond_init(&vbp_cond, NULL));
	WRK_BgThread(&thr, "backend-poller", vbp_thread, NULL);
}
