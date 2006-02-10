/*
 * $Id$
 */

#ifndef LISTENER_H_INCLUDED
#define LISTENER_H_INCLUDED

#include "connection.h"

typedef struct listener listener_t;

listener_t *listener_create(int port);
void listener_destroy(listener_t *l);
connection_t *listener_accept(listener_t *l);

#endif
