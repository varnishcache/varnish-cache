/*
 * $Id$
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#include "cli.h"
#include "cli_priv.h"
#include "mgt.h"
#include "mgt_cli.h"

#include "heritage.h"

struct parspec;

typedef void tweak_t(struct cli *, struct parspec *, const char *arg);

struct parspec {
	const char	*name;
	tweak_t		*func;
	const char	*expl;
};

/*--------------------------------------------------------------------*/

static void
tweak_default_ttl(struct cli *cli, struct parspec *par, const char *arg)
{

	(void)par;
	if (arg != NULL)
		params->default_ttl = strtoul(arg, NULL, 0);
	cli_out(cli, "%u [seconds]\n", params->default_ttl);
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_min(struct cli *cli, struct parspec *par, const char *arg)
{
	unsigned u;

	(void)par;
	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u >= params->wthread_max) {
			cli_out(cli, "Minimum must be less than maximum\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		params->wthread_min = u;
	}
	cli_out(cli, "%u [threads]\n", params->wthread_min);
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_max(struct cli *cli, struct parspec *par, const char *arg)
{
	unsigned u;

	(void)par;
	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u <= params->wthread_min) {
			cli_out(cli, "Maximum must be greater than minimum\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		params->wthread_max = u;
	}
	if (params->wthread_max == UINT_MAX) 
		cli_out(cli, "unlimited\n");
	else 
		cli_out(cli, "%u [threads]\n", params->wthread_max);
}

/*--------------------------------------------------------------------*/

static void
tweak_thread_pool_timeout(struct cli *cli, struct parspec *par, const char *arg)
{
	unsigned u;

	(void)par;
	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u == 0) {
			cli_out(cli, "Timeout must be greater than zero\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		params->wthread_timeout = u;
	}
	cli_out(cli, "%u [seconds]\n", params->wthread_timeout);
}
/*--------------------------------------------------------------------*/

static void
tweak_http_workspace(struct cli *cli, struct parspec *par, const char *arg)
{
	unsigned u;

	(void)par;
	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u <= 1024) {
			cli_out(cli, "Workspace must be at least 1024 bytes\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		params->mem_workspace = u;
	}
	cli_out(cli, "%u [bytes]\n", params->mem_workspace);
}

/*--------------------------------------------------------------------*/

static void
tweak_sess_timeout(struct cli *cli, struct parspec *par, const char *arg)
{
	unsigned u;

	(void)par;
	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u == 0) {
			cli_out(cli, "Timeout must be greater than zero\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		params->sess_timeout = u;
	}
	cli_out(cli, "%u [seconds]\n", params->sess_timeout);
}

/*--------------------------------------------------------------------*/

static void
tweak_send_timeout(struct cli *cli, struct parspec *par, const char *arg)
{
	unsigned u;

	(void)par;
	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u == 0) {
			cli_out(cli, "Timeout must be greater than zero\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		params->send_timeout = u;
	}
	cli_out(cli, "%u [seconds]\n", params->send_timeout);
}

/*--------------------------------------------------------------------*/

static void
tweak_auto_restart(struct cli *cli, struct parspec *par, const char *arg)
{
	unsigned u;

	(void)par;
	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u != 0 && u != 1) {
			cli_out(cli, "Only zero and one allowed.\n");
			cli_result(cli, CLIS_PARAM);
			return;
		}
		params->auto_restart = u;
	}
	cli_out(cli, "%u {1 = yes, 0 = no}\n", params->auto_restart);
}

/*--------------------------------------------------------------------*/

/*
 * Make sure to end all lines with either a space or newline of the
 * formatting will go haywire.
 */

#define DELAYED_EFFECT \
	"\nNB: This parameter will take some time to take effect.\n"

#define SHOULD_RESTART \
	"\nNB: This parameter will not take full effect until the " \
	"child process has been restarted.\n"

#define MUST_RESTART \
	"\nNB: This parameter will not take any effect until the " \
	"child process has been restarted.\n"


static struct parspec parspec[] = {
	{ "default_ttl", tweak_default_ttl,
		"The TTL assigned to objects if neither the backend nor "
		"the VCL code assigns one.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n"
		"To force an immediate effect at the expense of a total "
		"flush of the cache use \"url.purge .\"\n"
		"Default is 120 seconds. " },
	{ "thread_pool_min", tweak_thread_pool_min,
		"The minimum number of threads in the worker pool.\n"
		DELAYED_EFFECT
		"Default is 1 thread. " 
		"Minimum is 1 thread. " },
	{ "thread_pool_max", tweak_thread_pool_max,
		"The maximum number of threads in the worker pool.\n"
		DELAYED_EFFECT
		"Default is no limit." },
	{ "thread_pool_timeout", tweak_thread_pool_timeout,
		"Thread dies after this many seconds of inactivity.\n"
		"Default is 10 seconds. "
		"Minimum is 1 second. " },
	{ "http_workspace", tweak_http_workspace,
		"Bytes of HTTP protocol workspace allocated. "
		"This space must be big enough for the entire HTTP protocol "
		"header and any edits done to it in the VCL code.\n"
		SHOULD_RESTART
		"Default is 4096 bytes. "
		"Minimum is 1024 bytes. " },
	{ "sess_timeout", tweak_sess_timeout,
		"Idle timeout for persistent sessions. "
		"If a HTTP request has not been received in this many "
		"seconds, the session is closed.\n"
#ifdef HAVE_ACCEPT_FILTERS
		DELAYED_EFFECT
#endif
		"Default is 15 seconds. " },
	{ "send_timeout", tweak_send_timeout,
		"Send timeout for client connections. "
		"If no data has been sent to the client in this many seconds, "
		"the session is closed.\n"
		DELAYED_EFFECT
		"See getopt(3) under SO_SNDTIMEO for more information.\n"
		"Default is 600 seconds. " },
	{ "auto_restart", tweak_auto_restart,
		"Restart child process automatically if it dies. "
		"1 = yes, 0 = no.\n"
		"Default is 1. " },
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
		if (av[2] != NULL) {
			/* Format text to 72 col width */
			for (p = pp->expl; *p != '\0'; ) {
				q = strchr(p, '\n');
				if (q == NULL)
					q = strchr(p, '\0');
				assert(q != NULL);
				if (q > p + 52) {
					q = p + 52;
					while (q > p && *q != ' ')
						q--;
					assert(q != NULL);
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
mcf_param_set(struct cli *cli, char **av, void *priv)
{
	struct parspec *pp;

	(void)priv;
	for (pp = parspec; pp->name != NULL; pp++) {
		if (!strcmp(pp->name, av[2])) {
			cli_out(cli, "%-20s ", pp->name);
			pp->func(cli, pp, av[3]);
			return;
		}
	}
	if (av[2] != NULL) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "Unknown paramter \"%s\".", av[2]);
	}
}

