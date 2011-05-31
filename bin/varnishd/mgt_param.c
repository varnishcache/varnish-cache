/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <grp.h>
#include <limits.h>
#include <pwd.h>
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
#include "vparam.h"

#include "vss.h"

#define MAGIC_INIT_STRING	"\001"
struct params master;
static int nparspec;
static struct parspec const ** parspec;
static int margin;

/*--------------------------------------------------------------------*/

static const struct parspec *
mcf_findpar(const char *name)
{
	int i;

	for (i = 0; i < nparspec; i++)
		if (!strcmp(parspec[i]->name, name))
			return (parspec[i]);
	return (NULL);
}

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

void
tweak_timeout(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	tweak_generic_timeout(cli, dest, arg);
}

static void
tweak_timeout_double(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	volatile double *dest;
	double u;

	dest = par->priv;
	if (arg != NULL) {
		u = strtod(arg, NULL);
		if (u < par->min) {
			cli_out(cli,
			    "Timeout must be greater or equal to %.g\n",
				 par->min);
			cli_result(cli, CLIS_PARAM);
			return;
		}
		if (u > par->max) {
			cli_out(cli,
			    "Timeout must be less than or equal to %.g\n",
				 par->max);
			cli_result(cli, CLIS_PARAM);
			return;
		}
		*dest = u;
	} else
		cli_out(cli, "%.6f", *dest);
}

/*--------------------------------------------------------------------*/

