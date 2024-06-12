/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Private include file for the pool aware code.
 */

VTAILQ_HEAD(taskhead, pool_task);

struct poolsock;

struct pool {
	unsigned			magic;
#define POOL_MAGIC			0x606658fa
	VTAILQ_ENTRY(pool)		list;
	VTAILQ_HEAD(,poolsock)		poolsocks;

	int				die;
	pthread_cond_t			herder_cond;
	pthread_t			herder_thr;

	struct lock			mtx;
	unsigned			nidle;
	struct taskhead			idle_queue;
	struct taskhead			queues[TASK_QUEUE_RESERVE];
	unsigned			nthr;
	unsigned			lqueue;
	uintmax_t			ndequeued;
	struct VSC_main_pool		stats[1];
	struct VSC_main_wrk		*a_stat;
	struct VSC_main_wrk		*b_stat;

	struct mempool			*mpl_req;
	struct mempool			*mpl_sess;
	struct waiter			*waiter;
};

void *pool_herder(void*);
task_func_t pool_stat_summ;
extern struct lock			pool_mtx;
void VCA_NewPool(struct pool *, unsigned);
void VCA_DestroyPool(struct pool *);
