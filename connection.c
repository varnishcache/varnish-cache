/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <stdlib.h>
#include <unistd.h>

#include "varnish.h"
#include "log.h"
#include "connection.h"

struct connection {
	int sd;
	struct sockaddr_storage addr;
};

/*
 * Accepts a connection from the provided listening descriptor.  Does not
 * loop to handle EINTR or other similar conditions.
 */
connection_t *
connection_accept(int ld)
{
	connection_t *c;
	socklen_t len;

	if ((c = calloc(1, sizeof *c)) == NULL)
		return (NULL);

	len = sizeof c->addr;
	if ((c->sd = accept(ld, (struct sockaddr *)&c->addr, &len)) == -1) {
		free(c);
		return (NULL);
	}
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

void
connection_destroy(struct connection *c)
{
	close(c->sd);
	/* bzero(c, sizeof *c); */
	free(c);
}
