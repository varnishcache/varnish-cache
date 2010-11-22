/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

	/* Hash method */
	const struct hash_slinger	*hash;

	char				*name;
	char                            identity[1024];
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

	/* Maximum concurrent sessions */
	unsigned		max_sess;

	/* Worker threads and pool */
	unsigned		wthread_min;
	unsigned		wthread_max;
	unsigned		wthread_timeout;
	unsigned		wthread_pools;
	unsigned		wthread_add_threshold;
	unsigned		wthread_add_delay;
	unsigned		wthread_fail_delay;
	unsigned		wthread_purge_delay;
	unsigned		wthread_stats_rate;
	unsigned		wthread_stacksize;

	unsigned		overflow_max;

	/* Memory allocation hints */
	unsigned		sess_workspace;
	unsigned		shm_workspace;
	unsigned		http_headers;

	unsigned		shm_reclen;

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

	/* Cache vbcs */
	unsigned		cache_vbcs;

	/* Default connection_timeout */
	double			connect_timeout;

	/* Read timeouts for backend */
	double			first_byte_timeout;
	double			between_bytes_timeout;

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

	/* Expiry pacer parameters */
	double			expiry_sleep;

	/* Acceptor pacer parameters */
	double			acceptor_sleep_max;
	double			acceptor_sleep_incr;
	double			acceptor_sleep_decay;

	/* Get rid of duplicate purges */
	unsigned		purge_dups;

	/* How long time does the ban lurker sleep */
	double			ban_lurker_sleep;

	/* Max size of the saintmode list. 0 == no saint mode. */
	unsigned		saintmode_threshold;

	unsigned		syslog_cli_traffic;

	unsigned		http_range_support;

	double			critbit_cooloff;
};

/*
 * We declare this a volatile pointer, so that reads of parameters
 * become atomic, leaving the CLI thread lattitude to change the values
 */
extern volatile struct params * volatile params;
extern struct heritage heritage;

void child_main(void);
