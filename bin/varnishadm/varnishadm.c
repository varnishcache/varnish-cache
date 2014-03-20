/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Cecilie Fritzvold <cecilihf@linpro.no>
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

#ifdef HAVE_EDIT_READLINE_READLINE_H
#  include <edit/readline/readline.h>
#elif HAVE_READLINE_READLINE_H
#  include <readline/readline.h>
#  ifdef HAVE_READLINE_HISTORY_H
#    include <readline/history.h>
#  endif
#else
#  include <editline/readline.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vas.h"
#include "vcli.h"
#include "vss.h"

#define RL_EXIT(status) \
	do { \
		rl_callback_handler_remove(); \
		exit(status); \
	} while (0)


static double timeout = 5;

static void
cli_write(int sock, const char *s)
{
	int i, l;

	i = strlen(s);
	l = write (sock, s, i);
	if (i == l)
		return;
	perror("Write error CLI socket");
	RL_EXIT(1);
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

	sock = VSS_open(T_arg, timeout);
	if (sock < 0) {
		fprintf(stderr, "Connection failed (%s)\n", T_arg);
		return (-1);
	}

	(void)VCLI_ReadResult(sock, &status, &answer, timeout);
	if (status == CLIS_AUTH) {
		if (S_arg == NULL) {
			fprintf(stderr, "Authentication required\n");
			AZ(close(sock));
			return(-1);
		}
		fd = open(S_arg, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Cannot open \"%s\": %s\n",
			    S_arg, strerror(errno));
			AZ(close(sock));
			return (-1);
		}
		VCLI_AuthResponse(fd, answer, buf);
		AZ(close(fd));
		free(answer);

		cli_write(sock, "auth ");
		cli_write(sock, buf);
		cli_write(sock, "\n");
		(void)VCLI_ReadResult(sock, &status, &answer, timeout);
	}
	if (status != CLIS_OK) {
		fprintf(stderr, "Rejected %u\n%s\n", status, answer);
		AZ(close(sock));
		free(answer);
		return (-1);
	}
	free(answer);

	cli_write(sock, "ping\n");
	(void)VCLI_ReadResult(sock, &status, &answer, timeout);
	if (status != CLIS_OK || strstr(answer, "PONG") == NULL) {
		fprintf(stderr, "No pong received from server\n");
		AZ(close(sock));
		free(answer);
		return (-1);
	}
	free(answer);

	return (sock);
}

static void
do_args(int sock, int argc, char * const *argv)
{
	int i;
	unsigned status;
	char *answer = NULL;

	for (i=0; i<argc; i++) {
		/* XXX: We should really CLI-quote these */
		if (i > 0)
			cli_write(sock, " ");
		cli_write(sock, argv[i]);
	}
	cli_write(sock, "\n");

	(void)VCLI_ReadResult(sock, &status, &answer, 2000);

	/* XXX: AZ() ? */
	(void)close(sock);

	printf("%s\n", answer);
	if (status == CLIS_OK) {
		exit(0);
	}
	fprintf(stderr, "Command failed with error code %u\n", status);
	exit(1);
}

/* Callback for readline, doesn't take a private pointer, so we need
 * to have a global variable.
 */
static int _line_sock;
static void send_line(char *l)
{
	if (l) {
		cli_write(_line_sock, l);
		cli_write(_line_sock, "\n");
		add_history(l);
	} else {
		RL_EXIT(0);
	}
}

