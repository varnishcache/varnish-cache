/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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

struct parspec;

typedef void tweak_t(struct cli *, struct parspec *, const char *arg);

struct parspec {
	const char	*name;
	tweak_t		*func;
	const char	*descr;
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
tweak_generic_uint(struct cli *cli, volatile unsigned *dest, const char *arg, unsigned min, unsigned max)
{
	unsigned u;

	if (arg != NULL) {
		if (!strcasecmp(arg, "unlimited"))
			u = UINT_MAX;
		else
			u = strtoul(arg, NULL, 0);
		if (u < min) {
			cli_out(cli, "Must be at least %u", min);
			cli_result(cli, CLIS_PARAM);
			return;
		}
		if (u > max) {
			cli_out(cli, "Must be no more than %u", max);
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
tweak_user(struct cli *cli, struct parspec *par, const char *arg)
{
	struct passwd *pw;
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if ((pw = getpwnam(arg)) == NULL) {
			cli_out(cli, "Unknown user");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		if (master.user)
			free(master.user);
		master.user = strdup(pw->pw_name);
		AN(master.user);
		master.uid = pw->pw_uid;

		/* set group to user's primary group */
		if (master.group)
			free(master.group);
		if ((gr = getgrgid(pw->pw_gid)) != NULL &&
		    (gr = getgrnam(gr->gr_name)) != NULL &&
		    gr->gr_gid == pw->pw_gid) {
			master.group = strdup(gr->gr_name);
			AN(master.group);
		}
		master.gid = pw->pw_gid;
	} else if (master.user) {
		cli_out(cli, "%s (%d)", master.user, (int)master.uid);
	} else {
		cli_out(cli, "%d", (int)master.uid);
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_group(struct cli *cli, struct parspec *par, const char *arg)
{
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if ((gr = getgrnam(arg)) == NULL) {
			cli_out(cli, "Unknown group");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		if (master.group)
			free(master.group);
		master.group = strdup(gr->gr_name);
		AN(master.group);
		master.gid = gr->gr_gid;
	} else if (master.group) {
		cli_out(cli, "%s (%d)", master.group, (int)master.gid);
	} else {
		cli_out(cli, "%d", (int)master.gid);
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_default_ttl(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.default_ttl, arg, 0, UINT_MAX);
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pools(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.wthread_pools, arg,
	    1, UINT_MAX);
}


/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_min(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.wthread_min, arg,
	    0, master.wthread_max);
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_max(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.wthread_max, arg,
	    master.wthread_min, UINT_MAX);
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_timeout(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_timeout(cli, &master.wthread_timeout, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_overflow_max(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.overflow_max, arg, 0, UINT_MAX);
}

/*--------------------------------------------------------------------*/

static void
tweak_http_workspace(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.mem_workspace, arg,
	    1024, UINT_MAX);
}

/*--------------------------------------------------------------------*/

static void
tweak_sess_timeout(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_timeout(cli, &master.sess_timeout, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_pipe_timeout(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_timeout(cli, &master.pipe_timeout, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_send_timeout(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_timeout(cli, &master.send_timeout, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_auto_restart(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_bool(cli, &master.auto_restart, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_fetch_chunksize(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.fetch_chunksize, arg,
	    4, UINT_MAX / 1024);
}

#ifdef HAVE_SENDFILE
/*--------------------------------------------------------------------*/

static void
tweak_sendfile_threshold(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	tweak_generic_uint(cli, &master.sendfile_threshold, arg, 0, UINT_MAX);
}
#endif /* HAVE_SENDFILE */

/*--------------------------------------------------------------------*/

static void
tweak_vcl_trace(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_bool(cli, &master.vcl_trace, arg);
}

/*--------------------------------------------------------------------*/

static void
clean_listen_sock_head(struct listen_sock_head *lsh)
{
	struct listen_sock *ls, *ls2;

	TAILQ_FOREACH_SAFE(ls, lsh, list, ls2) {
		TAILQ_REMOVE(lsh, ls, list);
		free(ls->addr);
		free(ls);
	}
}

static void
tweak_listen_address(struct cli *cli, struct parspec *par, const char *arg)
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
	TAILQ_INIT(&lsh);
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
			TAILQ_INSERT_TAIL(&lsh, ls, list);
		}
		free(ta);
	}
	FreeArgv(av);
	if (cli->result != CLIS_OK) {
		clean_listen_sock_head(&lsh);
		return;
	}

	free(master.listen_address);
	master.listen_address = strdup(arg);
	AN(master.listen_address);

	clean_listen_sock_head(&heritage.socks);
	heritage.nsocks = 0;

	while (!TAILQ_EMPTY(&lsh)) {
		ls = TAILQ_FIRST(&lsh);
		TAILQ_REMOVE(&lsh, ls, list);
		TAILQ_INSERT_TAIL(&heritage.socks, ls, list);
		heritage.nsocks++;
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_listen_depth(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_uint(cli, &master.listen_depth, arg, 0, UINT_MAX);
}

/*--------------------------------------------------------------------*/

static void
tweak_srcaddr_hash(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_uint(cli, &master.srcaddr_hash, arg, 63, UINT_MAX);
}

/*--------------------------------------------------------------------*/

static void
tweak_srcaddr_ttl(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_uint(cli, &master.srcaddr_ttl, arg, 0, UINT_MAX);
}

/*--------------------------------------------------------------------*/

static void
tweak_backend_http11(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_bool(cli, &master.backend_http11, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_client_http11(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_bool(cli, &master.client_http11, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_ping_interval(struct cli *cli, struct parspec *par, const char *arg)
{
	(void)par;
	tweak_generic_uint(cli, &master.ping_interval, arg, 0, UINT_MAX);
}

/*--------------------------------------------------------------------*/

#define NAME_RE "^([0-9A-Za-z-]+)(\\.[0-9A-Za-z-]+)*$"
static regex_t *name_re;

static void
tweak_name(struct cli *cli, struct parspec *par, const char *arg)
{
	char hostname[1024], path[1024];
	int error;

	(void)par;

	if (arg == NULL) {
		cli_out(cli, "\"%s\"", master.name);
		return;
	}

	/* empty string -> hostname */
	if (*arg == '\0') {
		if (gethostname(hostname, sizeof hostname) == 0)
			arg = hostname;
		else
			arg = "localhost";
	}

	/* check that the new name follows hostname convention */
	if (name_re == NULL) {
		name_re = malloc(sizeof *name_re);
		AN(name_re);
		AZ(regcomp(name_re, NAME_RE, REG_EXTENDED|REG_NOSUB));
	}
	if (regexec(name_re, arg, 0, NULL, 0) != 0) {
		cli_out(cli, "Invalid instance name");
		cli_result(cli, CLIS_PARAM);
		return;
	}

	error = 0;
	snprintf(path, sizeof path, "/tmp/%s", arg); /* XXX overflow */
	if (master.name && *master.name) {
		struct stat old_st;
		char old_path[1024];

		/* rename old directory */
		snprintf(old_path, sizeof old_path, "/tmp/%s", master.name); /* XXX overflow */
		if (stat(old_path, &old_st) == 0 && S_ISDIR(old_st.st_mode)) {
			error = rename(old_path, path);
		} else {
			error = (mkdir(path, 0755) != 0 && errno != EEXIST);
		}
	} else {
		error = (mkdir(path, 0755) != 0 && errno != EEXIST);
	}

	if (error || chdir(path) != 0) {
		cli_out(cli, "could not create %s: %s", path, strerror(errno));
		cli_result(cli, CLIS_CANT);
		return;
	}

	/* Everything is fine, store the (new) name */
	master.name = strdup(arg);
	XXXAN(master.name);
}

/*--------------------------------------------------------------------*/

/*
 * Make sure to end all lines with either a space or newline of the
 * formatting will go haywire.
 */

#define DELAYED_EFFECT \
	"\nNB: This parameter will take some time to take effect.\n"

#define MUST_RESTART \
	"\nNB: This parameter will not take any effect until the " \
	"child process has been restarted.\n"

#define EXPERIMENTAL \
	"\nNB: We don't know yet if it is a good idea to change " \
	"this parameter.  Caution advised.\n"

/*
 * Remember to update varnishd.1 whenever you add / remove a parameter or
 * change its default value.
 */
static struct parspec parspec[] = {
	{ "user", tweak_user,
		"The unprivileged user to run as.  Setting this will "
		"also set \"group\" to the specified user's primary group.\n"
		MUST_RESTART,
		"nobody" },
	{ "group", tweak_group,
		"The unprivileged group to run as.\n"
		MUST_RESTART,
		"nogroup" },
	{ "default_ttl", tweak_default_ttl,
		"The TTL assigned to objects if neither the backend nor "
		"the VCL code assigns one.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n"
		"To force an immediate effect at the expense of a total "
		"flush of the cache use \"url.purge .\"",
		"120", "seconds" },
	{ "thread_pools", tweak_thread_pools,
		"Number of worker pools. "
		"Increasing number of worker pools decreases lock "
		"contention but increases the number of threads as well. "
		"Can be increased on the fly, but decreases require a "
		"restart to take effect.\n"
		EXPERIMENTAL,
		"1", "pools" },
	{ "thread_pool_max", tweak_thread_pool_max,
		"The maximum number of threads in the total worker pool.\n"
		"-1 is unlimited.\n"
		EXPERIMENTAL
		DELAYED_EFFECT,
		"1000", "threads" },
	{ "thread_pool_min", tweak_thread_pool_min,
		"The minimum number of threads in the worker pool.\n"
		"Minimum is 1 thread. "
		EXPERIMENTAL
		DELAYED_EFFECT,
		"1", "threads" },
	{ "thread_pool_timeout", tweak_thread_pool_timeout,
		"Thread dies after this many seconds of inactivity.\n"
		"Minimum is 1 second. "
		EXPERIMENTAL
		DELAYED_EFFECT,
		"120", "seconds" },
	{ "overflow_max", tweak_overflow_max,
		"Limit on overflow queue length in percent of "
		"thread_pool_max parameter.\n"
		EXPERIMENTAL,
		"100", "%" },
	{ "http_workspace", tweak_http_workspace,
		"Bytes of HTTP protocol workspace allocated. "
		"This space must be big enough for the entire HTTP protocol "
		"header and any edits done to it in the VCL code.\n"
		"Minimum is 1024 bytes. "
		DELAYED_EFFECT,
		"8192", "bytes" },
	{ "sess_timeout", tweak_sess_timeout,
		"Idle timeout for persistent sessions. "
		"If a HTTP request has not been received in this many "
		"seconds, the session is closed.\n",
		"5", "seconds" },
	{ "pipe_timeout", tweak_pipe_timeout,
		"Idle timeout for PIPE sessions. "
		"If nothing have been received in either direction for "
		"this many seconds, the session is closed.\n",
		"60", "seconds" },
	{ "send_timeout", tweak_send_timeout,
		"Send timeout for client connections. "
		"If no data has been sent to the client in this many seconds, "
		"the session is closed.\n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.\n"
		DELAYED_EFFECT,
		"600", "seconds" },
	{ "auto_restart", tweak_auto_restart,
		"Restart child process automatically if it dies.\n",
		"on", "bool" },
	{ "fetch_chunksize", tweak_fetch_chunksize,
		"The default chunksize used by fetcher. "
		"This should be bigger than the majority of objects with "
		"short TTLs.\n"
		"Internal limits in the storage_file module makes increases "
		"above 128kb a dubious idea.\n"
		EXPERIMENTAL,
		"128", "kilobytes" },
#ifdef HAVE_SENDFILE
	{ "sendfile_threshold", tweak_sendfile_threshold,
		"The minimum size of objects transmitted with sendfile.\n"
#if defined(__FreeBSD__)
		"In \"plenty-of-RAM\" scenarios this is unlikely to "
		"have any effect.  Once disk-I/O becomes frequent "
		"we guess smaller values are likely to be better.\n"
#elif defined(__Linux__)
		"Linux sendfile(2) does not allow for inclusion of "
		"header data and therefore using sendfile(2) means "
		"an extra system call, compared to using writev(2) for "
		"both the header and body.\n"
		"We suspect that sendfile(2) on Linux will only start "
		"to be beneficial in low-ram scenarios.  Therefore it "
		"may make sense to set this to \"unlimited\".\n"
#endif
		EXPERIMENTAL,
		"-1", "bytes" },
#endif /* HAVE_SENDFILE */
	{ "vcl_trace", tweak_vcl_trace,
		"Trace VCL execution in the shmlog.\n"
		"Enabling this will allow you to see the path each "
		"request has taken through the VCL program.\n"
		"This generates a lot of logrecords so it is off by "
		"default. ",
		"off", "bool" },
	{ "listen_address", tweak_listen_address,
		"Whitespace separated list of network endpoints where "
		"Varnish will accept requests.\n"
		"Possible formats: host, host:port, :port\n"
		MUST_RESTART,
		":80" },
	{ "listen_depth", tweak_listen_depth,
		"Listen(2) queue depth.\n"
#if defined(__FreeBSD__)
		"Please see FreeBSDs tuning(7) manual page for more "
		"information.\n"
#endif
		MUST_RESTART,
		"1024", "connections" },
	{ "srcaddr_hash", tweak_srcaddr_hash,
		"Number of source address hash buckets.\n"
		"Powers of two are bad, prime numbers are good.\n"
		EXPERIMENTAL
		MUST_RESTART,
		"1049", "buckets" },
	{ "srcaddr_ttl", tweak_srcaddr_ttl,
		"Lifetime of srcaddr entries.\n"
		"Zero will disable srcaddr accounting entirely.\n"
		EXPERIMENTAL,
		"30", "seconds" },
	{ "backend_http11", tweak_backend_http11,
		"Force all backend requests to be HTTP/1.1.\n"
		"By default we copy the protocol version from the "
		"incoming client request."
		EXPERIMENTAL,
		"off", "bool" },
	{ "client_http11", tweak_client_http11,
		"Force all client responses to be HTTP/1.1.\n"
		"By default we copy the protocol version from the "
		"backend response."
		EXPERIMENTAL,
		"off", "bool" },
	{ "ping_interval", tweak_ping_interval,
		"Interval between pings from parent to child.\n"
		"Zero will disable pinging entirely, which makes "
		"it possible to attach a debugger to the child.\n"
		MUST_RESTART,
		"3", "seconds" },
	{ "name", tweak_name,
		"Name of varnishd instance. Must follow hostname "
		"naming conventions. Makes it possible to run "
		"multiple varnishd instances on one server.\n"
		EXPERIMENTAL,
		"" },
	{ NULL, NULL, NULL }
};

/*--------------------------------------------------------------------*/

void
mcf_param_show(struct cli *cli, char **av, void *priv)
{
	struct parspec *pp;
	const char *p, *q;
	int lfmt;

	(void)priv;
	if (av[2] == NULL || strcmp(av[2], "-l"))
		lfmt = 0;
	else
		lfmt = 1;
	for (pp = parspec; pp->name != NULL; pp++) {
		if (av[2] != NULL && !lfmt && strcmp(pp->name, av[2]))
			continue;
		cli_out(cli, "%-20s ", pp->name);
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
			cli_out(cli, "%-20s Default is %s\n", "", pp->def);
			/* Format text to 72 col width */
			for (p = pp->descr; *p != '\0'; ) {
				q = strchr(p, '\n');
				if (q == NULL)
					q = strchr(p, '\0');
				if (q > p + 52) {
					q = p + 52;
					while (q > p && *q != ' ')
						q--;
					AN(q);
				}
				cli_out(cli, "%20s %.*s\n", "", q - p, p);
				p = q;
				if (*p == ' ' || *p == '\n')
					p++;
			}
			if (!lfmt)
				return;
			else
				cli_out(cli, "\n");
		}
	}
	if (av[2] != NULL && !lfmt) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "Unknown paramter \"%s\".", av[2]);
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
	struct parspec *pp;

	for (pp = parspec; pp->name != NULL; pp++) {
		if (!strcmp(pp->name, param)) {
			pp->func(cli, pp, val);
			return;
		}
	}
	cli_result(cli, CLIS_PARAM);
	cli_out(cli, "Unknown paramter \"%s\".", param);
	MCF_ParamSync();
}


/*--------------------------------------------------------------------*/

void
mcf_param_set(struct cli *cli, char **av, void *priv)
{

	(void)priv;
	MCF_ParamSet(cli, av[2], av[3]);
}

/*--------------------------------------------------------------------*/

void
MCF_ParamInit(struct cli *cli)
{
	struct parspec *pp;

	for (pp = parspec; pp->name != NULL; pp++) {
		cli_out(cli, "Set Default for %s = %s\n", pp->name, pp->def);
		pp->func(cli, pp, pp->def);
		if (cli->result != CLIS_OK)
			return;
	}
	params = &master;
}
