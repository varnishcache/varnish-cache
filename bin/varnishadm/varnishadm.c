/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Cecilie Fritzvold <cecilihf@linpro.no>
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>

#if defined(HAVE_EDIT_READLINE_READLINE_H)
#  include <edit/readline/readline.h>
#elif defined(HAVE_LIBEDIT)
#  include <editline/readline.h>
#elif defined (HAVE_READLINE_READLINE_H)
#  include <readline/readline.h>
#  ifdef HAVE_READLINE_HISTORY_H
#    include <readline/history.h>
#  else
#    error missing history.h - this should have got caught in configure
#  endif
#else
#  error missing readline.h - this should have got caught in configure
#endif

#include <math.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"

#include "vqueue.h"

#include "vapi/vsig.h"
#include "vapi/vsm.h"
#include "vas.h"
#include "vcli_serve.h"
#include "vsb.h"
#include "vjsn.h"
#include "vtcp.h"

#define RL_EXIT(status) \
	do { \
		rl_callback_handler_remove(); \
		exit(status); \
	} while (0)

enum pass_mode_e {
	pass_script,
	pass_interactive,
};

static double timeout = 5;
static int p_arg = 0;
static int e_arg = 0;
static int line_sock;

static void v_noreturn_
adm_exit(enum VCLI_status_e status) {
	RL_EXIT(status / 100);
}

static void
cli_write(int sock, const char *s)
{
	int i, l;

	i = strlen(s);
	l = write (sock, s, i);
	if (i == l)
		return;
	perror("Write error CLI socket");
	adm_exit(CLIS_COMMS);
}

/*
 * This function establishes a connection to the specified ip and port and
 * sends a command to varnishd. If varnishd returns an OK status, the result
 * is printed and 0 returned. Else, an error message is printed and 1 is
 * returned
 */
static int
cli_sock(const char *T_arg, const char *S_arg)
{
	int fd;
	int sock;
	unsigned status;
	char *answer = NULL;
	char buf[CLI_AUTH_RESPONSE_LEN + 1];
	const char *err;

	sock = VTCP_open(T_arg, NULL, timeout, &err);
	if (sock < 0) {
		fprintf(stderr, "Connection failed (%s): %s\n", T_arg, err);
		return (-1);
	}

	(void)VCLI_ReadResult(sock, &status, &answer, timeout);
	if (status == CLIS_AUTH) {
		if (S_arg == NULL) {
			fprintf(stderr, "Authentication required\n");
			free(answer);
			closefd(&sock);
			return (-1);
		}
		fd = open(S_arg, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Cannot open \"%s\": %s\n",
			    S_arg, strerror(errno));
			closefd(&sock);
			free(answer);
			return (-1);
		}
		VCLI_AuthResponse(fd, answer, buf);
		closefd(&fd);
		free(answer);

		cli_write(sock, "auth ");
		cli_write(sock, buf);
		cli_write(sock, "\n");
		(void)VCLI_ReadResult(sock, &status, &answer, timeout);
	}
	if (status != CLIS_OK && status != CLIS_TRUNCATED) {
		fprintf(stderr, "Rejected %u\n%s\n", status, answer);
		closefd(&sock);
		free(answer);
		return (-1);
	}
	free(answer);

	cli_write(sock, "ping\n");
	(void)VCLI_ReadResult(sock, &status, &answer, timeout);
	if (status != CLIS_OK || strstr(answer, "PONG") == NULL) {
		fprintf(stderr, "No pong received from server\n");
		closefd(&sock);
		free(answer);
		return (-1);
	}
	free(answer);

	return (sock);
}

static void
pass_print_answer(int status, char *answer, enum pass_mode_e mode)
{
	if (p_arg && answer != NULL) {
		printf("%-3u %-8zu\n%s", status, strlen(answer), answer);
	} else if (p_arg) {
		printf("%-3u %-8u\n", status, 0U);
	} else {
		if (mode == pass_interactive)
			printf("%u\n", status);
		if (answer != NULL)
			printf("%s\n", answer);
		if (status == CLIS_TRUNCATED)
			printf("[response was truncated]\n");
	}
	fflush(stdout);
}

static unsigned
pass_answer(int fd, enum pass_mode_e mode)
{
	unsigned u, status;
	char *answer = NULL;

	u = VCLI_ReadResult(fd, &status, &answer, timeout);
	if (u) {
		if (status == CLIS_COMMS) {
			fprintf(stderr, "%s\n", answer);
			adm_exit(CLIS_COMMS);
		}
		if (answer)
			fprintf(stderr, "%s\n", answer);
		adm_exit(status);
	}

	pass_print_answer(status, answer, mode);
	free(answer);
	return (status);
}

