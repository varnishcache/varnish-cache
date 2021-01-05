/*-
 * Copyright (c) 2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 *
 * TCP connection pools.
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"

#include "vend.h"
#include "vsa.h"
#include "vsha256.h"
#include "vtcp.h"
#include "vus.h"
#include "vtim.h"
#include "waiter/waiter.h"

#include "cache_tcp_pool.h"
#include "cache_pool.h"

struct conn_pool;

/*--------------------------------------------------------------------
 */

struct pfd {
	unsigned		magic;
#define PFD_MAGIC		0x0c5e6593
	int			fd;
	VTAILQ_ENTRY(pfd)	list;
	const void		*priv;
	uint8_t			state;
	struct waited		waited[1];
	struct conn_pool	*conn_pool;

	pthread_cond_t		*cond;
};

/*--------------------------------------------------------------------
 */

typedef int cp_open_f(const struct conn_pool *, vtim_dur tmo,
    const void **privp);
typedef void cp_close_f(struct pfd *);
typedef void cp_name_f(const struct pfd *, char *, unsigned, char *, unsigned);

struct cp_methods {
	cp_open_f				*open;
	cp_close_f				*close;
	cp_name_f				*local_name;
	cp_name_f				*remote_name;
};

struct conn_pool {
	unsigned				magic;
#define CONN_POOL_MAGIC				0x85099bc3

	const struct cp_methods			*methods;

	char					ident[VSHA256_DIGEST_LENGTH];
	void					*priv;

	VTAILQ_ENTRY(conn_pool)			list;
	int					refcnt;
	struct lock				mtx;

	VTAILQ_HEAD(, pfd)			connlist;
	int					n_conn;

	VTAILQ_HEAD(, pfd)			killlist;
	int					n_kill;

	int					n_used;

	vtim_mono				holddown;
	int					holddown_errno;
};

struct tcp_pool {
	unsigned				magic;
#define TCP_POOL_MAGIC				0x28b0e42a

	struct suckaddr				*ip4;
	struct suckaddr				*ip6;
	char					*uds;
	struct conn_pool			cp[1];
};

static struct lock		conn_pools_mtx;
static VTAILQ_HEAD(, conn_pool)	conn_pools =
    VTAILQ_HEAD_INITIALIZER(conn_pools);

/*--------------------------------------------------------------------
 */

unsigned
PFD_State(const struct pfd *p)
{
	CHECK_OBJ_NOTNULL(p, PFD_MAGIC);
	return (p->state);
}

int *
PFD_Fd(struct pfd *p)
{
	CHECK_OBJ_NOTNULL(p, PFD_MAGIC);
	return (&(p->fd));
}

void
PFD_LocalName(const struct pfd *p, char *abuf, unsigned alen, char *pbuf,
	      unsigned plen)
{
	CHECK_OBJ_NOTNULL(p, PFD_MAGIC);
	CHECK_OBJ_NOTNULL(p->conn_pool, CONN_POOL_MAGIC);
	p->conn_pool->methods->local_name(p, abuf, alen, pbuf, plen);
}

void
PFD_RemoteName(const struct pfd *p, char *abuf, unsigned alen, char *pbuf,
	       unsigned plen)
{
	CHECK_OBJ_NOTNULL(p, PFD_MAGIC);
	CHECK_OBJ_NOTNULL(p->conn_pool, CONN_POOL_MAGIC);
	p->conn_pool->methods->remote_name(p, abuf, alen, pbuf, plen);
}

/*--------------------------------------------------------------------
 * Waiter-handler
 */

