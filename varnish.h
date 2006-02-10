/*
 * $Id$
 */

#ifndef VARNISH_H_INCLUDED
#define VARNISH_H_INCLUDED

#include "config.h"

/* opaque structures */
typedef struct listener listener_t;
typedef struct connection connection_t;
typedef struct request request_t;

/* connection.c */
void connection_destroy(connection_t *c);

/* listener.c */
listener_t *listener_create(int port);
void listener_destroy(listener_t *l);
connection_t *listener_accept(listener_t *l);

/* log.c */
void log_info(const char *fmt, ...);
void log_err(const char *fmt, ...);
void log_syserr(const char *fmt, ...);
void log_panic(const char *fmt, ...);
void log_syspanic(const char *fmt, ...);

#define LOG_UNREACHABLE() \
	log_panic("%s(%d): %s(): unreachable code reached", \
	    __FILE__, __LINE__, __func__)

/* request.c */
request_t *request_wait(connection_t *c, unsigned int timeout);
void request_destroy(request_t *r);

#endif
