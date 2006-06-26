/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
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
	char *b;

	assert(hash != NULL);
	/* Precreate an objhead and object in case we need them */
	if (w->nobjhead == NULL) {
		w->nobjhead = calloc(sizeof *w->nobjhead, 1);
		assert(w->nobjhead != NULL);
		TAILQ_INIT(&w->nobjhead->objects);
		AZ(pthread_mutex_init(&w->nobjhead->mtx, NULL));
	}
	if (w->nobj == NULL) {
		w->nobj = calloc(sizeof *w->nobj, 1);
		assert(w->nobj != NULL);
		w->nobj->busy = 1;
		TAILQ_INIT(&w->nobj->store);
		AZ(pthread_cond_init(&w->nobj->cv, NULL));
	}

	assert(http_GetURL(h, &b));
	oh = hash->lookup(b, w->nobjhead);
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
	if (o != NULL) {
		AZ(pthread_mutex_unlock(&oh->mtx));
		hash->deref(oh);
		return (o);
	}

	/* Insert (precreated) object in objecthead */
	o = w->nobj;
	w->nobj = NULL;
	o->refcnt = 1;
	o->objhead = oh;
	TAILQ_INSERT_TAIL(&oh->objects, o, list);
	/* NB: do not deref objhead the new object inherits our reference */
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
HSH_Deref(struct object *o)
{
	struct objhead *oh;
	struct storage *st, *stn;

	oh = o->objhead;

	/* drop ref on object */
	AZ(pthread_mutex_lock(&oh->mtx));
	if (--o->refcnt == 0)
		TAILQ_REMOVE(&oh->objects, o, list);
	else 
		o = NULL;
	AZ(pthread_mutex_unlock(&oh->mtx));

	/* If still referenced, done */
	if (o == NULL)
		return;

	AZ(pthread_cond_destroy(&o->cv));

	TAILQ_FOREACH_SAFE(st, &o->store, list, stn) {
		TAILQ_REMOVE(&o->store, st, list);
		st->stevedore->free(st);
	}
	free(o);

	/* Drop our ref on the objhead */
	if (hash->deref(oh))
		return;
	assert(TAILQ_EMPTY(&oh->objects));
	AZ(pthread_mutex_destroy(&oh->mtx));
	free(oh);
}

void
HSH_Init(void)
{

	hash = &hsl_slinger;
	if (hash->init != NULL)
		hash->init();
}
