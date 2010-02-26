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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "cli.h"
#include "cli_common.h"
#include "libvarnish.h"
#include "vss.h"

static double timeout = 5;

/*
 * This function establishes a connection to the specified ip and port and
 * sends a command to varnishd. If varnishd returns an OK status, the result
 * is printed and 0 returned. Else, an error message is printed and 1 is
 * returned
 */
static void
telnet_mgt(const char *T_arg, const char *S_arg, int argc, char *argv[])
{
	int i, fd;
	int sock;
	unsigned status;
	char *answer = NULL;
	char buf[CLI_AUTH_RESPONSE_LEN];

	sock = VSS_open(T_arg, timeout);
	if (sock < 0) {
		fprintf(stderr, "Connection failed\n");
		exit(1);
	}

	cli_readres(sock, &status, &answer, timeout);
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
		write(sock, "auth ", 5);
		write(sock, buf, strlen(buf));
		write(sock, "\n", 1);
		cli_readres(sock, &status, &answer, timeout);
	}
	if (status != CLIS_OK) {
		fprintf(stderr, "Rejected %u\n%s\n", status, answer);
		exit(1);
	}

	write(sock, "ping\n", 5);
	cli_readres(sock, &status, &answer, timeout);
	if (status != CLIS_OK || strstr(answer, "PONG") == NULL) {
		fprintf(stderr, "No pong received from server\n");
		exit(1);
	}
	free(answer);

	for (i=0; i<argc; i++) {
		if (i > 0)
			write(sock, " ", 1);
		write(sock, argv[i], strlen(argv[i]));
	}
	write(sock, "\n", 1);

	cli_readres(sock, &status, &answer, 2000);

	close(sock);

	if (status == CLIS_OK) {
		printf("%s\n", answer);
		exit(0);
	}
	fprintf(stderr, "Command failed with error code %u\n", status);
	exit(1);

}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: varnishadm [-t timeout] [-S secretfile] -T [address]:port command [...]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	const char *T_arg = NULL;
	const char *S_arg = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "S:T:t:")) != -1) {
		switch (opt) {
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

	if (T_arg == NULL || argc < 1)
		usage();

	telnet_mgt(T_arg, S_arg, argc, argv);

	exit(0);
}
