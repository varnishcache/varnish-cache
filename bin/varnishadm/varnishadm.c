/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 *
 * $Id$
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "config.h"
#include "libvarnish.h"
#include "vss.h"

#define STATUS_OK	200

/*
 * This function establishes a connection to the specified ip and port and
 * sends a command to varnishd. If varnishd returns an OK status, the result
 * is printed and 0 returned. Else, an error message is printed and 1 is
 * returned
 */
static void
telnet_mgt(const char *T_arg, int argc, char *argv[])
{
	struct vss_addr **ta;
	char *addr, *port;
	int i, n;
	int sock;
	long status, bytes;
	char *answer = NULL;
	char buf[13];
	char *p, *pp;

	XXXAZ(VSS_parse(T_arg, &addr, &port));
	XXXAN(n = VSS_resolve(addr, port, &ta));
	free(addr);
	free(port);
	if (n == 0) {
		fprintf(stderr, "Could not resolve '%s'\n", T_arg);
		exit(2);
	}

	sock = VSS_connect(ta[0]);

	for (i = 0; i < n; ++i) {
		free(ta[i]);
		ta[i] = NULL;
	}
	free(ta);

	for (i=0; i<argc; i++) {
		if (i > 0)
			write(sock, " ", 1);
		write(sock, argv[i], strlen(argv[i]));
	}
	write(sock, "\n", 1);

	n = read(sock, buf, 13);
	if (n != 13) {
		fprintf(stderr, "An error occured in receiving status.\n");
		exit(1);
	}
	if (!(p = strchr(buf, ' '))) {
		fprintf(stderr, "An error occured in parsing of status code.\n");
		exit(1);
	}
	*p = '\0';
	status = strtol(buf, &p, 10);
	pp = p+1;
	if (!(p = strchr(pp, '\n'))) {
		fprintf(stderr, "An error occured in parsing of number of bytes returned.\n");
		exit(1);
	}
	*p = '\0';
	bytes = strtol(pp, &p, 10);

	answer = malloc(bytes+1);
	n = read(sock, answer, bytes);
	if (n != bytes) {
		fprintf(stderr, "An error occured in receiving answer.\n");
		exit(1);
	}
	answer[bytes] = '\0';
	close(sock);

	if (status == STATUS_OK) {
		printf("%s\n", answer);
		exit(0);
	}
	fprintf(stderr, "Command failed with error code %ld\n", status);
	exit(1);

}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: varnishadm -T [address]:port command [...]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	const char *T_arg = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "T:")) != -1) {
		switch (opt) {
		case 'T':
			T_arg = optarg;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (T_arg == NULL || argc < 1)
		usage();

	telnet_mgt(T_arg, argc, argv);

	exit(0);
}
