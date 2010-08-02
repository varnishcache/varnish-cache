/*-
 * Copyright (c) 2009 Redpill Linpro AS
 * Copyright (c) 2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Kristian Lyngstol <kristian@redpill-linpro.com>
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>

#include <stdio.h>
#include <netinet/in.h>

#include "cache.h"
#include "cache_backend.h"
#include "vrt.h"

/*--------------------------------------------------------------------*/

/* FIXME: Should eventually be a configurable variable. */
#define VDI_DNS_MAX_CACHE		1024
#define VDI_DNS_GROUP_MAX_BACKENDS	1024

/* DNS Cache entry 
 */
struct vdi_dns_hostgroup {
	unsigned			magic;
#define VDI_DNSDIR_MAGIC		0x1bacab21
	char 				*hostname;
	struct director			*hosts[VDI_DNS_GROUP_MAX_BACKENDS];
	unsigned			nhosts;
	unsigned			next_host; /* Next to use...*/
	double				ttl;
	VTAILQ_ENTRY(vdi_dns_hostgroup)	list;
};

struct vdi_dns {
	unsigned			magic;
#define VDI_DNS_MAGIC			0x1337a178
	struct director			dir;
	struct director			**hosts;
	unsigned			nhosts;
	VTAILQ_HEAD(_cachelist,vdi_dns_hostgroup)	cachelist;
	unsigned			ncachelist;
	pthread_rwlock_t		rwlock;
	const char			*suffix;
	double			ttl;
	const unsigned			max_cache_size;
};



/* Compare an IPv4 backend to a IPv4 addr/len */
static int
vdi_dns_comp_addrinfo4(struct backend *bp, 
		       const struct sockaddr_in *addr,
		       const socklen_t len)
{
	uint32_t u, p;
	struct sockaddr_in *bps = (struct sockaddr_in *) bp->ipv4;

	if (bp->ipv4len != len || len <= 0)
		return 0;

	u = addr->sin_addr.s_addr;
	p = bps->sin_addr.s_addr;

	return u == p;
}

/* Compare an IPv6 backend to a IPv6 addr/len */
static int
vdi_dns_comp_addrinfo6(struct backend *bp,
		       struct sockaddr_in6 *addr,
		       const socklen_t len)
{
	uint8_t *u, *p;
	int i;
	struct sockaddr_in6 *bps = (struct sockaddr_in6 *) bp->ipv6;

	if (bp->ipv6len != len || len <= 0)
		return 0;

	u = addr->sin6_addr.s6_addr;
	p = bps->sin6_addr.s6_addr;

	for (i=0; i < 16; i++) {
		if (u[i] != p[i])
			return 0;
	}

	return 1;
}

/* Check if a backends socket is the same as addr */
static int
vdi_dns_comp_addrinfo(struct director *dir,
		      struct sockaddr *addr,
		      const socklen_t len)
{
	struct backend *bp;
	bp = vdi_get_backend_if_simple(dir);
	if (addr->sa_family == PF_INET && bp->ipv4) {
		return (vdi_dns_comp_addrinfo4(bp, (struct sockaddr_in *)
			addr, len));
	} else if (addr->sa_family == PF_INET6 && bp->ipv6) {
		return (vdi_dns_comp_addrinfo6(bp, (struct sockaddr_in6 *)
			addr, len));
	}
	return 0;
}

/* Pick a host from an existing hostgroup.
 * Balance on round-robin if multiple backends are available and only pick
 * healthy ones.
 */
static struct director *
vdi_dns_pick_host(const struct sess *sp, struct vdi_dns_hostgroup *group) {
	int initial, i, nhosts, current;
	if (group->nhosts == 0)
		return (NULL); // In case of error.
	if (group->next_host >= group->nhosts)
		group->next_host = 0;

	/* Pick a healthy backend */
	initial = group->next_host;
	nhosts = group->nhosts;
	for (i=0; i < nhosts; i++) {
		if (i + initial >= nhosts)
			current = i + initial - nhosts;
		else
			current = i + initial;
		if (VBE_Healthy_sp(sp, group->hosts[current])) {
			group->next_host = current+1;
			return group->hosts[current];
		}
	}

	return NULL;
}

/* Remove an item from the dns cache.
 * If *group is NULL, the head is popped.
 * Remember locking.
 */
static void
vdi_dns_pop_cache(struct vdi_dns *vs,
		  struct vdi_dns_hostgroup *group)
{
	if (group == NULL)
		group = VTAILQ_LAST( &vs->cachelist, _cachelist );
	assert(group != NULL);
	free(group->hostname);
	VTAILQ_REMOVE(&vs->cachelist, group, list);
	FREE_OBJ(group);
	vs->ncachelist--;
}

