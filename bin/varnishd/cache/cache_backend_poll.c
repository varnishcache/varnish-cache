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

#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#include "cache_backend.h"
#include "vcli_priv.h"
#include "vrt.h"
#include "vtcp.h"
#include "vtim.h"

/* Default averaging rate, we want something pretty responsive */
#define AVG_RATE			4

struct vbp_vcl {
	unsigned			magic;
#define VBP_VCL_MAGIC			0x70829764

	VTAILQ_ENTRY(vbp_vcl)		list;
	const struct vrt_backend_probe	*probep;
	struct vrt_backend_probe	probe;
	const char			*hosthdr;
};

struct vbp_target {
	unsigned			magic;
#define VBP_TARGET_MAGIC		0x6b7cb656

	struct backend			*backend;
	VTAILQ_HEAD( ,vbp_vcl)		vcls;

	struct vrt_backend_probe	probe;
	int				stop;
	struct vsb			*vsb;
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

static struct lock			vbp_mtx;

/*--------------------------------------------------------------------
 * Poke one backend, once, but possibly at both IPv4 and IPv6 addresses.
 *
 * We do deliberately not use the stuff in cache_backend.c, because we
 * want to measure the backends response without local distractions.
 */

static int
vbp_connect(int pf, const struct suckaddr *sa, int tmo)
{
	int s, i;

	s = socket(pf, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	i = VTCP_connect(s, sa, tmo);
	if (i == 0)
		return (s);
	VTCP_close(&s);
	return (-1);
}

static void
vbp_poke(struct vbp_target *vt)
{
	int s, tmo, i;
	double t_start, t_now, t_end;
	unsigned rlen, resp;
	struct backend *bp;
	char buf[8192], *p;
	struct pollfd pfda[1], *pfd = pfda;

	bp = vt->backend;
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);

	t_start = t_now = VTIM_real();
	t_end = t_start + vt->probe.timeout;
	tmo = (int)round((t_end - t_now) * 1e3);

	s = -1;
	if (cache_param->prefer_ipv6 && bp->ipv6 != NULL) {
		s = vbp_connect(PF_INET6, bp->ipv6, tmo);
		t_now = VTIM_real();
		tmo = (int)round((t_end - t_now) * 1e3);
		if (s >= 0)
			vt->good_ipv6 |= 1;
	}
	if (tmo > 0 && s < 0 && bp->ipv4 != NULL) {
		s = vbp_connect(PF_INET, bp->ipv4, tmo);
		t_now = VTIM_real();
		tmo = (int)round((t_end - t_now) * 1e3);
		if (s >= 0)
			vt->good_ipv4 |= 1;
	}
	if (tmo > 0 && s < 0 && bp->ipv6 != NULL) {
		s = vbp_connect(PF_INET6, bp->ipv6, tmo);
		t_now = VTIM_real();
		tmo = (int)round((t_end - t_now) * 1e3);
		if (s >= 0)
			vt->good_ipv6 |= 1;
	}
	if (s < 0) {
		/* Got no connection: failed */
		return;
	}
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
	    vt->backend->vcl_name, logmsg, bits,
	    vt->good, vt->probe.threshold, vt->probe.window,
	    vt->last, vt->avg, vt->resp_buf);
	vt->backend->vsc->happy = vt->happy;
}

/*--------------------------------------------------------------------
 * Build request from probe spec
 */

static void
vbp_build_req(struct vsb *vsb, const struct vbp_vcl *vcl)
{

	XXXAN(vsb);
	XXXAN(vcl);
	VSB_clear(vsb);
	if(vcl->probe.request != NULL) {
		VSB_cat(vsb, vcl->probe.request);
	} else {
		VSB_printf(vsb, "GET %s HTTP/1.1\r\n",
		    vcl->probe.url != NULL ?  vcl->probe.url : "/");
		if (vcl->hosthdr != NULL)
			VSB_printf(vsb, "Host: %s\r\n", vcl->hosthdr);
		VSB_printf(vsb, "Connection: close\r\n");
		VSB_printf(vsb, "\r\n");
	}
	AZ(VSB_finish(vsb));
}

