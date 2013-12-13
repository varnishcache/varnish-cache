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
 * This file contains the heritage passed when mgt forks cache
 */

#include <stdint.h>

#include "vre.h"

#define VSM_CLASS_PARAM		"Params"

enum debug_bits {
#define DEBUG_BIT(U, l, d) DBG_##U,
#include "tbl/debug_bits.h"
#undef DEBUG_BIT
       DBG_Reserved
};

enum feature_bits {
#define FEATURE_BIT(U, l, d, ld) FEATURE_##U,
#include "tbl/feature_bits.h"
#undef FEATURE_BIT
       FEATURE_Reserved
};

struct poolparam {
	unsigned		min_pool;
	unsigned		max_pool;
	double			max_age;
};

struct params {

	/* Unprivileged user / group */
	char			*user;
	uid_t			uid;
	char			*group;
	gid_t			gid;

	/* TTL used for lack of anything better */
	double			default_ttl;

	/* Default grace period */
	double			default_grace;

	/* Default keep period */
	double			default_keep;

	/* Maximum concurrent sessions */
	unsigned		max_sess;

	/* Worker threads and pool */
	unsigned		wthread_min;
	unsigned		wthread_max;
	double			wthread_timeout;
	unsigned		wthread_pools;
	unsigned		wthread_add_threshold;
	double			wthread_add_delay;
	double			wthread_fail_delay;
	double			wthread_destroy_delay;
	double			wthread_stats_rate;
	ssize_t			wthread_stacksize;
	unsigned		wthread_queue_limit;

	/* Memory allocation hints */
	unsigned		workspace_backend;
	unsigned		workspace_client;
	unsigned		workspace_session;
	unsigned		workspace_thread;

	unsigned		vsl_buffer;

	unsigned		shm_workspace;
	unsigned		http_req_size;
	unsigned		http_req_hdr_len;
	unsigned		http_resp_size;
	unsigned		http_resp_hdr_len;
	unsigned		http_max_hdr;

	unsigned		shm_reclen;

	double			timeout_linger;
	double			timeout_idle;
	double			timeout_req;
	double			pipe_timeout;
	double			send_timeout;
	double			idle_send_timeout;
#ifdef HAVE_TCP_KEEP
	double			tcp_keepalive_time;
	unsigned		tcp_keepalive_probes;
	double			tcp_keepalive_intvl;
#endif

	/* Management hints */
	unsigned		auto_restart;

	/* Fetcher hints */
	ssize_t			fetch_chunksize;
	ssize_t			fetch_maxchunksize;
	unsigned		nuke_limit;

	unsigned		accept_filter;

	/* Listen address */
	char			*listen_address;

	/* Listen depth */
	unsigned		listen_depth;

	/* CLI related */
	double			cli_timeout;
	unsigned		cli_limit;
	unsigned		ping_interval;

	/* LRU list ordering interval */
	double			lru_interval;

	/* Maximum restarts allowed */
	unsigned		max_restarts;

	/* Maximum backend retriesallowed */
	unsigned		max_retries;

	/* Maximum esi:include depth allowed */
	unsigned		max_esi_depth;

	/* Rush exponent */
	unsigned		rush_exponent;

	/* Default connection_timeout */
	double			connect_timeout;

	/* Read timeouts for backend */
	double			first_byte_timeout;
	double			between_bytes_timeout;

	/* CLI buffer size */
	unsigned		cli_buffer;

	/* Prefer IPv6 connections to backend*/
	unsigned		prefer_ipv6;

	/* Acceptable clockskew with backends */
	unsigned		clock_skew;

	/* Acceptor pacer parameters */
	double			acceptor_sleep_max;
	double			acceptor_sleep_incr;
	double			acceptor_sleep_decay;

	/* Get rid of duplicate bans */
	unsigned		ban_dups;

	double			ban_lurker_age;
	double			ban_lurker_sleep;
	unsigned		ban_lurker_batch;

	unsigned		syslog_cli_traffic;

	unsigned		http_range_support;

	unsigned		http_gzip_support;
	unsigned		gzip_buffer;
	unsigned		gzip_level;
	unsigned		gzip_memlevel;

	unsigned		obj_readonly;

	double			critbit_cooloff;

	double			shortlived;

	struct vre_limits	vre_limits;

	unsigned		bo_cache;

	/* Install a SIGSEGV handler */
	unsigned		sigsegv_handler;

	/* VSM dimensions */
	ssize_t			vsm_space;
	ssize_t			vsl_space;

	struct poolparam	vbc_pool;
	struct poolparam	req_pool;
	struct poolparam	sess_pool;
	struct poolparam	vbo_pool;

	uint8_t			vsl_mask[256>>3];
	uint8_t			debug_bits[(DBG_Reserved+7)>>3];
	uint8_t			feature_bits[(FEATURE_Reserved+7)>>3];
};
