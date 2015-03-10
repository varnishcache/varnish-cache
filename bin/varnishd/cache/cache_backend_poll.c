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

#include "cache_backend.h"
#include "vcli_priv.h"
#include "vrt.h"
#include "vtim.h"
#include "vtcp.h"
#include "vsa.h"

/* Default averaging rate, we want something pretty responsive */
#define AVG_RATE			4

struct vbp_target {
	unsigned			magic;
#define VBP_TARGET_MAGIC		0x6b7cb656

	struct lock			mtx;
	int				disable;
	int				stop;
	struct backend			*backend;

	struct tcp_pool			*tcp_pool;

	struct vrt_backend_probe	probe;

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

	VTAILQ_ENTRY(vbp_target)	list;
	pthread_t			thread;
};

static VTAILQ_HEAD(, vbp_target)	vbp_list =
    VTAILQ_HEAD_INITIALIZER(vbp_list);

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
	t_end = t_start + vt->probe.timeout;

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
	do {
		pfd->events = POLLIN;
		pfd->revents = 0;
		tmo = (int)round((t_end - t_now) * 1e3);
		if (tmo > 0)
			i = poll(pfd, 1, tmo);
		if (i == 0 || tmo <= 0) {
			VTCP_close(&s);
			return;
		}
		if (rlen < sizeof vt->resp_buf)
			i = read(s, vt->resp_buf + rlen,
			    sizeof vt->resp_buf - rlen);
		else
			i = read(s, buf, sizeof buf);
		rlen += i;
	} while (i > 0);

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

	if ((i == 1 || i == 2) && resp == vt->probe.exp_status)
		vt->happy |= 1;
}

/*--------------------------------------------------------------------
 * Record pokings...
 */

static void
vbp_start_poke(struct vbp_target *vt)
{
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

#define BITMAP(n, c, t, b)	vt->n <<= 1;
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
	const char *logmsg;
	char bits[10];

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	/* Calculate exponential average */
	if (vt->happy & 1) {
		if (vt->rate < AVG_RATE)
			vt->rate += 1.0;
		vt->avg += (vt->last - vt->avg) / vt->rate;
	}

	i = 0;
#define BITMAP(n, c, t, b)	bits[i++] = (vt->n & 1) ? c : '-';
#include "tbl/backend_poll.h"
#undef BITMAP
	bits[i] = '\0';

	u = vt->happy;
	for (i = j = 0; i < vt->probe.window; i++) {
		if (u & 1)
			j++;
		u >>= 1;
	}
	vt->good = j;

	Lck_Lock(&vt->mtx);
	if (vt->backend != NULL) {
		if (vt->good >= vt->probe.threshold) {
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
		    vt->good, vt->probe.threshold, vt->probe.window,
		    vt->last, vt->avg, vt->resp_buf);
		if (!vt->disable) {
			AN(vt->backend->vsc);
			vt->backend->vsc->happy = vt->happy;
		}
	}
	Lck_Unlock(&vt->mtx);
}

/*--------------------------------------------------------------------
 * One thread per backend to be poked.
 */

static void *
vbp_wrk_poll_backend(void *priv)
{
	struct vbp_target *vt;

	THR_SetName("backend poll");

	CAST_OBJ_NOTNULL(vt, priv, VBP_TARGET_MAGIC);

	while (!vt->stop) {
		AN(vt->req);
		assert(vt->req_len > 0);

		if (!vt->disable) {
			vbp_start_poke(vt);
			vbp_poke(vt);
			vbp_has_poked(vt);
		}

		if (!vt->stop)
			VTIM_sleep(vt->probe.interval);
	}
	Lck_Delete(&vt->mtx);
	VTAILQ_REMOVE(&vbp_list, vt, list);
	VBT_Rel(&vt->tcp_pool);
	free(vt->req);
	FREE_OBJ(vt);
	return (NULL);
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
	    vt->good, vt->probe.threshold, vt->probe.window);
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
	VCLI_Out(cli, "%d/%d", vt->good, vt->probe.window);
	if (details) {
		VCLI_Out(cli, "\n");
		vbp_health_one(cli, vt);
	}
}

/*--------------------------------------------------------------------
 * Build request from probe spec
 */

static void
vbp_build_req(struct vbp_target *vt, const char *hosthdr)
{
	struct vsb *vsb;

	vsb = VSB_new_auto();
	AN(vsb);
	VSB_clear(vsb);
	if(vt->probe.request != NULL) {
		VSB_cat(vsb, vt->probe.request);
	} else {
		VSB_printf(vsb, "GET %s HTTP/1.1\r\n",
		    vt->probe.url != NULL ?  vt->probe.url : "/");
		if (hosthdr != NULL)
			VSB_printf(vsb, "Host: %s\r\n", hosthdr);
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
vbp_set_defaults(struct vbp_target *vt)
{

	if (vt->probe.timeout == 0.0)
		vt->probe.timeout = 2.0;
	if (vt->probe.interval == 0.0)
		vt->probe.interval = 5.0;
	if (vt->probe.window == 0)
		vt->probe.window = 8;
	if (vt->probe.threshold == 0)
		vt->probe.threshold = 3;
	if (vt->probe.exp_status == 0)
		vt->probe.exp_status = 200;

	if (vt->probe.initial == ~0U)
		vt->probe.initial = vt->probe.threshold - 1;

	if (vt->probe.initial > vt->probe.threshold)
		vt->probe.initial = vt->probe.threshold;
}

/*--------------------------------------------------------------------
 */

void
VBP_Control(const struct backend *be, int stop)
{
	struct vbp_target *vt;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	vt = be->probe;
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	Lck_Lock(&vt->mtx);
	vt->disable = stop;
	Lck_Unlock(&vt->mtx);
}

/*--------------------------------------------------------------------
 * Insert/Remove/Use called from cache_backend.c
 */

void
VBP_Insert(struct backend *b, const struct vrt_backend_probe *p,
    const char *hosthdr)
{
	struct vbp_target *vt;
	unsigned u;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	CHECK_OBJ_NOTNULL(p, VRT_BACKEND_PROBE_MAGIC);

	AZ(b->probe);

	ALLOC_OBJ(vt, VBP_TARGET_MAGIC);
	XXXAN(vt);
	VTAILQ_INSERT_TAIL(&vbp_list, vt, list);
	Lck_New(&vt->mtx, lck_backend);
	vt->disable = 1;

	vt->tcp_pool = VBT_Ref(b->ipv4, b->ipv6);
	AN(vt->tcp_pool);

	vt->probe = *p;

	vbp_set_defaults(vt);
	vbp_build_req(vt, hosthdr);

	for (u = 1; u < vt->probe.initial; u++) {
		vbp_start_poke(vt);
		vt->happy |= 1;
		vbp_has_poked(vt);
	}
	vt->backend = b;
	b->probe = vt;
	vbp_has_poked(vt);
	AZ(pthread_create(&vt->thread, NULL, vbp_wrk_poll_backend, vt));
	AZ(pthread_detach(vt->thread));
}

void
VBP_Remove(struct backend *be)
{
	struct vbp_target *vt;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	vt = be->probe;
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	Lck_Lock(&vt->mtx);
	vt->stop = 1;
	vt->backend = NULL;
	Lck_Unlock(&vt->mtx);

	be->healthy = 1;
	be->probe = NULL;
}