/*--------------------------------------------------------------------
 * One thread per backend to be poked.
 */

static void *
vbp_wrk_poll_backend(void *priv)
{
	struct vbp_target *vt;
	struct vbp_vcl *vcl = NULL;

	THR_SetName("backend poll");

	CAST_OBJ_NOTNULL(vt, priv, VBP_TARGET_MAGIC);

	while (!vt->stop) {
		Lck_Lock(&vbp_mtx);
		if (VTAILQ_FIRST(&vt->vcls) != vcl) {
			vcl = VTAILQ_FIRST(&vt->vcls);
			vbp_build_req(vt->vsb, vcl);
			vt->probe = vcl->probe;
		}
		Lck_Unlock(&vbp_mtx);

		vt->req = VSB_data(vt->vsb);
		vt->req_len = VSB_len(vt->vsb);
		assert(vt->req_len > 0);

		vbp_start_poke(vt);
		vbp_poke(vt);
		vbp_has_poked(vt);

		if (!vt->stop)
			VTIM_sleep(vt->probe.interval);
	}
	return (NULL);
}

/*--------------------------------------------------------------------
 * Cli functions
 */

void
VBP_Summary(struct cli *cli, const struct vbp_target *vt)
{

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);
	VCLI_Out(cli, "%d/%d", vt->good, vt->probe.window);
}

static void
vbp_bitmap(struct cli *cli, char c, uint64_t map, const char *lbl)
{
	int i;
	uint64_t u = (1ULL << 63);

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

	VCLI_Out(cli, "Backend %s is %s\n",
	    vt->backend->vcl_name,
	    vt->backend->healthy ? "Healthy" : "Sick");
	VCLI_Out(cli, "Current states  good: %2u threshold: %2u window: %2u\n",
	    vt->good, vt->probe.threshold, vt->probe.window);
	VCLI_Out(cli, "Average responsetime of good probes: %.6f\n", vt->avg);
	VCLI_Out(cli,
	    "Oldest                       "
	    "                             Newest\n");
	VCLI_Out(cli,
	    "============================="
	    "===================================\n");

#define BITMAP(n, c, t, b)					\
		if ((vt->n != 0) || (b))			\
			vbp_bitmap(cli, (c), vt->n, (t));
#include "tbl/backend_poll.h"
#undef BITMAP
}

static void
vbp_health(struct cli *cli, const char * const *av, void *priv)
{
	struct vbp_target *vt;

	ASSERT_CLI();
	(void)av;
	(void)priv;

	VTAILQ_FOREACH(vt, &vbp_list, list)
		vbp_health_one(cli, vt);
}

static struct cli_proto debug_cmds[] = {
	{ "debug.health", "debug.health",
		"\tDump backend health stuff\n",
		0, 0, "d", vbp_health },
	{ NULL }
};

/*--------------------------------------------------------------------
 * A new VCL wants to probe this backend,
 */

static struct vbp_vcl *
vbp_new_vcl(const struct vrt_backend_probe *p, const char *hosthdr)
{
	struct vbp_vcl *vcl;

	ALLOC_OBJ(vcl, VBP_VCL_MAGIC);
	XXXAN(vcl);
	vcl->probep = p;
	vcl->probe = *p;
	vcl->hosthdr = hosthdr;

	/*
	 * Sanitize and insert defaults
	 * XXX: we could make these defaults parameters
	 */
	if (vcl->probe.timeout == 0.0)
		vcl->probe.timeout = 2.0;
	if (vcl->probe.interval == 0.0)
		vcl->probe.interval = 5.0;
	if (vcl->probe.window == 0)
		vcl->probe.window = 8;
	if (vcl->probe.threshold == 0)
		vcl->probe.threshold = 3;
	if (vcl->probe.exp_status == 0)
		vcl->probe.exp_status = 200;

	if (vcl->probe.initial == ~0U)
		vcl->probe.initial = vcl->probe.threshold - 1;

	if (vcl->probe.initial > vcl->probe.threshold)
		vcl->probe.initial = vcl->probe.threshold;
	return (vcl);
}

