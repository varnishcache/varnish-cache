/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "compat/strlcpy.h"
#include "heritage.h"
#include "mgt.h"

/*--------------------------------------------------------------------*/

void
TCP_name(struct sockaddr *addr, unsigned l, char *abuf, unsigned alen, char *pbuf, unsigned plen)
{
	int i;

	i = getnameinfo(addr, l, abuf, alen, pbuf, plen,
	   NI_NUMERICHOST | NI_NUMERICSERV);
	if (i) {
		printf("getnameinfo = %d %s\n", i, gai_strerror(i));
		strlcpy(abuf, "Conversion", alen);
		strlcpy(pbuf, "Failed", plen);
		return;
	}
}

/*--------------------------------------------------------------------*/

void
TCP_myname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen)
{
	struct sockaddr addr[2];	/* XXX: IPv6 hack */
	socklen_t l;

	l = sizeof addr;
	AZ(getsockname(sock, addr, &l));
	TCP_name(addr, l, abuf, alen, pbuf, plen);
}

/*--------------------------------------------------------------------*/

#ifdef HAVE_ACCEPT_FILTERS
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
#endif

int
open_tcp(const char *port)
{
	struct addrinfo hints, *res;
	int ret, sd, val;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((ret = getaddrinfo(NULL, port, &hints, &res)) != 0) {
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(ret));
		return (-1);
	}

	sd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sd < 0 && errno == EPROTONOSUPPORT) {
		freeaddrinfo(res);
		hints.ai_family = AF_INET;
		if ((ret = getaddrinfo(NULL, port, &hints, &res)) != 0) {
			fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(ret));
			return (-1);
		}
		sd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	}
	if (sd < 0) {
		perror("socket()");
		freeaddrinfo(res);
		return (-1);
	}
	val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) != 0) {
		perror("setsockopt(SO_REUSEADDR, 1)");
		freeaddrinfo(res);
		close(sd);
		return (-1);
	}
	val = 0;
	if (res->ai_family == AF_INET6 &&
	    setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof val) != 0) {
		perror("setsockopt(IPV6_V6ONLY, 0)");
		freeaddrinfo(res);
		close(sd);
		return (-1);
	}
	if (bind(sd, res->ai_addr, res->ai_addrlen) != 0) {
		perror("bind()");
		freeaddrinfo(res);
		close(sd);
		return (-1);
	}
	if (listen(sd, 16) != 0) {
		perror("listen()");
		freeaddrinfo(res);
		close(sd);
		return (-1);
	}
#ifdef HAVE_ACCEPT_FILTERS
	accept_filter(sd);
#endif
	heritage.socket = sd;
	return (0);
}