/* Dummy in case someone feels like optimizing it? meh...
 */
static inline int
vdi_dns_groupmatch(const struct vdi_dns_hostgroup *group, const char *hostname)
{
	return !strcmp(group->hostname, hostname);
}

/* Search the cache for 'hostname' and put a backend-pointer as necessary,
 * return true for cache hit. This could still be a NULL backend if we did
 * a lookup earlier and didn't find a host (ie: cache failed too)
 *
 * if rwlock is true, the first timed out object found (if any) is popped
 * and freed.
 */
static int
vdi_dns_cache_has(const struct sess *sp,
		  struct vdi_dns *vs,
		  const char *hostname,
		  struct director **backend,
		  int rwlock)
{
	struct director *ret;
	struct vdi_dns_hostgroup *hostgr;
	struct vdi_dns_hostgroup *hostgr2;
	VTAILQ_FOREACH_SAFE(hostgr, &vs->cachelist, list, hostgr2) {
		CHECK_OBJ_NOTNULL(hostgr, VDI_DNSDIR_MAGIC);
		if (hostgr->ttl <= sp->t_req) {
			if (rwlock)
				vdi_dns_pop_cache(vs, hostgr);
			return 0;
		}
		if (vdi_dns_groupmatch(hostgr, hostname)) {
			ret = (vdi_dns_pick_host(sp, hostgr));
			*backend = ret;
			if (*backend != NULL) 
				CHECK_OBJ_NOTNULL(*backend, DIRECTOR_MAGIC);
			return 1;
		}
	}
	return 0;
}

/* Add a newly cached item to the dns cache list.
 * (Sorry for the list_add/_add confusion...)
 */
static void
vdi_dns_cache_list_add(const struct sess *sp,
		       struct vdi_dns *vs,
		       struct vdi_dns_hostgroup *new)
{
	if (vs->ncachelist >= VDI_DNS_MAX_CACHE) {
		VSC_main->dir_dns_cache_full++;
		vdi_dns_pop_cache(vs, NULL);
	}
	CHECK_OBJ_NOTNULL(new, VDI_DNSDIR_MAGIC);
	assert(new->hostname != 0);
	new->ttl = sp->t_req + vs->ttl;
	VTAILQ_INSERT_HEAD(&vs->cachelist, new, list);
	vs->ncachelist++;
}

/* Add an item to the dns cache.
 * XXX: Might want to factor the getaddrinfo() out of the lock and do the
 * cache_has() afterwards to do multiple dns lookups in parallel...
 */
static int
vdi_dns_cache_add(const struct sess *sp,
		  struct vdi_dns *vs,
		  const char *hostname,
		  struct director **backend)
{
	int error, i, host = 0;
	struct addrinfo *res0, *res, hint;
	struct vdi_dns_hostgroup *new;
	/* Due to possible race while upgrading the lock, we have to
	 * recheck if the result is already looked up. The overhead for
	 * this is insignificant unless dns isn't cached properly (all
	 * unique names or something equally troublesome).
	 */

	if (vdi_dns_cache_has(sp, vs, hostname, backend, 1))
		return 1;
	
	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;

	ALLOC_OBJ(new, VDI_DNSDIR_MAGIC);
	new->hostname = calloc(sizeof(char), strlen(hostname)+1);
	assert(new->hostname != NULL);
	strcpy(new->hostname, hostname);

	error = getaddrinfo(hostname, "80", &hint, &res0);
	VSC_main->dir_dns_lookups++;
	if (error) {
		vdi_dns_cache_list_add(sp, vs, new);
		VSC_main->dir_dns_failed++;
		return 0;
	}

	for (res = res0; res; res = res->ai_next) {
		if (res->ai_family != PF_INET &&
				res->ai_family != PF_INET6)
			continue;

		for (i = 0; i < vs->nhosts; i++) {
			if (vdi_dns_comp_addrinfo(vs->hosts[i],
						res->ai_addr, res->ai_addrlen)) {
				new->hosts[host] = vs->hosts[i];
				CHECK_OBJ_NOTNULL(new->hosts[host], DIRECTOR_MAGIC);
				host++;
			}
		}
	}
	freeaddrinfo(res0);

	new->nhosts = host;
	vdi_dns_cache_list_add(sp, vs, new);
	*backend = vdi_dns_pick_host(sp, new);	
	return 1;
}

/* Walk through the cached lookups looking for the relevant host, add one
 * if it isn't already cached.
 *
 * Returns a backend or NULL.
 */
