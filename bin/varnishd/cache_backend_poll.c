/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id: cache_backend_cfg.c 2905 2008-07-08 10:09:03Z phk $
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

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <sys/socket.h>

#include "shmlog.h"
#include "cli_priv.h"
#include "cache.h"
#include "vrt.h"
#include "cache_backend.h"

struct vbp_target {
	unsigned			magic;
#define VBP_TARGET_MAGIC		0x6b7cb656

	struct backend			*backend;
	struct vrt_backend_probe 	probe;
	int				stop;
	char				*req;
	int				req_len;

	unsigned			good;
	
	/* Collected statistics */
#define BITMAP(n, c, t, b)	uint64_t	n;
#include "cache_backend_poll.h"
#undef BITMAP

	VTAILQ_ENTRY(vbp_target)	list;
	pthread_t			thread;
};

static VTAILQ_HEAD(, vbp_target)	vbp_list =
    VTAILQ_HEAD_INITIALIZER(vbp_list);

static char default_request[] = 
    "GET / HTTP/1.1\r\n"
    "Connection: close\r\n"
    "\r\n";

static void
dsleep(double t)
{
	if (t > 100.0)
		(void)sleep((int)round(t));
	else
		(void)usleep((int)round(t * 1e6));
}

/*--------------------------------------------------------------------
 * Poke one backend, once, but possibly at both IPv4 and IPv6 addresses.
 *
 * We do deliberately not use the stuff in cache_backend.c, because we
 * want to measure the backends response without local distractions.
 */

static int
vbp_connect(int pf, const struct sockaddr *sa, socklen_t salen, int tmo)
{
	int s, i;

	s = socket(pf, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	i = TCP_connect(s, sa, salen, tmo);
	if (i == 0)
		return (s);
	TCP_close(&s);
	return (-1);
}

static int
vbp_poke(struct vbp_target *vt)
{
	int s, tmo, i;
	double t_start, t_now, t_end, rlen;
	struct backend *bp;
	char buf[8192];
	struct pollfd pfda[1], *pfd = pfda;

	bp = vt->backend;
	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);

	t_start = t_now = TIM_real();
	t_end = t_start + vt->probe.timeout;
	tmo = (int)round((t_end - t_now) * 1e3);

	s = -1;
	if (params->prefer_ipv6 && bp->ipv6 != NULL) {
		s = vbp_connect(PF_INET6, bp->ipv6, bp->ipv6len, tmo);
		t_now = TIM_real();
		tmo = (int)round((t_end - t_now) * 1e3);
		if (s >= 0)
			vt->good_ipv6 |= 1;
	}
	if (tmo > 0 && s < 0 && bp->ipv4 != NULL) {
		s = vbp_connect(PF_INET, bp->ipv4, bp->ipv4len, tmo);
		t_now = TIM_real();
		tmo = (int)round((t_end - t_now) * 1e3);
		if (s >= 0)
			vt->good_ipv4 |= 1;
	}
	if (tmo > 0 && s < 0 && bp->ipv6 != NULL) {
		s = vbp_connect(PF_INET6, bp->ipv6, bp->ipv6len, tmo);
		t_now = TIM_real();
		tmo = (int)round((t_end - t_now) * 1e3);
		if (s >= 0)
			vt->good_ipv6 |= 1;
	}
	if (s < 0) {
		/* Got no connection: failed */
		return (0);
	}

	i = write(s, vt->req, vt->req_len);
	if (i != vt->req_len) {
		if (i < 0)
			vt->err_xmit |= 1;
		TCP_close(&s);
		return (0);
	}
	vt->good_xmit |= 1;
	i = shutdown(s, SHUT_WR);
	if (i != 0) {
		vt->err_shut |= 1;
		TCP_close(&s);
		return (0);
	}
	vt->good_shut |= 1;

	t_now = TIM_real();
	tmo = (int)round((t_end - t_now) * 1e3);
	if (tmo < 0) {
		TCP_close(&s);
		return (0);
	}

	pfd->fd = s;
	rlen = 0;
	do {
		pfd->events = POLLIN;
		pfd->revents = 0;
		tmo = (int)round((t_end - t_now) * 1e3);
		if (tmo > 0)
			i = poll(pfd, 1, tmo);
		if (i == 0 || tmo <= 0) {
			TCP_close(&s);
			return (0);
		}
		i = read(s, buf, sizeof buf);
		rlen += i;
	} while (i > 0);

	if (i < 0) {
		vt->err_recv |= 1;
		TCP_close(&s);
		return (0);
	}

	TCP_close(&s);
	t_now = TIM_real();
	vt->good_recv |= 1;
	/* XXX: Check reponse status */
	vt->happy |= 1;
	return (1);
}

/*--------------------------------------------------------------------
 * One thread per backend to be poked.
 */

