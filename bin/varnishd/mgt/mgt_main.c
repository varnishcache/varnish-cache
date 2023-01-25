/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2017 Varnish Software AS
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
 * The management process and CLI handling
 */

#include "config.h"

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "hash/hash_slinger.h"
#include "libvcc.h"
#include "vcli_serve.h"
#include "vct.h"
#include "vev.h"
#include "vfil.h"
#include "vin.h"
#include "vpf.h"
#include "vrnd.h"
#include "vsha256.h"
#include "vsub.h"
#include "vtim.h"
#include "waiter/mgt_waiter.h"
#include "vsa.h"
#include "vus.h"

struct heritage		heritage;
unsigned		d_flag = 0;
struct vev_root		*mgt_evb;
int			exit_status = 0;
struct vsb		*vident;
struct VSC_mgt		*VSC_C_mgt;
static int		I_fd = -1;
static char		*workdir;

static struct vfil_path *vcl_path = NULL;

static const char opt_spec[] = "?a:b:CdE:f:Fh:i:I:j:l:M:n:P:p:r:S:s:T:t:VW:x:";

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	char *p;

#define FMT_NONL "  %-28s # %s"
#define FMT FMT_NONL "\n"

	printf( "Usage: varnishd [options]\n");

	printf("\nBasic options:\n");

	printf(FMT, "-a [<name>=]address[:port][,proto]",
	    "HTTP listen address and port");
	printf(FMT, "   [,user=<u>][,group=<g>]",
	    "Can be specified multiple times.");
	printf(FMT, "   [,mode=<m>]", "  default: \":80,HTTP\"");
	printf(FMT, "", "Proto can be \"PROXY\" or \"HTTP\" (default)");
	printf(FMT, "", "user, group and mode set permissions for");
	printf(FMT, "", "  a Unix domain socket.");
	printf(FMT, "-b none", "No backend");
	printf(FMT, "-b [addr[:port]|path]", "Backend address and port");
	printf(FMT, "", "  or socket file path");
	printf(FMT, "", "  default: \":80\"");
	printf(FMT, "-f vclfile", "VCL program");
	printf(FMT, "", "Can be specified multiple times.");
	printf(FMT, "-n dir", "Working directory");

	p = VIN_n_Arg(NULL);
	AN(p);
	printf(FMT_NONL "  default: %s\n", "", "", p);
	free(p);

	printf("\n-b can be used only once, and not together with -f\n");

	printf("\nDocumentation options:\n");
	printf(FMT, "-?", "Prints this usage message");
	printf(FMT, "-x parameter", "Parameter documentation");
	printf(FMT, "-x vsl", "VSL record documentation");
	printf(FMT, "-x cli", "CLI command documentation");
	printf(FMT, "-x builtin", "Builtin VCL program");
	printf(FMT, "-x optstring", "List of getopt options");

	printf("\nOperations options:\n");

	printf(FMT, "-F", "Run in foreground");
	printf(FMT, "-T address[:port]", "CLI address");
	printf(FMT, "", "Can be specified multiple times.");
	printf(FMT, "-M address:port", "Reverse CLI destination");
	printf(FMT, "", "Can be specified multiple times.");
	printf(FMT, "-P file", "PID file");
	printf(FMT, "-i identity", "Identity of varnish instance");
	printf(FMT, "-I clifile", "Initialization CLI commands");
	printf(FMT, "-E extension", "Load extension");

	printf("\nTuning options:\n");

	printf(FMT, "-t TTL", "Default TTL");
	printf(FMT, "-p param=value", "set parameter");
	printf(FMT, "", "Can be specified multiple times.");

	printf(FMT, "-s [name=]kind[,options]", "Storage specification");
	printf(FMT, "", "Can be specified multiple times.");
#ifdef HAVE_UMEM_H
	printf(FMT, "", "  -s default (=umem)");
	printf(FMT, "", "  -s umem");
#else
	printf(FMT, "", "  -s default (=malloc)");
#endif
	printf(FMT, "", "  -s malloc");
	printf(FMT, "", "  -s file");

	printf(FMT, "-l vsl", "Size of shared memory log");
	printf(FMT, "", "  vsl: space for VSL records [80m]");

	printf("\nSecurity options:\n");

	printf(FMT, "-r param[,param...]", "Set parameters read-only from CLI");
	printf(FMT, "", "Can be specified multiple times.");
	printf(FMT, "-S secret-file", "Secret file for CLI authentication");
	printf(FMT, "-j jail[,options]", "Jail specification");