/*--------------------------------------------------------------------
 * Insert/Remove/Use called from cache_backend.c
 */

void
VBP_Insert(struct backend *b, const struct vrt_backend_probe *p,
    const char *hosthdr)
{
	struct vbp_target *vt;
	struct vbp_vcl *vcl;
	int startthread = 0;
	unsigned u;

	ASSERT_CLI();
	AN(p);

	if (b->probe == NULL) {
		ALLOC_OBJ(vt, VBP_TARGET_MAGIC);
		XXXAN(vt);
		VTAILQ_INIT(&vt->vcls);
		vt->backend = b;
		vt->vsb = VSB_new_auto();
		XXXAN(vt->vsb);
		b->probe = vt;
		startthread = 1;
		VTAILQ_INSERT_TAIL(&vbp_list, vt, list);
	} else {
		vt = b->probe;
	}

	VTAILQ_FOREACH(vcl, &vt->vcls, list)
		assert(vcl->probep != p);

	vcl = vbp_new_vcl(p, hosthdr);
	Lck_Lock(&vbp_mtx);
	VTAILQ_INSERT_TAIL(&vt->vcls, vcl, list);
	Lck_Unlock(&vbp_mtx);

	if (startthread) {
		for (u = 0; u < vcl->probe.initial; u++) {
			vbp_start_poke(vt);
			vt->happy |= 1;
			vbp_has_poked(vt);
		}
		AZ(pthread_create(&vt->thread, NULL, vbp_wrk_poll_backend, vt));
	}
}

void
VBP_Use(const struct backend *b, const struct vrt_backend_probe *p)
{
	struct vbp_target *vt;
	struct vbp_vcl *vcl;

	ASSERT_CLI();
	AN(p);
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	AN(b->probe);
	vt = b->probe;

	VTAILQ_FOREACH(vcl, &vt->vcls, list) {
		if (vcl->probep != p)
			continue;

		Lck_Lock(&vbp_mtx);
		VTAILQ_REMOVE(&vt->vcls, vcl, list);
		VTAILQ_INSERT_HEAD(&vt->vcls, vcl, list);
		Lck_Unlock(&vbp_mtx);
		return;
	}
}

void
VBP_Remove(struct backend *b, struct vrt_backend_probe const *p)
{
	struct vbp_target *vt;
	struct vbp_vcl *vcl;
	void *ret;

	ASSERT_CLI();
	AN(p);
	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);
	AN(b->probe);
	vt = b->probe;

	VTAILQ_FOREACH(vcl, &vt->vcls, list)
		if (vcl->probep == p)
			break;

	AN(vcl);

	Lck_Lock(&vbp_mtx);
	VTAILQ_REMOVE(&vt->vcls, vcl, list);
	Lck_Unlock(&vbp_mtx);

	FREE_OBJ(vcl);

	if (!VTAILQ_EMPTY(&vt->vcls))
		return;

	/* No more polling for this backend */

	b->healthy = 1;

	vt->stop = 1;
	AZ(pthread_cancel(vt->thread));
	AZ(pthread_join(vt->thread, &ret));

	b->healthy = 1;

	VTAILQ_REMOVE(&vbp_list, vt, list);
	b->probe = NULL;
	VSB_delete(vt->vsb);
	FREE_OBJ(vt);
}

/*--------------------------------------------------------------------
 * Initialize the backend probe subsystem
 */

void
VBP_Init(void)
{

	Lck_New(&vbp_mtx, lck_vbp);
	CLI_AddFuncs(debug_cmds);
}
