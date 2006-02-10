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
#include "listener.h"
#include "connection.h"

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

struct connection *
listener_accept(struct listener *l)
{
	struct connection *c;
	socklen_t len;

	c = calloc(1, sizeof *c);
	for (;;) {
		len = sizeof c->addr;
		c->sd = accept(l->sd, (struct sockaddr *)&c->addr, &len);
		if (c->sd != -1) {
			switch (c->addr.ss_family) {
#if defined(AF_INET6)
			case AF_INET6: {
				struct sockaddr_in6 *addr =
				    (struct sockaddr_in6 *)&c->addr;
				uint16_t *ip = (uint16_t *)&addr->sin6_addr;

				log_info("%s(): [%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]:%u",
				    __func__,
				    ntohs(ip[0]), ntohs(ip[1]),
				    ntohs(ip[2]), ntohs(ip[3]),
				    ntohs(ip[4]), ntohs(ip[5]),
				    ntohs(ip[6]), ntohs(ip[7]),
				    ntohs(addr->sin6_port));
				break;
			}
#endif
			case AF_INET: {
				struct sockaddr_in *addr =
				    (struct sockaddr_in *)&c->addr;
				uint8_t *ip = (uint8_t *)&addr->sin_addr;

				log_info("%s(): %u.%u.%u.%u:%u",
				    __func__,
				    ip[0], ip[1], ip[2], ip[3],
				    ntohs(addr->sin_port));
				break;
			}
			default:
				LOG_UNREACHABLE();
			}
			return (c);
		}
		if (errno != EINTR) {
			log_syserr("accept()");
			free(c);
			return (NULL);
		}
	}
}
