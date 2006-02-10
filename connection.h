/*
 * $Id$
 */

#ifndef CONNECTION_H_INCLUDED
#define CONNECTION_H_INCLUDED

typedef struct connection connection_t;

connection_t *connection_accept(int ld);
void connection_destroy(connection_t *c);

#endif
