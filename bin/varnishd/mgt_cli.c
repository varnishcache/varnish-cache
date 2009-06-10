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
 * The management process' CLI handling
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#ifndef HAVE_VASPRINTF
#include "compat/vasprintf.h"
#endif

#ifndef HAVE_SRANDOMDEV
#include "compat/srandomdev.h"
#endif

#include "cli_priv.h"
#include "cli.h"
#include "vsb.h"
#include "cli_common.h"
#include "vev.h"
#include "vsha256.h"
#include "shmlog.h"
#include "vlu.h"
#include "vss.h"


#include "mgt.h"
#include "heritage.h"
#include "mgt_cli.h"

static int		cli_i = -1, cli_o = -1;
static const char	*secret_file;

struct telnet {
	int			fd;
	struct vev		*ev;
	VTAILQ_ENTRY(telnet)	list;
};

struct cli_port {
	unsigned		magic;
#define CLI_PORT_MAGIC		0x5791079f
	struct vev		*ev;
	int			fdi;
	int			fdo;
	int			verbose;
	struct vlu		*vlu;
	struct cli		cli[1];
	char			*name;
	char			challenge[34];
};

static VTAILQ_HEAD(,telnet)	telnets = VTAILQ_HEAD_INITIALIZER(telnets);
static void telnet_close_all(void);
static void telnet_close_one(int fd);

/*--------------------------------------------------------------------*/

static void
mcf_stats(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	(void)priv;

	AN(VSL_stats);
#define MAC_STAT(n, t, l, f, d) \
	if (VSL_stats->n != 0) \
	    cli_out(cli, "%12ju  %s\n", (VSL_stats->n), d);
#include "stat_field.h"
#undef MAC_STAT
}

/*--------------------------------------------------------------------*/

static void
mcf_help(struct cli *cli, const char * const *av, void *priv)
{
	unsigned u;
	char *p;

	cli_func_help(cli, av, priv);
	if (cli_o >= 0 && (av[2] == NULL || *av[2] == '-')) {
		p = NULL;
		if (!mgt_cli_askchild(&u, &p,
		    "help %s\n", av[2] != NULL ? av[2] : "")) {
			cli_out(cli, "%s", p);
			cli_result(cli, u);
		}
		free(p);
	}
}

/*--------------------------------------------------------------------*/

static void
mcf_close(struct cli *cli, const char *const *av, void *priv)
{

	(void)av;
	(void)priv;
	cli_out(cli, "Closing CLI connection");
	cli_result(cli, CLIS_CLOSE);
}

/*--------------------------------------------------------------------*/

static void
mcf_banner(struct cli *cli, const char *const *av, void *priv)
{

	(void)av;
	(void)priv;
	cli_out(cli, "-----------------------------\n");
	cli_out(cli, "Varnish HTTP accelerator CLI.\n");
	cli_out(cli, "-----------------------------\n");
	cli_out(cli, "Type 'help' for command list.\n");
	cli_out(cli, "Type 'quit' to close CLI session.\n");
	if (child_pid < 0)
		cli_out(cli, "Type 'start' to launch worker process.\n");
	cli_result(cli, CLIS_OK);
}

/*--------------------------------------------------------------------*/

/* XXX: what order should this list be in ? */
static struct cli_proto cli_proto[] = {
	{ CLI_HELP,		mcf_help, cli_proto },
	{ CLI_BANNER,		mcf_banner, NULL },
	{ CLI_PING,		cli_func_ping },
	{ CLI_SERVER_STATUS,	mcf_server_status, NULL },
	{ CLI_SERVER_START,	mcf_server_startstop, NULL },
	{ CLI_SERVER_STOP,	mcf_server_startstop, cli_proto },
	{ CLI_STATS,		mcf_stats, NULL },
	{ CLI_VCL_LOAD,		mcf_config_load, NULL },
	{ CLI_VCL_INLINE,	mcf_config_inline, NULL },
	{ CLI_VCL_USE,		mcf_config_use, NULL },
	{ CLI_VCL_DISCARD,	mcf_config_discard, NULL },
	{ CLI_VCL_LIST,		mcf_config_list, NULL },
	{ CLI_VCL_SHOW,		mcf_config_show, NULL },
	{ CLI_PARAM_SHOW,	mcf_param_show, NULL },
	{ CLI_PARAM_SET,	mcf_param_set, NULL },

	{ CLI_QUIT,		mcf_close, NULL},
	{ NULL }
};

/*--------------------------------------------------------------------*/

static void
mcf_panic(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;
	assert(!strcmp("", "You asked for it"));
}