static void v_noreturn_
do_args(int sock, int argc, char * const *argv)
{
	int i;
	unsigned status;

	for (i = 0; i < argc; i++) {
		/* XXX: We should really CLI-quote these */
		if (i > 0)
			cli_write(sock, " ");
		cli_write(sock, argv[i]);
	}
	cli_write(sock, "\n");
	status = pass_answer(sock, pass_script);
	closefd(&sock);
	if (status == CLIS_OK || status == CLIS_TRUNCATED)
		exit(0);
	if (!p_arg)
		fprintf(stderr, "Command failed with error code %u\n", status);
	adm_exit(status);
}

/* Callback for readline, doesn't take a private pointer, so we need
 * to have a global variable.
 */
static void v_matchproto_()
send_line(char *l)
{
	if (l) {
		cli_write(line_sock, l);
		cli_write(line_sock, "\n");
		if (*l)
			add_history(l);
		rl_callback_handler_install("varnish> ", send_line);
	} else {
		RL_EXIT(0);
	}
}

static char *
command_generator (const char *text, int state)
{
	static struct vjsn *jsn_cmds;
	static const struct vjsn_val *jv;
	struct vjsn_val *jv2;
	unsigned u;
	char *answer = NULL;
	const char *err;

	if (!state) {
		cli_write(line_sock, "help -j\n");
		u = VCLI_ReadResult(line_sock, NULL, &answer, timeout);
		if (u) {
			free(answer);
			return (NULL);
		}
		jsn_cmds = vjsn_parse(answer, &err);
		free(answer);
		if (err != NULL)
			return (NULL);
		AN(jsn_cmds);
		AN(jsn_cmds->value);
		assert (vjsn_is_array(jsn_cmds->value));
		jv = VTAILQ_FIRST(&jsn_cmds->value->children);
		assert (vjsn_is_number(jv));
		jv = VTAILQ_NEXT(jv, list);
		assert (vjsn_is_array(jv));
		jv = VTAILQ_NEXT(jv, list);
		assert (vjsn_is_number(jv));
		jv = VTAILQ_NEXT(jv, list);
	}
	while (jv != NULL) {
		assert (vjsn_is_object(jv));
		jv2 = VTAILQ_FIRST(&jv->children);
		AN(jv2);
		jv = VTAILQ_NEXT(jv, list);
		assert (vjsn_is_string(jv2));
		assert (!strcmp(jv2->name, "request"));
		if (!strncmp(text, jv2->value, strlen(text)))
			return (strdup(jv2->value));
	}
	vjsn_delete(&jsn_cmds);
	return (NULL);
}

static char **
varnishadm_completion (const char *text, int start, int end)
{
	char **matches;
	(void)end;
	matches = (char **)NULL;
	if (start == 0)
		matches = rl_completion_matches(text, command_generator);
	return (matches);
}


/*
 * No arguments given, simply pass bytes on stdin/stdout and CLI socket
 * Send a "banner" to varnish, to provoke a welcome message.
 */
static void v_noreturn_
interactive(int sock)
{
	struct pollfd fds[2];
	int i;
	unsigned status;
	line_sock = sock;
	rl_already_prompted = 1;
	rl_callback_handler_install("varnish> ", send_line);
	rl_attempted_completion_function = varnishadm_completion;

	fds[0].fd = sock;
	fds[0].events = POLLIN;
	fds[1].fd = 0;
	fds[1].events = POLLIN;

	cli_write(sock, "banner\n");
	while (1) {
		i = poll(fds, 2, -1);
		if (i == -1 && errno == EINTR) {
			continue;
		}
		assert(i > 0);
		if (fds[0].revents & POLLIN) {
			/* Get rid of the prompt, kinda hackish */
			printf("\r           \r");
			status = pass_answer(fds[0].fd, pass_interactive);
			rl_forced_update_display();
			if (status == CLIS_CLOSE)
				RL_EXIT(0);
		}
		if (fds[1].revents & POLLIN) {
			rl_callback_read_char();
		}
	}
}

/*--------------------------------------------------------------------*/

static void
vadm_cli_cb_after(const struct cli *cli)
{
	const char *cmd;

	if (!e_arg)
		return;

	if (cli->result == CLIS_OK || cli->result == CLIS_TRUNCATED)
		return;

	cmd = VSB_data(cli->cmd);
	if (*cmd == '-')
		return;

	pass_print_answer(cli->result, VSB_data(cli->sb), pass_script);
	fprintf(stderr, "\nCommand \"%s\" failed with error code %u\n",
	    cmd, cli->result);
	adm_exit(cli->result);
}

