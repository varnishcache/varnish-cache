/*
 * $Id$
 */

#ifndef LISTENER_H_INCLUDED
#define LISTENER_H_INCLUDED

struct listener {
	int sd;
	struct sockaddr_storage addr;
};

#endif