static void  v_matchproto_(waiter_handle_f)
vcp_handle(struct waited *w, enum wait_event ev, vtim_real now)
{
	struct pfd *pfd;
	struct conn_pool *cp;

	CAST_OBJ_NOTNULL(pfd, w->priv1, PFD_MAGIC);
	(void)ev;
	(void)now;
	CHECK_OBJ_NOTNULL(pfd->conn_pool, CONN_POOL_MAGIC);
	cp = pfd->conn_pool;

	Lck_Lock(&cp->mtx);

	switch (pfd->state) {
	case PFD_STATE_STOLEN:
		pfd->state = PFD_STATE_USED;
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		AN(pfd->cond);
		AZ(pthread_cond_signal(pfd->cond));
		break;
	case PFD_STATE_AVAIL:
		cp->methods->close(pfd);
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		cp->n_conn--;
		FREE_OBJ(pfd);
		break;
	case PFD_STATE_CLEANUP:
		cp->methods->close(pfd);
		cp->n_kill--;
		VTAILQ_REMOVE(&cp->killlist, pfd, list);
		memset(pfd, 0x11, sizeof *pfd);
		free(pfd);
		break;
	default:
		WRONG("Wrong pfd state");
	}
	Lck_Unlock(&cp->mtx);
}

/*--------------------------------------------------------------------
 */

static struct conn_pool *
VCP_Ref(const uint8_t *ident)
{
	struct conn_pool *cp;

	Lck_Lock(&conn_pools_mtx);
	VTAILQ_FOREACH(cp, &conn_pools, list) {
		assert(cp->refcnt > 0);
		if (memcmp(ident, cp->ident, sizeof cp->ident))
			continue;
		cp->refcnt++;
		Lck_Unlock(&conn_pools_mtx);
		return (cp);
	}
	Lck_Unlock(&conn_pools_mtx);
	return (NULL);
}

/*--------------------------------------------------------------------
 */

static void *
VCP_New(struct conn_pool *cp, uint8_t ident[VSHA256_DIGEST_LENGTH],
    void *priv, const struct cp_methods *cm)
{

	AN(cp);
	AN(cm);
	AN(cm->open);
	AN(cm->close);

	INIT_OBJ(cp, CONN_POOL_MAGIC);
	memcpy(cp->ident, ident, sizeof cp->ident);
	cp->priv = priv;
	cp->methods = cm;
	cp->refcnt = 1;
	cp->holddown = 0;
	Lck_New(&cp->mtx, lck_tcp_pool);
	VTAILQ_INIT(&cp->connlist);
	VTAILQ_INIT(&cp->killlist);

	Lck_Lock(&conn_pools_mtx);
	VTAILQ_INSERT_HEAD(&conn_pools, cp, list);
	Lck_Unlock(&conn_pools_mtx);

	return (priv);
}


/*--------------------------------------------------------------------
 */

static void
VCP_AddRef(struct conn_pool *cp)
{
	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	Lck_Lock(&conn_pools_mtx);
	assert(cp->refcnt > 0);
	cp->refcnt++;
	Lck_Unlock(&conn_pools_mtx);
}

/*--------------------------------------------------------------------
 * Release Conn pool, destroy if last reference.
 */

static int
VCP_Rel(struct conn_pool *cp)
{
	struct pfd *pfd, *pfd2;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	Lck_Lock(&conn_pools_mtx);
	assert(cp->refcnt > 0);
	if (--cp->refcnt > 0) {
		Lck_Unlock(&conn_pools_mtx);
		return (1);
	}
	AZ(cp->n_used);
	VTAILQ_REMOVE(&conn_pools, cp, list);
	Lck_Unlock(&conn_pools_mtx);

	Lck_Lock(&cp->mtx);
	VTAILQ_FOREACH_SAFE(pfd, &cp->connlist, list, pfd2) {
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		cp->n_conn--;
		assert(pfd->state == PFD_STATE_AVAIL);
		pfd->state = PFD_STATE_CLEANUP;
		(void)shutdown(pfd->fd, SHUT_WR);
		VTAILQ_INSERT_TAIL(&cp->killlist, pfd, list);
		cp->n_kill++;
	}
	while (cp->n_kill) {
		Lck_Unlock(&cp->mtx);
		(void)usleep(20000);
		Lck_Lock(&cp->mtx);
	}
	Lck_Unlock(&cp->mtx);
	Lck_Delete(&cp->mtx);
	AZ(cp->n_conn);
	AZ(cp->n_kill);
	return (0);
}