#ifdef HAVE_SETPPRIV
	printf(FMT, "", "  -j solaris");
#endif
	printf(FMT, "", "  -j unix");
	printf(FMT, "", "  -j none");

	printf("\nAdvanced/Dev/Debug options:\n");

	printf(FMT, "-d", "debug mode");
	printf(FMT, "", "Stay in foreground, CLI on stdin.");
	printf(FMT, "-C", "Output VCL code compiled to C language");
	printf(FMT, "-V", "version");
	printf(FMT, "-h kind[,options]", "Hash specification");
	printf(FMT, "-W waiter", "Waiter implementation");
}

/*--------------------------------------------------------------------*/

struct arg_list {
	unsigned		magic;
#define ARG_LIST_MAGIC		0x7b0d1bc4
	char			arg[2];
	char			*val;
	VTAILQ_ENTRY(arg_list)	list;
	void			*priv;
};

static VTAILQ_HEAD(, arg_list) arglist = VTAILQ_HEAD_INITIALIZER(arglist);

static struct arg_list *
arg_list_add(const char arg, const char *val)
{
	struct arg_list *alp;

	ALLOC_OBJ(alp, ARG_LIST_MAGIC);
	AN(alp);
	alp->arg[0] = arg;
	alp->arg[1] = '\0';
	REPLACE(alp->val, val);
	VTAILQ_INSERT_TAIL(&arglist, alp, list);
	return (alp);
}

static unsigned
arg_list_count(const char *arg)
{
	unsigned retval = 0;
	struct arg_list *alp;

	VTAILQ_FOREACH(alp, &arglist, list) {
		if (!strcmp(alp->arg, arg))
			retval++;
	}
	return (retval);
}

/*--------------------------------------------------------------------*/

static void
cli_check(const struct cli *cli)
{
	if (cli->result == CLIS_OK || cli->result == CLIS_TRUNCATED) {
		AZ(VSB_finish(cli->sb));
		if (VSB_len(cli->sb) > 0)
			fprintf(stderr, "Warnings:\n%s\n", VSB_data(cli->sb));
		VSB_clear(cli->sb);
		return;
	}
	AZ(VSB_finish(cli->sb));
	fprintf(stderr, "Error:\n%s\n", VSB_data(cli->sb));
	exit(2);
}

/*--------------------------------------------------------------------
 * This function is called when the CLI on stdin is closed.
 */

static int v_matchproto_(mgt_cli_close_f)
mgt_stdin_close(void *priv)
{

	(void)priv;
	return (-42);
}

/*--------------------------------------------------------------------
 * Autogenerate a -S file using strong random bits from the kernel.
 */

static void
mgt_secret_atexit(void)
{

	/* Only master process */
	if (getpid() != heritage.mgt_pid)
		return;
	VJ_master(JAIL_MASTER_FILE);
	(void)unlink("_.secret");
	VJ_master(JAIL_MASTER_LOW);
}

static const char *
make_secret(const char *dirname)
{
	char *fn;
	int fdo;
	int i;
	unsigned char b;

	assert(asprintf(&fn, "%s/_.secret", dirname) > 0);

	VJ_master(JAIL_MASTER_FILE);
	fdo = open(fn, O_RDWR|O_CREAT|O_TRUNC, 0640);
	if (fdo < 0)
		ARGV_ERR("Cannot create secret-file in %s (%s)\n",
		    dirname, VAS_errtxt(errno));

	for (i = 0; i < 256; i++) {
		AZ(VRND_RandomCrypto(&b, 1));
		assert(1 == write(fdo, &b, 1));
	}
	closefd(&fdo);
	VJ_master(JAIL_MASTER_LOW);
	AZ(atexit(mgt_secret_atexit));
	return (fn);
}

static void
mgt_Cflag_atexit(void)
{

	/* Only master process */
	if (getpid() != heritage.mgt_pid)
		return;
	if (arg_list_count("E")) {
		vext_cleanup(1);
		VJ_rmdir("vext_cache");
	}
	VJ_rmdir("vmod_cache");
	(void)chdir("/");
	VJ_rmdir(workdir);
}

/*--------------------------------------------------------------------*/