/*
 * No arguments given, simply pass bytes on stdin/stdout and CLI socket
 */

static void v_noreturn_
pass(int sock)
{
	struct VCLP *vclp;
	int i;

	vclp = VCLP_New(STDIN_FILENO, STDOUT_FILENO,
	    sock, p_arg ? PROTO_FULL : PROTO_HEADLESS, timeout);
	AN(vclp);
	VCLP_SetHooks(vclp, NULL, vadm_cli_cb_after);
	do {
		i = VCLP_Poll(vclp, -1);
	} while (i == 0);
	RL_EXIT(0);
}

static void v_noreturn_
usage(int status)
{
	fprintf(stderr,
	    "Usage: varnishadm [-e] [-h] [-n ident] [-p] [-S secretfile] "
	    "[-T [address]:port] [-t timeout] [command [...]]\n");
	fprintf(stderr, "\t-n is mutually exclusive with -S and -T\n");
	exit(status);
}

static int
n_arg_sock(const char *n_arg, const char *t_arg)
{
	char *T_arg, *T_start;
	char *S_arg;
	struct vsm *vsm;
	char *p;
	int sock;

	vsm = VSM_New();
	AN(vsm);
	if (VSM_Arg(vsm, 'n', n_arg) < 0 ||
	    VSM_Arg(vsm, 't', t_arg) < 0 ||
	    VSM_Attach(vsm, STDERR_FILENO) < 0) {
		fprintf(stderr, "%s\n", VSM_Error(vsm));
		VSM_Destroy(&vsm);
		return (-1);
	}

	T_start = T_arg = VSM_Dup(vsm, "Arg", "-T");
	S_arg = VSM_Dup(vsm, "Arg", "-S");
	VSM_Destroy(&vsm);

	if (T_arg == NULL) {
		fprintf(stderr, "No -T in shared memory\n");
		return (-1);
	}

	sock = -1;
	while (*T_arg) {
		p = strchr(T_arg, '\n');
		AN(p);
		*p = '\0';
		sock = cli_sock(T_arg, S_arg);
		if (sock >= 0)
			break;
		T_arg = p + 1;
	}
	free(T_start);
	free(S_arg);
	return (sock);
}

static int
t_arg_timeout(const char *t_arg)
{
	char *p = NULL;

	AN(t_arg);
	timeout = strtod(t_arg, &p);
	if ((p != NULL && *p != '\0') ||
	    !isfinite(timeout) || timeout < 0) {
		fprintf(stderr, "-t: Invalid argument: %s", t_arg);
		return (-1);
	}
	return (1);
}

#define OPTARG "ehn:pS:T:t:"

int
main(int argc, char * const *argv)
{
	const char *T_arg = NULL;
	const char *S_arg = NULL;
	const char *n_arg = NULL;
	const char *t_arg = NULL;
	int opt, sock;

	if (argc == 2 && !strcmp(argv[1], "--optstring")) {
		printf(OPTARG "\n");
		exit(0);
	}
	/*
	 * By default linux::getopt(3) mangles the argv order, such that
	 *	varnishadm -n bla param.set foo -bar
	 * gets interpreted as
	 *	varnishadm -n bla -bar param.set foo
	 * The '+' stops that from happening
	 * See #1496
	 */
	while ((opt = getopt(argc, argv, "+" OPTARG)) != -1) {
		switch (opt) {
		case 'e':
			e_arg = 1;
			break;
		case 'h':
			/* Usage help */
			usage(0);
		case 'n':
			n_arg = optarg;
			break;
		case 'p':
			p_arg = 1;
			break;
		case 'S':
			S_arg = optarg;
			break;
		case 'T':
			T_arg = optarg;
			break;
		case 't':
			t_arg = optarg;
			break;
		default:
			usage(2);
		}
	}

	argc -= optind;
	argv += optind;

	if (T_arg != NULL) {
		if (n_arg != NULL)
			usage(2);
		sock = cli_sock(T_arg, S_arg);
	} else {
		if (S_arg != NULL)
			usage(2);
		sock = n_arg_sock(n_arg, t_arg);
	}
	if (sock < 0)
		exit(2);

	if (t_arg != NULL && t_arg_timeout(t_arg) < 0)
		exit(2);

	if (argc > 0) {
		VSIG_Arm_int();
		VSIG_Arm_term();
		do_args(sock, argc, argv);
		NEEDLESS(exit(0));
	}

	if (isatty(0) && !p_arg)
		interactive(sock);
	else
		pass(sock);
	NEEDLESS(exit(0));
}
