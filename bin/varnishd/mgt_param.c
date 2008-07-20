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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"
#include "mgt.h"
#include "mgt_cli.h"

#include "heritage.h"

#include "vss.h"

#define MAGIC_INIT_STRING	"\001"

struct parspec;
static int margin;

typedef void tweak_t(struct cli *, const struct parspec *, const char *arg);

struct parspec {
	const char	*name;
	tweak_t		*func;
	volatile void	*priv;
	unsigned	umin;
	unsigned	umax;
	const char	*descr;
	int		 flags;
#define DELAYED_EFFECT 1
#define EXPERIMENTAL   2
#define MUST_RESTART   4
#define MUST_RELOAD    8
	const char	*def;
	const char	*units;
};

static struct params master;

/*--------------------------------------------------------------------*/

static void
tweak_generic_timeout(struct cli *cli, volatile unsigned *dst, const char *arg)
{
	unsigned u;

	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u == 0) {
			cli_out(cli, "Timeout must be greater than zero\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		*dst = u;
	} else
		cli_out(cli, "%u", *dst);
}

/*--------------------------------------------------------------------*/

static void
tweak_timeout(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	tweak_generic_timeout(cli, dest, arg);
}


/*--------------------------------------------------------------------*/

static void
tweak_generic_bool(struct cli *cli, volatile unsigned *dest, const char *arg)
{
	if (arg != NULL) {
		if (!strcasecmp(arg, "off"))
			*dest = 0;
		else if (!strcasecmp(arg, "disable"))
			*dest = 0;
		else if (!strcasecmp(arg, "no"))
			*dest = 0;
		else if (!strcasecmp(arg, "on"))
			*dest = 1;
		else if (!strcasecmp(arg, "enable"))
			*dest = 1;
		else if (!strcasecmp(arg, "yes"))
			*dest = 1;
		else {
			cli_out(cli, "use \"on\" or \"off\"\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
	} else
		cli_out(cli, *dest ? "on" : "off");
}

/*--------------------------------------------------------------------*/

static void
tweak_bool(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	tweak_generic_bool(cli, dest, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_generic_uint(struct cli *cli, volatile unsigned *dest, const char *arg, unsigned min, unsigned max)
{
	unsigned u;

	if (arg != NULL) {
		if (!strcasecmp(arg, "unlimited"))
			u = UINT_MAX;
		else
			u = strtoul(arg, NULL, 0);
		if (u < min) {
			cli_out(cli, "Must be at least %u\n", min);
			cli_result(cli, CLIS_PARAM);
			return;
		}
		if (u > max) {
			cli_out(cli, "Must be no more than %u\n", max);
			cli_result(cli, CLIS_PARAM);
			return;
		}
		*dest = u;
	} else if (*dest == UINT_MAX) {
		cli_out(cli, "unlimited", *dest);
	} else {
		cli_out(cli, "%u", *dest);
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_uint(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	tweak_generic_uint(cli, dest, arg, par->umin, par->umax);
}

/*--------------------------------------------------------------------
 * XXX: slightly magic.  We want to initialize to "nobody" (XXX: shouldn't
 * XXX: that be something autocrap found for us ?) but we don't want to
 * XXX: fail initialization if that user doesn't exists, even though we
 * XXX: do want to fail it, in subsequent sets.
 * XXX: The magic init string is a hack for this.
 */

static void
tweak_user(struct cli *cli, const struct parspec *par, const char *arg)
{
	struct passwd *pw;
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if (!strcmp(arg, MAGIC_INIT_STRING)) {
			pw = getpwnam("nobody");
			if (pw == NULL) {
				master.uid = getuid();
				return;
			}
		} else 
			pw = getpwnam(arg);
		if (pw == NULL) {
			cli_out(cli, "Unknown user");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		REPLACE(master.user, pw->pw_name);
		master.uid = pw->pw_uid;
		master.gid = pw->pw_gid;

		/* set group to user's primary group */
		if ((gr = getgrgid(pw->pw_gid)) != NULL &&
		    (gr = getgrnam(gr->gr_name)) != NULL &&
		    gr->gr_gid == pw->pw_gid) 
			REPLACE(master.group, gr->gr_name);
	} else if (master.user) {
		cli_out(cli, "%s (%d)", master.user, (int)master.uid);
	} else {
		cli_out(cli, "%d", (int)master.uid);
	}
}

/*--------------------------------------------------------------------
 * XXX: see comment for tweak_user, same thing here.
 */

static void
tweak_group(struct cli *cli, const struct parspec *par, const char *arg)
{
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if (!strcmp(arg, MAGIC_INIT_STRING)) {
			gr = getgrnam("nogroup");
			if (gr == NULL) {
				/* Only replace if tweak_user didn't */
				if (master.gid == 0)
					master.gid = getgid();
				return;
			}
		} else
			gr = getgrnam(arg);
		if (gr == NULL) {
			cli_out(cli, "Unknown group");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		REPLACE(master.group, gr->gr_name);
		master.gid = gr->gr_gid;
	} else if (master.group) {
		cli_out(cli, "%s (%d)", master.group, (int)master.gid);
	} else {
		cli_out(cli, "%d", (int)master.gid);
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_min(struct cli *cli, const struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.wthread_min, arg,
	    0, master.wthread_max);
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_max(struct cli *cli, const struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.wthread_max, arg,
	    master.wthread_min, UINT_MAX);
}

/*--------------------------------------------------------------------*/

static void
clean_listen_sock_head(struct listen_sock_head *lsh)
{
	struct listen_sock *ls, *ls2;

	VTAILQ_FOREACH_SAFE(ls, lsh, list, ls2) {
		VTAILQ_REMOVE(lsh, ls, list);
		free(ls->name);
		free(ls->addr);
		free(ls);
	}
}

static void
tweak_listen_address(struct cli *cli, const struct parspec *par, const char *arg)
{
	char **av;
	int i;
	struct listen_sock		*ls;
	struct listen_sock_head		lsh;

	(void)par;
	if (arg == NULL) {
		/* Quote the string if we have more than one socket */
		if (heritage.nsocks > 1)
			cli_out(cli, "\"%s\"", master.listen_address);
		else
			cli_out(cli, "%s", master.listen_address);
		return;
	}

	av = ParseArgv(arg, 0);
	if (av == NULL) {
		cli_out(cli, "Parse error: out of memory");
		cli_result(cli, CLIS_PARAM);
		return;
	}
	if (av[0] != NULL) {
		cli_out(cli, "Parse error: %s", av[0]);
		cli_result(cli, CLIS_PARAM);
		FreeArgv(av);
		return;
	}
	if (av[1] == NULL) {
		cli_out(cli, "Empty listen address");
		cli_result(cli, CLIS_PARAM);
		FreeArgv(av);
		return;
	}
	VTAILQ_INIT(&lsh);
	for (i = 1; av[i] != NULL; i++) {
		struct vss_addr **ta;
		char *host, *port;
		int j, n;

		if (VSS_parse(av[i], &host, &port) != 0) {
			cli_out(cli, "Invalid listen address \"%s\"", av[i]);
			cli_result(cli, CLIS_PARAM);
			break;
		}
		n = VSS_resolve(host, port ? port : "http", &ta);
		free(host);
		free(port);
		if (n == 0) {
			cli_out(cli, "Invalid listen address \"%s\"", av[i]);
			cli_result(cli, CLIS_PARAM);
			break;
		}
		for (j = 0; j < n; ++j) {
			ls = calloc(sizeof *ls, 1);
			AN(ls);
			ls->sock = -1;
			ls->addr = ta[j];
			ls->name = strdup(av[i]);
			AN(ls->name);
			VTAILQ_INSERT_TAIL(&lsh, ls, list);
		}
		free(ta);
	}
	FreeArgv(av);
	if (cli->result != CLIS_OK) {
		clean_listen_sock_head(&lsh);
		return;
	}

	REPLACE(master.listen_address, arg);

	clean_listen_sock_head(&heritage.socks);
	heritage.nsocks = 0;

	while (!VTAILQ_EMPTY(&lsh)) {
		ls = VTAILQ_FIRST(&lsh);
		VTAILQ_REMOVE(&lsh, ls, list);
		VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
		heritage.nsocks++;
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_cc_command(struct cli *cli, const struct parspec *par, const char *arg)
{

	/* XXX should have tweak_generic_string */
	(void)par;
	if (arg == NULL) {
		cli_out(cli, "%s", mgt_cc_cmd);
	} else {
		free(mgt_cc_cmd);
		mgt_cc_cmd = strdup(arg);
		XXXAN(mgt_cc_cmd);
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_acceptor(struct cli *cli, const struct parspec *par, const char *arg)
{

	/* XXX should have tweak_generic_string */
	(void)par;
	VCA_tweak_acceptor(cli, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_diag_bitmap(struct cli *cli, const struct parspec *par, const char *arg)
{
	unsigned u;

	(void)par;
	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		master.diag_bitmap = u;
	} else {
		cli_out(cli, "0x%x", master.diag_bitmap);
	}
}

/*--------------------------------------------------------------------*/

/*
 * Make sure to end all lines with either a space or newline of the
 * formatting will go haywire.
 */

#define DELAYED_EFFECT_TEXT \
	"\nNB: This parameter may take quite some time to take (full) effect."

#define MUST_RESTART_TEXT \
	"\nNB: This parameter will not take any effect until the " \
	"child process has been restarted."

#define MUST_RELOAD_TEXT \
	"\nNB: This parameter will not take any effect until the " \
	"VCL programs have been reloaded."

#define EXPERIMENTAL_TEXT \
	"\nNB: We do not know yet if it is a good idea to change " \
	"this parameter, or if the default value is even sensible.  " \
	"Caution is advised, and feedback is most welcome."

/*
 * Remember to update varnishd.1 whenever you add / remove a parameter or
 * change its default value.
 * XXX: we should generate the relevant section of varnishd.1 from here.
 */
static const struct parspec parspec[] = {
	{ "user", tweak_user, NULL, 0, 0,
		"The unprivileged user to run as.  Setting this will "
		"also set \"group\" to the specified user's primary group.",
		MUST_RESTART,
		MAGIC_INIT_STRING },
	{ "group", tweak_group, NULL, 0, 0,
		"The unprivileged group to run as.",
		MUST_RESTART,
		MAGIC_INIT_STRING },
	{ "default_ttl", tweak_uint, &master.default_ttl, 0, UINT_MAX,
		"The TTL assigned to objects if neither the backend nor "
		"the VCL code assigns one.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n"
		"To force an immediate effect at the expense of a total "
		"flush of the cache use \"url.purge .\"",
		0,
		"120", "seconds" },
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
	{ "thread_pool_min", tweak_thread_pool_min, NULL, 1, 0,
		"The minimum number of threads in all worker pools combined.\n"
		"\n"
		"Increasing this may help ramp up faster from low load "
		"situations where threads have expired.\n"
		"\n"
		"Minimum is 1 thread.",
		EXPERIMENTAL | DELAYED_EFFECT,
		"1", "threads" },
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
	{ "overflow_max", tweak_uint, &master.overflow_max, 0, UINT_MAX,
		"Percentage permitted overflow queue length.\n"
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
		"number of worker threads.  ",
		EXPERIMENTAL,
		"3", "requests per request" },
	{ "sess_workspace", tweak_uint, &master.sess_workspace, 1024, UINT_MAX,
		"Bytes of HTTP protocol workspace allocated for sessions. "
		"This space must be big enough for the entire HTTP protocol "
		"header and any edits done to it in the VCL code.\n"
		"Minimum is 1024 bytes.",
		DELAYED_EFFECT,
		"8192", "bytes" },
	{ "obj_workspace", tweak_uint, &master.obj_workspace, 1024, UINT_MAX,
		"Bytes of HTTP protocol workspace allocated for objects. "
		"This space must be big enough for the entire HTTP protocol "
		"header and any edits done to it in the VCL code while it "
		"is cached.\n"
		"Minimum is 1024 bytes.",
		DELAYED_EFFECT,
		"8192", "bytes" },
	{ "default_grace", tweak_uint, &master.default_grace, 0, UINT_MAX,
		"Default grace period.  We will deliver an object "
		"this long after it has expired, provided another thread "
		"is attempting to get a new copy.",
		DELAYED_EFFECT,
		"10" "seconds" },
	{ "sess_timeout", tweak_timeout, &master.sess_timeout, 0, 0,
		"Idle timeout for persistent sessions. "
		"If a HTTP request has not been received in this many "
		"seconds, the session is closed.",
		0,
		"5", "seconds" },
	{ "pipe_timeout", tweak_timeout, &master.pipe_timeout, 0, 0,
		"Idle timeout for PIPE sessions. "
		"If nothing have been received in either direction for "
		"this many seconds, the session is closed.\n",
		0,
		"60", "seconds" },
	{ "send_timeout", tweak_timeout, &master.send_timeout, 0, 0,
		"Send timeout for client connections. "
		"If no data has been sent to the client in this many seconds, "
		"the session is closed.\n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"600", "seconds" },
	{ "auto_restart", tweak_bool, &master.auto_restart, 0, 0,
		"Restart child process automatically if it dies.\n",
		0,
		"on", "bool" },
	{ "fetch_chunksize",
		tweak_uint, &master.fetch_chunksize, 4, UINT_MAX / 1024,
		"The default chunksize used by fetcher. "
		"This should be bigger than the majority of objects with "
		"short TTLs.\n"
		"Internal limits in the storage_file module makes increases "
		"above 128kb a dubious idea.",
		EXPERIMENTAL,
		"128", "kilobytes" },
#ifdef SENDFILE_WORKS
	{ "sendfile_threshold",
		tweak_uint, &master.sendfile_threshold, 0, UINT_MAX,
		"The minimum size of objects transmitted with sendfile.",
		EXPERIMENTAL,
		"-1", "bytes" },
#endif /* SENDFILE_WORKS */
	{ "vcl_trace", tweak_bool,  &master.vcl_trace, 0, 0,
		"Trace VCL execution in the shmlog.\n"
		"Enabling this will allow you to see the path each "
		"request has taken through the VCL program.\n"
		"This generates a lot of logrecords so it is off by "
		"default.",
		0,
		"off", "bool" },
	{ "listen_address", tweak_listen_address, NULL, 0, 0,
		"Whitespace separated list of network endpoints where "
		"Varnish will accept requests.\n"
		"Possible formats: host, host:port, :port",
		MUST_RESTART,
		":80" },
	{ "listen_depth", tweak_uint, &master.listen_depth, 0, UINT_MAX,
		"Listen queue depth.",
		MUST_RESTART,
		"1024", "connections" },
	{ "srcaddr_hash", tweak_uint, &master.srcaddr_hash, 63, UINT_MAX,
		"Number of source address hash buckets.\n"
		"Powers of two are bad, prime numbers are good.",
		EXPERIMENTAL | MUST_RESTART,
		"1049", "buckets" },
	{ "srcaddr_ttl", tweak_uint, &master.srcaddr_ttl, 0, UINT_MAX,
		"Lifetime of srcaddr entries.\n"
		"Zero will disable srcaddr accounting entirely.",
		EXPERIMENTAL,
		"30", "seconds" },
	{ "backend_http11", tweak_bool, &master.backend_http11, 0, 0,
		"Force all backend requests to be HTTP/1.1.\n"
		"By default we copy the protocol version from the "
		"incoming client request.",
		EXPERIMENTAL,
		"off", "bool" },
	{ "client_http11", tweak_bool, &master.client_http11, 0, 0,
		"Force all client responses to be HTTP/1.1.\n"
		"By default we copy the protocol version from the "
		"backend response.",
		EXPERIMENTAL,
		"off", "bool" },
	{ "cli_timeout", tweak_timeout, &master.cli_timeout, 0, 0,
		"Timeout for the childs replies to CLI requests from "
		"the master.",
		0,
		"5", "seconds" },
	{ "ping_interval", tweak_uint, &master.ping_interval, 0, UINT_MAX,
		"Interval between pings from parent to child.\n"
		"Zero will disable pinging entirely, which makes "
		"it possible to attach a debugger to the child.",
		MUST_RESTART,
		"3", "seconds" },
	{ "lru_interval", tweak_timeout, &master.lru_timeout, 0, 0,
		"Grace period before object moves on LRU list.\n"
		"Objects are only moved to the front of the LRU "
		"list if they have not been moved there already inside "
		"this timeout period.  This reduces the amount of lock "
		"operations necessary for LRU list access.",
		EXPERIMENTAL,
		"2", "seconds" },
	{ "cc_command", tweak_cc_command, NULL, 0, 0,
		"Command used for compiling the C source code to a "
		"dlopen(3) loadable object.  Any occurrence of %s in "
		"the string will be replaced with the source file name, "
		"and %o will be replaced with the output file name.",
		MUST_RELOAD,
#ifdef __APPLE__
		"exec cc -dynamiclib -Wl,-undefined,dynamic_lookup -o %o %s"
#elif defined(__SOLARIS__)
		"exec cc -shared -fpic -c %o %s"
#else /* default: GCC on Linux & FreeBSD */
		"exec cc -fpic -shared -Wl,-x -o %o %s"
#endif
		, NULL },
	{ "max_restarts", tweak_uint, &master.max_restarts, 0, UINT_MAX,
		"Upper limit on how many times a request can restart."
		"\nBe aware that restarts are likely to cause a hit against "
		"the backend, so don't increase thoughtlessly.\n",
		0,
		"4", "restarts" },
	{ "max_esi_includes",
		tweak_uint, &master.max_esi_includes, 0, UINT_MAX,
		"Maximum depth of esi:include processing."
		"\nBe aware that restarts are likely to cause a hit against "
		"the backend, so don't increase thoughtlessly.\n",
		0,
		"5", "restarts" },
	{ "cache_vbe_conns", tweak_bool,  &master.cache_vbe_conns, 0, 0,
		"Cache vbe_conn's or rely on malloc, that's the question.",
		EXPERIMENTAL,
		"off", "bool" },
	{ "connect_timeout", tweak_uint,
		&master.connect_timeout,0, UINT_MAX,
		"Default connection timeout for backend connections.  "
		"We only try to connect to the backend for this many "
		"milliseconds before giving up.  "
		"VCL can override this default value for each backend.",
		0,
		"400", "ms" },
	{ "prefer_ipv6", tweak_bool, &master.prefer_ipv6, 0, 0,
		"Prefer IPv6 address when connecting to backends which "
		"have both IPv4 and IPv6 addresses.",
		0,
		"off", "bool" },
	{ "session_linger", tweak_uint,
		&master.session_linger,0, UINT_MAX,
		"How long time the workerthread lingers on the session "
		"to see if a new request appears right away.\n"
		"If sessions are reused, as much as half of all reuses "
		"happen within the first 100 msec of the previous request "
		"completing.\n"
		"Setting this too high results in worker threads not doing "
		"anything for their keep, setting it too low just means that "
		"more sessions take a detour around the acceptor.",
		EXPERIMENTAL,
		"0", "ms" },
	{ "cli_buffer", tweak_uint, &master.cli_buffer, 4096, UINT_MAX,
		"Size of buffer for CLI input."
		"\nYou may need to increase this if you have big VCL files "
		"and use the vcl.inline CLI command.\n"
		"NB: Must be specified with -p to have effect.\n",
		0,
		"8192", "bytes" },
	{ "log_hashstring", tweak_bool, &master.log_hash, 0, 0,
		"Log the hash string to shared memory log.\n",
		0,
		"off", "bool" },
	{ "log_local_address", tweak_bool, &master.log_local_addr, 0, 0,
		"Log the local address on the TCP connection in the "
		"SessionOpen shared memory record.\n",
		0,
		"off", "bool" },
	{ "acceptor", tweak_acceptor, NULL, 0, 0,
		"Select the acceptor kernel interface.\n",
		EXPERIMENTAL | MUST_RESTART,
		"default", NULL },
	{ "diag_bitmap", tweak_diag_bitmap, 0, 0, 0,
		"Bitmap controlling diagnostics code:\n"
		"  0x00000001 - CNT_Session states.\n"
		"  0x00000002 - workspace debugging.\n"
		"  0x00000004 - kqueue debugging.\n"
		"  0x00000008 - mutex logging.\n"
		"  0x00000010 - mutex contests.\n"
		"  0x00000020 - waiting list.\n"
		"  0x00000040 - object workspace.\n"
		"  0x00001000 - do not core-dump child process.\n"
		"  0x00002000 - only short panic message.\n"
		"  0x00004000 - panic to stderr.\n"
#ifdef HAVE_ABORT2
		"  0x00008000 - panic to abort2().\n"
#endif
		"Use 0x notation and do the bitor in your head :-)\n",
		0,
		"0", "bitmap" },
	{ "err_ttl", tweak_uint, &master.err_ttl, 0, UINT_MAX,
		"The TTL assigned to the synthesized error pages\n",
		0,
		"0", "seconds" },
	{ NULL, NULL, NULL }
};

/*--------------------------------------------------------------------*/

#define WIDTH 76

static void
mcf_wrap(struct cli *cli, const char *text)
{
	const char *p, *q;

	/* Format text to COLUMNS width */
	for (p = text; *p != '\0'; ) {
		q = strchr(p, '\n');
		if (q == NULL)
			q = strchr(p, '\0');
		if (q > p + WIDTH - margin) {
			q = p + WIDTH - margin;
			while (q > p && *q != ' ')
				q--;
			AN(q);
		}
		cli_out(cli, "%*s %.*s\n", margin, "", (int)(q - p), p);
		p = q;
		if (*p == ' ' || *p == '\n')
			p++;
	}
}

void
mcf_param_show(struct cli *cli, const char * const *av, void *priv)
{
	const struct parspec *pp;
	int lfmt;

	(void)priv;
	if (av[2] == NULL || strcmp(av[2], "-l"))
		lfmt = 0;
	else
		lfmt = 1;
	for (pp = parspec; pp->name != NULL; pp++) {
		if (av[2] != NULL && !lfmt && strcmp(pp->name, av[2]))
			continue;
		cli_out(cli, "%-*s ", margin, pp->name);
		if (pp->func == NULL) {
			cli_out(cli, "Not implemented.\n");
			if (av[2] != NULL && !lfmt)
				return;
			else
				continue;
		}
		pp->func(cli, pp, NULL);
		if (pp->units != NULL)
			cli_out(cli, " [%s]\n", pp->units);
		else
			cli_out(cli, "\n");
		if (av[2] != NULL) {
			cli_out(cli, "%-*s Default is %s\n",
			    margin, "", pp->def);
			mcf_wrap(cli, pp->descr);
			if (pp->flags & DELAYED_EFFECT)
				mcf_wrap(cli, DELAYED_EFFECT_TEXT);
			if (pp->flags & EXPERIMENTAL)
				mcf_wrap(cli, EXPERIMENTAL_TEXT);
			if (pp->flags & MUST_RELOAD)
				mcf_wrap(cli, MUST_RELOAD_TEXT);
			if (pp->flags & MUST_RESTART)
				mcf_wrap(cli, MUST_RESTART_TEXT);
			if (!lfmt)
				return;
			else
				cli_out(cli, "\n");
		}
	}
	if (av[2] != NULL && !lfmt) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "Unknown parameter \"%s\".", av[2]);
	}
}

/*--------------------------------------------------------------------*/

void
MCF_ParamSync(void)
{
	if (params != &master)
		*params = master;
}

/*--------------------------------------------------------------------*/

void
MCF_ParamSet(struct cli *cli, const char *param, const char *val)
{
	const struct parspec *pp;

	for (pp = parspec; pp->name != NULL; pp++) {
		if (!strcmp(pp->name, param)) {
			pp->func(cli, pp, val);
			if (cli->result != CLIS_OK) {
			} else if (child_pid >= 0 && pp->flags & MUST_RESTART) {
				cli_out(cli, "Change will take effect"
				    " when child is restarted");
			} else if (pp->flags & MUST_RELOAD) {
				cli_out(cli, "Change will take effect"
				    " when VCL script is reloaded");
			}
			MCF_ParamSync();
			return;
		}
	}
	cli_result(cli, CLIS_PARAM);
	cli_out(cli, "Unknown parameter \"%s\".", param);
}


/*--------------------------------------------------------------------*/

void
mcf_param_set(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	MCF_ParamSet(cli, av[2], av[3]);
}

/*--------------------------------------------------------------------*/

void
MCF_ParamInit(struct cli *cli)
{
	const struct parspec *pp;

	for (pp = parspec; pp->name != NULL; pp++) {
		cli_out(cli, "Set Default for %s = %s\n", pp->name, pp->def);
		if (strlen(pp->name) + 1 > margin)
			margin = strlen(pp->name) + 1;
		pp->func(cli, pp, pp->def);
		if (cli->result != CLIS_OK)
			return;
	}
	params = &master;
}
