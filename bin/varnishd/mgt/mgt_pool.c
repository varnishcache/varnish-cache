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

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/params.h"

#include "mgt/mgt_param.h"

/*--------------------------------------------------------------------*/

static int
tweak_thread_pool_min(struct vsb *vsb, const struct parspec *par,
    const char *arg)
{

	return (tweak_generic_uint(vsb, &mgt_param.wthread_min, arg,
	    (unsigned)par->min, mgt_param.wthread_max));
}

/*--------------------------------------------------------------------
 * This is utterly ridiculous:  POSIX does not guarantee that the
 * minimum thread stack size is a compile time constant.
 * XXX: "32bit" is a magic marker for 32bit systems.
 */

static int
tweak_stack_size(struct vsb *vsb, const struct parspec *par,
    const char *arg)
{
	ssize_t low;

	low = sysconf(_SC_THREAD_STACK_MIN);

	if (tweak_bytes(vsb, par, arg))
		return (-1);
	if (mgt_param.wthread_stacksize < low)
		mgt_param.wthread_stacksize = low;
	return (0);
}

/*--------------------------------------------------------------------*/

static int
tweak_thread_pool_max(struct vsb *vsb, const struct parspec *par,
    const char *arg)
{

	(void)par;
	return (tweak_generic_uint(vsb, &mgt_param.wthread_max, arg,
	    mgt_param.wthread_min, UINT_MAX));
}

/*--------------------------------------------------------------------*/

struct parspec WRK_parspec[] = {
	{ "thread_pools", tweak_uint, &mgt_param.wthread_pools,
		1, UINT_MAX,
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
	{ "thread_pool_max", tweak_thread_pool_max, NULL, 10, 0,
		"The maximum number of worker threads in each pool.\n"
		"\n"
		"Do not set this higher than you have to, since excess "
		"worker threads soak up RAM and CPU and generally just get "
		"in the way of getting work done.\n"
		"\n"
		"Minimum is 10 threads.",
		DELAYED_EFFECT,
		"5000", "threads" },
	{ "thread_pool_min", tweak_thread_pool_min, NULL, 10, 0,
		"The minimum number of worker threads in each pool.\n"
		"\n"
		"Increasing this may help ramp up faster from low load "
		"situations or when threads have expired.\n"
		"\n"
		"Minimum is 10 threads.",
		DELAYED_EFFECT,
		"100", "threads" },
	{ "thread_pool_timeout",
		tweak_timeout_double, &mgt_param.wthread_timeout,
		10, UINT_MAX,
		"Thread idle threshold.\n"
		"\n"
		"Threads in excess of thread_pool_min, which have been idle "
		"for at least this long, will be destroyed.\n"
		"\n"
		"Minimum is 10 seconds.",
		EXPERIMENTAL | DELAYED_EFFECT,
		"300", "seconds" },
	{ "thread_pool_destroy_delay",
		tweak_timeout_double, &mgt_param.wthread_destroy_delay,
		0.01, UINT_MAX,
		"Wait this long after destroying a thread.\n"
		"\n"
		"This controls the decay of thread pools when idle(-ish).\n"
		"\n"
		"Minimum is 0.01 second.",
		EXPERIMENTAL | DELAYED_EFFECT,
		"1", "seconds" },
	{ "thread_pool_add_delay",
		tweak_timeout_double, &mgt_param.wthread_add_delay,
		0, UINT_MAX,
		"Wait at least this long after creating a thread.\n"
		"\n"
		"Some (buggy) systems may need a short (sub-second) "
		"delay between creating threads.\n"
		"Set this to a few milliseconds if you see the "
		"'threads_failed' counter grow too much.\n"
		"\n"
		"Setting this too high results in insuffient worker threads.",
		EXPERIMENTAL,
		"0", "seconds" },
	{ "thread_pool_fail_delay",
		tweak_timeout_double, &mgt_param.wthread_fail_delay,
		10e-3, UINT_MAX,
		"Wait at least this long after a failed thread creation "
		"before trying to create another thread.\n"
		"\n"
		"Failure to create a worker thread is often a sign that "
		" the end is near, because the process is running out of "
		"some resource.  "
		"This delay tries to not rush the end on needlessly.\n"
		"\n"
		"If thread creation failures are a problem, check that "
		"thread_pool_max is not too high.\n"
		"\n"
		"It may also help to increase thread_pool_timeout and "
		"thread_pool_min, to reduce the rate at which treads are "
		"destroyed and later recreated.",
		EXPERIMENTAL,
		"0.2", "seconds" },
	{ "thread_stats_rate",
		tweak_uint, &mgt_param.wthread_stats_rate, 0, UINT_MAX,
		"Worker threads accumulate statistics, and dump these into "
		"the global stats counters if the lock is free when they "
		"finish a request.\n"
		"This parameters defines the maximum number of requests "
		"a worker thread may handle, before it is forced to dump "
		"its accumulated stats into the global counters.",
		EXPERIMENTAL,
		"10", "requests" },
	{ "thread_queue_limit", tweak_uint, &mgt_param.wthread_queue_limit,
		0, UINT_MAX,
		"Permitted queue length per thread-pool.\n"
		"\n"
		"This sets the number of requests we will queue, waiting "
		"for an available thread.  Above this limit sessions will "
		"be dropped instead of queued.",
		EXPERIMENTAL,
		"20", "" },
	{ "rush_exponent", tweak_uint, &mgt_param.rush_exponent, 2, UINT_MAX,
		"How many parked request we start for each completed "
		"request on the object.\n"
		"NB: Even with the implict delay of delivery, "
		"this parameter controls an exponential increase in "
		"number of worker threads.",
		EXPERIMENTAL,
		"3", "requests per request" },
	{ "thread_pool_stack",
		tweak_stack_size, &mgt_param.wthread_stacksize, 0, UINT_MAX,
		"Worker thread stack size.\n"
		"This is likely rounded up to a multiple of 4k by the kernel.\n"
		"The kernel/OS has a lower limit which will be enforced.",
		EXPERIMENTAL,
		"48k", "bytes" },
	{ NULL, NULL, NULL }
};