static void
mgt_tests(void)
{
	assert(VTIM_parse("Sun, 06 Nov 1994 08:49:37 GMT") == 784111777);
	assert(VTIM_parse("Sunday, 06-Nov-94 08:49:37 GMT") == 784111777);
	assert(VTIM_parse("Sun Nov  6 08:49:37 1994") == 784111777);

	/* Check that our VSHA256 works */
	VSHA256_Test();
}

static void
mgt_initialize(struct cli *cli)
{
	static unsigned clilim = 32768;

	/* for ASSERT_MGT() */
	heritage.mgt_pid = getpid();

	/* Create a cli for convenience in otherwise CLI functions */
	INIT_OBJ(cli, CLI_MAGIC);
	cli[0].sb = VSB_new_auto();
	AN(cli[0].sb);
	cli[0].result = CLIS_OK;
	cli[0].limit = &clilim;

	mgt_cli_init_cls();		// CLI commands can be registered

	TRF_Init();

	MCF_InitParams(cli);

	VCC_VCL_Range(&heritage.min_vcl_version, &heritage.max_vcl_version);

	cli_check(cli);
}

static void
mgt_x_arg(const char *x_arg)
{
	if (!strcmp(x_arg, "parameter"))
		MCF_DumpRstParam();
	else if (!strcmp(x_arg, "vsl"))
		mgt_DumpRstVsl();
	else if (!strcmp(x_arg, "cli"))
		mgt_DumpRstCli();
	else if (!strcmp(x_arg, "builtin"))
		mgt_DumpBuiltin();
	else if (!strcmp(x_arg, "optstring"))
		(void)printf("%s\n", opt_spec);
	else
		ARGV_ERR("Invalid -x argument\n");
}

/*--------------------------------------------------------------------*/

#define ERIC_MAGIC 0x2246988a		/* Eric is not random */

static int
mgt_eric(void)
{
	int eric_pipes[2];
	unsigned u;
	ssize_t sz;

	AZ(pipe(eric_pipes));

	switch (fork()) {
	case -1:
		fprintf(stderr, "Fork() failed: %s\n", VAS_errtxt(errno));
		exit(-1);
	case 0:
		closefd(&eric_pipes[0]);
		assert(setsid() > 1);

		VFIL_null_fd(STDIN_FILENO);
		return (eric_pipes[1]);
	default:
		break;
	}
	closefd(&eric_pipes[1]);
	sz = read(eric_pipes[0], &u, sizeof u);
	if (sz == sizeof u && u == ERIC_MAGIC)
		exit(0);
	else if (sz == sizeof u && u != 0)
		exit(u);
	else
		exit(-1);
}

