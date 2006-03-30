/*
 * $Id$
 */

#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include "libvarnish.h"
#include "vcl_lang.h"
#include "cache.h"

static TAILQ_HEAD(, sess) shd = TAILQ_HEAD_INITIALIZER(shd);

static pthread_mutex_t	shdmtx;
static pthread_cond_t	shdcnd;

static void *
CacheWorker(void *priv __unused)
{
	struct sess *sp;

	while (1) {
		AZ(pthread_mutex_lock(&shdmtx));
		while (1) {
			sp = TAILQ_FIRST(&shd);
			if (sp != NULL)
				break;
			AZ(pthread_cond_wait(&shdcnd, &shdmtx));
		}
		TAILQ_REMOVE(&shd, sp, list);
		AZ(pthread_mutex_unlock(&shdmtx));

		HttpdAnalyze(sp);

		/*
		 * XXX send session to acceptor for reuse/disposal
	 	 */
	}
}

void
DealWithSession(struct sess *sp)
{
	AZ(pthread_mutex_lock(&shdmtx));
	TAILQ_INSERT_TAIL(&shd, sp, list);
	AZ(pthread_mutex_unlock(&shdmtx));
	AZ(pthread_cond_signal(&shdcnd));
}

void
CacheInitPool(void)
{
	pthread_t tp;

	AZ(pthread_mutex_init(&shdmtx, NULL));
	AZ(pthread_cond_init(&shdcnd, NULL));

	AZ(pthread_create(&tp, NULL, CacheWorker, NULL));
	AZ(pthread_detach(tp));
}
