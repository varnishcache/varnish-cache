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

static pthread_cond_t	shdcnd;

static void *
CacheWorker(void *priv __unused)
{
	struct sess *sp;

	AZ(pthread_mutex_lock(&sessmtx));
	while (1) {
		while (1) {
			sp = TAILQ_FIRST(&shd);
			if (sp != NULL)
				break;
			AZ(pthread_cond_wait(&shdcnd, &sessmtx));
		}
		TAILQ_REMOVE(&shd, sp, list);
		sp->vcl = GetVCL();
		AZ(pthread_mutex_unlock(&sessmtx));

		HttpdAnalyze(sp);

		sp->backend = sp->vcl->default_backend;
		/* Call the VCL program */
		sp->vcl->main_func(sp);

		printf("Handling: %d\n", sp->handling);

		PipeSession(sp);

		AZ(pthread_mutex_lock(&sessmtx));
		RelVCL(sp->vcl);
		sp->vcl = NULL;
		vca_retire_session(sp);
	}
}

void
DealWithSession(struct sess *sp)
{
	AZ(pthread_mutex_lock(&sessmtx));
	TAILQ_INSERT_TAIL(&shd, sp, list);
	AZ(pthread_mutex_unlock(&sessmtx));
	AZ(pthread_cond_signal(&shdcnd));
}

void
CacheInitPool(void)
{
	pthread_t tp;

	AZ(pthread_cond_init(&shdcnd, NULL));

	AZ(pthread_create(&tp, NULL, CacheWorker, NULL));
	AZ(pthread_detach(tp));
}
