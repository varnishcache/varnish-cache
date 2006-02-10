/*
 * $Id$
 */

#ifndef REQUEST_H_INCLUDED
#define REQUEST_H_INCLUDED

typedef struct request request_t;

request_t *request_wait(connection_t *c, unsigned int timeout);
void request_destroy(request_t *r);

#endif