static char *commands[256];
static char *
command_generator (const char *text, int state)
{
	static int list_index, len;
	const char *name;

	/* If this is a new word to complete, initialize now.  This
	   includes saving the length of TEXT for efficiency, and
	   initializing the index variable to 0. */
	if (!state) {
		list_index = 0;
		len = strlen(text);
	}

	while ((name = commands[list_index]) != NULL) {
		list_index++;
		if (strncmp (name, text, len) == 0)
			return (strdup(name));
	}
	/* If no names matched, then return NULL. */
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
static void
interactive(int sock)
{
	struct pollfd fds[2];
	char buf[1024];
	int i;
	char *answer = NULL;
	unsigned u, status;
	_line_sock = sock;
	rl_already_prompted = 1;
	rl_callback_handler_install("varnish> ", send_line);
	rl_attempted_completion_function = varnishadm_completion;

	fds[0].fd = sock;
	fds[0].events = POLLIN;
	fds[1].fd = 0;
	fds[1].events = POLLIN;

	/* Grab the commands, for completion */
	cli_write(sock, "help\n");
	u = VCLI_ReadResult(fds[0].fd, &status, &answer, timeout);
	if (!u) {
		char *t, c[128];
		if (status == CLIS_COMMS) {
			RL_EXIT(0);
		}
		t = answer;

		i = 0;
		while (*t) {
			if (sscanf(t, "%127s", c) == 1) {
				commands[i++] = strdup(c);
				while (*t != '\n' && *t != '\0')
					t++;
				if (*t == '\n')
					t++;
			} else {
				/* what? */
				fprintf(stderr, "Unknown command '%s' parsing "
					"help output. Tab completion may be "
					"broken\n", t);
				break;
			}
		}
	}
	cli_write(sock, "banner\n");
	while (1) {
		i = poll(fds, 2, -1);
		if (i == -1 && errno == EINTR) {
			continue;
		}
		assert(i > 0);
		if (fds[0].revents & POLLIN) {
			/* Get rid of the prompt, kinda hackish */
			u = write(1, "\r           \r", 13);
			u = VCLI_ReadResult(fds[0].fd, &status, &answer,
			    timeout);
			if (u) {
				if (status == CLIS_COMMS)
					RL_EXIT(0);
				if (answer)
					fprintf(stderr, "%s\n", answer);
				RL_EXIT(1);
			}

			sprintf(buf, "%u\n", status);
			u = write(1, buf, strlen(buf));
			if (answer) {
				u = write(1, answer, strlen(answer));
				u = write(1, "\n", 1);
				free(answer);
				answer = NULL;
			}
			rl_forced_update_display();
		}
		if (fds[1].revents & POLLIN) {
			rl_callback_read_char();
		}
	}
}

/*
 * No arguments given, simply pass bytes on stdin/stdout and CLI socket
 */
static void
pass(int sock)
{
	struct pollfd fds[2];
	char buf[1024];
	int i;
	char *answer = NULL;
	unsigned u, status;
	ssize_t n;

	fds[0].fd = sock;
	fds[0].events = POLLIN;
	fds[1].fd = 0;
	fds[1].events = POLLIN;
	while (1) {
		i = poll(fds, 2, -1);
		if (i == -1 && errno == EINTR) {
			continue;
		}
		assert(i > 0);
		if (fds[0].revents & POLLIN) {
			u = VCLI_ReadResult(fds[0].fd, &status, &answer,
			    timeout);
			if (u) {
				if (status == CLIS_COMMS)
					RL_EXIT(0);
				if (answer)
					fprintf(stderr, "%s\n", answer);
				RL_EXIT(1);
			}

			sprintf(buf, "%u\n", status);
			u = write(1, buf, strlen(buf));
			if (answer) {
				u = write(1, answer, strlen(answer));
				u = write(1, "\n", 1);
				free(answer);
				answer = NULL;
			}
		}
		if (fds[1].revents & POLLIN || fds[1].revents & POLLHUP) {
			n = read(fds[1].fd, buf, sizeof buf - 1);
			if (n == 0) {
				AZ(shutdown(sock, SHUT_WR));
				fds[1].fd = -1;
			} else if (n < 0) {
				RL_EXIT(0);
			} else {
				buf[n] = '\0';
				cli_write(sock, buf);
			}
		}
	}
}


static void
usage(void)
{
	fprintf(stderr,
	    "usage: varnishadm [-n ident] [-t timeout] [-S secretfile] "
	    "-T [address]:port command [...]\n");
	fprintf(stderr, "\t-n is mutually exlusive with -S and -T\n");
	exit(1);
}

static int
n_arg_sock(const char *n_arg)
{
	char *T_arg = NULL, *T_start = NULL;
	char *S_arg = NULL;
	struct VSM_data *vsm;
	char *p;
	int sock;
	struct VSM_fantom vt;

	vsm = VSM_New();
	AN(vsm);
	if (VSM_n_Arg(vsm, n_arg) < 0) {
		fprintf(stderr, "%s\n", VSM_Error(vsm));
		VSM_Delete(vsm);
		return (-1);
	}
	if (VSM_Open(vsm)) {
		fprintf(stderr, "%s\n", VSM_Error(vsm));
		VSM_Delete(vsm);
		return (-1);
	}

	if (!VSM_Get(vsm, &vt, "Arg", "-T", "")) {
		fprintf(stderr, "No -T arg in shared memory\n");
		VSM_Delete(vsm);
		return (-1);
	}
	AN(vt.b);
	T_start = T_arg = strdup(vt.b);

	if (VSM_Get(vsm, &vt, "Arg", "-S", "")) {
		AN(vt.b);
		S_arg = strdup(vt.b);
	}

	VSM_Delete(vsm);

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

int
main(int argc, char * const *argv)
{
	const char *T_arg = NULL;
	const char *S_arg = NULL;
	const char *n_arg = NULL;
	int opt, sock;

	while ((opt = getopt(argc, argv, "n:S:T:t:")) != -1) {
		switch (opt) {
		case 'n':
			n_arg = optarg;
			break;
		case 'S':
			S_arg = optarg;
			break;
		case 'T':
			T_arg = optarg;
			break;
		case 't':
			timeout = strtod(optarg, NULL);
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (n_arg != NULL) {
		if (T_arg != NULL || S_arg != NULL) {
			usage();
		}
		sock = n_arg_sock(n_arg);
	} else if (T_arg == NULL) {
		sock = n_arg_sock("");
	} else {
		assert(T_arg != NULL);
		sock = cli_sock(T_arg, S_arg);
	}
	if (sock < 0)
		exit(2);

	if (argc > 0)
		do_args(sock, argc, argv);
	else {
		if (isatty(0)) {
			interactive(sock);
		} else {
			pass(sock);
		}
	}
	exit(0);
}