static struct cli_proto cli_debug[] = {
	{ "debug.panic.master", "debug.panic.master",
		"\tPanic the master process.\n",
		0, 0, mcf_panic, NULL},
	{ NULL }
};

/*--------------------------------------------------------------------
 * Ask the child something over CLI, return zero only if everything is
 * happy happy.
 */

int
mgt_cli_askchild(unsigned *status, char **resp, const char *fmt, ...) {
	char *p;
	int i, j;
	va_list ap;
	unsigned u;

	if (resp != NULL)
		*resp = NULL;
	if (status != NULL)
		*status = 0;
	if (cli_i < 0|| cli_o < 0) {
		if (status != NULL)
			*status = CLIS_CANT;
		return (CLIS_CANT);
	}
	va_start(ap, fmt);
	i = vasprintf(&p, fmt, ap);
	va_end(ap);
	if (i < 0)
		return (i);
	assert(p[i - 1] == '\n');
	j = write(cli_o, p, i);
	free(p);
	if (j != i) {
		if (status != NULL)
			*status = CLIS_COMMS;
		if (resp != NULL)
			*resp = strdup("CLI communication error");
		return (CLIS_COMMS);
	}

	(void)cli_readres(cli_i, &u, resp, params->cli_timeout);
	if (status != NULL)
		*status = u;
	return (u == CLIS_OK ? 0 : u);
}

/*--------------------------------------------------------------------*/

void
mgt_cli_start_child(int fdi, int fdo)
{

	cli_i = fdi;
	cli_o = fdo;
}

/*--------------------------------------------------------------------*/

void
mgt_cli_stop_child(void)
{

	cli_i = -1;
	cli_o = -1;
	/* XXX: kick any users */
}

/*--------------------------------------------------------------------
 * Validate the authentication
 */

static void
mcf_auth(struct cli *cli, const char *const *av, void *priv)
{
	char buf[1025];
	int i, fd;
	struct SHA256Context sha256ctx;
	unsigned char digest[SHA256_LEN];
	struct cli_port *cp;

	AN(av[2]);
	CAST_OBJ_NOTNULL(cp, cli->priv, CLI_PORT_MAGIC);
	(void)priv;
	AN(secret_file);
	fd = open(secret_file, O_RDONLY);
	if (fd < 0) {
		cli_out(cli, "Cannot open secret file (%s)\n",
		    strerror(errno));
		cli_result(cli, CLIS_CANT);
		return;
	}
	i = read(fd, buf, sizeof buf);
	if (i == 0) {
		cli_out(cli, "Empty secret file");
		cli_result(cli, CLIS_CANT);
		return;
	}
	if (i < 0) {
		cli_out(cli, "Read error on secret file (%s)\n",
		    strerror(errno));
		cli_result(cli, CLIS_CANT);
		return;
	}
	if (i == sizeof buf) {
		cli_out(cli, "Secret file too long (> %d)\n",
		    sizeof buf - 1);
		cli_result(cli, CLIS_CANT);
		return;
	}
	buf[i] = '\0';
	AZ(close(fd));
	SHA256_Init(&sha256ctx);
	SHA256_Update(&sha256ctx, cp->challenge, strlen(cp->challenge));
	SHA256_Update(&sha256ctx, buf, i);
	SHA256_Update(&sha256ctx, cp->challenge, strlen(cp->challenge));
	SHA256_Final(digest, &sha256ctx);
	for (i = 0; i < SHA256_LEN; i++)
		sprintf(buf + i + i, "%02x", digest[i]);
	if (strcasecmp(buf, av[2])) {
		cli_result(cli, CLIS_UNKNOWN);
		return;
	}
	cp->challenge[0] = '\0';
	cli_result(cli, CLIS_OK);
	if (params->cli_banner) 
		mcf_banner(cli, av, priv);
}

static struct cli_proto cli_auth[] = {
	{ CLI_HELP,		mcf_help, cli_auth },
	{ CLI_AUTH,		mcf_auth, NULL },
	{ CLI_QUIT,		mcf_close, NULL},
	{ NULL }
};

/*--------------------------------------------------------------------
 * Generate a random challenge
 */

static void
mgt_cli_challenge(struct cli_port *cp)
{
	int i;

	for (i = 0; i + 2 < sizeof cp->challenge; i++)
		cp->challenge[i] = (random() % 26) + 'a';
	cp->challenge[i++] = '\n';
	cp->challenge[i] = '\0';
	cli_out(cp->cli, "%s", cp->challenge);
	cli_out(cp->cli, "\nAuthentication required.\n");
	cli_result(cp->cli, CLIS_AUTH);
}

