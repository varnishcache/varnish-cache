/*
 * $Id$
 */

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "heritage.h"
#include "mgt.h"
#include "libvarnish.h"

static void
accept_filter(int fd)
{
	struct accept_filter_arg afa;
	int i;

	bzero(&afa, sizeof(afa));
	strcpy(afa.af_name, "httpready");
	errno = 0;
	i = setsockopt(fd, SOL_SOCKET, SO_ACCEPTFILTER,
	    &afa, sizeof(afa));
	if (i)
		printf("Acceptfilter(%d, httpready): %d %s\n",
		    fd, i, strerror(errno));
}

static void
create_listen_socket(const char *addr, const char *port, int *sp, int nsp)
{
	struct addrinfo ai, *r0, *r1;
	int i, j, s;

	memset(&ai, 0, sizeof ai);
	ai.ai_family = PF_UNSPEC;
	ai.ai_socktype = SOCK_STREAM;
	ai.ai_flags = AI_PASSIVE;
	i = getaddrinfo(addr, port, &ai, &r0);

	if (i) {
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(i));
		return;
	}

	for (r1 = r0; r1 != NULL && nsp > 0; r1 = r1->ai_next) {
		s = socket(r1->ai_family, r1->ai_socktype, r1->ai_protocol);
		if (s < 0)
			continue;
		j = 1;
		i = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &j, sizeof j);
		assert(i == 0);

		i = bind(s, r1->ai_addr, r1->ai_addrlen);
		if (i != 0) {
			perror("bind");
			continue;
		}
		assert(i == 0);
		*sp = s;
		sp++;
		nsp--;
	}

	freeaddrinfo(r0);
}

int
open_tcp(const char *port)
{
	unsigned u;

	for (u = 0; u < HERITAGE_NSOCKS; u++) {
		heritage.sock_local[u] = -1;
		heritage.sock_remote[u] = -1;
	}

	create_listen_socket("localhost", port,
	    &heritage.sock_local[0], HERITAGE_NSOCKS);

	create_listen_socket(NULL, port,
	    &heritage.sock_remote[0], HERITAGE_NSOCKS);

	for (u = 0; u < HERITAGE_NSOCKS; u++) {
		if (heritage.sock_local[u] >= 0) {
			AZ(listen(heritage.sock_local[u], 16));
			accept_filter(heritage.sock_local[u]);
		}
		if (heritage.sock_remote[u] >= 0) {
			AZ(listen(heritage.sock_remote[u], 16));
			accept_filter(heritage.sock_remote[u]);
		}
	}
	return (0);
}