static void
mgt_eric_im_done(int eric_fd, unsigned u)
{

	if (u == 0)
		u = ERIC_MAGIC;

	VFIL_null_fd(STDIN_FILENO);
	VFIL_null_fd(STDOUT_FILENO);
	VFIL_null_fd(STDERR_FILENO);

	assert(write(eric_fd, &u, sizeof u) == sizeof u);
	closefd(&eric_fd);
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(vev_cb_f)
mgt_sigint(const struct vev *e, int what)
{

	(void)e;
	(void)what;
	MGT_Complain(C_ERR, "Manager got %s", e->name);
	(void)fflush(stdout);
	if (MCH_Running())
		MCH_Stop_Child();
	return (-42);
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(vev_cb_f)
mgt_uptime(const struct vev *e, int what)
{
	static double mgt_uptime_t0 = 0;

	(void)e;
	(void)what;
	AN(VSC_C_mgt);
	if (mgt_uptime_t0 == 0)
		mgt_uptime_t0 = VTIM_real();
	VSC_C_mgt->uptime = (uint64_t)(VTIM_real() - mgt_uptime_t0);
	return (0);
}

/*--------------------------------------------------------------------*/

static int v_matchproto_(mgt_cli_close_f)
mgt_I_close(void *priv)
{
	(void)priv;
	fprintf(stderr, "END of -I file processing\n");
	I_fd = -1;
	return (0);
}

/*--------------------------------------------------------------------*/

struct f_arg {
	unsigned		magic;
#define F_ARG_MAGIC		0x840649a8
	char			*farg;
	char			*src;
};

static struct f_arg *
mgt_f_read(const char *fn)
{
	struct f_arg *fa;
	char *f, *fnp;

	ALLOC_OBJ(fa, F_ARG_MAGIC);
	AN(fa);
	REPLACE(fa->farg, fn);
	VFIL_setpath(&vcl_path, mgt_vcl_path);
	if (VFIL_searchpath(vcl_path, NULL, &f, fn, &fnp) || f == NULL) {
		ARGV_ERR("Cannot read -f file '%s' (%s)\n",
		    fnp != NULL ? fnp : fn, VAS_errtxt(errno));
	}
	free(fa->farg);
	fa->farg = fnp;
	fa->src = f;
	return (fa);
}

static void
mgt_b_conv(const char *b_arg)
{
	struct f_arg *fa;
	struct vsb *vsb;

	ALLOC_OBJ(fa, F_ARG_MAGIC);
	AN(fa);
	REPLACE(fa->farg, "<-b argument>");
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_cat(vsb, "vcl 4.1;\n");
	VSB_cat(vsb, "backend default ");
	if (!strcasecmp(b_arg, "none"))
		VSB_cat(vsb, "none;\n");
	else if (VUS_is(b_arg))
		VSB_printf(vsb, "{\n    .path = \"%s\";\n}\n", b_arg);
	else
		VSB_printf(vsb, "{\n    .host = \"%s\";\n}\n", b_arg);
	AZ(VSB_finish(vsb));
	fa->src = strdup(VSB_data(vsb));
	AN(fa->src);
	VSB_destroy(&vsb);
	AZ(arg_list_count("f"));
	arg_list_add('f', "")->priv = fa;
}

static int
mgt_process_f_arg(struct cli *cli, unsigned C_flag, void **fap)
{
	int retval = 0;
	struct f_arg *fa;
	const char *name = NULL;

	TAKE_OBJ_NOTNULL(fa, fap, F_ARG_MAGIC);
	if (arg_list_count("f") == 1)
		name = "boot";
	mgt_vcl_startup(cli, fa->src, name, fa->farg, C_flag);
	if (C_flag) {
		if (cli->result != CLIS_OK &&
		    cli->result != CLIS_TRUNCATED)
			retval = 2;
		AZ(VSB_finish(cli->sb));
		fprintf(stderr, "%s\n", VSB_data(cli->sb));
		VSB_clear(cli->sb);
	} else {
		cli_check(cli);
	}
	free(fa->farg);
	free(fa->src);
	FREE_OBJ(fa);
	return (retval);
}

static const char *
create_bogo_n_arg(void)
{
	struct vsb *vsb;
	char *p;

	vsb = VSB_new_auto();
	AN(vsb);
	if (getenv("TMPDIR") != NULL)
		VSB_printf(vsb, "%s", getenv("TMPDIR"));
	else
		VSB_cat(vsb, "/tmp");
	VSB_cat(vsb, "/varnishd_C_XXXXXXX");
	AZ(VSB_finish(vsb));
	p = strdup(VSB_data(vsb));
	AN(p);
	VSB_destroy(&vsb);
	AN(mkdtemp(p));
	AZ(chmod(p, 0750));
	return (p);
}

static struct vpf_fh *
create_pid_file(pid_t *ppid, const char *fmt, ...)
{
	struct vsb *vsb;
	va_list ap;
	struct vpf_fh *pfh;

	va_start(ap, fmt);
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_vprintf(vsb, fmt, ap);
	AZ(VSB_finish(vsb));
	VJ_master(JAIL_MASTER_FILE);
	pfh = VPF_Open(VSB_data(vsb), 0644, ppid);
	if (pfh == NULL && errno == EEXIST)
		ARGV_ERR(
		    "Varnishd is already running (pid=%jd) (pidfile=%s)\n",
		    (intmax_t)*ppid, VSB_data(vsb));
	if (pfh == NULL)
		ARGV_ERR("Could not open pid-file (%s): %s\n",
		    VSB_data(vsb), VAS_errtxt(errno));
	VJ_master(JAIL_MASTER_LOW);
	VSB_destroy(&vsb);
	va_end(ap);
	return (pfh);
}

int
main(int argc, char * const *argv)
{
	int o, eric_fd = -1;
	unsigned C_flag = 0;
	unsigned F_flag = 0;
	const char *b_arg = NULL;
	const char *i_arg = NULL;
	const char *j_arg = NULL;
	const char *h_arg = "critbit";
	const char *n_arg = NULL;
	const char *S_arg = NULL;
	const char *s_arg = "default,100m";
	const char *W_arg = NULL;
	const char *c;
	char *p;
	struct cli cli[1];
	const char *err;
	unsigned u;
	struct sigaction sac;
	struct vev *e;
	pid_t pid;
	struct arg_list *alp;
	int first_arg = 1;

	if (argc == 2 && !strcmp(argv[1], "--optstring")) {
		printf("%s\n", opt_spec);
		exit(0);
	}

	heritage.argc = argc;
	heritage.argv = argv;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	mgt_tests();

	mgt_initialize(cli);

	for (; (o = getopt(argc, argv, opt_spec)) != -1; first_arg = 0) {
		switch (o) {
		case 'V':
			if (!first_arg)
				ARGV_ERR("-V must be the first argument\n");
			if (argc != 2)
				ARGV_ERR("Too many arguments for -V\n");
			VCS_Message("varnishd");
			exit(0);
		case 'x':
			if (!first_arg)
				ARGV_ERR("-x must be the first argument\n");
			if (argc != 3)
				ARGV_ERR("Too many arguments for -x\n");
			mgt_x_arg(optarg);
			exit(0);
		case 'b':
			b_arg = optarg;
			break;
		case 'C':
			C_flag = 1;
			break;
		case 'd':
			d_flag++;
			break;
		case 'F':
			F_flag = 1;
			break;
		case 'j':
			j_arg = optarg;
			break;
		case 'h':
			h_arg = optarg;
			break;
		case 'i':
			i_arg = optarg;
			break;
		case 'l':
			arg_list_add('p', "vsl_space")->priv = optarg;
			break;
		case 'n':
			n_arg = optarg;
			break;
		case 'S':
			S_arg = optarg;
			break;
		case 't':
			arg_list_add('p', "default_ttl")->priv = optarg;
			break;
		case 'W':
			W_arg = optarg;
			break;
		case 'a':
		case 'E':
		case 'f':
		case 'I':
		case 'p':
		case 'P':
		case 'M':
		case 'r':
		case 's':
		case 'T':
			(void)arg_list_add(o, optarg);
			break;
		default:
			usage();
			exit(2);
		}
	}

	if (argc != optind)
		ARGV_ERR("Too many arguments (%s...)\n", argv[optind]);

	if (b_arg != NULL && arg_list_count("f"))
		ARGV_ERR("Only one of -b or -f can be specified\n");

	if (d_flag && F_flag)
		ARGV_ERR("Only one of -d or -F can be specified\n");

	if (C_flag && b_arg == NULL && !arg_list_count("f"))
		ARGV_ERR("-C needs either -b <backend> or -f <vcl_file>\n");

	if (d_flag && C_flag)
		ARGV_ERR("-d makes no sense with -C\n");

	if (F_flag && C_flag)
		ARGV_ERR("-F makes no sense with -C\n");

	if (!d_flag && b_arg == NULL && !arg_list_count("f"))
		ARGV_ERR("Neither -b nor -f given. (use -f '' to override)\n");

	if (arg_list_count("I") > 1)
		ARGV_ERR("\tOnly one -I allowed\n");

	if (d_flag || F_flag)
		complain_to_stderr = 1;

	if (!arg_list_count("T"))
		(void)arg_list_add('T', "localhost:0");

	/*
	 * Start out by closing all unwanted file descriptors we might
	 * have inherited from sloppy process control daemons.
	 */
	VSUB_closefrom(STDERR_FILENO + 1);
	MCH_TrackHighFd(STDERR_FILENO);

	/*
	 * Have Eric Daemonize us if need be
	 */
	if (!C_flag && !d_flag && !F_flag) {
		eric_fd = mgt_eric();
		MCH_TrackHighFd(eric_fd);
		heritage.mgt_pid = getpid();
	}

	VRND_SeedAll();

	vident = mgt_BuildVident();

	/* Various initializations */
	VTAILQ_INIT(&heritage.socks);
	mgt_evb = VEV_New();
	AN(mgt_evb);

	/* Initialize transport protocols */
	XPORT_Init();

	VJ_Init(j_arg);

	/* Initialize the bogo-IP VSA */
	VSA_Init();

	if (b_arg != NULL)
		mgt_b_conv(b_arg);

	/* Process delayed arguments */
	VTAILQ_FOREACH(alp, &arglist, list) {
		switch(alp->arg[0]) {
		case 'a':
			MAC_Arg(alp->val);
			break;
		case 'f':
			if (*alp->val != '\0')
				alp->priv = mgt_f_read(alp->val);
			break;
		case 'E':
			vext_argument(alp->val);
			break;
		case 'I':
			VJ_master(JAIL_MASTER_FILE);
			I_fd = open(alp->val, O_RDONLY);
			if (I_fd < 0)
				ARGV_ERR("\tCan't open %s: %s\n",
				    alp->val, VAS_errtxt(errno));
			VJ_master(JAIL_MASTER_LOW);
			break;
		case 'p':
			if (alp->priv) {
				MCF_ParamSet(cli, alp->val, alp->priv);
			} else {
				p = strchr(alp->val, '=');
				if (p == NULL)
					ARGV_ERR("\t-p lacks '='\n");
				AN(p);
				*p++ = '\0';
				MCF_ParamSet(cli, alp->val, p);
			}
			break;
		case 'r':
			MCF_ParamProtect(cli, alp->val);
			break;
		case 's':
			STV_Config(alp->val);
			break;
		default:
			break;
		}
		cli_check(cli);
	}

	VCLS_SetLimit(mgt_cls, &mgt_param.cli_limit);

	assert(d_flag == 0 || F_flag == 0);

	if (C_flag && n_arg == NULL)
		n_arg = create_bogo_n_arg();

	if (S_arg != NULL && !strcmp(S_arg, "none")) {
		fprintf(stderr,
		    "Warning: CLI authentication disabled.\n");
	} else if (S_arg != NULL) {
		VJ_master(JAIL_MASTER_FILE);
		o = open(S_arg, O_RDONLY, 0);
		if (o < 0)
			ARGV_ERR("Cannot open -S file (%s): %s\n",
			    S_arg, VAS_errtxt(errno));
		closefd(&o);
		VJ_master(JAIL_MASTER_LOW);
	}

	workdir = VIN_n_Arg(n_arg);
	AN(workdir);

	if (i_arg == NULL || *i_arg == '\0')
		i_arg = mgt_HostName();
	else for (c = i_arg; *c != '\0'; c++) {
		if (!vct_istchar(*c))
			ARGV_ERR("Invalid character '%c' for -i\n", *c);
	}
	heritage.identity = i_arg;

	mgt_ProcTitle("Mgt");

	openlog("varnishd", LOG_PID, LOG_LOCAL0);

	if (VJ_make_workdir(workdir))
		ARGV_ERR("Cannot create working directory (%s): %s\n",
		    workdir, VAS_errtxt(errno));

	VJ_master(JAIL_MASTER_SYSTEM);
	AZ(system("rm -rf vmod_cache"));
	AZ(system("rm -rf vext_cache"));
	VJ_master(JAIL_MASTER_LOW);

	if (VJ_make_subdir("vmod_cache", "VMOD cache", NULL)) {
		ARGV_ERR(
		    "Cannot create vmod directory (%s/vmod_cache): %s\n",
		    workdir, VAS_errtxt(errno));
	}

	if (arg_list_count("E") &&
	    VJ_make_subdir("vext_cache", "VMOD cache", NULL)) {
		ARGV_ERR(
		    "Cannot create vmod directory (%s/vext_cache): %s\n",
		    workdir, VAS_errtxt(errno));
	}

	if (C_flag)
		AZ(atexit(mgt_Cflag_atexit));

	/* If no -s argument specified, process default -s argument */
	if (!arg_list_count("s"))
		STV_Config(s_arg);

	/* Configure CLI and Transient storage, if user did not */
	STV_Config_Final();

	mgt_vcl_init();

	vext_copyin(vident);

	u = 0;
	VTAILQ_FOREACH(alp, &arglist, list) {
		if (!strcmp(alp->arg, "f") && alp->priv != NULL)
			u |= mgt_process_f_arg(cli, C_flag, &alp->priv);
	}
	if (C_flag)
		exit(u);

	VTAILQ_FOREACH(alp, &arglist, list) {
		if (!strcmp(alp->arg, "P"))
			alp->priv = create_pid_file(&pid, "%s", alp->val);
	}

	/* Implict -P argument */
	alp = arg_list_add('P', NULL);
	alp->priv = create_pid_file(&pid, "%s/_.pid", workdir);

	if (VTAILQ_EMPTY(&heritage.socks))
		MAC_Arg(":80\0");	// XXX: extra NUL for FlexeLint

	assert(!VTAILQ_EMPTY(&heritage.socks));

	HSH_config(h_arg);

	Wait_config(W_arg);

	mgt_SHM_Init();

	mgt_SHM_static_alloc(i_arg, strlen(i_arg) + 1L, "Arg", "-i");
	VSC_C_mgt = VSC_mgt_New(NULL, NULL, "");

	VTAILQ_FOREACH(alp, &arglist, list) {
		if (!strcmp(alp->arg, "M"))
			mgt_cli_master(alp->val);
		else if (!strcmp(alp->arg, "T") && strcmp(alp->val, "none"))
			mgt_cli_telnet(alp->val);
		else if (!strcmp(alp->arg, "P"))
			VPF_Write(alp->priv);
	}

	AZ(VSB_finish(vident));

	if (S_arg == NULL)
		S_arg = make_secret(workdir);
	AN(S_arg);

	MGT_Complain(C_DEBUG, "Version: %s", VCS_String("V"));
	MGT_Complain(C_DEBUG, "Platform: %s", VSB_data(vident) + 1);

	if (d_flag)
		mgt_cli_setup(0, 1, 1, "debug", mgt_stdin_close, NULL);

	if (strcmp(S_arg, "none"))
		mgt_cli_secret(S_arg);

	memset(&sac, 0, sizeof sac);
	sac.sa_handler = SIG_IGN;
	sac.sa_flags = SA_RESTART;

	AZ(sigaction(SIGPIPE, &sac, NULL));
	AZ(sigaction(SIGHUP, &sac, NULL));

	MCH_Init();

	if (I_fd >= 0) {
		fprintf(stderr, "BEGIN of -I file processing\n");
		/* We must dup stderr, because VCLS closes the output fd */
		mgt_cli_setup(I_fd, dup(2), 1, "-I file", mgt_I_close, stderr);
		while (I_fd >= 0) {
			o = VEV_Once(mgt_evb);
			if (o != 1)
				MGT_Complain(C_ERR,
				    "VEV_Once() = %d", o);
		}
	}
	assert(I_fd == -1);

	err = mgt_has_vcl();
	if (!d_flag && err != NULL && !arg_list_count("f"))
		MGT_Complain(C_ERR, "%s", err);

	if (err == NULL && !d_flag)
		u = MCH_Start_Child();
	else
		u = 0;

	if (eric_fd >= 0)
		mgt_eric_im_done(eric_fd, u);

	if (u)
		exit(u);

	/* Failure is no longer an option */

	if (F_flag)
		VFIL_null_fd(STDIN_FILENO);

	e = VEV_Alloc();
	AN(e);
	e->callback = mgt_uptime;
	e->timeout = 1.0;
	e->name = "mgt_uptime";
	AZ(VEV_Start(mgt_evb, e));

	e = VEV_Alloc();
	AN(e);
	e->sig = SIGTERM;
	e->callback = mgt_sigint;
	e->name = "SIGTERM";
	AZ(VEV_Start(mgt_evb, e));

	e = VEV_Alloc();
	AN(e);
	e->sig = SIGINT;
	e->callback = mgt_sigint;
	e->name = "SIGINT";
	AZ(VEV_Start(mgt_evb, e));

	o = VEV_Loop(mgt_evb);
	if (o != 0 && o != -42)
		MGT_Complain(C_ERR, "VEV_Loop() = %d", o);

	MGT_Complain(C_INFO, "manager stopping child");
	MCH_Stop_Child();
	MGT_Complain(C_INFO, "manager dies");
	mgt_cli_close_all();
	VEV_Destroy(&mgt_evb);
	VJ_master(JAIL_MASTER_SYSTEM);
	/*lint -e(730)*/
	vext_cleanup(! MGT_DO_DEBUG(DBG_VMOD_SO_KEEP));
	(void)rmdir("vext_cache");
	VJ_master(JAIL_MASTER_LOW);
	VTAILQ_FOREACH(alp, &arglist, list) {
		if (!strcmp(alp->arg, "P"))
			VPF_Remove(alp->priv);
	}
	exit(exit_status);
}