static void *
vbp_wrk_poll_backend(void *priv)
{
	struct vbp_target *vt;
	unsigned i, j;
	uint64_t u;
	const char *logmsg;
	char bits[10];

	THR_SetName("backend poll");

	CAST_OBJ_NOTNULL(vt, priv, VBP_TARGET_MAGIC);

	/*
	 * Establish defaults
	 * XXX: we could make these defaults parameters
	 */
	if (vt->probe.request == NULL)
		vt->probe.request = default_request;
	if (vt->probe.timeout == 0.0)
		vt->probe.timeout = 2.0;
	if (vt->probe.interval == 0.0)
		vt->probe.timeout = 5.0;
	if (vt->probe.window == 0)
		vt->probe.window = 8;
	if (vt->probe.threshold == 0)
		vt->probe.threshold = 3;

	printf("Probe(\"%s\", %g, %g)\n",
	    vt->req,
	    vt->probe.timeout,
	    vt->probe.interval);

	vt->req_len = strlen(vt->probe.request);

	/*lint -e{525} indent */
	while (!vt->stop) {
#define BITMAP(n, c, t, b)	vt->n <<= 1;
#include "cache_backend_poll.h"
#undef BITMAP
		vbp_poke(vt);

		i = 0;
#define BITMAP(n, c, t, b)	bits[i++] = (vt->n & 1) ? c : '-';
#include "cache_backend_poll.h"
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
			else
				logmsg = "Back healthy";
			vt->backend->healthy = 1;
		} else {
			if (vt->backend->healthy)
				logmsg = "Went sick";
			else
				logmsg = "Still sick";
			vt->backend->healthy = 0;
		}
		VSL(SLT_Backend_health, 0, "%s %s %s %u %u %u",
		    vt->backend->vcl_name, logmsg, bits,
		    vt->good, vt->probe.threshold, vt->probe.window);
			
		if (!vt->stop)
			dsleep(vt->probe.interval);
	}
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

	for (i = 0; i < 64; i++) {
		if (map & u)
			cli_out(cli, "%c", c);
		else
			cli_out(cli, "-");
		map <<= 1;
	}
	cli_out(cli, " %s\n", lbl);
}

/*lint -e{506} constant value boolean */
/*lint -e{774} constant value boolean */
static void
vbp_health_one(struct cli *cli, const struct vbp_target *vt)
{

	cli_out(cli, "Backend %s is %s\n",
	    vt->backend->vcl_name,
	    vt->backend->healthy ? "Healthy" : "Sick");
	cli_out(cli, "Current states  good: %2u threshold: %2u window: %2u\n",
	    vt->good, vt->probe.threshold, vt->probe.window);
	cli_out(cli, 
	    "Oldest                       "
	    "                             Newest\n");
	cli_out(cli, 
	    "============================="
	    "===================================\n");

#define BITMAP(n, c, t, b)					\
		if ((vt->n != 0) || (b)) 				\
			vbp_bitmap(cli, (c), vt->n, (t));
#include "cache_backend_poll.h"
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
                0, 0, vbp_health },
        { NULL }
};

/*--------------------------------------------------------------------
 * Start/Stop called from cache_backend_cfg.c
 */

void
VBP_Start(struct backend *b, struct vrt_backend_probe const *p)
{
	struct vbp_target *vt;
	struct vsb *vsb;

	ASSERT_CLI();

	ALLOC_OBJ(vt, VBP_TARGET_MAGIC);
	AN(vt);
	if (!memcmp(&vt->probe, p, sizeof *p)) {
		FREE_OBJ(vt);
		return;
	}
	vt->backend = b;
	vt->probe = *p;

	if(p->request != NULL) {
		vt->req = strdup(p->request);
		XXXAN(vt->req);
	} else {
		vsb = vsb_newauto();
		XXXAN(vsb);
		vsb_printf(vsb, "GET %s HTTP/1.1\r\n",
		    p->url != NULL ? p->url : "/");
		vsb_printf(vsb, "Connection: close\r\n");
		if (b->hosthdr != NULL)
			vsb_printf(vsb, "Host: %s\r\n", b->hosthdr);
		vsb_printf(vsb, "\r\n", b->hosthdr);
		vsb_finish(vsb);
		AZ(vsb_overflowed(vsb));
		vt->req = strdup(vsb_data(vsb));
		XXXAN(vt->req);
		vsb_delete(vsb);
	}
	vt->req_len = strlen(vt->req);

	b->probe = vt;

	VTAILQ_INSERT_TAIL(&vbp_list, vt, list);

	AZ(pthread_create(&vt->thread, NULL, vbp_wrk_poll_backend, vt));
}

void
VBP_Stop(struct backend *b)
{
	struct vbp_target *vt;
	void *ret;

	CHECK_OBJ_NOTNULL(b, BACKEND_MAGIC);

	ASSERT_CLI();
	if (b->probe == NULL)
		return;
	CHECK_OBJ_NOTNULL(b->probe, VBP_TARGET_MAGIC);
	vt = b->probe;

	vt->stop = 1;
	AZ(pthread_cancel(vt->thread));
	AZ(pthread_join(vt->thread, &ret));

	VTAILQ_REMOVE(&vbp_list, vt, list);
	b->probe = NULL;
	FREE_OBJ(vt);
}

/*--------------------------------------------------------------------
 * Initialize the backend probe subsystem
 */

void
VBP_Init(void)
{

	CLI_AddFuncs(DEBUG_CLI, debug_cmds);
}
