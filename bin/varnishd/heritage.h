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
 * $Id$
 *
 * This file contains the heritage passed when mgt forks cache
 */

#include <pthread.h>

#include "vqueue.h"

struct listen_sock {
	VTAILQ_ENTRY(listen_sock)	list;
	int				sock;
	char				*name;
	struct vss_addr			*addr;
};

VTAILQ_HEAD(listen_sock_head, listen_sock);

struct heritage {

	/* Two pipe(2)'s for CLI connection between cache and mgt.  */
	int				cli_in;
	int				cli_out;

	/* File descriptor for stdout/stderr */
	int				std_fd;

	/* Sockets from which to accept connections */
	struct listen_sock_head		socks;
	unsigned			nsocks;

	/* Share memory log fd and size (incl header) */
	int				vsl_fd;
	unsigned			vsl_size;

	/* Hash method */
	struct hash_slinger		*hash;

	char				name[1024];
};

struct params {

	/* Unprivileged user / group */
	char			*user;
	uid_t			uid;
	char			*group;
	gid_t			gid;

	/* TTL used for lack of anything better */
	unsigned		default_ttl;

	/* TTL used for synthesized error pages */
	unsigned		err_ttl;

	/* Worker threads and pool */
	unsigned		wthread_min;
	unsigned		wthread_max;
	unsigned		wthread_timeout;
	unsigned		wthread_pools;
	unsigned		wthread_add_threshold;
	unsigned		wthread_add_delay;
	unsigned		wthread_fail_delay;
	unsigned		wthread_purge_delay;

	unsigned		overflow_max;

	/* Memory allocation hints */
	unsigned		sess_workspace;
	unsigned		obj_workspace;
	unsigned		shm_workspace;

	/* Acceptor hints */
	unsigned		sess_timeout;
	unsigned		pipe_timeout;
	unsigned		send_timeout;

	/* Management hints */
	unsigned		auto_restart;

	/* Fetcher hints */
	unsigned		fetch_chunksize;

#ifdef SENDFILE_WORKS
	/* Sendfile object minimum size */
	unsigned		sendfile_threshold;
#endif

	/* VCL traces */
	unsigned		vcl_trace;

	/* Listen address */
	char			*listen_address;

	/* Listen depth */
	unsigned		listen_depth;

	/* Srcaddr hash */
	unsigned		srcaddr_hash;
	unsigned		srcaddr_ttl;

	/* HTTP proto behaviour */
	unsigned		backend_http11;
	unsigned		client_http11;

	/* CLI related */
	unsigned		cli_timeout;
	unsigned		ping_interval;

	/* LRU list ordering interval */
	unsigned		lru_timeout;

	/* Maximum restarts allowed */
	unsigned		max_restarts;

	/* Maximum esi:include depth allowed */
	unsigned		max_esi_includes;

	/* ESI parser hints */
	unsigned		esi_syntax;

	/* Rush exponent */
	unsigned		rush_exponent;

	/* Cache vbe_conns */
	unsigned		cache_vbe_conns;

	/* Default connection_timeout */
	unsigned		connect_timeout;

	/* How long to linger on sessions */
	unsigned		session_linger;

	/* CLI buffer size */
	unsigned		cli_buffer;

	/* Control diagnostic code */
	unsigned		diag_bitmap;

	/* Default grace period */
	unsigned		default_grace;

	/* Log hash string to shm */
	unsigned		log_hash;

	/* Log local socket address to shm */
	unsigned		log_local_addr;

	/* Prefer IPv6 connections to backend*/
	unsigned		prefer_ipv6;

	/* Acceptable clockskew with backends */
	unsigned		clock_skew;

	/* Amount of time to sleep when running out of file
	   descriptors.  In msecs */
	unsigned		accept_fd_holdoff;

	/* Get rid of duplicate purges */
	unsigned		purge_dups;
};

extern volatile struct params *params;
extern struct heritage heritage;

void child_main(void);

int varnish_instance(const char *n_arg, char *name, size_t namelen,
    char *dir, size_t dirlen);
