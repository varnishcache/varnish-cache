/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include "varnish.h"
#include "connection.h"
#include "listener.h"
#include "log.h"

struct listener {
	int sd;
	struct sockaddr_storage addr;
};

/*
 * Create a socket that listens on the specified port.  We use an IPv6 TCP
 * socket and clear the IPV6_V6ONLY option to accept IPv4 connections on
 * the same socket.
 */
struct listener *
listener_create(int port)
{
	struct listener *l;
	socklen_t addr_len;
	int zero = 0;

	l = calloc(1, sizeof *l);
	if (l == NULL) {
		log_syserr("calloc()");
		return (NULL);
	}
	l->sd = -1;
#if defined(AF_INET6) && defined(IPV6_V6ONLY)
	if ((l->sd = socket(AF_INET6, SOCK_STREAM, 0)) > 0) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&l->addr;
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
		addr->sin6_len =
#endif
		addr_len = sizeof *addr;
		addr->sin6_family = AF_INET6;
		addr->sin6_port = htons(port);
		if (setsockopt(l->sd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero) != 0) {
			log_syserr("setsockopt()");
			return (NULL);
		}
	} else if (errno != EPROTONOSUPPORT) {
		log_syserr("socket()");
		return (NULL);
	} else
#endif
	if ((l->sd = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
		struct sockaddr_in *addr = (struct sockaddr_in *)&l->addr;
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
		addr->sin_len =
#endif
		addr_len = sizeof *addr;
		addr->sin_family = AF_INET;
		addr->sin_port = htons(port);
	} else {
		log_syserr("socket()");
		return (NULL);
	}
	if (bind(l->sd, (struct sockaddr *)&l->addr, addr_len) != 0) {
		log_syserr("bind()");
		return (NULL);
	}
	if (listen(l->sd, 16) != 0) {
		log_syserr("listen()");
		return (NULL);
	}
	return (l);
}

void
listener_destroy(struct listener *l)
{
	close(l->sd);
	/* bzero(l, sizeof *l); */
	free(l);
}

connection_t *
listener_accept(struct listener *l)
{
	connection_t *c;

	for (;;) {
		if ((c = connection_accept(l->sd)) != NULL)
			return (c);
		if (errno != EINTR) {
			log_syserr("accept()");
			free(c);
			return (NULL);
		}
	}
}