/*--------------------------------------------------------------------
 * Recycle a connection.
 */

static void
VCP_Recycle(const struct worker *wrk, struct pfd **pfdp)
{
	struct pfd *pfd;
	struct conn_pool *cp;
	int i = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	TAKE_OBJ_NOTNULL(pfd, pfdp, PFD_MAGIC);
	cp = pfd->conn_pool;
	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	assert(pfd->state == PFD_STATE_USED);
	assert(pfd->fd > 0);

	Lck_Lock(&cp->mtx);
	cp->n_used--;

	pfd->waited->priv1 = pfd;
	pfd->waited->fd = pfd->fd;
	pfd->waited->idle = VTIM_real();
	pfd->state = PFD_STATE_AVAIL;
	pfd->waited->func = vcp_handle;
	pfd->waited->tmo = cache_param->backend_idle_timeout;
	if (Wait_Enter(wrk->pool->waiter, pfd->waited)) {
		cp->methods->close(pfd);
		memset(pfd, 0x33, sizeof *pfd);
		free(pfd);
		// XXX: stats
		pfd = NULL;
	} else {
		VTAILQ_INSERT_HEAD(&cp->connlist, pfd, list);
		i++;
	}

	if (pfd != NULL)
		cp->n_conn++;
	Lck_Unlock(&cp->mtx);

	if (i && DO_DEBUG(DBG_VTC_MODE)) {
		/*
		 * In varnishtest we do not have the luxury of using
		 * multiple backend connections, so whenever we end up
		 * in the "pending" case, take a short nap to let the
		 * waiter catch up and put the pfd back into circulations.
		 *
		 * In particular ESI:include related tests suffer random
		 * failures without this.
		 *
		 * In normal operation, the only effect is that we will
		 * have N+1 backend connections rather than N, which is
		 * entirely harmless.
		 */
		(void)usleep(10000);
	}
}

/*--------------------------------------------------------------------
 * Open a new connection from pool.
 */

static int
VCP_Open(struct conn_pool *cp, vtim_dur tmo, const void **privp, int *err)
{
	int r;
	vtim_mono h;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	AN(err);

	while (cp->holddown > 0) {
		Lck_Lock(&cp->mtx);
		if (cp->holddown == 0) {
			Lck_Unlock(&cp->mtx);
			break;
		}

		if (VTIM_mono() >= cp->holddown) {
			cp->holddown = 0;
			Lck_Unlock(&cp->mtx);
			break;
		}

		*err = 0;
		errno = cp->holddown_errno;
		Lck_Unlock(&cp->mtx);
		return (-1);
	}

	r = cp->methods->open(cp, tmo, privp);

	*err = errno;

	if (r >= 0)
		return (r);

	h = 0;

	switch (errno) {
	case EACCES:
	case EPERM:
		h = cache_param->backend_local_error_holddown;
		break;
	case EADDRNOTAVAIL:
		h = cache_param->backend_local_error_holddown;
		break;
	case ECONNREFUSED:
		h = cache_param->backend_remote_error_holddown;
		break;
	case ENETUNREACH:
		h = cache_param->backend_remote_error_holddown;
		break;
	default:
		break;
	}

	if (h == 0)
		return (r);

	Lck_Lock(&cp->mtx);
	h += VTIM_mono();
	if (cp->holddown == 0 || h < cp->holddown) {
		cp->holddown = h;
		cp->holddown_errno = errno;
	}

	Lck_Unlock(&cp->mtx);

	return (r);
}

/*--------------------------------------------------------------------
 * Close a connection.
 */

