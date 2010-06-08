/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

#include "svnid.h"
SVNID("$Id$")

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>

#include "cli.h"
#include "cli_common.h"
#include "libvarnish.h"
#include "varnishapi.h"
#include "vss.h"

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
	exit (1);
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
	char buf[CLI_AUTH_RESPONSE_LEN];

	sock = VSS_open(T_arg, timeout);
	if (sock < 0) {
		fprintf(stderr, "Connection failed\n");
		exit(1);
	}

	(void)cli_readres(sock, &status, &answer, timeout);
	if (status == CLIS_AUTH) {
		if (S_arg == NULL) {
			fprintf(stderr, "Authentication required\n");
			exit(1);
		}
		fd = open(S_arg, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Cannot open \"%s\": %s\n",
			    S_arg, strerror(errno));
			exit (1);
		}
		CLI_response(fd, answer, buf);
		AZ(close(fd));
		free(answer);

		cli_write(sock, "auth ");
		cli_write(sock, buf);
		cli_write(sock, "\n");
		(void)cli_readres(sock, &status, &answer, timeout);
	}
	if (status != CLIS_OK) {
		fprintf(stderr, "Rejected %u\n%s\n", status, answer);
		exit(1);
	}
	free(answer);

	cli_write(sock, "ping\n");
	(void)cli_readres(sock, &status, &answer, timeout);
	if (status != CLIS_OK || strstr(answer, "PONG") == NULL) {
		fprintf(stderr, "No pong received from server\n");
		exit(1);
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

	(void)cli_readres(sock, &status, &answer, 2000);

	/* XXX: AZ() ? */
	(void)close(sock);

	printf("%s\n", answer);
	if (status == CLIS_OK) {
		exit(0);
	}
	fprintf(stderr, "Command failed with error code %u\n", status);
	exit(1);
}

/*
 * No arguments given, simply pass bytes on stdin/stdout and CLI socket
 * Send a "banner" to varnish, to provoke a welcome message.
 */
static void
pass(int sock)
{
	struct pollfd fds[2];
	char buf[1024];
	int i, n, m;

	cli_write(sock, "banner\n");
	fds[0].fd = sock;
	fds[0].events = POLLIN;
	fds[1].fd = 0;
	fds[1].events = POLLIN;
	while (1) {
		i = poll(fds, 2, -1);
		assert(i > 0);
		if (fds[0].revents & POLLIN) {
			n = read(fds[0].fd, buf, sizeof buf);
			if (n == 0)
				exit (0);
			if (n < 0) {
				perror("Read error reading CLI socket");
				exit (0);
			}
			assert(n > 0);
			m = write(1, buf, n);
			if (n != m) {
				perror("Write error writing stdout");
				exit (1);
			}
		}
		if (fds[1].revents & POLLIN) {
			n = read(fds[1].fd, buf, sizeof buf);
			if (n == 0) {
				AZ(shutdown(sock, SHUT_WR));
				fds[1].fd = -1;
			} else if (n < 0) {
				exit(0);
			} else {
				m = write(sock, buf, n);
				if (n != m) {
					perror("Write error writing CLI socket");
					exit (1);
				}
			}
		}
	}
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: varnishadm [-t timeout] [-S secretfile] "
	    "-T [address]:port command [...]\n");
	exit(1);
}

int
main(int argc, char * const *argv)
{
	const char *T_arg = NULL;
	const char *S_arg = NULL;
	const char *n_arg = NULL;
	struct VSM_data *vsd;
	char *p;
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
		vsd = VSM_New();
		assert(VSL_Log_Arg(vsd, 'n', n_arg));
		if (!VSM_Open(vsd, 1)) {
			if (T_arg == NULL) {
				p = VSM_Find_Chunk(vsd, "Arg", "-T", "", NULL);
				if (p != NULL) {
					T_arg = strdup(p);
				}
			}
			if (S_arg == NULL) {
				p = VSM_Find_Chunk(vsd, "Arg", "-S", "", NULL);
				if (p != NULL) {
					S_arg = strdup(p);
				}
			}
		}
	}

	if (T_arg == NULL)
		usage();

	assert(T_arg != NULL);
	sock = cli_sock(T_arg, S_arg);

	if (argc > 0)
		do_args(sock, argc, argv);
	else
		pass(sock);

	exit(0);
}
