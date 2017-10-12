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
#include <stdlib.h>

#include "cache.h"

#include "binary_heap.h"
#include "vcli_priv.h"
#include "vrt.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

#include "cache_director.h"
#include "cache_backend.h"

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
#define BITMAP(n, c, t, b)	uint64_t	n;
#include "tbl/backend_poll.h"
#undef BITMAP

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

/*--------------------------------------------------------------------*/

static void
vbp_delete(struct vbp_target *vt)
{
#define DN(x)	/**/
	VRT_BACKEND_PROBE_HANDLE();
#undef DN
	VBT_Rel(&vt->tcp_pool);
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
#undef BITMAP

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

static void
vbp_update_backend(struct vbp_target *vt)
{
	unsigned i;
	char bits[10];
	const char *logmsg;

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	Lck_Lock(&vbp_mtx);
	if (vt->backend != NULL) {
		i = 0;
#define BITMAP(n, c, t, b) \
		bits[i++] = (vt->n & 1) ? c : '-';
#include "tbl/backend_poll.h"
#undef BITMAP
		bits[i] = '\0';

		if (vt->good >= vt->threshold) {
			if (vt->backend->healthy)
				logmsg = "Still healthy";
			else {
				logmsg = "Back healthy";
				vt->backend->health_changed = VTIM_real();
			}
			vt->backend->healthy = 1;
		} else {
			if (vt->backend->healthy) {
				logmsg = "Went sick";
				vt->backend->health_changed = VTIM_real();
			} else
				logmsg = "Still sick";
			vt->backend->healthy = 0;
		}
		VSL(SLT_Backend_health, 0, "%s %s %s %u %u %u %.6f %.6f %s",
		    vt->backend->display_name, logmsg, bits,
		    vt->good, vt->threshold, vt->window,
		    vt->last, vt->avg, vt->resp_buf);
		if (vt->backend != NULL && vt->backend->vsc != NULL)
			vt->backend->vsc->happy = vt->happy;
	}
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
#undef BITMAP

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

static void
vbp_poke(struct vbp_target *vt)
{
	int s, tmo, i;
	double t_start, t_now, t_end;
	unsigned rlen, resp;
	char buf[8192], *p;
	struct pollfd pfda[1], *pfd = pfda;
	const struct suckaddr *sa;

	t_start = t_now = VTIM_real();
	t_end = t_start + vt->timeout;

	s = VBT_Open(vt->tcp_pool, t_end - t_now, &sa);
	if (s < 0) {
		/* Got no connection: failed */
		return;
	}

	i = VSA_Get_Proto(sa);
	if (i == AF_INET)
		vt->good_ipv4 |= 1;
	else if(i == AF_INET6)
		vt->good_ipv6 |= 1;
	else
		WRONG("Wrong probe protocol family");

	t_now = VTIM_real();
	tmo = (int)round((t_end - t_now) * 1e3);
	if (tmo <= 0) {
		/* Spent too long time getting it */
		VTCP_close(&s);
		return;
	}

	/* Send the request */
	i = write(s, vt->req, vt->req_len);
	if (i != vt->req_len) {
		if (i < 0)
			vt->err_xmit |= 1;
		VTCP_close(&s);
		return;
	}
	vt->good_xmit |= 1;

	pfd->fd = s;
	rlen = 0;
	while (1) {
		pfd->events = POLLIN;
		pfd->revents = 0;
		tmo = (int)round((t_end - t_now) * 1e3);
		if (tmo > 0)
			i = poll(pfd, 1, tmo);
		if (i == 0 || tmo <= 0) {
			if (i == 0)
				vt->err_recv |= 1;
			VTCP_close(&s);
			return;
		}
		if (rlen < sizeof vt->resp_buf)
			i = read(s, vt->resp_buf + rlen,
			    sizeof vt->resp_buf - rlen);
		else
			i = read(s, buf, sizeof buf);
		if (i <= 0)
			break;
		rlen += i;
	}

	VTCP_close(&s);

	if (i < 0) {
		vt->err_recv |= 1;
		return;
	}

	if (rlen == 0)
		return;

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

	i = sscanf(vt->resp_buf, "HTTP/%*f %u %s", &resp, buf);

	if ((i == 1 || i == 2) && resp == vt->exp_status)
		vt->happy |= 1;
}

/*--------------------------------------------------------------------
 */

static void __match_proto__(task_func_t)
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
	vbp_update_backend(vt);

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

static void * __match_proto__()
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
	Lck_Unlock(&vbp_mtx);
	NEEDLESS_RETURN(NULL);
}


/*--------------------------------------------------------------------
 * Cli functions
 */

static void
vbp_bitmap(struct cli *cli, char c, uint64_t map, const char *lbl)
{
	int i;
	uint64_t u = (1ULL << 63);

	VCLI_Out(cli, "  ");
	for (i = 0; i < 64; i++) {
		if (map & u)
			VCLI_Out(cli, "%c", c);
		else
			VCLI_Out(cli, "-");
		map <<= 1;
	}
	VCLI_Out(cli, " %s\n", lbl);
}

/*lint -e{506} constant value boolean */
/*lint -e{774} constant value boolean */
static void
vbp_health_one(struct cli *cli, const struct vbp_target *vt)
{

	VCLI_Out(cli,
	    "  Current states  good: %2u threshold: %2u window: %2u\n",
	    vt->good, vt->threshold, vt->window);
	VCLI_Out(cli,
	    "  Average response time of good probes: %.6f\n", vt->avg);
	VCLI_Out(cli,
	    "  Oldest ======================"
	    "============================ Newest\n");

#define BITMAP(n, c, t, b)					\
		if ((vt->n != 0) || (b))			\
			vbp_bitmap(cli, (c), vt->n, (t));
#include "tbl/backend_poll.h"
#undef BITMAP
}

void
VBP_Status(struct cli *cli, const struct backend *be, int details)
{
	struct vbp_target *vt;

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	vt = be->probe;
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);
	VCLI_Out(cli, "%d/%d", vt->good, vt->window);
	if (details) {
		VCLI_Out(cli, "\n");
		vbp_health_one(cli, vt);
	}
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
	if(vbp->request != NULL) {
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
	VSB_delete(vsb);
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
	vbp_update_backend(vt);

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
	vt->backend = b;
	b->probe = vt;

	vbp_set_defaults(vt, vp);
	vbp_build_req(vt, vp, b);

	vbp_reset(vt);
	vbp_update_backend(vt);
}

void
VBP_Remove(struct backend *be)
{
	struct vbp_target *vt;

	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	vt = be->probe;
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	Lck_Lock(&vbp_mtx);
	be->healthy = 1;
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

static int __match_proto__(binheap_cmp_t)
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

static void __match_proto__(binheap_update_t)
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
	WRK_BgThread(&thr, "Backend poller", vbp_thread, NULL);
}