static void
VCP_Close(struct pfd **pfdp)
{
	struct pfd *pfd;
	struct conn_pool *cp;

	TAKE_OBJ_NOTNULL(pfd, pfdp, PFD_MAGIC);
	cp = pfd->conn_pool;
	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	assert(pfd->fd > 0);

	Lck_Lock(&cp->mtx);
	assert(pfd->state == PFD_STATE_USED || pfd->state == PFD_STATE_STOLEN);
	cp->n_used--;
	if (pfd->state == PFD_STATE_STOLEN) {
		(void)shutdown(pfd->fd, SHUT_RDWR);
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		pfd->state = PFD_STATE_CLEANUP;
		VTAILQ_INSERT_HEAD(&cp->killlist, pfd, list);
		cp->n_kill++;
	} else {
		assert(pfd->state == PFD_STATE_USED);
		cp->methods->close(pfd);
		memset(pfd, 0x44, sizeof *pfd);
		free(pfd);
	}
	Lck_Unlock(&cp->mtx);
}

/*--------------------------------------------------------------------
 * Get a connection, possibly recycled
 */

static struct pfd *
VCP_Get(struct conn_pool *cp, vtim_dur tmo, struct worker *wrk,
    unsigned force_fresh, int *err)
{
	struct pfd *pfd;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(err);

	*err = 0;
	Lck_Lock(&cp->mtx);
	pfd = VTAILQ_FIRST(&cp->connlist);
	CHECK_OBJ_ORNULL(pfd, PFD_MAGIC);
	if (force_fresh || pfd == NULL || pfd->state == PFD_STATE_STOLEN)
		pfd = NULL;
	else {
		assert(pfd->conn_pool == cp);
		assert(pfd->state == PFD_STATE_AVAIL);
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		VTAILQ_INSERT_TAIL(&cp->connlist, pfd, list);
		cp->n_conn--;
		VSC_C_main->backend_reuse++;
		pfd->state = PFD_STATE_STOLEN;
		pfd->cond = &wrk->cond;
	}
	cp->n_used++;			// Opening mostly works
	Lck_Unlock(&cp->mtx);

	if (pfd != NULL)
		return (pfd);

	ALLOC_OBJ(pfd, PFD_MAGIC);
	AN(pfd);
	INIT_OBJ(pfd->waited, WAITED_MAGIC);
	pfd->state = PFD_STATE_USED;
	pfd->conn_pool = cp;
	pfd->fd = VCP_Open(cp, tmo, &pfd->priv, err);
	if (pfd->fd < 0) {
		FREE_OBJ(pfd);
		Lck_Lock(&cp->mtx);
		cp->n_used--;		// Nope, didn't work after all.
		Lck_Unlock(&cp->mtx);
	} else
		VSC_C_main->backend_conn++;

	return (pfd);
}

/*--------------------------------------------------------------------
 */

static int
VCP_Wait(struct worker *wrk, struct pfd *pfd, vtim_real tmo)
{
	struct conn_pool *cp;
	int r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	cp = pfd->conn_pool;
	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	assert(pfd->cond == &wrk->cond);
	Lck_Lock(&cp->mtx);
	while (pfd->state == PFD_STATE_STOLEN) {
		r = Lck_CondWait(&wrk->cond, &cp->mtx, tmo);
		if (r != 0) {
			if (r == EINTR)
				continue;
			assert(r == ETIMEDOUT);
			Lck_Unlock(&cp->mtx);
			return (1);
		}
	}
	assert(pfd->state == PFD_STATE_USED);
	pfd->cond = NULL;
	Lck_Unlock(&cp->mtx);

	return (0);
}

/*--------------------------------------------------------------------
 */

static inline int
tmo2msec(vtim_dur tmo)
{
	return ((int)floor(tmo * 1000.0));
}

static int v_matchproto_(cp_open_f)
vtp_open(const struct conn_pool *cp, vtim_dur tmo, const void **privp)
{
	int s;
	int msec;
	struct tcp_pool *tp;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	CAST_OBJ_NOTNULL(tp, cp->priv, TCP_POOL_MAGIC);

	msec = tmo2msec(tmo);
	if (cache_param->prefer_ipv6) {
		*privp = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
		if (s >= 0)
			return (s);
	}
	*privp = tp->ip4;
	s = VTCP_connect(tp->ip4, msec);
	if (s >= 0)
		return (s);
	if (!cache_param->prefer_ipv6) {
		*privp = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
	}
	return (s);
}

