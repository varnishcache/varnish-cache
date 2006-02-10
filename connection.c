/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <stdlib.h>
#include <unistd.h>

#include "varnish.h"
#include "connection.h"

void
connection_destroy(struct connection *c)
{
	close(c->sd);
	/* bzero(c, sizeof *c); */
	free(c);
}
