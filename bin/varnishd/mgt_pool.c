/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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
 * We maintain a number of worker thread pools, to spread lock contention.
 *
 * Pools can be added on the fly, as a means to mitigate lock contention,
 * but can only be removed again by a restart. (XXX: we could fix that)
 *
 * Two threads herd the pools, one eliminates idle threads and aggregates
 * statistics for all the pools, the other thread creates new threads
 * on demand, subject to various numerical constraints.
 *
 * The algorithm for when to create threads needs to be reactive enough
 * to handle startup spikes, but sufficiently attenuated to not cause
 * thread pileups.  This remains subject for improvement.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>

#include "cli_priv.h"
#include "mgt.h"

#include "vparam.h"
#include "heritage.h"

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_min(struct cli *cli, const struct parspec *par,
    const char *arg)
{

	tweak_generic_uint(cli, &master.wthread_min, arg,
	    (unsigned)par->min, master.wthread_max);
}

/*--------------------------------------------------------------------
 * This is utterly ridiculous:  POSIX does not guarantee that the
 * minimum thread stack size is a compile time constant.
 * XXX: "32" is a magic marker for 32bit systems.
 */

static void
tweak_stack_size(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	unsigned low, u;
	char buf[12];

	low = sysconf(_SC_THREAD_STACK_MIN);

	if (arg != NULL && !strcmp(arg, "32")) {
		u = 65536;
		if (u < low)
			u = low;
		sprintf(buf, "%u", u);
		arg = buf;
	}

	tweak_generic_uint(cli, &master.wthread_stacksize, arg,
	    low, (uint)par->max);
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_max(struct cli *cli, const struct parspec *par,
    const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.wthread_max, arg,
	    master.wthread_min, UINT_MAX);
}

/*--------------------------------------------------------------------*/

const struct parspec WRK_parspec[] = {
	{ "thread_pools", tweak_uint, &master.wthread_pools, 1, UINT_MAX,
		"Number of worker thread pools.\n"
		"\n"
		"Increasing number of worker pools decreases lock "
		"contention.\n"
		"\n"
		"Too many pools waste CPU and RAM resources, and more than "
		"one pool for each CPU is probably detrimal to performance.\n"
		"\n"
		"Can be increased on the fly, but decreases require a "
		"restart to take effect.",
		EXPERIMENTAL | DELAYED_EFFECT,
		"2", "pools" },
	{ "thread_pool_max", tweak_thread_pool_max, NULL, 1, 0,
		"The maximum number of worker threads in all pools combined.\n"
		"\n"
		"Do not set this higher than you have to, since excess "
		"worker threads soak up RAM and CPU and generally just get "
		"in the way of getting work done.\n",
		EXPERIMENTAL | DELAYED_EFFECT,
		"500", "threads" },
	{ "thread_pool_min", tweak_thread_pool_min, NULL, 2, 0,
		"The minimum number of threads in each worker pool.\n"
		"\n"
		"Increasing this may help ramp up faster from low load "
		"situations where threads have expired.\n"
		"\n"
		"Minimum is 2 threads.",
		EXPERIMENTAL | DELAYED_EFFECT,
		"5", "threads" },
	{ "thread_pool_timeout", tweak_timeout, &master.wthread_timeout, 1, 0,
		"Thread idle threshold.\n"
		"\n"
		"Threads in excess of thread_pool_min, which have been idle "
		"for at least this long are candidates for purging.\n"
		"\n"
		"Minimum is 1 second.",
		EXPERIMENTAL | DELAYED_EFFECT,
		"300", "seconds" },
	{ "thread_pool_purge_delay",
		tweak_timeout, &master.wthread_purge_delay, 100, 0,
		"Wait this long between purging threads.\n"
		"\n"
		"This controls the decay of thread pools when idle(-ish).\n"
		"\n"
		"Minimum is 100 milliseconds.",
		EXPERIMENTAL | DELAYED_EFFECT,
		"1000", "milliseconds" },
	{ "thread_pool_add_threshold",
		tweak_uint, &master.wthread_add_threshold, 0, UINT_MAX,
		"Overflow threshold for worker thread creation.\n"
		"\n"
		"Setting this too low, will result in excess worker threads, "
		"which is generally a bad idea.\n"
		"\n"
		"Setting it too high results in insuffient worker threads.\n",
		EXPERIMENTAL,
		"2", "requests" },
	{ "thread_pool_add_delay",
		tweak_timeout, &master.wthread_add_delay, 0, UINT_MAX,
		"Wait at least this long between creating threads.\n"
		"\n"
		"Setting this too long results in insuffient worker threads.\n"
		"\n"
		"Setting this too short increases the risk of worker "
		"thread pile-up.\n",
		EXPERIMENTAL,
		"20", "milliseconds" },
	{ "thread_pool_fail_delay",
		tweak_timeout, &master.wthread_fail_delay, 100, UINT_MAX,
		"Wait at least this long after a failed thread creation "
		"before trying to create another thread.\n"
		"\n"
		"Failure to create a worker thread is often a sign that "
		" the end is near, because the process is running out of "
		"RAM resources for thread stacks.\n"
		"This delay tries to not rush it on needlessly.\n"
		"\n"
		"If thread creation failures are a problem, check that "
		"thread_pool_max is not too high.\n"
		"\n"
		"It may also help to increase thread_pool_timeout and "
		"thread_pool_min, to reduce the rate at which treads are "
		"destroyed and later recreated.\n",
		EXPERIMENTAL,
		"200", "milliseconds" },
	{ "thread_stats_rate",
		tweak_uint, &master.wthread_stats_rate, 0, UINT_MAX,
		"Worker threads accumulate statistics, and dump these into "
		"the global stats counters if the lock is free when they "
		"finish a request.\n"
		"This parameters defines the maximum number of requests "
		"a worker thread may handle, before it is forced to dump "
		"its accumulated stats into the global counters.\n",
		EXPERIMENTAL,
		"10", "requests" },
	{ "queue_max", tweak_uint, &master.queue_max, 0, UINT_MAX,
		"Percentage permitted queue length.\n"
		"\n"
		"This sets the ratio of queued requests to worker threads, "
		"above which sessions will be dropped instead of queued.\n",
		EXPERIMENTAL,
		"100", "%" },
	{ "rush_exponent", tweak_uint, &master.rush_exponent, 2, UINT_MAX,
		"How many parked request we start for each completed "
		"request on the object.\n"
		"NB: Even with the implict delay of delivery, "
		"this parameter controls an exponential increase in "
		"number of worker threads.",
		EXPERIMENTAL,
		"3", "requests per request" },
	{ "thread_pool_stack",
		tweak_stack_size, &master.wthread_stacksize, 0, UINT_MAX,
		"Worker thread stack size.\n"
		"On 32bit systems you may need to tweak this down to fit "
		"many threads into the limited address space.\n",
		EXPERIMENTAL,
		"-1", "bytes" },
	{ NULL, NULL, NULL }
};
