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
	unsigned		magic;
#define TELNET_MAGIC		0x53ec3ac0
	int			fd;
	struct vev		*ev;
};

struct cli_port {
	unsigned		magic;
#define CLI_PORT_MAGIC		0x5791079f
	VTAILQ_ENTRY(cli_port)	list;
	struct vev		*ev;
	int			fdi;
	int			fdo;
	int			verbose;
	struct vlu		*vlu;
	struct cli		cli[1];
	char			*name;
	char			challenge[34];
	mgt_cli_close_f		*closefunc;
	void			*priv;
};

static VTAILQ_HEAD(,cli_port)	clilist = VTAILQ_HEAD_INITIALIZER(clilist);

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

	for (i = 0; i + 2L < sizeof cp->challenge; i++)
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
	/*
	 * Remove from list, so that if closefunc calls mgt_cli_close_all
	 * it will not try to remove this one too.
	 */
	VTAILQ_REMOVE(&clilist, cp, list);

	free(cp->name);
	vsb_delete(cp->cli->sb);
	VLU_Destroy(cp->vlu);

	cp->closefunc(cp->priv);
	FREE_OBJ(cp);
	return (1);
}

/*--------------------------------------------------------------------
 * Get rid of all CLI sessions
 */

void
mgt_cli_close_all(void)
{
	struct cli_port *cp;

	while (!VTAILQ_EMPTY(&clilist)) {
		cp = VTAILQ_FIRST(&clilist);
		vev_del(mgt_evb, cp->ev);
		(void)mgt_cli_close(cp);
	}
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
mgt_cli_setup(int fdi, int fdo, int verbose, const char *ident, mgt_cli_close_f *closefunc, void *priv)
{
	struct cli_port *cp;

	cp = calloc(sizeof *cp, 1);
	XXXAN(cp);
	cp->vlu = VLU_New(cp, mgt_cli_vlu, params->cli_buffer);

	cp->name = strdup(ident);
	XXXAN(cp->name);
	syslog(LOG_NOTICE, "CLI %d open %s", fdi, cp->name);
	cp->magic = CLI_PORT_MAGIC;

	cp->fdi = fdi;
	cp->fdo = fdo;
	cp->verbose = verbose;

	cp->closefunc = closefunc;
	cp->priv = priv;

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

	cp->ev = vev_new();
	cp->ev->name = cp->name;
	cp->ev->fd = fdi;
	cp->ev->fd_flags = EV_RD;
	cp->ev->callback = mgt_cli_callback;
	cp->ev->priv = cp;
	VTAILQ_INSERT_TAIL(&clilist, cp, list);
	AZ(vev_add(mgt_evb, cp->ev));
}

/*--------------------------------------------------------------------*/

static struct vsb *
sock_id(const char *pfx, int fd)
{
	struct vsb *vsb;

	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];

	vsb = vsb_newauto();
	AN(vsb);
	TCP_myname(fd, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	TCP_hisname(fd, abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	vsb_printf(vsb, "%s %s:%s %s:%s", pfx, abuf2, pbuf2, abuf1, pbuf1);
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));
	return (vsb);
}

/*--------------------------------------------------------------------*/

static void
telnet_close(void *priv)
{
	struct telnet *tn;

	CAST_OBJ_NOTNULL(tn, priv, TELNET_MAGIC);
	(void)close(tn->fd);
	FREE_OBJ(tn);
}

static struct telnet *
telnet_new(int fd)
{
	struct telnet *tn;

	ALLOC_OBJ(tn, TELNET_MAGIC);
	AN(tn);
	tn->fd = fd;
	return (tn);
}

static int
telnet_accept(const struct vev *ev, int what)
{
	struct vsb *vsb;
	struct sockaddr_storage addr;
	socklen_t addrlen;
	struct telnet *tn;
	int i;

	(void)what;
	addrlen = sizeof addr;
	i = accept(ev->fd, (void *)&addr, &addrlen);
	if (i < 0 && errno == EBADF)
		return (1);
	if (i < 0)
		return (0);

	tn = telnet_new(i);
	vsb = sock_id("telnet", ev->fd);
	mgt_cli_setup(i, i, 0, vsb_data(vsb), telnet_close, tn);
	vsb_delete(vsb);
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

/* Reverse CLI ("Master") connections --------------------------------*/

static int M_fd = -1;
static struct vev *M_poker, *M_conn;
static char *M_addr, *M_port;
static struct vss_addr **M_ta;
static int M_nta, M_nxt;
static double M_poll = 0.1;

static void
Marg_closer(void *priv)
{

	(void)priv;
	(void)close(M_fd);
	M_fd = -1;
}

static int
Marg_poker(const struct vev *e, int what)
{
	struct vsb *vsb;
	int s, k;
	socklen_t l;

	(void)what;	/* XXX: ??? */

	if (e == M_conn) {
		/* Our connect(2) returned, check result */
		l = sizeof k;
		AZ(getsockopt(M_fd, SOL_SOCKET, SO_ERROR, &k, &l));
		if (k) {
			errno = k;
			syslog(LOG_INFO, "Could not connect to CLI-master: %m");
			(void)close(M_fd);
			M_fd = -1;
			/* Try next address */
			if (++M_nxt >= M_nta) {
				M_nxt = 0;
				if (M_poll < 10)
					M_poll *= 2;
			}
			return (1);
		}
		vsb = sock_id("master", M_fd);
		mgt_cli_setup(M_fd, M_fd, 0, vsb_data(vsb), Marg_closer, NULL);
		vsb_delete(vsb);
		M_poll = 1;
		return (1);
	}

	assert(e == M_poker);

	M_poker->timeout = M_poll;	/* XXX nasty ? */
	if (M_fd >= 0)
		return (0);

	/* Try to connect asynchronously */
	s = VSS_connect(M_ta[M_nxt], 1);
	if (s < 0)
		return (0);

	M_conn = vev_new();
	AN(M_conn);
	M_conn->callback = Marg_poker;
	M_conn->name = "-M connector";	
	M_conn->fd_flags = EV_WR;
	M_conn->fd = s;
	M_fd = s;
	AZ(vev_add(mgt_evb, M_conn));
	return (0);
}

void
mgt_cli_master(const char *M_arg)
{
	(void)M_arg;

	if (VSS_parse(M_arg, &M_addr, &M_port) || M_port == NULL) {
		fprintf(stderr, "Could not parse -M argument\n");
		exit (1);
	}
	M_nta = VSS_resolve(M_addr, M_port, &M_ta);
	if (M_nta <= 0) {
		fprintf(stderr, "Could resolve -M argument to address\n");
		exit (1);
	}
	M_nxt = 0;
	AZ(M_poker);
	M_poker = vev_new();
	AN(M_poker);
	M_poker->timeout = M_poll;
	M_poker->callback = Marg_poker;
	M_poker->name = "-M poker";	
	AZ(vev_add(mgt_evb, M_poker));
}
