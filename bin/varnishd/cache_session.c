/*
 * $Id$
 *
 * Session and Client management.
 *
 * The client structures are kept around only as a convenience feature to
 * make it possible to track down offenders and misconfigured caches.
 * As such it is pure overhead and we do not want to spend too much time
 * on maintaining it.
 *
 * We identify clients by their address only and disregard the port number,
 * because the desired level of granularity is "whois is abuse@ or tech-c@
 * in the RIPE database.
 */

#include <stdlib.h>
#include <sys/uio.h>

#include "heritage.h"
#include "cache.h"
#include "shmlog.h"

#define CLIENT_HASH			256

/*--------------------------------------------------------------------*/

struct sessmem {
	struct sess	sess;
	struct http	http;
	char		*http_hdr;
};

/*--------------------------------------------------------------------*/

TAILQ_HEAD(clienthead ,client);

static struct clienthead	client_hash[CLIENT_HASH];

/*--------------------------------------------------------------------*/

struct sess *
SES_New(struct sockaddr *addr, unsigned len)
{
	struct sessmem *sm;

	(void)addr;	/* XXX */
	(void)len;	/* XXX */
	sm = calloc(
	    sizeof *sm +
	    heritage.mem_http_headers * sizeof sm->http_hdr +
	    heritage.mem_http_headerspace +
	    heritage.mem_workspace,
	    1);
	if (sm == NULL)
		return (NULL);
	VSL_stats->n_sess++;
	sm->sess.mem = sm;
	sm->sess.http = &sm->http;
	http_Init(&sm->http, (void *)(sm + 1));
	return (&sm->sess);
}

void
SES_Delete(const struct sess *sp)
{

	VSL_stats->n_sess--;
	free(sp->mem);
}

/*--------------------------------------------------------------------*/

void
SES_Init()
{
	int i;

	for (i = 0; i < CLIENT_HASH; i++)
		TAILQ_INIT(&client_hash[i]);
}