static void
tweak_generic_double(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	volatile double *dest;
	double u;

	dest = par->priv;
	if (arg != NULL) {
		u = strtod(arg, NULL);
		if (u < par->min) {
			cli_out(cli,
			    "Must be greater or equal to %.g\n",
				 par->min);
			cli_result(cli, CLIS_PARAM);
			return;
		}
		if (u > par->max) {
			cli_out(cli,
			    "Must be less than or equal to %.g\n",
				 par->max);
			cli_result(cli, CLIS_PARAM);
			return;
		}
		*dest = u;
	} else
		cli_out(cli, "%f", *dest);
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
		else if (!strcasecmp(arg, "false"))
			*dest = 0;
		else if (!strcasecmp(arg, "on"))
			*dest = 1;
		else if (!strcasecmp(arg, "enable"))
			*dest = 1;
		else if (!strcasecmp(arg, "yes"))
			*dest = 1;
		else if (!strcasecmp(arg, "true"))
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

void
tweak_generic_uint(struct cli *cli, volatile unsigned *dest, const char *arg,
    unsigned min, unsigned max)
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

void
tweak_uint(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	tweak_generic_uint(cli, dest, arg, (uint)par->min, (uint)par->max);
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
tweak_listen_address(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	char **av;
	int i;
	struct listen_sock		*ls;
	struct listen_sock_head		lsh;

	(void)par;
	if (arg == NULL) {
		cli_quote(cli, master.listen_address);
		return;
	}

	av = VAV_Parse(arg, NULL, ARGV_COMMA);
	if (av == NULL) {
		cli_out(cli, "Parse error: out of memory");
		cli_result(cli, CLIS_PARAM);
		return;
	}
	if (av[0] != NULL) {
		cli_out(cli, "Parse error: %s", av[0]);
		cli_result(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	if (av[1] == NULL) {
		cli_out(cli, "Empty listen address");
		cli_result(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	VTAILQ_INIT(&lsh);
	for (i = 1; av[i] != NULL; i++) {
		struct vss_addr **ta;
		int j, n;

		n = VSS_resolve(av[i], "http", &ta);
		if (n == 0) {
			cli_out(cli, "Invalid listen address ");
			cli_quote(cli, av[i]);
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
	VAV_Free(av);
	if (cli != NULL && cli->result != CLIS_OK) {
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
tweak_string(struct cli *cli, const struct parspec *par, const char *arg)
{
	char **p = TRUST_ME(par->priv);

	AN(p);
	/* XXX should have tweak_generic_string */
	if (arg == NULL) {
		cli_quote(cli, *p);
	} else {
		REPLACE(*p, arg);
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_waiter(struct cli *cli, const struct parspec *par, const char *arg)
{

	/* XXX should have tweak_generic_string */
	(void)par;
	VCA_tweak_waiter(cli, arg);
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

#define WIZARD_TEXT \
	"\nNB: Do not change this parameter, unless a developer tell " \
	"you to do so."

/*
 * Remember to update varnishd.1 whenever you add / remove a parameter or
 * change its default value.
 * XXX: we should generate the relevant section of varnishd.1 from here.
 */
static const struct parspec input_parspec[] = {
	{ "user", tweak_user, NULL, 0, 0,
		"The unprivileged user to run as.  Setting this will "
		"also set \"group\" to the specified user's primary group.",
		MUST_RESTART,
		MAGIC_INIT_STRING },
	{ "group", tweak_group, NULL, 0, 0,
		"The unprivileged group to run as.",
		MUST_RESTART,
		MAGIC_INIT_STRING },
	{ "default_ttl", tweak_timeout_double, &master.default_ttl,
		0, UINT_MAX,
		"The TTL assigned to objects if neither the backend nor "
		"the VCL code assigns one.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n"
		"To force an immediate effect at the expense of a total "
		"flush of the cache use \"ban.url .\"",
		0,
		"120", "seconds" },
	{ "sess_workspace", tweak_uint, &master.sess_workspace, 1024, UINT_MAX,
		"Bytes of HTTP protocol workspace allocated for sessions. "
		"This space must be big enough for the entire HTTP protocol "
		"header and any edits done to it in the VCL code.\n"
		"Minimum is 1024 bytes.",
		DELAYED_EFFECT,
		"65536",
		"bytes" },
	{ "http_req_hdr_len", tweak_uint, &master.http_req_hdr_len,
		40, UINT_MAX,
		"Maximum length of any HTTP client request header we will "
		"allow.  The limit is inclusive its continuation lines.\n",
		0,
		"2048", "bytes" },
	{ "http_req_size", tweak_uint, &master.http_req_size,
		256, UINT_MAX,
		"Maximum number of bytes of HTTP client request we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the session "
		"workspace (param: sess_workspace) and this parameter limits "
		"how much of that the request is allowed to take up.",
		0,
		"32768", "bytes" },
	{ "http_resp_hdr_len", tweak_uint, &master.http_resp_hdr_len,
		40, UINT_MAX,
		"Maximum length of any HTTP backend response header we will "
		"allow.  The limit is inclusive its continuation lines.\n",
		0,
		"2048", "bytes" },
	{ "http_resp_size", tweak_uint, &master.http_resp_size,
		256, UINT_MAX,
		"Maximum number of bytes of HTTP backend resonse we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the worker "
		"workspace (param: sess_workspace) and this parameter limits "
		"how much of that the request is allowed to take up.",
		0,
		"32768", "bytes" },
	{ "http_max_hdr", tweak_uint, &master.http_max_hdr, 32, UINT_MAX,
		"Maximum number of HTTP headers we will deal with in "
		"client request or backend reponses.  "
		"Note that the first line occupies five header fields.\n"
		"This paramter does not influence storage consumption, "
		"objects allocate exact space for the headers they store.\n",
		0,
		"64", "header lines" },
	{ "shm_workspace", tweak_uint, &master.shm_workspace, 4096, UINT_MAX,
		"Bytes of shmlog workspace allocated for worker threads. "
		"If too big, it wastes some ram, if too small it causes "
		"needless flushes of the SHM workspace.\n"
		"These flushes show up in stats as "
		"\"SHM flushes due to overflow\".\n"
		"Minimum is 4096 bytes.",
		DELAYED_EFFECT,
		"8192", "bytes" },
	{ "shm_reclen", tweak_uint, &master.shm_reclen, 16, 65535,
		"Maximum number of bytes in SHM log record.\n"
		"Maximum is 65535 bytes.",
		0,
		"255", "bytes" },
	{ "default_grace", tweak_timeout_double, &master.default_grace,
		0, UINT_MAX,
		"Default grace period.  We will deliver an object "
		"this long after it has expired, provided another thread "
		"is attempting to get a new copy.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n",
		DELAYED_EFFECT,
		"10", "seconds" },
	{ "default_keep", tweak_timeout_double, &master.default_keep,
		0, UINT_MAX,
		"Default keep period.  We will keep a useless object "
		"around this long, making it available for conditional "
		"backend fetches.  "
		"That means that the object will be removed from the "
		"cache at the end of ttl+grace+keep.",
		DELAYED_EFFECT,
		"0", "seconds" },
	{ "sess_timeout", tweak_timeout, &master.sess_timeout, 0, 0,
		"Idle timeout for persistent sessions. "
		"If a HTTP request has not been received in this many "
		"seconds, the session is closed.",
		0,
		"5", "seconds" },
	{ "expiry_sleep", tweak_timeout_double, &master.expiry_sleep, 0, 60,
		"How long the expiry thread sleeps when there is nothing "
		"for it to do.\n",
		0,
		"1", "seconds" },
	{ "pipe_timeout", tweak_timeout, &master.pipe_timeout, 0, 0,
		"Idle timeout for PIPE sessions. "
		"If nothing have been received in either direction for "
		"this many seconds, the session is closed.\n",
		0,
		"60", "seconds" },
	{ "send_timeout", tweak_timeout, &master.send_timeout, 0, 0,
		"Send timeout for client connections. "
		"If the HTTP response hasn't been transmitted in this many\n"
                "seconds the session is closed. \n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"60", "seconds" },
	{ "auto_restart", tweak_bool, &master.auto_restart, 0, 0,
		"Restart child process automatically if it dies.\n",
		0,
		"on", "bool" },
	{ "fetch_chunksize",
		tweak_uint, &master.fetch_chunksize, 4, UINT_MAX / 1024.,
		"The default chunksize used by fetcher. "
		"This should be bigger than the majority of objects with "
		"short TTLs.\n"
		"Internal limits in the storage_file module makes increases "
		"above 128kb a dubious idea.",
		EXPERIMENTAL,
		"128", "kilobytes" },
	{ "fetch_maxchunksize",
		tweak_uint, &master.fetch_maxchunksize, 64, UINT_MAX / 1024.,
		"The maximum chunksize we attempt to allocate from storage. "
		"Making this too large may cause delays and storage "
		"fragmentation.\n",
		EXPERIMENTAL,
		"262144", "kilobytes" },
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
	{ "cli_timeout", tweak_timeout, &master.cli_timeout, 0, 0,
		"Timeout for the childs replies to CLI requests from "
		"the master.",
		0,
		"10", "seconds" },
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
	{ "cc_command", tweak_string, &mgt_cc_cmd, 0, 0,
		"Command used for compiling the C source code to a "
		"dlopen(3) loadable object.  Any occurrence of %s in "
		"the string will be replaced with the source file name, "
		"and %o will be replaced with the output file name.",
		MUST_RELOAD,
		VCC_CC , NULL },
	{ "max_restarts", tweak_uint, &master.max_restarts, 0, UINT_MAX,
		"Upper limit on how many times a request can restart."
		"\nBe aware that restarts are likely to cause a hit against "
		"the backend, so don't increase thoughtlessly.\n",
		0,
		"4", "restarts" },
	{ "esi_syntax",
		tweak_uint, &master.esi_syntax, 0, UINT_MAX,
		"Bitmap controlling ESI parsing code:\n"
		"  0x00000001 - Don't check if it looks like XML\n"
		"  0x00000002 - Ignore non-esi elements\n"
		"  0x00000004 - Emit parsing debug records\n"
		"  0x00000008 - Force-split parser input (debugging)\n"
		"Use 0x notation and do the bitor in your head :-)\n",
		0,
		"0", "bitmap" },
	{ "max_esi_depth",
		tweak_uint, &master.max_esi_depth, 0, UINT_MAX,
		"Maximum depth of esi:include processing.\n",
		0,
		"5", "levels" },
	{ "connect_timeout", tweak_timeout_double,
		&master.connect_timeout,0, UINT_MAX,
		"Default connection timeout for backend connections. "
		"We only try to connect to the backend for this many "
		"seconds before giving up. "
		"VCL can override this default value for each backend and "
		"backend request.",
		0,
		"0.7", "s" },
	{ "first_byte_timeout", tweak_timeout_double,
		&master.first_byte_timeout,0, UINT_MAX,
		"Default timeout for receiving first byte from backend. "
		"We only wait for this many seconds for the first "
		"byte before giving up. A value of 0 means it will never time "
		"out. "
		"VCL can override this default value for each backend and "
		"backend request. This parameter does not apply to pipe.",
		0,
		"60", "s" },
	{ "between_bytes_timeout", tweak_timeout_double,
		&master.between_bytes_timeout,0, UINT_MAX,
		"Default timeout between bytes when receiving data from "
		"backend. "
		"We only wait for this many seconds between bytes "
		"before giving up. A value of 0 means it will never time out. "
		"VCL can override this default value for each backend request "
		"and backend request. This parameter does not apply to pipe.",
		0,
		"60", "s" },
	{ "acceptor_sleep_max", tweak_timeout_double,
		&master.acceptor_sleep_max, 0,  10,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter limits how long it can sleep between "
		"attempts to accept new connections.",
		EXPERIMENTAL,
		"0.050", "s" },
	{ "acceptor_sleep_incr", tweak_timeout_double,
		&master.acceptor_sleep_incr, 0,  1,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter control how much longer we sleep, each time "
		"we fail to accept a new connection.",
		EXPERIMENTAL,
		"0.001", "s" },
	{ "acceptor_sleep_decay", tweak_generic_double,
		&master.acceptor_sleep_decay, 0,  1,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter (multiplicatively) reduce the sleep duration "
		"for each succesfull accept. (ie: 0.9 = reduce by 10%)",
		EXPERIMENTAL,
		"0.900", "" },
	{ "clock_skew", tweak_uint, &master.clock_skew, 0, UINT_MAX,
		"How much clockskew we are willing to accept between the "
		"backend and our own clock.",
		0,
		"10", "s" },
	{ "prefer_ipv6", tweak_bool, &master.prefer_ipv6, 0, 0,
		"Prefer IPv6 address when connecting to backends which "
		"have both IPv4 and IPv6 addresses.",
		0,
		"off", "bool" },
	{ "session_max", tweak_uint,
		&master.max_sess, 1000, UINT_MAX,
		"Maximum number of sessions we will allocate "
		"before just dropping connections.\n"
		"This is mostly an anti-DoS measure, and setting it plenty "
		"high should not hurt, as long as you have the memory for "
		"it.\n",
		0,
		"100000", "sessions" },
	{ "session_linger", tweak_uint,
		&master.session_linger,0, UINT_MAX,
		"How long time the workerthread lingers on the session "
		"to see if a new request appears right away.\n"
		"If sessions are reused, as much as half of all reuses "
		"happen within the first 100 msec of the previous request "
		"completing.\n"
		"Setting this too high results in worker threads not doing "
		"anything for their keep, setting it too low just means that "
		"more sessions take a detour around the waiter.",
		EXPERIMENTAL,
		"50", "ms" },
	{ "cli_buffer", tweak_uint, &master.cli_buffer, 4096, UINT_MAX,
		"Size of buffer for CLI input."
		"\nYou may need to increase this if you have big VCL files "
		"and use the vcl.inline CLI command.\n"
		"NB: Must be specified with -p to have effect.\n",
		0,
		"8192", "bytes" },
	{ "log_hashstring", tweak_bool, &master.log_hash, 0, 0,
		"Log the hash string components to shared memory log.\n",
		0,
		"on", "bool" },
	{ "log_local_address", tweak_bool, &master.log_local_addr, 0, 0,
		"Log the local address on the TCP connection in the "
		"SessionOpen shared memory record.\n",
		0,
		"off", "bool" },
	{ "waiter", tweak_waiter, NULL, 0, 0,
		"Select the waiter kernel interface.\n",
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
		"  0x00010000 - synchronize shmlog.\n"
		"  0x00020000 - synchronous start of persistence.\n"
		"  0x00040000 - release VCL early.\n"
		"  0x80000000 - do edge-detection on digest.\n"
		"Use 0x notation and do the bitor in your head :-)\n",
		0,
		"0", "bitmap" },
	{ "ban_dups", tweak_bool, &master.ban_dups, 0, 0,
		"Detect and eliminate duplicate bans.\n",
		0,
		"on", "bool" },
	{ "syslog_cli_traffic", tweak_bool, &master.syslog_cli_traffic, 0, 0,
		"Log all CLI traffic to syslog(LOG_INFO).\n",
		0,
		"on", "bool" },
	{ "ban_lurker_sleep", tweak_timeout_double,
		&master.ban_lurker_sleep, 0, UINT_MAX,
		"How long time does the ban lurker thread sleeps between "
		"successful attempts to push the last item up the ban "
		" list.  It always sleeps a second when nothing can be done.\n"
		"A value of zero disables the ban lurker.",
		0,
		"0.01", "s" },
	{ "saintmode_threshold", tweak_uint,
		&master.saintmode_threshold, 0, UINT_MAX,
		"The maximum number of objects held off by saint mode before "
		"no further will be made to the backend until one times out.  "
		"A value of 0 disables saintmode.",
		EXPERIMENTAL,
		"10", "objects" },
	{ "http_range_support", tweak_bool, &master.http_range_support, 0, 0,
		"Enable support for HTTP Range headers.\n",
		EXPERIMENTAL,
		"on", "bool" },
	{ "http_gzip_support", tweak_bool, &master.http_gzip_support, 0, 0,
		"Enable gzip support. When enabled Varnish will compress "
		"uncompressed objects before they are stored in the cache. "
		"If a client does not support gzip encoding Varnish will "
		"uncompress compressed objects on demand. Varnish will also "
		"rewrite the Accept-Encoding header of clients indicating "
		"support for gzip to:\n"
		"Accept-Encoding: gzip\n\n"
		"Clients that do not support gzip will have their "
		"Accept-Encoding header removed. For more information on how "
		"gzip is implemented please see the chapter on gzip in the "
		"Varnish reference.",
		EXPERIMENTAL,
		"on", "bool" },
	{ "gzip_tmp_space", tweak_uint, &master.gzip_tmp_space, 0, 2,
		"Where temporary space for gzip/gunzip is allocated:\n"
		"  0 - malloc\n"
		"  1 - session workspace\n"
		"  2 - thread workspace\n"
		"If you have much gzip/gunzip activity, it may be an"
		" advantage to use workspace for these allocations to reduce"
		" malloc activity.  Be aware that gzip needs 256+KB and gunzip"
		" needs 32+KB of workspace (64+KB if ESI processing).",
		EXPERIMENTAL,
		"0", "" },
	{ "gzip_level", tweak_uint, &master.gzip_level, 0, 9,
		"Gzip compression level: 0=debug, 1=fast, 9=best",
		0,
		"6", ""},
	{ "gzip_stack_buffer", tweak_uint, &master.gzip_stack_buffer,
	        2048, UINT_MAX,
		"Size of stack buffer used for gzip processing.\n"
		"The stack buffers are used for in-transit data,"
		" for instance gunzip'ed data being sent to a client."
		"Making this space to small results in more overhead,"
		" writes to sockets etc, making it too big is probably"
		" just a waste of memory.",
		EXPERIMENTAL,
		"32768", "Bytes" },
	{ "shortlived", tweak_timeout_double,
		&master.shortlived, 0, UINT_MAX,
		"Objects created with TTL shorter than this are always "
		"put in transient storage.\n",
		0,
		"10.0", "s" },
	{ "critbit_cooloff", tweak_timeout_double,
		&master.critbit_cooloff, 60, 254,
		"How long time the critbit hasher keeps deleted objheads "
		"on the cooloff list.\n",
		WIZARD,
		"180.0", "s" },
	{ "vcl_dir", tweak_string, &mgt_vcl_dir, 0, 0,
		"Directory from which relative VCL filenames (vcl.load and "
		"include) are opened.",
		0,
#ifdef VARNISH_VCL_DIR
		VARNISH_VCL_DIR,
#else
		".",
#endif
		NULL },
	{ "vmod_dir", tweak_string, &mgt_vmod_dir, 0, 0,
		"Directory where VCL modules are to be found.",
		0,
#ifdef VARNISH_VMOD_DIR
		VARNISH_VMOD_DIR,
#else
		".",
#endif
		NULL },
	{ "vcc_err_unref", tweak_bool, &mgt_vcc_err_unref, 0, 0,
		"Unreferenced VCL objects result in error.\n",
		0,
		"on", "bool" },
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
	int i;
	const struct parspec *pp;
	int lfmt;

	(void)priv;
	if (av[2] == NULL || strcmp(av[2], "-l"))
		lfmt = 0;
	else
		lfmt = 1;
	for (i = 0; i < nparspec; i++) {
		pp = parspec[i];
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
			if (pp->flags & WIZARD)
				mcf_wrap(cli, WIZARD_TEXT);
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

	pp = mcf_findpar(param);
	if (pp != NULL) {
		pp->func(cli, pp, val);
		if (cli->result != CLIS_OK) {
			cli_out(cli, "(attempting to set param %s to %s)\n",
			    pp->name, val);
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

/*--------------------------------------------------------------------
 * Add a group of parameters to the global set and sort by name.
 */

static int
parspec_cmp(const void *a, const void *b)
{
	struct parspec * const * pa = a;
	struct parspec * const * pb = b;
	return (strcmp((*pa)->name, (*pb)->name));
}

static void
MCF_AddParams(const struct parspec *ps)
{
	const struct parspec *pp;
	int n;

	n = 0;
	for (pp = ps; pp->name != NULL; pp++) {
		if (mcf_findpar(pp->name) != NULL)
			fprintf(stderr, "Duplicate param: %s\n", pp->name);
		if (strlen(pp->name) + 1 > margin)
			margin = strlen(pp->name) + 1;
		n++;
	}
	parspec = realloc(parspec, (1L + nparspec + n) * sizeof *parspec);
	XXXAN(parspec);
	for (pp = ps; pp->name != NULL; pp++)
		parspec[nparspec++] = pp;
	parspec[nparspec] = NULL;
	qsort (parspec, nparspec, sizeof parspec[0], parspec_cmp);
}

/*--------------------------------------------------------------------
 * Set defaults for all parameters
 */

static void
MCF_SetDefaults(struct cli *cli)
{
	const struct parspec *pp;
	int i;

	for (i = 0; i < nparspec; i++) {
		pp = parspec[i];
		if (cli != NULL)
			cli_out(cli,
			    "Set Default for %s = %s\n", pp->name, pp->def);
		pp->func(cli, pp, pp->def);
		if (cli != NULL && cli->result != CLIS_OK)
			return;
	}
}

/*--------------------------------------------------------------------*/

void
MCF_ParamInit(struct cli *cli)
{

	MCF_AddParams(input_parspec);
	MCF_AddParams(WRK_parspec);

	/* XXX: We do this twice, to get past any interdependencies */
	MCF_SetDefaults(NULL);
	MCF_SetDefaults(cli);

	params = &master;
}

/*--------------------------------------------------------------------*/

#ifdef DIAGNOSTICS

void
MCF_DumpRst(void)
{
	const struct parspec *pp;
	const char *p, *q;
	int i;

	for (i = 0; i < nparspec; i++) {
		pp = parspec[i];
		printf("%s\n", pp->name);
		if (pp->units != NULL && *pp->units != '\0')
			printf("\t- Units: %s\n", pp->units);
		printf("\t- Default: %s\n", strcmp(pp->def,MAGIC_INIT_STRING) == 0 ? "magic" : pp->def);
		/*
		 * XXX: we should mark the params with one/two flags
		 * XXX: that say if ->min/->max are valid, so we
		 * XXX: can emit those also in help texts.
		 */
		if (pp->flags) {
			printf("\t- Flags: ");
			q = "";
			if (pp->flags & DELAYED_EFFECT) {
				printf("%sdelayed", q);
				q = ", ";
			}
			if (pp->flags & MUST_RESTART) {
				printf("%smust_restart", q);
				q = ", ";
			}
			if (pp->flags & MUST_RELOAD) {
				printf("%smust_reload", q);
				q = ", ";
			}
			if (pp->flags & EXPERIMENTAL) {
				printf("%sexperimental", q);
				q = ", ";
			}
			printf("\n");
		}
		printf("\n\t");
		for (p = pp->descr; *p; p++) {
			if (*p == '\n' && p[1] =='\0')
				break;
			if (*p == '\n' && p[1] =='\n') {
				printf("\n\n\t");
				p++;
			} else if (*p == '\n') {
				printf("\n\t");
			} else if (*p == ':' && p[1] == '\n') {
				/* Start of definition list, use RSTs code mode for this */
				printf("::\n");
			} else {
				printf("%c", *p);
			}
		}
		printf("\n\n");
	}
	printf("\n");
}
#endif /* DIAGNOSTICS */