static void v_matchproto_(cp_close_f)
vtp_close(struct pfd *pfd)
{

	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	VTCP_close(&pfd->fd);
}

static void v_matchproto_(cp_name_f)
vtp_local_name(const struct pfd *pfd, char *addr, unsigned alen, char *pbuf,
	       unsigned plen)
{
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	VTCP_myname(pfd->fd, addr, alen, pbuf, plen);
}

static void v_matchproto_(cp_name_f)
vtp_remote_name(const struct pfd *pfd, char *addr, unsigned alen, char *pbuf,
		unsigned plen)
{
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	VTCP_hisname(pfd->fd, addr, alen, pbuf, plen);
}

static const struct cp_methods vtp_methods = {
	.open = vtp_open,
	.close = vtp_close,
	.local_name = vtp_local_name,
	.remote_name = vtp_remote_name,
};

/*--------------------------------------------------------------------
 */

static int v_matchproto_(cp_open_f)
vus_open(const struct conn_pool *cp, vtim_dur tmo, const void **privp)
{
	int s;
	int msec;
	struct tcp_pool *tp;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	CAST_OBJ_NOTNULL(tp, cp->priv, TCP_POOL_MAGIC);
	AN(tp->uds);

	msec = tmo2msec(tmo);
	*privp = bogo_ip;
	s = VUS_connect(tp->uds, msec);
	return (s);
}

static void v_matchproto_(cp_name_f)
vus_name(const struct pfd *pfd, char *addr, unsigned alen, char *pbuf,
	 unsigned plen)
{
	(void) pfd;
	assert(alen > strlen("0.0.0.0"));
	assert(plen > 1);
	strcpy(addr, "0.0.0.0");
	strcpy(pbuf, "0");
}

static const struct cp_methods vus_methods = {
	.open = vus_open,
	.close = vtp_close,
	.local_name = vus_name,
	.remote_name = vus_name,
};

/*--------------------------------------------------------------------
 * Reference a TCP pool given by {ip4, ip6} pair or a UDS.  Create if
 * it doesn't exist already.
 */

struct tcp_pool *
VTP_Ref(const struct vrt_endpoint *vep, const char *ident)
{
	struct tcp_pool *tp;
	struct conn_pool *cp;
	const struct cp_methods *methods;
	struct VSHA256Context cx[1];
	unsigned char digest[VSHA256_DIGEST_LENGTH];

	CHECK_OBJ_NOTNULL(vep, VRT_ENDPOINT_MAGIC);
	AN(ident);
	VSHA256_Init(cx);
	VSHA256_Update(cx, ident, strlen(ident) + 1); // include \0
	if (vep->uds_path != NULL) {
		AZ(vep->ipv4);
		AZ(vep->ipv6);
		VSHA256_Update(cx, "UDS", 4); // include \0
		VSHA256_Update(cx, vep->uds_path, strlen(vep->uds_path));
	} else {
		assert(vep->ipv4 != NULL || vep->ipv6 != NULL);
		if (vep->ipv4 != NULL) {
			assert(VSA_Sane(vep->ipv4));
			VSHA256_Update(cx, "IP4", 4); // include \0
			VSHA256_Update(cx, vep->ipv4, vsa_suckaddr_len);
		}
		if (vep->ipv6 != NULL) {
			assert(VSA_Sane(vep->ipv6));
			VSHA256_Update(cx, "IP6", 4); // include \0
			VSHA256_Update(cx, vep->ipv6, vsa_suckaddr_len);
		}
	}
	VSHA256_Final(digest, cx);

	cp = VCP_Ref(digest);
	if (cp != NULL)
		return (cp->priv);

	/*
	 * this is racy - we could end up with additional pools on the same id
	 * with just a single connection
	 */
	ALLOC_OBJ(tp, TCP_POOL_MAGIC);
	AN(tp);
	if (vep->uds_path != NULL) {
		methods = &vus_methods;
		tp->uds = strdup(vep->uds_path);
		AN(tp->uds);
	}
	else {
		methods = &vtp_methods;
		if (vep->ipv4 != NULL)
			tp->ip4 = VSA_Clone(vep->ipv4);
		if (vep->ipv6 != NULL)
			tp->ip6 = VSA_Clone(vep->ipv6);
	}
	return (VCP_New(tp->cp, digest, tp, methods));
}