/*--------------------------------------------------------------------*/

static int
mgt_cli_vlu(void *priv, const char *p)
{
	struct cli_port *cp;
	char *q;
	unsigned u;
	int i;

	CAST_OBJ_NOTNULL(cp, priv, CLI_PORT_MAGIC);
	vsb_clear(cp->cli->sb);

	/* Skip whitespace */
	for (; isspace(*p); p++)
		continue;

	/* Ignore empty lines */
	if (*p == '\0')
		return (0);

	if (secret_file != NULL && cp->challenge[0] != '\0') {
		/* Authentication not yet passed */
		cli_dispatch(cp->cli, cli_auth, p);
		if (cp->cli->result == CLIS_UNKNOWN) 
			mgt_cli_challenge(cp);
	} else {
		cli_dispatch(cp->cli, cli_proto, p);
		if (cp->cli->result == CLIS_UNKNOWN) 
			cli_dispatch(cp->cli, cli_debug, p);
		if (cp->cli->result == CLIS_UNKNOWN) {
			/*
			 * Command not recognized in master, try cacher if it is
			 * running.
			 */
			vsb_clear(cp->cli->sb);
			cp->cli->result = CLIS_OK;
			if (cli_o <= 0) {
				cli_result(cp->cli, CLIS_UNKNOWN);
				cli_out(cp->cli,
				    "Unknown request in manager process "
				    "(child not running).\n"
				    "Type 'help' for more info.");
			} else {
				i = write(cli_o, p, strlen(p));
				xxxassert(i == strlen(p));
				i = write(cli_o, "\n", 1);
				xxxassert(i == 1);
				(void)cli_readres(cli_i,
				    &u, &q, params->cli_timeout);
				cli_result(cp->cli, u);
				cli_out(cp->cli, "%s", q);
				free(q);
			}
		}
	}
	vsb_finish(cp->cli->sb);
	AZ(vsb_overflowed(cp->cli->sb));

	/* send the result back */
	syslog(LOG_INFO, "CLI %d result %d \"%s\"",
	    cp->fdi, cp->cli->result, p);
	if (cli_writeres(cp->fdo, cp->cli) || cp->cli->result == CLIS_CLOSE)
		return (1);
	return (0);
}

/*--------------------------------------------------------------------
 * Get rid of a CLI session.
 *
 * Always and only called from mgt_cli_callback().
 *
 * We must get rid of everything but the event, which gets GC'ed by
 * ev_schdule_one() when mgt_cli_callback, through our return value
 * returns non-zero.
 */

static int
mgt_cli_close(struct cli_port *cp)
{

	CHECK_OBJ_NOTNULL(cp, CLI_PORT_MAGIC);
	syslog(LOG_NOTICE, "CLI %d closed", cp->fdi);
	free(cp->name);
	vsb_delete(cp->cli->sb);
	VLU_Destroy(cp->vlu);

	(void)close(cp->fdi);
	if (cp->fdo != cp->fdi)
		(void)close(cp->fdo);

	/* Special case for stdin/out/err */
	if (cp->fdi == 0) {
		assert(open("/dev/null", O_RDONLY) == 0);
		assert(open("/dev/null", O_WRONLY) == 1);
		(void)close(2);
		assert(open("/dev/null", O_WRONLY) == 2);

		if (d_flag == 2) {
			mgt_stop_child();
			telnet_close_all();
		}
	} else {
		telnet_close_one(cp->fdi);
	}

	free(cp);
	return (1);
}

/*--------------------------------------------------------------------
 * Callback whenever something happens to the input fd of the session.
 */

static int
mgt_cli_callback(const struct vev *e, int what)
{
	struct cli_port *cp;

	CAST_OBJ_NOTNULL(cp, e->priv, CLI_PORT_MAGIC);

	if (what & (EV_ERR | EV_HUP | EV_GONE))
		return (mgt_cli_close(cp));

	if (VLU_Fd(cp->fdi, cp->vlu))
		return (mgt_cli_close(cp));
	return (0);
}

/*--------------------------------------------------------------------*/

