/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <md5.h>
#include <event.h>
#include <pthread.h>

#include "libvarnish.h"
#include "cache.h"

static struct hash_slinger      *hash;


struct object *
HSH_Lookup(struct worker *w, struct http *h)
{
	struct objhead *oh;
	struct object *o;
	unsigned char key[16];
	MD5_CTX ctx;
	char *b;

	assert(hash != NULL);
	/* Make sure we have both a new objhead and object if we need them */
	if (w->nobjhead == NULL) {
		w->nobjhead = calloc(sizeof *w->nobjhead, 1);
		assert(w->nobjhead != NULL);
		TAILQ_INIT(&w->nobjhead->objects);
	}
	if (w->nobj == NULL) {
		w->nobj = calloc(sizeof *w->nobj, 1);
		assert(w->nobj != NULL);
		w->nobj->busy = 1;
		TAILQ_INIT(&w->nobj->store);
		pthread_cond_init(&w->nobj->cv, NULL);
	}

	assert(http_GetURL(h, &b));
	MD5Init(&ctx);
	MD5Update(&ctx, b, strlen(b));
	MD5Final(key, &ctx);
	oh = hash->lookup(key, w->nobjhead);
	if (oh == w->nobjhead)
		w->nobjhead = NULL;
	AZ(pthread_mutex_lock(&oh->mtx));
	TAILQ_FOREACH(o, &oh->objects, list) {
		o->refcnt++;
		if (o->busy)
			AZ(pthread_cond_wait(&o->cv, &oh->mtx));
		/* XXX: do Vary: comparison */
		if (1)
			break;
		o->refcnt--;
	}
	if (o == NULL) {
		o = w->nobj;
		w->nobj = NULL;
		TAILQ_INSERT_TAIL(&oh->objects, o, list);
	}
	AZ(pthread_mutex_unlock(&oh->mtx));
	return (o);
}

void
HSH_Unbusy(struct object *o)
{

	AZ(pthread_mutex_lock(&o->objhead->mtx));
	o->busy = 0;
	AZ(pthread_mutex_unlock(&o->objhead->mtx));
	AZ(pthread_cond_broadcast(&o->cv));
}

void
HSH_Unref(struct object *o)
{

	AZ(pthread_mutex_lock(&o->objhead->mtx));
	o->refcnt--;
	AZ(pthread_mutex_unlock(&o->objhead->mtx));
}

void
HSH_Init(void)
{

	hash = &hsl_slinger;
	if (hash->init != NULL)
		hash->init();
}
