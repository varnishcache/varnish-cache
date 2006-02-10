/*
 * $Id$
 */

#ifndef CONNECTION_H_INCLUDED
#define CONNECTION_H_INCLUDED

struct connection {
	int sd;
	struct sockaddr_storage addr;
};

#endif