void
mgt_cli_setup(int fdi, int fdo, int verbose, const char *ident)
{
	struct cli_port *cp;

	cp = calloc(sizeof *cp, 1);
	XXXAN(cp);
	cp->vlu = VLU_New(cp, mgt_cli_vlu, params->cli_buffer);

	cp->name = strdup(ident);
	XXXAN(cp->name);
	syslog(LOG_NOTICE, "CLI %d open from %s", fdi, cp->name);
	cp->magic = CLI_PORT_MAGIC;

	cp->fdi = fdi;
	cp->fdo = fdo;
	cp->verbose = verbose;

	cp->cli->sb = vsb_newauto();
	XXXAN(cp->cli->sb);
	cp->cli->priv = cp;

	/* Deal with TELNET options */
	if (cp->fdi != 0)
		VLU_SetTelnet(cp->vlu, cp->fdo);

	/*
	 * If we have a secret file authenticate all CLI connections
	 * except the stdin/stdout debug port.
	 */
	if (cp->fdi != 0 && secret_file != NULL) {
		mgt_cli_challenge(cp);
		(void)VLU_Data("auth -\n", -1, cp->vlu);
	} else if (params->cli_banner)
		(void)VLU_Data("banner\n", -1, cp->vlu);

	cp->ev = calloc(sizeof *cp->ev, 1);
	cp->ev->name = cp->name;
	cp->ev->fd = fdi;
	cp->ev->fd_flags = EV_RD;
	cp->ev->callback = mgt_cli_callback;
	cp->ev->priv = cp;
	AZ(vev_add(mgt_evb, cp->ev));
}

/*--------------------------------------------------------------------*/

static void
telnet_close_one(int fd)
{
	struct telnet *tn, *tn2;

	VTAILQ_FOREACH_SAFE(tn, &telnets, list, tn2) {
		if (tn->fd != fd)
			continue;
		VTAILQ_REMOVE(&telnets, tn, list);
		(void)close(tn->fd);
		free(tn);
		break;
	}
}

static void
telnet_close_all()
{
	struct telnet *tn, *tn2;

	VTAILQ_FOREACH_SAFE(tn, &telnets, list, tn2) {
		VTAILQ_REMOVE(&telnets, tn, list);
		AZ(close(tn->fd));
		free(tn);
	}
}

static struct telnet *
telnet_new(int fd)
{
	struct telnet *tn;

	tn = calloc(sizeof *tn, 1);
	AN(tn);
	tn->fd = fd;
	VTAILQ_INSERT_TAIL(&telnets, tn, list);
	return (tn);
}

static int
telnet_accept(const struct vev *ev, int what)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
	int i;
	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];
	char *p;

	(void)what;
	addrlen = sizeof addr;
	i = accept(ev->fd, (void *)&addr, &addrlen);
	if (i < 0 && errno == EBADF)
		return (1);
	if (i < 0)
		return (0);

	TCP_myname(ev->fd, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	TCP_name((void*)&addr, addrlen, abuf2, sizeof abuf2,
	    pbuf2, sizeof pbuf2);
	asprintf(&p, "telnet %s:%s %s:%s", abuf2, pbuf2, abuf1, pbuf1);
	XXXAN(p);

	(void)telnet_new(i);

	mgt_cli_setup(i, i, 0, p);
	free(p);
	return (0);
}

void
mgt_cli_secret(const char *S_arg)
{
	int i, fd;
	char buf[BUFSIZ];


	srandomdev();
	fd = open(S_arg, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can not open secret-file \"%s\"\n", S_arg);
		exit (2);
	}
	i = read(fd, buf, sizeof buf);
	if (i == 0) {
		fprintf(stderr, "Empty secret-file \"%s\"\n", S_arg);
		exit (2);
	}
	if (i < 0) {
		fprintf(stderr, "Can not read secret-file \"%s\"\n", S_arg);
		exit (2);
	}
	secret_file = S_arg;
}

void
mgt_cli_telnet(const char *T_arg)
{
	struct vss_addr **ta;
	char *addr, *port;
	int i, n, sock, good;
	struct telnet *tn;

	XXXAZ(VSS_parse(T_arg, &addr, &port));
	n = VSS_resolve(addr, port, &ta);
	if (n == 0) {
		fprintf(stderr, "Could not open management port\n");
		exit(2);
	}
	good = 0;
	for (i = 0; i < n; ++i) {
		sock = VSS_listen(ta[i], 10);
		if (sock < 0)
			continue;
		good++;
		tn = telnet_new(sock);
		tn->ev = vev_new();
		XXXAN(tn->ev);
		tn->ev->fd = sock;
		tn->ev->fd_flags = POLLIN;
		tn->ev->callback = telnet_accept;
		AZ(vev_add(mgt_evb, tn->ev));
		free(ta[i]);
		ta[i] = NULL;
	}
	free(ta);
	if (good == 0) {
		REPORT(LOG_ERR, "-T %s:%s could not be listened on.",
		    addr, port);
		exit(2);
	}
	free(addr);
	free(port);
}