/*--------------------------------------------------------------------
 * Add a reference to a tcp_pool
 */

void
VTP_AddRef(struct tcp_pool *tp)
{
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	VCP_AddRef(tp->cp);
}

/*--------------------------------------------------------------------
 * Release TCP pool, destroy if last reference.
 */

void
VTP_Rel(struct tcp_pool **tpp)
{
	struct tcp_pool *tp;

	TAKE_OBJ_NOTNULL(tp, tpp, TCP_POOL_MAGIC);
	if (VCP_Rel(tp->cp))
		return;

	free(tp->ip4);
	free(tp->ip6);
	free(tp->uds);
	FREE_OBJ(tp);
}

/*--------------------------------------------------------------------
 * Open a new connection from pool.
 */

int
VTP_Open(struct tcp_pool *tp, vtim_dur tmo, const void **privp, int *err)
{
	return (VCP_Open(tp->cp, tmo, privp, err));
}

/*--------------------------------------------------------------------
 * Recycle a connection.
 */

void
VTP_Recycle(const struct worker *wrk, struct pfd **pfdp)
{

	VCP_Recycle(wrk, pfdp);
}

/*--------------------------------------------------------------------
 * Close a connection.
 */

void
VTP_Close(struct pfd **pfdp)
{

	VCP_Close(pfdp);
}

/*--------------------------------------------------------------------
 * Get a connection
 */

struct pfd *
VTP_Get(struct tcp_pool *tp, vtim_dur tmo, struct worker *wrk,
	unsigned force_fresh, int *err)
{

	return (VCP_Get(tp->cp, tmo, wrk, force_fresh, err));
}

/*--------------------------------------------------------------------
 */

int
VTP_Wait(struct worker *wrk, struct pfd *pfd, vtim_real tmo)
{
	return (VCP_Wait(wrk, pfd, tmo));
}

/*--------------------------------------------------------------------
 */

const struct suckaddr *
VTP_getip(struct pfd *pfd)
{
	struct tcp_pool *tp;

	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	CAST_OBJ_NOTNULL(tp, pfd->conn_pool->priv, TCP_POOL_MAGIC);
	return (pfd->priv);
}

/*--------------------------------------------------------------------*/

void
VTP_panic(struct vsb *vsb, struct tcp_pool *tp)
{
	char h[VTCP_ADDRBUFSIZE];
	char p[VTCP_PORTBUFSIZE];

	if (PAN_dump_struct(vsb, tp, TCP_POOL_MAGIC, "tcp_pool"))
		return;
	VSB_printf(vsb, "ident = ");
	VSB_quote(vsb, tp->cp->ident, VSHA256_DIGEST_LENGTH, VSB_QUOTE_HEX);
	VSB_printf(vsb, ",\n");
	if (tp->uds)
		VSB_printf(vsb, "uds = %s,\n", tp->uds);
	if (tp->ip4 && VSA_Sane(tp->ip4)) {
		VTCP_name(tp->ip4, h, sizeof h, p, sizeof p);
		VSB_printf(vsb, "ipv4 = %s, ", h);
		VSB_printf(vsb, "port = %s,\n", p);
	}
	if (tp->ip6 && VSA_Sane(tp->ip6)) {
		VTCP_name(tp->ip4, h, sizeof h, p, sizeof p);
		VSB_printf(vsb, "ipv6 = %s, ", h);
		VSB_printf(vsb, "port = %s,\n", p);
	}
	VSB_indent(vsb, -2);
	VSB_cat(vsb, "},\n");
}

/*--------------------------------------------------------------------*/

void
VTP_Init(void)
{
	Lck_New(&conn_pools_mtx, lck_tcp_pool);
}