static struct director *
vdi_dns_walk_cache(const struct sess *sp,
		   struct vdi_dns *vs,
		   const char *hostname)
{
	struct director *backend = NULL;
	int ret;
	AZ(pthread_rwlock_rdlock(&vs->rwlock));
	ret = vdi_dns_cache_has(sp, vs, hostname, &backend, 0);
	pthread_rwlock_unlock(&vs->rwlock);
	if (!ret) {
		AZ(pthread_rwlock_wrlock(&vs->rwlock));
		ret = vdi_dns_cache_add(sp, vs, hostname, &backend);
		pthread_rwlock_unlock(&vs->rwlock);
	} else
		VSC_main->dir_dns_hit++;

	/* Bank backend == cached a failure, so to speak */
	if (backend != NULL)
		CHECK_OBJ_NOTNULL(backend, DIRECTOR_MAGIC);
	return backend;
}

/* Parses the Host:-header and heads out to find a backend.
 */
static struct director *
vdi_dns_find_backend(const struct sess *sp, struct vdi_dns *vs)
{
	struct director *ret;
	struct http *hp;
	char *p;
	char hostname[NI_MAXHOST];
	int i;

	/* bereq is only present after recv et. al, otherwise use req (ie:
	 * use req for health checks in vcl_recv and such).
	 */
	if (sp->wrk->bereq)
		hp = sp->wrk->bereq;
	else
		hp = sp->http;


	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (http_GetHdr(hp, H_Host, &p) == 0)
		return (NULL);

	/* We need a working copy since it's going to be modified */	
	strncpy(hostname, p, sizeof(hostname));

	/* remove port-portion of the Host-header, if present. */
	for (i = 0; i < strlen(hostname); i++) {
		if (hostname[i] == ':') {
			hostname[i] = '\0';
			break;
		}
	}

	if (vs->suffix)
		strncat(hostname, vs->suffix, sizeof(hostname) - strlen(hostname));

	ret = vdi_dns_walk_cache(sp, vs, hostname);
	return ret;
}

static struct vbe_conn *
vdi_dns_getfd(const struct director *director, struct sess *sp)
{
	struct vdi_dns *vs;
	struct director *dir;
	struct vbe_conn *vbe;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(director, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, director->priv, VDI_DNS_MAGIC);

	dir = vdi_dns_find_backend(sp, vs);
	if (!dir || !VBE_Healthy_sp(sp, dir))
		return (NULL);
	
	vbe = VBE_GetFd(dir, sp);
	return (vbe);
}

static unsigned
vdi_dns_healthy(double now, const struct director *dir, uintptr_t target)
{
	/* XXX: Fooling -Werror for a bit until it's actually implemented.
	 */
	if (now || dir || target)
		return 1;
	else
		return 1;
	return 1;
	/*
	struct vdi_dns *vs;
	struct director *dir;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->director, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, sp->director->priv, VDI_DNS_MAGIC);

	dir = vdi_dns_find_backend(sp, vs);

	if (dir)
		return 1;
	return 0;
	*/
}

/*lint -e{818} not const-able */
static void
vdi_dns_fini(struct director *d)
{
	struct vdi_dns *vs;
	struct director **vh;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, d->priv, VDI_DNS_MAGIC);

	vh = vs->hosts;
	free(vs->hosts);
	free(vs->dir.vcl_name);
	vs->dir.magic = 0;
	/* FIXME: Free the cache */
	pthread_rwlock_destroy(&vs->rwlock);
	FREE_OBJ(vs);
}

void
VRT_init_dir_dns(struct cli *cli, struct director **bp, int idx,
    const void *priv)
{
	const struct vrt_dir_dns *t;
	struct vdi_dns *vs;
	const struct vrt_dir_dns_entry *te;
	int i;

	ASSERT_CLI();
	(void)cli;
	t = priv;
	ALLOC_OBJ(vs, VDI_DNS_MAGIC);
	XXXAN(vs);
	vs->hosts = calloc(sizeof(struct director *), t->nmember);
	XXXAN(vs->hosts);

	vs->dir.magic = DIRECTOR_MAGIC;
	vs->dir.priv = vs;
	vs->dir.name = "dns";
	REPLACE(vs->dir.vcl_name, t->name);
	vs->dir.getfd = vdi_dns_getfd;
	vs->dir.fini = vdi_dns_fini;
	vs->dir.healthy = vdi_dns_healthy;

	vs->suffix = t->suffix;
	vs->ttl = t->ttl;

	te = t->members;
	for (i = 0; i < t->nmember; i++, te++)
		vs->hosts[i] = bp[te->host];
	vs->nhosts = t->nmember;
	vs->ttl = t->ttl;
	VTAILQ_INIT(&vs->cachelist);
	pthread_rwlock_init(&vs->rwlock, NULL);
	bp[idx] = &vs->dir;
}
