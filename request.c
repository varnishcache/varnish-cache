/*
 * $Id$
 */

#include <stdlib.h>

#include "varnish.h"
#include "request.h"

request_t *
request_wait(connection_t *c, unsigned int timeout)
{
	request_t *r;

	r = calloc(1, sizeof *r);
	if (r == NULL) {
		log_syserr("calloc()");
		return (NULL);
	}

	/* ... */

	return (r);
}

void
request_destroy(request_t *r)
{
	/* bzero(r, sizeof *r); */
	free(r);
}
